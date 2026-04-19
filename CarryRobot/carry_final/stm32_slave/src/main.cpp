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
#include "huskylens_uart.h"
#include "follow_runner.h"
#include "servo_control.h"

// USART2 for ESP32 communication
HardwareSerial Serial2(USART2);

// USART3 for HuskyLens
HardwareSerial Serial3(USART3);

// ── process frames from ESP32 ───────────────────────────────────────
static void handleESP32() {
    uint8_t cmd, buf[UART_MAX_FRAME], len;
    while (uartReceiveFrame(Serial2, cmd, buf, len)) {

        switch (cmd) {

        case CMD_SET_MODE:
            if (len >= 1) {
                g_mode = (RobotMode)buf[0];
                motorStop();

                switch (g_mode) {
                case MODE_AUTO:
                    autoRunnerInit();
                    nfcReset();
                    break;
                case MODE_FOLLOW:
                    followRunnerInit();
                    break;
                case MODE_RECOVERY:
                    nfcReset();
                    recoveryRunnerInit();
                    break;
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

        case CMD_WHEEL_SET:
            if (len >= 8) {
                g_wheelFL = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
                g_wheelFR = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
                g_wheelBL = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);
                g_wheelBR = (int16_t)(((uint16_t)buf[6] << 8) | buf[7]);
                g_newWheelCmd = true;
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

        case CMD_SERVO_SET:
            if (len >= 2) {
                // buf[0] = X placeholder (ignored), buf[1] = Y angle
                servoSetY(buf[1]);
            }
            break;

        case CMD_TUNE_SPEED:
            if (len >= 2) {
                g_runSpeed  = buf[0];
                g_turnSpeed = buf[1];
                Serial.printf("[UART] tune_speed run=%u turn=%u\n", g_runSpeed, g_turnSpeed);
            }
            break;

            Serial.printf("[UART] unknown cmd 0x%02X\n", cmd);
        }
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

    // UART to HuskyLens (USART3, PB10/PB11)
    Serial3.begin(HUSKY_BAUD);
    huskyInit(Serial3);

    // hardware
    motorInit();
    lineInit();
    nfcInit();
    tofInit();
    servoInit();
    autoRunnerInit();

    Serial.println("[BOOT] ready");
}

// ====================================================================
//  LOOP
// ====================================================================
static uint32_t lastTofPrint  = 0;
static uint32_t lastLineSend  = 0;
static uint32_t lastDirVelMs  = 0;
static const uint32_t DIR_VEL_TIMEOUT_MS = 400;

void loop() {
    handleESP32();


    // ── Direct velocity override (test lab D-pad) ───────────────────
    // g_newVelCmd is set by CMD_DIRECT_VEL; apply it and start timeout
    if (g_newVelCmd) {
        g_newVelCmd  = false;
        lastDirVelMs = millis();
        mecanumDrive(g_cmdVx, g_cmdVy, g_cmdVr);
    }
    // ── Per-wheel direct override (test lab individual wheels) ───────
    if (g_newWheelCmd) {
        g_newWheelCmd = false;
        lastDirVelMs  = millis();  // share same watchdog
        motorSet(g_wheelFL, g_wheelFR, g_wheelBL, g_wheelBR);
    }
    const bool inDirectVel = lastDirVelMs > 0 &&
                             (millis() - lastDirVelMs) < DIR_VEL_TIMEOUT_MS;
    // Watchdog: stop motors when timeout expires (no new cmd arriving)
    if (!inDirectVel && lastDirVelMs > 0) {
        lastDirVelMs = 0;
        motorStop();
    }

    // ── Periodic line sensor status → ESP32 (every 80ms) ────────────
    {
        uint32_t now = millis();
        if (now - lastLineSend >= 80) {
            lastLineSend = now;
            uint8_t bits = (lineLeft()   ? 0x01 : 0)
                         | (lineCenter() ? 0x02 : 0)
                         | (lineRight()  ? 0x04 : 0);
            uartSendFrame(Serial2, CMD_LINE_STATUS, &bits, 1);
        }
    }

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

    // ── Mode runners (skip when direct vel is active) ────────────────
    if (!inDirectVel) {
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
            followRunnerLoop();
            break;

        case MODE_RECOVERY:
            recoveryRunnerLoop();
            break;
        }
    }

    delay(MAIN_LOOP_DELAY_MS);
}
