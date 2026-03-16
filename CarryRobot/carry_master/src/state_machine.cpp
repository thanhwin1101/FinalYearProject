/*  state_machine.cpp  –  Master state-machine implementation
 *
 *  States:
 *    ST_IDLE            – parked at MED, waiting for mission or follow-mode
 *    ST_OUTBOUND        – line-follow delivery (Slave PID + Master RFID routing)
 *    ST_WAIT_AT_DEST    – arrived at bed, waiting for SW press to return
 *    ST_BACK            – returning to MED via line-follow
 *    ST_FOLLOW          – follow-person mode (HuskyLens + cascaded PID)
 *    ST_RECOVERY_VIS    – Recovery step 1: visual docking to line
 *    ST_RECOVERY_BLIND  – Recovery step 2: blind line-follow + RFID scan
 *    ST_RECOVERY_CALL   – Recovery step 3: ask backend for route home
 *    ST_OBSTACLE        – paused due to ToF obstacle
 */
#include "state_machine.h"
#include "config.h"
#include "globals.h"
#include "buzzer.h"
#include "display.h"
#include "sensors.h"
#include "servo_gimbal.h"
#include "huskylens_wrapper.h"
#include "espnow_comm.h"
#include "mqtt_comm.h"
#include "route_manager.h"
#include "mission_delegate.h"
#include "follow_pid.h"

/* ─── Forward declarations ─── */
static void enterIdle();
static void enterOutbound();
static void enterMissionDelegated();
static void enterWaitAtDest();
static void enterBack(bool doUturn);
static void enterFollow();
static void enterRecoveryVis();
static void enterRecoveryBlind();
static void enterRecoveryCall();
static void enterObstacle();
static void exitObstacle();

/* ─── Per-state update handlers ─── */
static void updateOutbound();
static void updateBack();
static void updateFollow();
static void updateRecoveryVis();
static void updateRecoveryBlind();
static void updateRecoveryCall();
static void updateObstacle();

/* ─── Obstacle check ─── */
static void checkObstacle();

/* ─── Helpers ─── */
static bool         medCardScanned  = false;
static bool         medHomeRead     = false;
static bool         s_waitingAtDestForSw = false;  // Slave reported complete, wait SW to start return
static String       lastCheckpointNode = "";
static unsigned long recoveryCallMs = 0;
static const unsigned long RECOVERY_CALL_TIMEOUT = 5000;
static String       lastRfidUid = "";
static unsigned long lastRfidMs = 0;
static void finalizeAtMedAndIdle();
static bool         followFaceAuthed = false;
static uint8_t      followFaceStreak = 0;

enum SmEventType : uint8_t {
    EVT_NONE = 0,
    EVT_BTN_SINGLE,
    EVT_BTN_LONG,
    EVT_BTN_DOUBLE,
    EVT_SLAVE_RFID,
    EVT_SLAVE_SYNC,
    EVT_CANCEL_MISSION,
    EVT_MED_REACHED,
    EVT_OBSTACLE_HIT,
    EVT_OBSTACLE_CLEAR
};

enum ModePriority : uint8_t {
    MODE_MANUAL = 0,
    MODE_MISSION = 1,
    MODE_SAFETY = 2,
    MODE_EMERGENCY = 3
};

struct SmEvent {
    SmEventType type;
    char uid[24];
};

static constexpr uint8_t SM_EVENT_Q_SIZE = 20;
static SmEvent eventQ[SM_EVENT_Q_SIZE];
static uint8_t eventHead = 0;
static uint8_t eventTail = 0;

static bool enqueueEvent(SmEventType type, const char* uid = nullptr) {
    const uint8_t nextTail = (uint8_t)((eventTail + 1) % SM_EVENT_Q_SIZE);
    if (nextTail == eventHead) {
        return false;
    }
    eventQ[eventTail].type = type;
    eventQ[eventTail].uid[0] = '\0';
    if (uid && uid[0] != '\0') {
        snprintf(eventQ[eventTail].uid, sizeof(eventQ[eventTail].uid), "%s", uid);
    }
    eventTail = nextTail;
    return true;
}

static bool dequeueEvent(SmEvent& out) {
    if (eventHead == eventTail) return false;
    out = eventQ[eventHead];
    eventHead = (uint8_t)((eventHead + 1) % SM_EVENT_Q_SIZE);
    return true;
}

static ModePriority eventPriority(SmEventType type) {
    switch (type) {
        case EVT_OBSTACLE_HIT:
            return MODE_EMERGENCY;
        case EVT_OBSTACLE_CLEAR:
        case EVT_MED_REACHED:
            return MODE_SAFETY;
        case EVT_CANCEL_MISSION:
            return MODE_MISSION;
        case EVT_SLAVE_RFID:
        case EVT_SLAVE_SYNC:
            return MODE_MISSION;
        case EVT_BTN_SINGLE:
            return MODE_MISSION;
        case EVT_BTN_LONG:
        case EVT_BTN_DOUBLE:
            return MODE_MANUAL;
        default:
            return MODE_MANUAL;
    }
}

static ModePriority statePriority(RobotState st) {
    switch (st) {
        case ST_OBSTACLE:
            return MODE_EMERGENCY;
        case ST_OUTBOUND:
        case ST_WAIT_AT_DEST:
        case ST_BACK:
        case ST_RECOVERY_VIS:
        case ST_RECOVERY_BLIND:
        case ST_RECOVERY_CALL:
            return MODE_MISSION;
        case ST_FOLLOW:
            return MODE_MANUAL;
        case ST_IDLE:
        default:
            return MODE_MANUAL;
    }
}

static bool modeAllowsEvent(const SmEvent& e) {
    if (robotState == ST_OBSTACLE && eventPriority(e.type) < MODE_SAFETY) {
        return false;
    }
    return eventPriority(e.type) >= statePriority((RobotState)robotState);
}

static void renderStateScreenNow(RobotState st) {
    switch (st) {
        case ST_IDLE:
            displayIdle();
            break;
        case ST_OUTBOUND: {
            String next = (outRouteIdx + 1 < outRouteLen) ? outRoute[outRouteIdx + 1].nodeId : destBed;
            displayOutbound(patientName.c_str(), next.c_str());
            break;
        }
        case ST_WAIT_AT_DEST:
            displayWaitAtDest();
            break;
        case ST_BACK: {
            String next = (retRouteIdx + 1 < retRouteLen) ? retRoute[retRouteIdx + 1].nodeId : String("MED");
            displayBack(next.c_str());
            break;
        }
        case ST_FOLLOW:
            if (!followFaceAuthed) {
                displayFaceAuth("Align Face", followFaceStreak, HUSKY_FACE_AUTH_STREAK);
            } else {
                displayFollow("Locked", 0);
            }
            break;
        case ST_RECOVERY_VIS:
            displayRecovery(1);
            break;
        case ST_RECOVERY_BLIND:
            displayRecovery(2);
            break;
        case ST_RECOVERY_CALL:
            displayRecovery(3);
            break;
        case ST_OBSTACLE:
            displayObstacle();
            break;
        case ST_MISSION_DELEGATED:
            displayOutbound(patientName.c_str(), "Slave running...");
            break;
        default:
            break;
    }
    lastOledMs = millis();
}

static void setStateWithBeep(RobotState next) {
    const bool changed = (robotState != next);
    if (changed) {
        buzzerBeep(45);
    }
    robotState = next;
    if (changed) {
#if MONITOR_SERIAL
        const char* names[] = {"IDLE","OUTBOUND","WAIT_AT_DEST","BACK","FOLLOW","RECOVERY_VIS","RECOVERY_BLIND","RECOVERY_CALL","OBSTACLE","MISSION_DELEGATED"};
        const char* n = (next <= ST_MISSION_DELEGATED) ? names[next] : "?";
        Serial.printf("[MON] t=%lu STATE=%s\n", millis(), n);
#endif
        renderStateScreenNow(next);
    }
}

static void handleButtonEvent(SmEventType type) {
    switch (type) {
        case EVT_BTN_SINGLE:
            switch (robotState) {
                case ST_IDLE:
                    if (outRouteLen > 0) {
                        if (medCardScanned) {
                            medCardScanned = false;
                            missionDelegateStartMission();
                        } else {
                            Serial.println(F("[SM] Scan MED first, then press SW"));
                            buzzerBeep(40);
                        }
                    } else {
                        Serial.println(F("[SM] Single click ignored: no mission assigned"));
                        buzzerBeep(40);
                    }
                    break;
                case ST_WAIT_AT_DEST:
                    enterBack(true);
                    break;
                case ST_MISSION_DELEGATED:
                    if (s_waitingAtDestForSw) {
                        s_waitingAtDestForSw = false;
                        masterMsg.startReturn = 1;
                        espnowSendToSlave(masterMsg);
                        masterMsg.startReturn = 0;
                        displayOutbound(patientName.c_str(), "Returning...");
                        Serial.println(F("[SM] SW at dest -> startReturn sent"));
                    }
                    break;
                default:
                    break;
            }
            break;

        case EVT_BTN_LONG:
            if (robotState == ST_FOLLOW) {
                enterIdle();
            } else if (robotState == ST_IDLE) {
                enterFollow();
            }
            break;

        case EVT_BTN_DOUBLE:
            if (robotState == ST_IDLE) {
                enterFollow();
            } else if (robotState == ST_FOLLOW) {
                enterIdle();
            }
            break;

        default:
            break;
    }
}

static void processRfidEvent(const char* uid) {
    bool handledWithCustomTone = false;

    const unsigned long now = millis();
    if (lastRfidUid.equalsIgnoreCase(uid) && (now - lastRfidMs) < 1200) {
        return;
    }
    lastRfidUid = uid;
    lastRfidMs = now;

    const UidEntry* entry = uidLookupByUid(uid);
    if (!entry) {
        Serial.printf("[SM] Unknown UID: %s\n", uid);
        return;
    }

    const bool checkpointChanged = !lastCheckpointNode.equalsIgnoreCase(entry->nodeId);
    if (checkpointChanged) {
        lastCheckpointNode = entry->nodeId;
    }

    currentNodeIdLive = entry->nodeId;
    if (strcasecmp(entry->nodeId, "MED") == 0) {
        medHomeRead = true;
        if (robotState == ST_IDLE) {
            medCardScanned = true;  // allow SW to start mission after MED scan
        }
        enqueueEvent(EVT_MED_REACHED);
    }
    Serial.printf("[SM] RFID: %s (%s)\n", entry->nodeId, uid);

    if (strcasecmp(entry->nodeId, "MED") == 0) {
        if (robotState != ST_IDLE) {
            if (missionId.length()) {
                mqttSendReturned(missionId.c_str());
            }
            finalizeAtMedAndIdle();
            handledWithCustomTone = true;
            return;
        }
        return;
    }

    if (robotState == ST_OUTBOUND || robotState == ST_BACK) {
        String expected = expectedNextUid();
        if (expected.length() == 0) return;

        if (expected.equalsIgnoreCase(uid)) {
            currentRouteIdx()++;
            mqttSendProgress(missionId.c_str(), entry->nodeId,
                             currentRouteIdx(), currentRouteLen());

            if (robotState == ST_OUTBOUND && cancelPending) {
                masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
                espnowSendToSlave(masterMsg);
                routeBuildReverseReturn();
                enterBack(true);
                return;
            }

            if (currentRouteIdx() + 1 >= currentRouteLen()) {
                if (robotState == ST_OUTBOUND) {
                    enterWaitAtDest();
                } else {
                    if (missionId.length()) {
                        mqttSendReturned(missionId.c_str());
                    }
                    finalizeAtMedAndIdle();
                    handledWithCustomTone = true;
                }
                return;
            }

            char act = upcomingTurnAction();
            if (act == 'L' || act == 'R' || act == 'B') {
                masterMsg.vX = 0;
                masterMsg.vY = 0;
                masterMsg.vR = 0;
                masterMsg.turnCmd = act;
                espnowSendToSlave(masterMsg);
            }
        }
    }

    if (robotState == ST_RECOVERY_BLIND) {
        if (recoveryCheckpointsHit < 2) {
            recoveryCpUids[recoveryCheckpointsHit] = String(uid);
            recoveryCheckpointsHit++;
            Serial.printf("[SM] Recovery CP %u: %s\n", recoveryCheckpointsHit, entry->nodeId);
        }
        if (recoveryCheckpointsHit >= 2) {
            enterRecoveryCall();
        }
    }

    if (checkpointChanged && !handledWithCustomTone) {
        buzzerBeep(45);
    }
}

static void processEvent(const SmEvent& e) {
    if (!modeAllowsEvent(e)) {
        Serial.printf("[SM] Event %u ignored in state %u\n",
                      (unsigned)e.type,
                      (unsigned)robotState);
        return;
    }

    switch (e.type) {
        case EVT_BTN_SINGLE:
        case EVT_BTN_LONG:
        case EVT_BTN_DOUBLE:
            handleButtonEvent(e.type);
            break;
        case EVT_SLAVE_RFID:
            if (e.uid[0] != '\0') {
                processRfidEvent(e.uid);
            }
            break;
        case EVT_SLAVE_SYNC:
            if (robotState == ST_RECOVERY_VIS) {
                Serial.println(F("[SM] Sync docking – line found!"));
                enterRecoveryBlind();
            }
            break;
        case EVT_CANCEL_MISSION:
            if (robotState == ST_OUTBOUND) {
                displayCentered("MISSION CANCELED", "Returning to MED...");
                routeBuildReverseReturn();
                enterBack(true);
            }
            break;
        case EVT_MED_REACHED:
            if (cancelPending && robotState != ST_IDLE && medHomeRead) {
                Serial.println(F("[SM] Cancel requested and MED confirmed by RFID -> IDLE"));
                if (missionId.length()) {
                    mqttSendReturned(missionId.c_str());
                }
                enterIdle();
            }
            break;
        case EVT_OBSTACLE_HIT:
            if (robotState != ST_OBSTACLE) {
                enterObstacle();
            }
            break;
        case EVT_OBSTACLE_CLEAR:
            if (robotState == ST_OBSTACLE) {
                exitObstacle();
            }
            break;
        default:
            break;
    }
}

// ================================================================
//  smInit
// ================================================================
void smInit() {
    enterIdle();
}

// ================================================================
//  smUpdate – called every loop()
// ================================================================
void smUpdate() {
    // Keep HuskyLens reconnect attempts running in background regardless of state.
    huskyMaintain();

    // Convert asynchronous flags to state-machine events.
    if (cancelPending && robotState == ST_OUTBOUND) {
        enqueueEvent(EVT_CANCEL_MISSION);
    }

    /* ── Obstacle check (applicable in drive states) ── */
    if (robotState == ST_OUTBOUND || robotState == ST_BACK ||
        robotState == ST_FOLLOW   || robotState == ST_RECOVERY_BLIND ||
        robotState == ST_MISSION_DELEGATED) {
        checkObstacle();
    }
    if (robotState == ST_OBSTACLE) { updateObstacle(); }

    // Process a bounded number of queued events per loop to keep timing stable.
    SmEvent ev{};
    uint8_t budget = 6;
    while (budget-- && dequeueEvent(ev)) {
        processEvent(ev);
    }

    if (robotState == ST_OBSTACLE) { return; }
    if (obstacleHold) return;            // remote stop command

    /* ── Per-state logic ── */
    switch (robotState) {
        case ST_IDLE:
            if (millis() - lastOledMs >= OLED_INTERVAL) {
                lastOledMs = millis();
                displayIdle();
            }
            break;

        case ST_OUTBOUND:        updateOutbound();      break;
        case ST_WAIT_AT_DEST:
            if (millis() - lastOledMs >= OLED_INTERVAL) {
                lastOledMs = millis();
                displayWaitAtDest();
            }
            break;
        case ST_BACK:            updateBack();           break;
        case ST_FOLLOW:          updateFollow();         break;
        case ST_RECOVERY_VIS:    updateRecoveryVis();    break;
        case ST_RECOVERY_BLIND:  updateRecoveryBlind();  break;
        case ST_RECOVERY_CALL:   updateRecoveryCall();   break;
        case ST_OBSTACLE:
            if (millis() - lastOledMs >= OLED_INTERVAL) {
                lastOledMs = millis();
                displayObstacle();
            }
            break;
        case ST_MISSION_DELEGATED: {
            // Slave runs route; Master keeps sending enableLine + enableRFID + baseSpeed
            masterMsg.state       = (uint8_t)ST_MISSION_DELEGATED;
            masterMsg.enableLine  = 1;
            masterMsg.enableRFID  = 1;
            masterMsg.baseSpeed   = LINE_BASE_SPEED;
            masterMsg.vX         = 0;
            masterMsg.vY         = 0;
            masterMsg.vR         = 0;
            masterMsg.turnCmd    = 0;
            masterMsg.missionStart = 0;
            masterMsg.missionCancel = 0;
            masterMsg.startReturn   = 0;
            if (slaveMsg.missionStatus == 2) {
                displayWaitAtDest();
            } else {
                displayOutbound(patientName.c_str(), "Slave running...");
            }
            if (millis() - lastOledMs >= OLED_INTERVAL) {
                lastOledMs = millis();
            }
            break;
        }
        default: break;
    }

    /* ── Send command to Slave at 20 Hz ── */
    if (millis() - lastEspnowTxMs >= ESPNOW_TX_INTERVAL) {
        lastEspnowTxMs = millis();
        masterMsg.state = (uint8_t)robotState;
        espnowSendToSlave(masterMsg);
    }
}

// ================================================================
//  Button handlers
// ================================================================

void smOnSingleClick() {
    enqueueEvent(EVT_BTN_SINGLE);
}

void smOnDoubleClick() {
    enqueueEvent(EVT_BTN_DOUBLE);
}

void smOnLongPress() {
    enqueueEvent(EVT_BTN_LONG);
}

void smOnMqttCommand(const char* cmd, const char* value) {
    if (!cmd) return;

    const bool isSetMode = (strcmp(cmd, "set_mode") == 0);
    const char* mode = isSetMode ? value : cmd;
    if (!mode) return;

    if (strcasecmp(mode, "follow") == 0) {
        if (robotState == ST_IDLE || robotState == ST_FOLLOW) {
            enterFollow();
        }
        return;
    }

    if (strcasecmp(mode, "idle") == 0) {
        if (robotState == ST_IDLE || robotState == ST_FOLLOW) {
            enterIdle();
        }
        return;
    }
}

// ================================================================
//  Slave RFID callback
// ================================================================
void smOnSlaveRfid(const char* uid) {
    enqueueEvent(EVT_SLAVE_RFID, uid);
}

// Called when Slave centre sensor locks onto line during recovery visual docking
void smOnSlaveSyncDocking() {
    enqueueEvent(EVT_SLAVE_SYNC);
}

void smEnterMissionDelegated() {
    enterMissionDelegated();
}

void smEnterIdle() {
    enterIdle();
}

void smSetWaitingAtDest(bool waiting) {
    s_waitingAtDestForSw = waiting;
}

// ================================================================
//  State entry functions
// ================================================================

static void enterIdle() {
    setStateWithBeep(ST_IDLE);
    masterMsg  = {};           // zero all velocities
    masterMsg.enableRFID = 1;  // keep scanning so MED/home position updates while parked
    masterMsg.enableLine = 0;
    masterMsg.missionStart = 0;
    masterMsg.missionCancel = 0;
    routeClear();
    cancelPending = false;
    medCardScanned = false;
    medHomeRead = false;
    lastRfidUid = "";
    lastRfidMs = 0;
    lastCheckpointNode = "";
    obstacleHold   = false;
    displayIdle();
    Serial.println(F("[SM] → IDLE"));
}

static void enterMissionDelegated() {
    setStateWithBeep(ST_MISSION_DELEGATED);
    gimbalSetY(SERVO_Y_LEVEL);  // Servo Y rest in autonomous mode
    masterMsg.state       = (uint8_t)ST_MISSION_DELEGATED;
    masterMsg.enableLine  = 1;
    masterMsg.enableRFID  = 1;
    masterMsg.baseSpeed   = LINE_BASE_SPEED;
    masterMsg.vX          = 0;
    masterMsg.vY          = 0;
    masterMsg.vR          = 0;
    masterMsg.turnCmd     = 0;
    masterMsg.missionStart  = 0;  // already sent by mission_delegate
    masterMsg.missionCancel = 0;
    masterMsg.startReturn   = 0;
    Serial.println(F("[SM] → MISSION_DELEGATED (Slave autonomous)"));
}

static void enterOutbound() {
    setStateWithBeep(ST_OUTBOUND);
    gimbalSetY(SERVO_Y_LEVEL);  // Servo Y rest in autonomous mode
    outRouteIdx = 0;
    cancelPending = false;
    medHomeRead = false;
    // Tell Slave: line-follow ON, RFID ON, base speed
    masterMsg.state      = ST_OUTBOUND;
    masterMsg.enableLine = 1;
    masterMsg.enableRFID = 1;
    masterMsg.baseSpeed  = LINE_BASE_SPEED;
    masterMsg.vX = LINE_BASE_SPEED;
    masterMsg.vY = 0;
    masterMsg.vR = 0;
    masterMsg.turnCmd = 0;
    displayOutbound(patientName.c_str(), currentNodeId().c_str());
    buzzerBeep(100);
    Serial.println(F("[SM] → OUTBOUND"));
}

static void enterWaitAtDest() {
    setStateWithBeep(ST_WAIT_AT_DEST);
    // Stop motors
    masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
    masterMsg.enableLine = 0;
    masterMsg.enableRFID = 0;
    masterMsg.turnCmd = 0;
    espnowSendToSlave(masterMsg);
    // U-turn at destination
    delay(200);
    masterMsg.turnCmd = 'B';
    espnowSendToSlave(masterMsg);
    delay(TURN_180_MS + 200);
    masterMsg.turnCmd = 0;
    masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
    espnowSendToSlave(masterMsg);
    mqttSendComplete(missionId.c_str());
    buzzerBeep(90);
    displayWaitAtDest();
    Serial.println(F("[SM] → WAIT_AT_DEST"));
}

static void finalizeAtMedAndIdle() {
    masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
    masterMsg.enableLine = 0;
    masterMsg.turnCmd = 'B';
    espnowSendToSlave(masterMsg);
    delay(TURN_180_MS + 200);
    masterMsg.turnCmd = 0;
    espnowSendToSlave(masterMsg);
    buzzerTone(1500, 500);
    enterIdle();
}

static void enterBack(bool doUturn) {
    setStateWithBeep(ST_BACK);
    gimbalSetY(SERVO_Y_LEVEL);  // Servo Y rest in autonomous mode
    retRouteIdx = 0;
    if (retRouteLen < 2) routeBuildReverseReturn();
    masterMsg.state      = ST_BACK;
    masterMsg.enableLine = 1;
    masterMsg.enableRFID = 1;
    masterMsg.baseSpeed  = LINE_BASE_SPEED;
    masterMsg.vX = LINE_BASE_SPEED;
    masterMsg.vY = 0;
    masterMsg.vR = 0;
    masterMsg.turnCmd = 0;
    displayBack(currentNodeId().c_str());
    buzzerBeep(100);
    Serial.println(F("[SM] → BACK"));
}

static void enterFollow() {
    setStateWithBeep(ST_FOLLOW);
    followFaceAuthed = false;
    followFaceStreak = 0;
    huskySwitchToFaceRecognition();
    gimbalSetY(SERVO_Y_LOOK_UP);
    gimbalLockX(true);
    servoXLocked = true;
    followPidReset();
    // Slave: disable line & RFID, direct velocity control
    masterMsg = {};
    masterMsg.state      = ST_FOLLOW;
    masterMsg.enableLine = 0;
    masterMsg.enableRFID = 0;
    displayFaceAuth("Align Face", 0, HUSKY_FACE_AUTH_STREAK);
    Serial.println(F("[SM] → FOLLOW"));
}

static void enterRecoveryVis() {
    setStateWithBeep(ST_RECOVERY_VIS);
    huskySwitchToLineTracking();
    gimbalSetY(SERVO_Y_TILT_DOWN);     // look at floor
    gimbalLockX(true);
    servoXLocked = true;
    // Slave: disable line PID (Master controls chassis), disable RFID
    masterMsg = {};
    masterMsg.state      = ST_RECOVERY_VIS;
    masterMsg.enableLine = 0;
    masterMsg.enableRFID = 0;
    displayRecovery(1);
    buzzerBeep(60);
    Serial.println(F("[SM] → RECOVERY_VIS (Step 1: Visual Docking)"));
}

static void enterRecoveryBlind() {
    setStateWithBeep(ST_RECOVERY_BLIND);
    // Servo Y back to level
    gimbalSetY(SERVO_Y_LEVEL);
    recoveryCheckpointsHit = 0;
    recoveryCpUids[0] = "";
    recoveryCpUids[1] = "";
    // Slave: line-follow ON, RFID ON
    masterMsg = {};
    masterMsg.state      = ST_RECOVERY_BLIND;
    masterMsg.enableLine = 1;
    masterMsg.enableRFID = 1;
    masterMsg.baseSpeed  = LINE_BASE_SPEED;
    masterMsg.vX = LINE_BASE_SPEED;
    displayRecovery(2);
    Serial.println(F("[SM] → RECOVERY_BLIND (Step 2: Blind Run)"));
}

static void enterRecoveryCall() {
    setStateWithBeep(ST_RECOVERY_CALL);
    // Stop
    masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
    masterMsg.enableLine = 0;
    espnowSendToSlave(masterMsg);
    // Send position to backend
    const char* lastNode = "";
    const char* previousNode = nullptr;

    if (recoveryCheckpointsHit > 0) {
        const UidEntry* eLast = uidLookupByUid(recoveryCpUids[recoveryCheckpointsHit - 1].c_str());
        if (eLast) lastNode = eLast->nodeId;
    }
    if (recoveryCheckpointsHit > 1) {
        const UidEntry* ePrev = uidLookupByUid(recoveryCpUids[recoveryCheckpointsHit - 2].c_str());
        if (ePrev) previousNode = ePrev->nodeId;
    }

    mqttSendWaitingReturn(lastNode, previousNode);
    recoveryCallMs = millis();
    displayRecovery(3);
    Serial.printf("[SM] → RECOVERY_CALL (Step 3: at %s, prev=%s)\n",
                  lastNode,
                  previousNode ? previousNode : "");
}

// ================================================================
//  Obstacle management
// ================================================================

static void checkObstacle() {
    uint16_t dist = 0;
    if (millis() - lastTofMs < TOF_INTERVAL) return;
    lastTofMs = millis();

    if (!tofRead(dist)) return;
    if (dist < TOF_STOP_DIST && !obstacleHold) {
        enqueueEvent(EVT_OBSTACLE_HIT);
    }
}

static void enterObstacle() {
    stateBeforeObstacle = robotState;
    setStateWithBeep(ST_OBSTACLE);
    masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
    espnowSendToSlave(masterMsg);
    displayObstacle();
    buzzerObstacle();
    Serial.println(F("[SM] → OBSTACLE"));
}

static void exitObstacle() {
    setStateWithBeep(stateBeforeObstacle);
    Serial.printf("[SM] Obstacle clear → state %u\n", robotState);
    // Restore driving (velocities will be set by next update cycle)
}

static void updateObstacle() {
    uint16_t dist = 0;
    if (millis() - lastTofMs < TOF_INTERVAL) return;
    lastTofMs = millis();
    if (!tofRead(dist)) return;
    if (dist >= TOF_RESUME_DIST) {
        enqueueEvent(EVT_OBSTACLE_CLEAR);
        return;
    }
    // Beep periodically
    if (millis() - lastObstacleBeepMs >= OBSTACLE_BEEP_MS) {
        lastObstacleBeepMs = millis();
        buzzerObstacle();
    }
}

// ================================================================
//  Per-state update functions
// ================================================================

static void updateOutbound() {
    // OLED refresh
    if (millis() - lastOledMs >= OLED_INTERVAL) {
        lastOledMs = millis();
        String next = (outRouteIdx + 1 < outRouteLen) ? outRoute[outRouteIdx + 1].nodeId : destBed;
        displayOutbound(patientName.c_str(), next.c_str());
    }
    // Velocities are set by Slave local line-follow; Master only sends turns via turnCmd
    // After Slave finishes a turn, clear turnCmd
    if (slaveMsg.turnDone && masterMsg.turnCmd != 0) {
        masterMsg.turnCmd = 0;
        masterMsg.vX = LINE_BASE_SPEED;
    }
}

static void updateBack() {
    if (millis() - lastOledMs >= OLED_INTERVAL) {
        lastOledMs = millis();
        String next = (retRouteIdx + 1 < retRouteLen) ? retRoute[retRouteIdx + 1].nodeId : String("MED");
        displayBack(next.c_str());
    }
    if (slaveMsg.turnDone && masterMsg.turnCmd != 0) {
        masterMsg.turnCmd = 0;
        masterMsg.vX = LINE_BASE_SPEED;
    }
}

static void updateFollow() {
    if (millis() - lastHuskyMs < HUSKY_INTERVAL) return;
    lastHuskyMs = millis();
    float dt = HUSKY_INTERVAL / 1000.0f;

    if (!huskyRequest()) {
        if (millis() - lastOledMs >= OLED_INTERVAL) {
            lastOledMs = millis();
            displayFaceAuth("Cam Offline", followFaceStreak, HUSKY_FACE_AUTH_STREAK);
        }
        return;
    }
    HuskyTarget tgt = huskyGetTarget();

    if (!followFaceAuthed) {
        const bool faceMatched = tgt.detected && (tgt.id == HUSKY_FACE_AUTH_ID);
        if (faceMatched) {
            if (followFaceStreak < 255) followFaceStreak++;
        } else {
            followFaceStreak = 0;
        }

        if (followFaceStreak >= HUSKY_FACE_AUTH_STREAK) {
            followFaceAuthed = true;
            gimbalSetY(SERVO_Y_LEVEL);
            huskySwitchToObjectTracking();
            buzzerBeep(90);
            displayFaceAuth("Auth OK", followFaceStreak, HUSKY_FACE_AUTH_STREAK);
            Serial.printf("[SM] FOLLOW face auth OK (id=%d) -> object tracking\n", HUSKY_FACE_AUTH_ID);
        }

        if (millis() - lastOledMs >= OLED_INTERVAL) {
            lastOledMs = millis();
            const char* phase = faceMatched ? "Matching" : "Find Target";
            displayFaceAuth(phase, followFaceStreak, HUSKY_FACE_AUTH_STREAK);
        }
        // Gate movement until the correct face is confirmed.
        masterMsg.vX = 0;
        masterMsg.vY = 0;
        masterMsg.vR = 0;
        return;
    }

    uint16_t tofDist = 0;
    tofRead(tofDist);

    FollowOutput fout = followPidUpdate(
        tgt.xCenter, tgt.detected, tofDist, dt);

    masterMsg.vX = fout.vX;
    masterMsg.vY = fout.vY;
    masterMsg.vR = fout.vR;

    // Side ultrasonics safety
    if (millis() - lastUsMs >= US_INTERVAL) {
        lastUsMs = millis();
        long lmm = usLeftMm();
        long rmm = usRightMm();
        if (lmm > 0 && lmm < US_SIDE_WARN_MM) masterMsg.vY = max(masterMsg.vY, 0.0f);  // don't strafe left
        if (rmm > 0 && rmm < US_SIDE_WARN_MM) masterMsg.vY = min(masterMsg.vY, 0.0f);  // don't strafe right
    }

    // OLED
    if (millis() - lastOledMs >= OLED_INTERVAL) {
        lastOledMs = millis();
        displayFollow(tgt.detected ? "Locked" : "Lost", tofDist / 10);
    }
}

/* ── Recovery Step 1: Visual Docking ── */
static void updateRecoveryVis() {
    if (millis() - lastHuskyMs < HUSKY_INTERVAL) return;
    lastHuskyMs = millis();

    huskyRequest();
    HuskyLine line = huskyGetLine();

    if (line.detected) {
        // Use line angle to steer chassis toward the line
        // xTarget − xOrigin gives lateral offset, yTarget − yOrigin gives angle
        float dx = line.xTarget - line.xOrigin;
        float dy = line.yTarget - line.yOrigin;
        float angleErr = atan2f(dx, dy) * 57.2958f;   // degrees from vertical

        // Lateral offset of line from image centre
        float cx = ((float)line.xOrigin + (float)line.xTarget) / 2.0f;
        float lateralErr = cx - (float)HUSKY_CENTER_X;

        // Strafe to centre on line, rotate to align
        masterMsg.vY = constrain(lateralErr * 1.5f, -120.0f, 120.0f);
        masterMsg.vR = constrain(angleErr * 1.0f,   -100.0f, 100.0f);
        masterMsg.vX = 40;   // creep forward
    } else {
        // No line visible – spin slowly looking for it
        masterMsg.vX = 0;
        masterMsg.vY = 0;
        masterMsg.vR = 60;    // slow rotation
    }

    // Check sync from Slave (centre line sensor triggered)
    // (handled by smOnSlaveSyncDocking callback)
}

/* ── Recovery Step 2: Blind line-follow ── */
static void updateRecoveryBlind() {
    // Slave does line-follow locally; Master just watches for RFID via callback
    // OLED update
    if (millis() - lastOledMs >= OLED_INTERVAL) {
        lastOledMs = millis();
        displayRecovery(2);
    }
    if (slaveMsg.turnDone && masterMsg.turnCmd != 0) {
        masterMsg.turnCmd = 0;
        masterMsg.vX = LINE_BASE_SPEED;
    }
}

/* ── Recovery Step 3: Call Home ── */
static void updateRecoveryCall() {
    // Wait for backend return_route via MQTT (handled in mqtt_comm → routeParseReturn)
    if (retRouteLen >= 2) {
        // Execute backend-provided initial alignment action at current node.
        // This removes local heading heuristics in recovery.
        const char firstAction = retRoute[0].action;
        if (firstAction == 'L' || firstAction == 'R' || firstAction == 'B') {
            masterMsg.vX = masterMsg.vY = masterMsg.vR = 0;
            masterMsg.turnCmd = firstAction;
            espnowSendToSlave(masterMsg);

            if (firstAction == 'B') delay(TURN_180_MS + 200);
            else                    delay(TURN_90_MS + 200);

            masterMsg.turnCmd = 0;
            espnowSendToSlave(masterMsg);
            retRoute[0].action = 'F';
            Serial.printf("[SM] Recovery initial turn from backend: %c\n", firstAction);
        }

        enterBack(false);
        return;
    }

    // Timeout – use reverse fallback
    if (millis() - recoveryCallMs > RECOVERY_CALL_TIMEOUT) {
        Serial.println(F("[SM] Recovery route timeout – using reverse"));
        routeBuildReverseReturn();
        enterBack(true);
    }
}
