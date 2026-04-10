#include "auto_runner.h"
#include "globals.h"
#include "config.h"
#include "mecanum.h"
#include "motor_control.h"
#include "line_sensor.h"
#include "pn532_reader.h"
#include "tof_sensor.h"
#include "uart_protocol.h"

extern HardwareSerial Serial2;   // UART to ESP32

// ── line-lost threshold (~50 × 2ms main loop = ~100ms) ─────────────
#define LINE_LOST_THRESHOLD  50
static bool     s_lineLostSent = false;
static uint32_t s_lastLineLostLog = 0;

// Send short debug text to ESP32 (shows as [STM32] ... on monitor)
static void sendDebug(const char *msg) {
    uint8_t n = strlen(msg);
    if (n > 100) n = 100;
    uartSendFrame(Serial2, CMD_DEBUG_MSG, (const uint8_t*)msg, n);
}

// ── PID state ───────────────────────────────────────────────────────
static float prevErr    = 0.0f;
static float integral   = 0.0f;

// ── auto runner states ──────────────────────────────────────────────
enum RunState : uint8_t {
    RUN_IDLE,
    RUN_LINE_FOLLOW,
    RUN_TURNING,
    RUN_OBSTACLE,
    RUN_DONE
};

static RunState  runState = RUN_IDLE;
static uint32_t  turnEnd  = 0;
static bool      obstacleReported = false;

// ── report checkpoint to ESP32 ──────────────────────────────────────
static void reportCheckpoint(uint16_t id) {
    uint8_t buf[2] = { (uint8_t)(id >> 8), (uint8_t)(id & 0xFF) };
    uartSendFrame(Serial2, CMD_CHECKPOINT, buf, 2);
}

static void reportMissionDone() {
    uartSendFrame(Serial2, CMD_MISSION_DONE, nullptr, 0);
}

static void reportObstacle() {
    uartSendFrame(Serial2, CMD_OBSTACLE, nullptr, 0);
}

static void reportMismatch(uint16_t got, uint16_t expected) {
    uint8_t buf[4] = {
        (uint8_t)(got >> 8),      (uint8_t)(got & 0xFF),
        (uint8_t)(expected >> 8), (uint8_t)(expected & 0xFF)
    };
    uartSendFrame(Serial2, CMD_MISMATCH, buf, 4);
}

// ── line-follow PID step ────────────────────────────────────────────
static void lineFollowStep() {
    float err = lineReadError();

    // ── line-lost health check ──────────────────────────────────────
    uint16_t lost = lineConsecLost();
    if (lost >= LINE_LOST_THRESHOLD) {
        if (!s_lineLostSent) {
            s_lineLostSent = true;
            sendDebug("LINE: lost line");
            uartSendFrame(Serial2, CMD_LINE_LOST, nullptr, 0);
        }
        // throttle repeated debug logs to every 2s
        uint32_t now = millis();
        if (now - s_lastLineLostLog > 2000) {
            s_lastLineLostLog = now;
            Serial.printf("[LINE] lost %u reads\n", lost);
        }
    } else {
        s_lineLostSent = false;
    }

    integral += err;
    float deriv = err - prevErr;
    prevErr = err;

    float correction = LF_KP * err + LF_KI * integral + LF_KD * deriv;
    correction = constrain(correction, -LF_MAX_CORR, LF_MAX_CORR);

    int vr = (int)correction;
    int vy = LF_BASE_SPEED;

    mecanumDrive(0, vy, vr);
}

// ── execute turn action ─────────────────────────────────────────────
static void startTurn(uint8_t action) {
    switch (action) {
    case 'L':
        mecanumTurnLeft90();
        break;
    case 'R':
        mecanumTurnRight90();
        break;
    case 'B':
        mecanumTurn180();
        break;
    case 'S':  // stop at destination
        motorStop();
        break;
    default:   // 'F' – just keep going
        return;
    }
}

// ──────────────────────────────────────────────────────────────────
void autoRunnerInit() {
    runState  = RUN_IDLE;
    prevErr   = 0.0f;
    integral  = 0.0f;
    obstacleReported = false;
    lineHealthReset();
    s_lineLostSent = false;
}

bool autoRunnerBusy() {
    return runState == RUN_LINE_FOLLOW ||
           runState == RUN_TURNING ||
           runState == RUN_OBSTACLE;
}

void autoRunnerLoop() {
    if (g_mode != MODE_AUTO) return;

    // ── start mission ──────────────────────────────────────────────
    if (g_missionStart) {
        g_missionStart  = false;
        g_missionCancel = false;
        g_missionRunning = true;
        g_routeIdx = 0;
        runState = RUN_LINE_FOLLOW;
        prevErr  = 0.0f;
        integral = 0.0f;
        Serial.println("[RUN] mission start");
    }

    // ── cancel ─────────────────────────────────────────────────────
    if (g_missionCancel) {
        g_missionCancel  = false;
        g_missionRunning = false;
        motorStop();
        runState = RUN_IDLE;
        Serial.println("[RUN] cancelled → reading NFC");
        // read current NFC checkpoint and report to ESP32
        uint16_t nfcId = nfcReadCheckpoint();
        if (nfcId != 0) {
            reportCheckpoint(nfcId);
            Serial.printf("[RUN] cancel CP=%u\n", nfcId);
        }
        return;
    }

    switch (runState) {

    case RUN_IDLE:
        break;

    case RUN_LINE_FOLLOW:
        // obstacle check
        if (tofObstacle()) {
            motorStop();
            if (!obstacleReported) {
                reportObstacle();
                obstacleReported = true;
            }
            runState = RUN_OBSTACLE;
            break;
        }
        obstacleReported = false;

        // line follow
        lineFollowStep();

        // NFC checkpoint check
        {
            uint16_t nfcId = nfcReadCheckpoint();
            if (nfcId != 0 && g_routeIdx < g_routeLen) {
                uint16_t expected = g_route[g_routeIdx].checkpointId;
                uint8_t  action   = g_route[g_routeIdx].action;

                if (nfcId == expected) {
                    reportCheckpoint(nfcId);
                    g_routeIdx++;

                    // last checkpoint?
                    if (g_routeIdx >= g_routeLen || action == 'S') {
                        motorStop();
                        mecanumTurn180();     // quay 180° tại đích
                        motorStop();
                        reportMissionDone();
                        g_missionRunning = false;
                        runState = RUN_DONE;
                        Serial.println("[RUN] arrived → 180° → done");
                    } else {
                        // execute action at this checkpoint
                        startTurn(action);
                    }
                } else {
                    // mismatch!
                    motorBrake();
                    reportMismatch(nfcId, expected);
                    mecanumTurn180();
                    // wait for ESP32 to send new route
                    g_missionRunning = false;
                    runState = RUN_IDLE;
                    Serial.printf("[RUN] mismatch got=%u exp=%u\n", nfcId, expected);
                }
            }
        }
        break;

    case RUN_OBSTACLE:
        if (tofClear()) {
            runState = RUN_LINE_FOLLOW;
            Serial.println("[RUN] obstacle cleared");
        }
        break;

    case RUN_DONE:
        // wait for new mission or return route
        break;
    }
}
