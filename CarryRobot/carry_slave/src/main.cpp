// ====================================================================
//  Carry Robot – STM32 SLAVE (BlackPill STM32F411CE)
//  ------------------------------------------------------------------
//  100% NATIVE STM32CUBE HAL + EMBEDDED C++ OOP + ZERO-MALLOC
// ====================================================================
#include "stm32f4xx_hal.h"
#include "bsp_clock.h"
#include "bsp_pins.h"
#include "bsp_gpio.h"
#include "bsp_tim.h"
#include "bsp_uart.h"
#include "bsp_spi.h"

#include "motor_tank_driver.hpp"
#include "line_sensor.hpp"
#include "ultrasonic_sr05.hpp"
#include "pn532_hal_driver.hpp"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ------ Global Objects (Tĩnh, không dùng Heap) -----------------------
static MotorTankDriver g_drive;
static LineSensor      g_line;
static Pn532HalDriver  g_rfid;

// ------ FSM State Machine -------------------------------------------
enum class Mode : uint8_t { AUTO, FOLLOW, FOLLOW_RECOVERY };
enum class Phase : uint8_t {
    IDLE,
    ROUTE_LOADED,
    EXECUTING,
    CANCEL_SEARCH_CP,
    FREC_LINE_SEARCH,
    FREC_APPROACH,
    FREC_TRACK_TO_CP
};

static Mode  g_mode  = Mode::AUTO;
static Phase g_phase = Phase::IDLE;

#define FLAG_OBSTACLE    (1 << 0)
#define FLAG_TURNING     (1 << 1)
#define FLAG_STRTILLLINE (1 << 2)
static uint8_t g_statusFlags = 0;

// ------ Route Container ---------------------------------------------
struct Checkpoint {
    char id[MAX_NODE_ID_LEN];
    char action; // 'F', 'L', 'R', 'B'
};

static Checkpoint g_route[MAX_ROUTE_STEPS];
static uint8_t    g_routeN = 0;
static uint8_t    g_routeIdx = 0;

static char    g_lastHandledCp[MAX_NODE_ID_LEN] = {0};
static char    g_lastTurnTag[MAX_NODE_ID_LEN]   = {0};
static int16_t g_lastSr05Cm = 999;

// ------ CRC8 & Frame Framing ----------------------------------------
static uint8_t crc8_calc(const char *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

static void uartSend(const char *cmd) {
    uint8_t crc = crc8_calc(cmd, strlen(cmd));
    char buf[64];
    snprintf(buf, sizeof(buf), "<%s|%02X>", cmd, crc);
    BSP_UART_SendString(buf);
}

static void uartSend(const char *cmd, const char *data) {
    char content[128];
    snprintf(content, sizeof(content), "%s:%s", cmd, data);
    uint8_t crc = crc8_calc(content, strlen(content));
    char buf[160];
    snprintf(buf, sizeof(buf), "<%s|%02X>", content, crc);
    BSP_UART_SendString(buf);
}

// ------ Route Parser ------------------------------------------------
static void parseRoute(const char *payload) {
    g_routeN = 0;
    g_routeIdx = 0;
    if (!payload || !*payload) return;

    const char *p = payload;
    while (*p && g_routeN < MAX_ROUTE_STEPS) {
        Checkpoint &cp = g_route[g_routeN];
        size_t i = 0;
        while (*p && *p != ',' && *p != '|' && i < MAX_NODE_ID_LEN - 1) {
            cp.id[i++] = *p++;
        }
        cp.id[i] = '\0';
        cp.action = 'F';

        while (*p && *p != ',' && *p != '|') ++p;
        if (*p == ',') {
            ++p;
            if (*p && *p != '|') {
                cp.action = *p;
                ++p;
            }
        }
        while (*p && *p != '|') ++p;
        if (*p == '|') ++p;

        if (i > 0) g_routeN++;
    }
}

static const Checkpoint* expectedCp() {
    if (g_routeIdx >= g_routeN) return nullptr;
    return &g_route[g_routeIdx];
}

static void executeAction(char action, const char* tag) {
    g_drive.startBrakePulse();

    if (action == 'L') {
        if (tag && strcmp(tag, g_lastTurnTag) != 0) {
            g_drive.startTurn90Left();
            strncpy(g_lastTurnTag, tag, sizeof(g_lastTurnTag) - 1);
        }
    } else if (action == 'R') {
        if (tag && strcmp(tag, g_lastTurnTag) != 0) {
            g_drive.startTurn90Right();
            strncpy(g_lastTurnTag, tag, sizeof(g_lastTurnTag) - 1);
        }
    } else if (action == 'B') {
        if (tag && strcmp(tag, g_lastTurnTag) != 0) {
            g_drive.startTurn180();
            strncpy(g_lastTurnTag, tag, sizeof(g_lastTurnTag) - 1);
        }
    }
}

static void finishRoute(const char *cpId, char action) {
    g_drive.startBrakePulse();
    if (action == 'B') {
        if (cpId && strcmp(cpId, g_lastTurnTag) != 0) {
            g_drive.startTurn180();
            strncpy(g_lastTurnTag, cpId, sizeof(g_lastTurnTag) - 1);
        }
    }
    if (strcmp(cpId, START_CHECKPOINT) == 0) {
        uartSend("DONE", cpId);
    } else {
        uartSend("ARRIVED", cpId);
    }
    g_phase = Phase::IDLE;
    g_routeN = 0;
    g_routeIdx = 0;
}

static void handleCheckpoint(const char* scannedId) {
    if (!scannedId || !*scannedId) return;
    const bool unknown = (scannedId[0] == '?');

    if (g_phase == Phase::EXECUTING || g_phase == Phase::CANCEL_SEARCH_CP ||
        g_phase == Phase::FREC_TRACK_TO_CP) {
        g_drive.startBrakePulse();
    }

    // Telemetry Scan
    {
        const Checkpoint *exp = expectedCp();
        char buf[80];
        snprintf(buf, sizeof(buf), "%s,phase=%d,exp=%s", scannedId,
                 (int)g_phase, exp ? exp->id : "-");
        uartSend("SCAN", buf);
    }

    const bool needDedup = (g_phase == Phase::EXECUTING || 
                            g_phase == Phase::CANCEL_SEARCH_CP ||
                            g_phase == Phase::FREC_TRACK_TO_CP);
    if (needDedup && strcmp(scannedId, g_lastHandledCp) == 0) {
        return;
    }
    strncpy(g_lastHandledCp, scannedId, sizeof(g_lastHandledCp) - 1);

    if (g_phase == Phase::EXECUTING) {
        const Checkpoint *exp = expectedCp();
        if (!exp) return;

        if (unknown) {
            g_drive.startBrakePulse();
            if (strcmp(scannedId, g_lastTurnTag) != 0) {
                g_drive.startTurn180();
                strncpy(g_lastTurnTag, scannedId, sizeof(g_lastTurnTag) - 1);
            }
            uartSend("WRONG_CP", scannedId);
            g_phase = Phase::IDLE;
            g_routeN = 0;
            g_routeIdx = 0;
            return;
        }

        if (strcmp(scannedId, exp->id) == 0) {
            char act = exp->action;
            bool last = (g_routeIdx == g_routeN - 1);
            if (last) {
                finishRoute(exp->id, act);
            } else {
                uartSend("CP_REACHED", exp->id);
                executeAction(act, scannedId);
                g_routeIdx++;
            }
        } else {
            g_drive.startBrakePulse();
            if (strcmp(scannedId, g_lastTurnTag) != 0) {
                g_drive.startTurn180();
                strncpy(g_lastTurnTag, scannedId, sizeof(g_lastTurnTag) - 1);
            }
            uartSend("WRONG_CP", scannedId);
            g_phase = Phase::IDLE;
            g_routeN = 0;
            g_routeIdx = 0;
        }
        return;
    }

    if (g_phase == Phase::CANCEL_SEARCH_CP || g_phase == Phase::FREC_TRACK_TO_CP) {
        g_drive.startBrakePulse();
        if (g_phase == Phase::CANCEL_SEARCH_CP && strcmp(scannedId, g_lastTurnTag) != 0) {
            g_drive.startTurn180();
            strncpy(g_lastTurnTag, scannedId, sizeof(g_lastTurnTag) - 1);
        }
        uartSend("CP_REACHED", scannedId);
        g_phase = Phase::IDLE;
        return;
    }

    // IDLE Broadcast
    static char sLastEmitId[MAX_NODE_ID_LEN] = {0};
    static uint32_t sLastEmitMs = 0;
    if (!unknown) {
        uint32_t now = HAL_GetTick();
        bool changed = (strcmp(scannedId, sLastEmitId) != 0);
        bool overdue = (now - sLastEmitMs >= 5000);
        if (changed || overdue) {
            uartSend("IDLE_SCAN", scannedId);
            strncpy(sLastEmitId, scannedId, sizeof(sLastEmitId) - 1);
            sLastEmitMs = now;
        }
    }
}

// ------ Lệnh Điều Khiển Từ Master -----------------------------------
static void onFrame(const char *cmd, const char *data) {
    if (strcmp(cmd, "MODE") == 0) {
        if (strcmp(data, "FOLLOW") == 0) {
            g_mode = Mode::FOLLOW;
            g_drive.stop();
        } else if (strcmp(data, "FOLLOW_RECOVERY") == 0) {
            g_mode = Mode::FOLLOW_RECOVERY;
            g_drive.stop();
            g_phase = Phase::IDLE;
        } else {
            g_mode = Mode::AUTO;
            g_drive.stop();
            g_phase = Phase::IDLE;
        }
    } else if (strcmp(cmd, "ROUTE") == 0) {
        if (g_mode != Mode::FOLLOW) {
            parseRoute(data);
            g_lastHandledCp[0] = '\0';
            g_phase = (g_routeN > 0) ? Phase::ROUTE_LOADED : Phase::IDLE;
        }
    } else if (strcmp(cmd, "START") == 0) {
        if (g_mode != Mode::FOLLOW && g_phase == Phase::ROUTE_LOADED && g_routeN > 0) {
            g_lastHandledCp[0] = '\0';
            g_lastTurnTag[0] = '\0';
            g_statusFlags |= FLAG_STRTILLLINE;
            g_phase = Phase::EXECUTING;

            const Checkpoint &first = g_route[0];
            strncpy(g_lastHandledCp, first.id, sizeof(g_lastHandledCp) - 1);
            if (g_routeN == 1) {
                finishRoute(first.id, first.action);
            } else {
                uartSend("CP_REACHED", first.id);
                executeAction(first.action, first.id);
                g_routeIdx = 1;
            }
        }
    } else if (strcmp(cmd, "CANCEL_MISSION") == 0) {
        if (g_mode != Mode::FOLLOW && g_phase == Phase::EXECUTING) {
            g_phase = Phase::CANCEL_SEARCH_CP;
        }
    }
}

// ====================================================================
//  HÀM MAIN CHUẨN C/C++ (KHÔNG CÒN ARDUINO SETUP / LOOP)
// ====================================================================
int main(void) {
    // 1. Khởi tạo STM32Cube HAL và Clock hệ thống 100MHz
    HAL_Init();
    SystemClock_Config();

    // 2. Khởi tạo các ngoại vi phần cứng (BSP)
    BSP_GPIO_Init();
    BSP_TIM4_PWM_Init();
    BSP_USART1_Init();

    g_drive.init();
    g_rfid.begin();

    // 3. Vòng lặp thời gian thực Real-Time Non-blocking
    uint32_t lastMotionMs = 0;
    uint32_t lastSafetyMs = 0;
    uint32_t lastSensorsMs = 0;
    uint32_t lastHbMs = 0;
    uint8_t  hits = 0, miss = 0;

    char rxBuf[128];
    size_t rxLen = 0;
    bool inFrame = false;
    char scannedId[MAX_NODE_ID_LEN] = {0};

    while (1) {
        uint32_t now = HAL_GetTick();

        // TASK 1: Motion Control (Chu kỳ 20ms = 50Hz)
        if (now - lastMotionMs >= 20) {
            lastMotionMs = now;

            if (g_drive.updateTurnStateMachine(now)) {
                g_statusFlags &= ~FLAG_TURNING;
            }

            if (g_drive.isTurning()) {
                g_statusFlags |= FLAG_TURNING;
            } else if (g_statusFlags & FLAG_OBSTACLE) {
                g_drive.stop();
            } else {
                bool tracking = (g_phase == Phase::EXECUTING || 
                                 g_phase == Phase::CANCEL_SEARCH_CP ||
                                 g_phase == Phase::FREC_TRACK_TO_CP);
                if (tracking) {
                    if (g_statusFlags & FLAG_STRTILLLINE) {
                        if (HAL_GPIO_ReadPin(LINE_C_PORT, LINE_C_PIN) == GPIO_PIN_RESET) {
                            g_statusFlags &= ~FLAG_STRTILLLINE;
                        } else {
                            g_drive.drive(LF_BASE_PWM, LF_BASE_PWM);
                        }
                    } else {
                        int outL = 0, outR = 0;
                        g_line.step(outL, outR);
                        g_drive.drive((int16_t)outL, (int16_t)outR);
                    }
                } else {
                    g_drive.stop();
                }
            }
        }

        // TASK 2: Safety Check (SR05 Siêu Âm, Chu kỳ 15ms)
        if (now - lastSafetyMs >= 15) {
            lastSafetyMs = now;
            long cm = UltrasonicSr05::readDistanceCm();
            g_lastSr05Cm = (int16_t)cm;

            if (!(g_statusFlags & FLAG_OBSTACLE)) {
                if (cm > 0 && cm < SR05_STOP_CM) {
                    if (++hits >= 2) {
                        g_statusFlags |= FLAG_OBSTACLE;
                        if (!(g_statusFlags & FLAG_TURNING)) {
                            g_drive.emergencyBrake();
                        }
                        uartSend("OBSTACLE", "1");
                        hits = 0;
                        miss = 0;
                    }
                } else {
                    hits = 0;
                }
            } else {
                if (cm >= 999 || cm >= SR05_RESUME_CM) {
                    if (++miss >= 2) {
                        g_statusFlags &= ~FLAG_OBSTACLE;
                        uartSend("OBSTACLE", "0");
                        miss = 0;
                        hits = 0;
                    }
                } else {
                    miss = 0;
                }
            }
        }

        // TASK 3: Sensors Polling (RFID PN532, Chu kỳ 50ms)
        if (now - lastSensorsMs >= 50) {
            lastSensorsMs = now;
            if (g_mode != Mode::FOLLOW) {
                if (g_rfid.poll(scannedId, sizeof(scannedId))) {
                    handleCheckpoint(scannedId);
                }
            }
        }

        // TASK 4: UART Communication (Nhận từng byte từ Ring Buffer)
        uint8_t byteIn = 0;
        while (BSP_UART_GetByte(&byteIn)) {
            char c = (char)byteIn;
            if (c == '<') {
                rxLen = 0;
                inFrame = true;
                continue;
            }
            if (c == '>' && inFrame) {
                inFrame = false;
                rxBuf[rxLen] = '\0';

                char *pipe = strrchr(rxBuf, '|');
                if (pipe && strlen(pipe + 1) == 2) {
                    uint8_t rxCrc = (uint8_t)strtol(pipe + 1, nullptr, 16);
                    uint8_t calcCrc = crc8_calc(rxBuf, (size_t)(pipe - rxBuf));
                    if (rxCrc == calcCrc) {
                        *pipe = '\0';
                        char *sep = strchr(rxBuf, ':');
                        if (sep) {
                            *sep = '\0';
                            onFrame(rxBuf, sep + 1);
                        } else {
                            onFrame(rxBuf, "");
                        }
                    }
                }
                rxLen = 0;
                continue;
            }
            if (inFrame && rxLen < sizeof(rxBuf) - 1) {
                rxBuf[rxLen++] = c;
            }
        }

        // TASK 5: Heartbeat Telemetry (Chu kỳ 1s)
        if (now - lastHbMs >= 1000) {
            lastHbMs = now;
            char buf[80];
            snprintf(buf, sizeof(buf), "m=%d,p=%d,obs=%d,L=%d,R=%d,sr=%d",
                     (int)g_mode, (int)g_phase,
                     (g_statusFlags & FLAG_OBSTACLE) ? 1 : 0,
                     g_drive.lastL(), g_drive.lastR(), g_lastSr05Cm);
            uartSend("HB", buf);

            // Nháy LED PC13 kiểm tra trạng thái hoạt động (Heartbeat LED)
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        }
    }
}
