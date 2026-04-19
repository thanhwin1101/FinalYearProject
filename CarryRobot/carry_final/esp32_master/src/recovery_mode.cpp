#include "recovery_mode.h"
#include "globals.h"
#include "config.h"
#include "relay_control.h"
#include "uart_protocol.h"
#include "servo_control.h"
#include "oled_display.h"
#include "mqtt_client.h"

extern HardwareSerial Serial2;

// Recovery: robot lost person during follow mode.
// Same algorithm as cancel: keep following line blindly until a checkpoint is
// scanned, then use that position to request the return route from the backend.
// NO button can exit recovery — only the backend return route can.

void recoveryModeInit() {
    // Switch relays: vision off, LINE+NFC on (needed for NFC navigation)
    if (!relayGetLineNfc()) {
        relaySetAuto();      // vision off, line+nfc on (5s relay delay)
    } else {
        relayVisionOff();    // line already on, just kill vision
    }

    // Switch STM32 from FOLLOW → AUTO so autoRunnerLoop() processes the cancel.
    // CMD_CANCEL_MISSION is only picked up in autoRunnerLoop() which requires MODE_AUTO.
    uint8_t m = MODE_AUTO;
    uartSendFrame(Serial2, CMD_SET_MODE, &m, 1);

    // Tell STM32 to enter blind-follow-to-CP mode (processed by autoRunnerLoop)
    uartSendFrame(Serial2, CMD_CANCEL_MISSION, nullptr, 0);
    servoSetY(SERVO_Y_LEVEL);

    // g_newCheckpoint will be set when STM32 finds a checkpoint.
    // recoveryModeLoop() watches for it and fires the return request.
    g_newCheckpoint = false;

    oledRecovery(1);   // step 1: relay switch
    Serial.println("[RECOVERY] init → blind follow to checkpoint");
}

void recoveryModeLoop() {
    if (g_mode != MODE_RECOVERY) return;

    static uint32_t lastOled    = 0;
    static bool     routeAsked  = false;
    uint32_t now = millis();

    // Phase 1: waiting for STM32 to report a checkpoint
    if (!routeAsked) {
        if (now - lastOled > OLED_UPDATE_MS) {
            lastOled = now;
            oledRecovery(2);   // step 2: finding CP
        }

        if (g_newCheckpoint) {
            g_newCheckpoint = false;
            routeAsked = true;
            oledRecovery(3, g_lastCheckpointId);   // step 3: CP found
            delay(400);                             // brief flash so user can read CP
            mqttPublishReturnRequest(g_lastCheckpointId);
            oledRecovery(4, g_lastCheckpointId);   // step 4: calling MED
            Serial.printf("[RECOVERY] CP=0x%04X → return_request\n", g_lastCheckpointId);
        }
        return;
    }

    // Phase 2: route requested, waiting for backend to reply
    if (now - lastOled > OLED_UPDATE_MS) {
        lastOled = now;
        oledRecovery(5, g_lastCheckpointId);   // step 5: waiting route
    }

    // Re-request every 5 s in case MQTT packet was missed
    static uint32_t lastRequest = 0;
    if (now - lastRequest > 5000) {
        lastRequest = now;
        mqttPublishReturnRequest(g_lastCheckpointId);
        Serial.printf("[RECOVERY] re-request CP=%u\n", g_lastCheckpointId);
    }

    // MQTT return_route callback calls autoModeActivateReturn() → g_mode = MODE_AUTO
    // Reset routeAsked for next recovery entry
    if (g_mode != MODE_RECOVERY) {
        routeAsked = false;
    }
}
