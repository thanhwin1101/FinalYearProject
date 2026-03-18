#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "espnow_comm.h"
#include "rfid_reader.h"
#include "line_follower.h"
#include "mecanum.h"
#include "route_runner.h"

static const unsigned long LINE_INTERVAL = 20;

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

static void driveLoop() {
    float vX = masterCmd.vX;
    float vY = masterCmd.vY;
    float vR = masterCmd.vR;

    if (masterCmd.enableLine && !turnBusy) {
        if (millis() - lastLineMs >= LINE_INTERVAL) {
            float dt = (millis() - lastLineMs) / 1000.0f;
            lastLineMs = millis();
            float correction = lineUpdate(dt);
            vR += correction;

            if (fabsf(vX) < 5.0f) vX = (float)masterCmd.baseSpeed;

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

    delay(100);
    lineResetPID();
    turnBusy = false;
    slaveReport.turnDone = 1;
    Serial.printf("[SLAVE] Turn '%c' done\n", cmd);
}

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

static void reportLoop() {
    if (millis() - lastEspnowTxMs < ESPNOW_TX_INTERVAL) return;
    lastEspnowTxMs = millis();
    espnowSendToMaster(slaveReport);

    slaveReport.rfid_new  = 0;
    slaveReport.turnDone  = 0;

}

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

    if (!routeRunnerMissionActive() && masterCmd.turnCmd != 0 && !turnBusy) {
        uint8_t cmd = masterCmd.turnCmd;
        executeTurn(cmd);
        return;
    }

    if (masterCmd.state == 0 && !routeRunnerMissionActive()) {
        mecanumHardBrake();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== Carry Robot – Slave ESP32 ==="));

    mecanumInit();
    lineInit();
    rfidInit();

    espnowSlaveInit();

    Serial.println(F("=== Slave setup complete ===\n"));
}

void loop() {

    espnowSlaveMaintainLink();

    if (!espnowSlaveLinked()) {
        mecanumHardBrake();
        masterCmd.enableLine = 0;
        masterCmd.enableRFID = 0;
        delay(MAIN_LOOP_DELAY);
        return;
    }

    processMasterCmd();

    if (!turnBusy) {
        driveLoop();
    }

    rfidLoop();

    routeRunnerUpdate();

    reportLoop();

    delay(MAIN_LOOP_DELAY);
}
