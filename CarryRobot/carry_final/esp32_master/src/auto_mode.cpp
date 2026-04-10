#include "auto_mode.h"
#include "globals.h"
#include "config.h"
#include "relay_control.h"
#include "uart_protocol.h"
#include "mqtt_client.h"
#include "oled_display.h"
#include "buzzer.h"
#include "battery.h"
#include "servo_control.h"

extern HardwareSerial Serial2;  // UART2 → STM32 (begin trong main.cpp)

// ── helpers: send route to STM32 ────────────────────────────────────
static void sendRouteToSTM32() {
    // CMD 0x02  [count][id_hi id_lo action] × N
    uint8_t buf[1 + MAX_ROUTE_LEN * 3];
    buf[0] = g_routeLen;
    for (uint8_t i = 0; i < g_routeLen; i++) {
        buf[1 + i * 3]     = (g_route[i].checkpointId >> 8) & 0xFF;
        buf[1 + i * 3 + 1] = g_route[i].checkpointId & 0xFF;
        buf[1 + i * 3 + 2] = g_route[i].action;
    }
    uartSendFrame(Serial2, CMD_SEND_ROUTE, buf, 1 + g_routeLen * 3);
}

static void sendCancelToSTM32() {
    uartSendFrame(Serial2, CMD_CANCEL_MISSION, nullptr, 0);
}

static void sendModeAuto() {
    uint8_t m = MODE_AUTO;
    uartSendFrame(Serial2, CMD_SET_MODE, &m, 1);
}

// ──────────────────────────────────────────────────────────────────
void autoModeInit() {
    buzzerOff();            // silence any active tone
    relaySetAuto();
    sendModeAuto();
    g_autoState = AUTO_IDLE;
    g_routeLen  = 0;
    g_routeIdx  = 0;
    servoSetX(SERVO_X_CENTER);  // 100
    servoSetY(SERVO_Y_LEVEL);   // 100
    oledIdle();
    Serial.println("[AUTO] init");
}

void autoModeLoop() {
    static uint32_t lastOled = 0;
    uint32_t now = millis();

    switch (g_autoState) {

    // ── IDLE: waiting for route from MQTT ───────────────────────────
    case AUTO_IDLE:
        if (now - lastOled > OLED_UPDATE_MS) {
            oledIdle(); lastOled = now;
        }
        // idle NFC scan → publish to dashboard (without setting busy)
        if (g_newCheckpoint) {
            g_newCheckpoint = false;
            mqttPublishIdleScan(g_lastCheckpointId);
            Serial.printf("[AUTO] idle scan CP %u\n", g_lastCheckpointId);
        }
        // transition to WAIT_START is done by MQTT callback
        break;

    // ── WAIT START: route received, show info, wait button ──────────
    case AUTO_WAIT_START:
        if (now - lastOled > OLED_UPDATE_MS) {
            oledAutoWaitStart(g_patientName, g_destination, g_routeLen);
            lastOled = now;
        }
        if (g_mqttCancel) {
            g_mqttCancel = false;
            sendCancelToSTM32();
            g_routeLen = 0;
            g_routeIdx = 0;
            g_autoState = AUTO_IDLE;
            oledIdle();
            Serial.println("[AUTO] MQTT cancel (before start) → IDLE");
            break;
        }
        if (g_btnSingleClick) {
            g_btnSingleClick = false;
            // tạm tắt kiểm tra pin
            // if (!batteryOk()) {
            //     oledBatteryLow(g_batteryPercent);
            //     buzzerBeepN(3);
            //     break;
            // }
            sendRouteToSTM32();
            g_autoState = AUTO_RUNNING;
            g_routeIdx  = 0;
            buzzerBeep(80);
            Serial.println("[AUTO] started");
        }
        break;

    // ── RUNNING: STM32 executing route ──────────────────────────────
    case AUTO_RUNNING:
        if (g_mqttCancel) {
            g_mqttCancel = false;
            sendCancelToSTM32();
            // STM32 will read NFC and report checkpoint after cancel
            // wait briefly for checkpoint report, then request return route
            g_autoState = AUTO_WAIT_RETURN_ROUTE;
            oledRecovery("Cancelled - waiting...");
            Serial.println("[AUTO] MQTT cancel → CMD 0x05, waiting for CP");
            break;
        }

        // if new route arrived while running (g_autoState set to WAIT_START by MQTT cb)
        if (g_autoState == AUTO_WAIT_START) {
            // MQTT callback already sent cancel and set new route
            sendCancelToSTM32();
            Serial.println("[AUTO] new route while running → cancel old");
            break;
        }

        // checkpoint reported by STM32
        if (g_newCheckpoint) {
            g_newCheckpoint = false;
            g_routeIdx++;
            mqttPublishCheckpoint(g_lastCheckpointId);
            Serial.printf("[AUTO] CP %u  (%u/%u)\n",
                          g_lastCheckpointId, g_routeIdx, g_routeLen);
        }

        // STM32 obstacle flag (display)
        if (g_stm32Obstacle) {
            oledObstacle();
        } else if (now - lastOled > OLED_UPDATE_MS) {
            oledAutoRunning(g_routeIdx, g_routeLen, g_destination);
            lastOled = now;
        }

        // mission done reported by STM32
        if (g_stm32MissionDone) {
            g_stm32MissionDone = false;
            g_autoState = AUTO_WAIT_RETURN_BTN;
            buzzerBeepN(2, 120, 80);
            Serial.println("[AUTO] arrived at destination");
        }
        break;

    // ── AT DESTINATION: wait button to request return ───────────────
    case AUTO_WAIT_RETURN_BTN:
        if (now - lastOled > OLED_UPDATE_MS) {
            oledAutoWaitReturn(); lastOled = now;
        }
        if (g_btnSingleClick) {
            g_btnSingleClick = false;
            mqttPublishReturnRequest(g_lastCheckpointId);
            g_autoState = AUTO_WAIT_RETURN_ROUTE;
            Serial.println("[AUTO] requesting return route");
        }
        break;

    // ── WAIT RETURN ROUTE: waiting for backend to send return route ─
    case AUTO_WAIT_RETURN_ROUTE:
        if (now - lastOled > OLED_UPDATE_MS) {
            oledRecovery("Waiting return route");
            lastOled = now;
        }
        // checkpoint from STM32 after cancel → publish return request
        if (g_newCheckpoint) {
            g_newCheckpoint = false;
            mqttPublishReturnRequest(g_lastCheckpointId);
            Serial.printf("[AUTO] CP %u → return_request\n", g_lastCheckpointId);
        }
        // MQTT callback sets AUTO_RETURNING when return_route arrives
        break;

    // ── RETURNING: running return route ─────────────────────────────
    case AUTO_RETURNING:
        // new return route just arrived → send to STM32
        {
            static bool returnSent = false;
            if (!returnSent) {
                sendRouteToSTM32();
                g_routeIdx = 0;
                returnSent = true;
            }

            if (g_newCheckpoint) {
                g_newCheckpoint = false;
                g_routeIdx++;
                mqttPublishCheckpoint(g_lastCheckpointId);
            }

            if (now - lastOled > OLED_UPDATE_MS) {
                oledAutoReturning(g_routeIdx, g_routeLen);
                lastOled = now;
            }

            if (g_stm32MissionDone) {
                g_stm32MissionDone = false;
                g_autoState = AUTO_COMPLETE;
                returnSent = false;
            }

            // mismatch handling
            if (g_stm32MismatchFlag) {
                g_stm32MismatchFlag = false;
                Serial.printf("[AUTO] mismatch! got=%u exp=%u\n",
                              g_stm32MismatchGot, g_stm32MismatchExp);
                // request new return route from current position
                mqttPublishReturnRequest(g_stm32MismatchGot);
                returnSent = false;
            }
        }
        break;

    // ── COMPLETE: back at MED ───────────────────────────────────────
    case AUTO_COMPLETE:
        mqttPublishMissionDone(g_missionId, true);
        buzzerBeepN(3, 80, 60);
        g_autoState = AUTO_IDLE;
        g_routeLen  = 0;
        oledIdle();
        Serial.println("[AUTO] mission complete, back to IDLE");
        break;
    }
}
