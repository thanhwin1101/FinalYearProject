// ====================================================================
//  carry_final  –  STM32 Slave  –  main.cpp
//  UART←→ESP32, Mecanum drive, line following, NFC, ToF
// ====================================================================
#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "uart_protocol.h"
#include "motor_control.h"
#include "mecanum.h"
#include "line_sensor.h"
#include "pn532_reader.h"
#include "tof_sensor.h"
#include "auto_runner.h"

// USART2 for ESP32 communication
HardwareSerial Serial2(USART2);

// ── process frames from ESP32 ───────────────────────────────────────
static void handleESP32() {
    uint8_t cmd, buf[UART_MAX_FRAME], len;
    while (uartReceiveFrame(Serial2, cmd, buf, len)) {

        switch (cmd) {

        case CMD_SET_MODE:
            if (len >= 1) {
                g_mode = (RobotMode)buf[0];
                motorStop();
                autoRunnerInit();

                // NFC relay may have been power-cycled; force re-init for
                // modes that need it (AUTO = navigation, RECOVERY = return)
                if (g_mode == MODE_AUTO || g_mode == MODE_RECOVERY) {
                    nfcReset();   // marks not-ready → re-init on next read
                }

                Serial.printf("[UART] mode=%u\n", g_mode);

                // ACK
                uint8_t ack = cmd;
                uartSendFrame(Serial2, CMD_ACK, &ack, 1);
            }
            break;

        case CMD_SEND_ROUTE:
            if (len >= 1) {
                g_routeLen = buf[0];
                if (g_routeLen > MAX_ROUTE_LEN) g_routeLen = MAX_ROUTE_LEN;
                for (uint8_t i = 0; i < g_routeLen; i++) {
                    uint8_t off = 1 + i * 3;
                    g_route[i].checkpointId = ((uint16_t)buf[off] << 8) | buf[off + 1];
                    g_route[i].action = buf[off + 2];
                }
                g_routeIdx = 0;
                g_missionStart = true;
                Serial.printf("[UART] route: %u points\n", g_routeLen);
            }
            break;

        case CMD_DIRECT_VEL:
            if (len >= 6) {
                g_cmdVx = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
                g_cmdVy = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
                g_cmdVr = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);
                g_newVelCmd = true;
            }
            break;

        case CMD_REQUEST_STATUS:
            // send battery (placeholder: 100%)
            {
                uint8_t pct = 100;
                uartSendFrame(Serial2, CMD_BATTERY, &pct, 1);
            }
            break;

        case CMD_CANCEL_MISSION:
            g_missionCancel = true;
            motorStop();
            Serial.println("[UART] cancel mission");
            break;

        case CMD_CONFIRM_ARRIVAL:
            // ESP32 confirms checkpoint – no additional action needed
            break;

        default:
            Serial.printf("[UART] unknown cmd 0x%02X\n", cmd);
        }
    }
}

// ── Follow mode: apply velocity commands from ESP32 ─────────────────
static void followDrive() {
    if (g_newVelCmd) {
        g_newVelCmd = false;
        mecanumDrive(g_cmdVx, g_cmdVy, g_cmdVr);
    }
}

// ====================================================================
//  SETUP
// ====================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CarryFinal STM32 Slave ===");

    // UART to ESP32
    Serial2.begin(ESP_BAUD);

    // hardware
    motorInit();
    lineInit();
    nfcInit();
    tofInit();
    autoRunnerInit();

    Serial.println("[BOOT] ready");
}

// ====================================================================
//  LOOP
// ====================================================================
static uint32_t lastTofPrint = 0;

void loop() {
    handleESP32();

    // ── ToF debug print mỗi 200ms ──────────────────────────────────
    {
        uint32_t now = millis();
        if (now - lastTofPrint >= 200) {
            lastTofPrint = now;
            int d = tofReadMm();
            Serial.printf("[TOF] %d mm  %s\n", d,
                          d <= TOF_STOP_MM ? "** OBSTACLE **" :
                          d <= TOF_RESUME_MM ? "(close)" : "ok");
        }
    }

    switch (g_mode) {
    case MODE_AUTO:
        autoRunnerLoop();
        // Read NFC when idle (for diagnostics / testing)
        if (!g_missionRunning) {
            uint16_t nfcId = nfcReadCheckpoint();
            if (nfcId != 0) {
                uint8_t buf[2] = { (uint8_t)(nfcId >> 8), (uint8_t)(nfcId & 0xFF) };
                uartSendFrame(Serial2, CMD_CHECKPOINT, buf, 2);
                Serial.printf("[NFC] idle scan: 0x%04X\n", nfcId);
            }
        }
        break;

    case MODE_FOLLOW:
    case MODE_FIND:
        followDrive();
        break;

    case MODE_RECOVERY:
        // recovery driving is handled by ESP32 sending direct velocity
        followDrive();

        // also read NFC and report to ESP32 if found
        {
            uint16_t nfcId = nfcReadCheckpoint();
            if (nfcId != 0) {
                uint8_t buf[2] = { (uint8_t)(nfcId >> 8), (uint8_t)(nfcId & 0xFF) };
                uartSendFrame(Serial2, CMD_CHECKPOINT, buf, 2);
            }
        }
        break;
    }

    delay(MAIN_LOOP_DELAY_MS);
}
