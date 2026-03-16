/*  main.cpp  –  Slave ESP32 entry point
 *
 *  Carry Robot – Dual-ESP32 architecture
 *  Slave handles: PN532 RFID, 3-sensor line following,
 *                 4× Mecanum motors via 2× L298N, ESP-NOW (→ Master)
 *
 *  Operating modes (set by Master via ESP-NOW):
 *    1) Line-follow + RFID  (ST_OUTBOUND / ST_BACK / ST_RECOVERY_BLIND)
 *       Slave runs local PID on line sensors, scans RFID, merges
 *       Master's vX with its own vR correction.
 *    2) Direct drive  (ST_FOLLOW / ST_RECOVERY_VIS)
 *       Slave applies vX, vY, vR from Master directly to Mecanum.
 *    3) Turn command  (turnCmd = 'L'/'R'/'B')
 *       Slave executes timed in-place rotation, reports turnDone.
 */
#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "espnow_comm.h"
#include "rfid_reader.h"
#include "line_follower.h"
#include "mecanum.h"
#include "route_runner.h"

/* ─── Timing ─── */
static const unsigned long LINE_INTERVAL = 20;  // 50 Hz

// Turn execution state
static bool  turnBusy = false;
static uint8_t lastMasterStateLogged = 0xFF;

static const char* stateName(uint8_t st) {
    switch (st) {
        case 0: return "IDLE";
        case 1: return "OUTBOUND";
        case 2: return "WAIT_AT_DEST";
        case 3: return "BACK";
        case 4: return "FOLLOW";
        case 5: return "RECOVERY_VIS";
        case 6: return "RECOVERY_BLIND";
        case 7: return "RECOVERY_CALL";
        case 8: return "OBSTACLE";
        case 9: return "MISSION_DELEGATED";
        default: return "UNKNOWN";
    }
}

/* ─── Apply Master command + optional line PID ─── */
static void driveLoop() {
    float vX = masterCmd.vX;
    float vY = masterCmd.vY;
    float vR = masterCmd.vR;

    // If line-follow enabled, run local PID and merge into vR
    if (masterCmd.enableLine && !turnBusy) {
        if (millis() - lastLineMs >= LINE_INTERVAL) {
            float dt = (millis() - lastLineMs) / 1000.0f;
            lastLineMs = millis();
            float correction = lineUpdate(dt);
            vR += correction;             // Master vR (usually 0) + PID correction

            // Use base speed from Master if vX is near zero during line mode
            if (fabsf(vX) < 5.0f) vX = (float)masterCmd.baseSpeed;

            // Update report
            slaveReport.line_detected = lineDetected() ? 1 : 0;
            slaveReport.sync_docking  = lineCentred() ? 1 : 0;
            slaveReport.lineError     = lineGetError();
            slaveReport.lineBits      = lineGetBits();
        }
    }

    const float runScale = constrain(RUN_SPEED_PERCENT / 100.0f, 0.20f, 2.00f);
    const float rotScale = constrain(ROTATE_SPEED_PERCENT / 100.0f, 0.20f, 2.00f);

    vX = constrain(vX * runScale, -255.0f, 255.0f);
    vY = constrain(vY * runScale, -255.0f, 255.0f);
    vR = constrain(vR * rotScale, -255.0f, 255.0f);

    mecanumDrive(vX, vY, vR);
}

/* ─── Turn command handler (blocking) ─── */
static void executeTurn(uint8_t cmd) {
    if (cmd == 0) return;
    turnBusy = true;
    mecanumStop();

    switch (cmd) {
        case 'L': mecanumTurnLeft90();   break;
        case 'R': mecanumTurnRight90();  break;
        case 'B': mecanumUturn();        break;
        default:  break;
    }

    // After turn, short delay then check if line sensor has re-acquired line
    delay(100);
    lineResetPID();
    turnBusy = false;
    slaveReport.turnDone = 1;
    Serial.printf("[SLAVE] Turn '%c' done\n", cmd);
}

/* ─── RFID scan ─── */
static void rfidLoop() {
    if (!masterCmd.enableRFID) return;
    if (millis() - lastNfcReadMs < NFC_READ_INTERVAL) return;
    lastNfcReadMs = millis();

    char uid[RFID_UID_LOCAL_LEN];
    if (rfidRead(uid)) {
        strncpy(slaveReport.rfid_uid, uid, sizeof(slaveReport.rfid_uid));
        slaveReport.rfid_new = 1;
        Serial.printf("[SLAVE] RFID: %s\n", uid);
    }
}

/* ─── Send report to Master at 20 Hz ─── */
static void reportLoop() {
    if (millis() - lastEspnowTxMs < ESPNOW_TX_INTERVAL) return;
    lastEspnowTxMs = millis();
    espnowSendToMaster(slaveReport);

    // Clear one-shot flags after sending
    slaveReport.rfid_new  = 0;
    slaveReport.turnDone  = 0;
    // Keep rfid_uid until next scan (Master might re-read it)
}

/* ─── Process new Master command ─── */
static void processMasterCmd() {
    if (!masterCmdNew) return;
    masterCmdNew = false;

    if (masterCmd.missionStart) {
        routeRunnerOnMissionStart();
    }
    if (masterCmd.missionCancel) {
        routeRunnerOnMissionCancel();
    }
    if (masterCmd.startReturn) {
        routeRunnerOnStartReturn();
    }

    if (masterCmd.state != lastMasterStateLogged) {
        lastMasterStateLogged = masterCmd.state;
        Serial.printf("[SLAVE] Master state -> %s (%u)\n",
                      stateName(masterCmd.state),
                      (unsigned)masterCmd.state);
    }

    // Handle turn command (one-shot, blocking) – only when NOT in autonomous mission
    if (!routeRunnerMissionActive() && masterCmd.turnCmd != 0 && !turnBusy) {
        uint8_t cmd = masterCmd.turnCmd;
        executeTurn(cmd);
        return;
    }

    // IDLE state → stop (when not in autonomous mission)
    if (masterCmd.state == 0 && !routeRunnerMissionActive()) {  // ST_IDLE
        mecanumHardBrake();
    }
}

// ================================================================
//  setup()
// ================================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== Carry Robot – Slave ESP32 ==="));

    // Hardware init
    mecanumInit();
    lineInit();
    rfidInit();

    // ESP-NOW
    espnowSlaveInit();

    Serial.println(F("=== Slave setup complete ===\n"));
}

// ================================================================
//  loop()
// ================================================================
void loop() {
    // Keep ESP-NOW aligned with Master's channel if WiFi/AP changed on Master.
    espnowSlaveMaintainLink();

    // Hard lock: without an active ESP-NOW link to the paired master, never move.
    if (!espnowSlaveLinked()) {
        mecanumHardBrake();
        masterCmd.enableLine = 0;
        masterCmd.enableRFID = 0;
        delay(MAIN_LOOP_DELAY);
        return;
    }

    // 1. Process Master commands (turn, state changes, mission start/cancel)
    processMasterCmd();

    // 2. Drive (line-follow PID or direct)
    if (!turnBusy) {
        driveLoop();
    }

    // 3. RFID scan (must run before routeRunnerUpdate so new UID is available)
    rfidLoop();

    // 4. Autonomous mission FSM (reads slaveReport.rfid_new, updates missionStatus)
    routeRunnerUpdate();

    // 5. Report back to Master
    reportLoop();

    delay(MAIN_LOOP_DELAY);
}
