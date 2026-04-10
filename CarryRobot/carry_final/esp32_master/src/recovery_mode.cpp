#include "recovery_mode.h"
#include "globals.h"
#include "config.h"
#include "relay_control.h"
#include "uart_protocol.h"
#include "huskylens_uart.h"
#include "servo_control.h"
#include "oled_display.h"
#include "mqtt_client.h"
#include "buzzer.h"

extern HardwareSerial Serial2;

// ── recovery phases ─────────────────────────────────────────────────
enum RecPhase : uint8_t {
    REC_INIT,
    REC_FIND_LINE,
    REC_ALIGN_LINE,
    REC_READ_CHECKPOINT,
    REC_WAIT_ROUTE,
    REC_DONE
};

static RecPhase phase = REC_INIT;
static int      sweepAngle = 0;
static int      sweepDir   = 1;
static uint8_t  sweepRetry = 0;
static uint32_t lastMs     = 0;
static uint32_t cpTimeout  = 0;
static uint32_t routeTimeout = 0;

// ── send velocity ───────────────────────────────────────────────────
static void sendVel(int16_t vx, int16_t vy, int16_t vr) {
    uint8_t buf[6];
    buf[0] = (vx >> 8); buf[1] = vx;
    buf[2] = (vy >> 8); buf[3] = vy;
    buf[4] = (vr >> 8); buf[5] = vr;
    uartSendFrame(Serial2, CMD_DIRECT_VEL, buf, 6);
}
static void stopSTM32() { sendVel(0, 0, 0); }

// ──────────────────────────────────────────────────────────────────
void recoveryModeInit() {
    stopSTM32();
    relaySetRecovery();
    huskyReconnect();   // re-init after relay vision powered on

    servoSetY(100);
    servoSetX(SERVO_X_CENTER);
    huskySetLineMode();

    phase      = REC_FIND_LINE;
    sweepAngle = 0;
    sweepDir   = 1;
    sweepRetry = 0;
    lastMs     = millis();

    oledRecovery("Finding line...");
    Serial.println("[RECOVERY] init – finding line");
}

void recoveryModeLoop() {
    if (g_mode != MODE_RECOVERY) return;
    uint32_t now = millis();

    switch (phase) {

    // ── Phase 1: sweep servo X to find line via HuskyLens ───────────
    case REC_FIND_LINE:
        if (now - lastMs >= 50) {
            lastMs = now;
            sweepAngle += sweepDir;          // 1°/50ms
            if (sweepAngle > 180) { sweepAngle = 180; sweepDir = -1; }
            if (sweepAngle < 0) {
                // full sweep done (0→180→0), no line found
                sweepRetry++;
                buzzerBeepN(3);
                Serial.printf("[RECOVERY] sweep fail #%u\n", sweepRetry);
                if (sweepRetry >= 3) {
                    oledError("No line found!");
                    Serial.println("[RECOVERY] 3 sweep fails – staying");
                    // stay in phase, keep trying but slower
                }
                sweepAngle = 0; sweepDir = 1;
            }
            servoSetX(sweepAngle);

            HuskyResult r = huskyRead();
            if (r.detected) {
                Serial.printf("[RECOVERY] line at servo X=%d\n", sweepAngle);
                phase = REC_ALIGN_LINE;
            }
        }
        oledRecovery("Scanning for line...");
        break;

    // ── Phase 2: drive robot so line sensor center picks up line ────
    case REC_ALIGN_LINE: {
        HuskyResult r = huskyRead();
        if (r.detected) {
            // steer toward line: x offset → rotation
            float errX = (float)(r.xCenter - 160);
            int16_t vr = (int16_t)(errX * 0.3f);
            sendVel(0, 80, vr);     // crawl forward while aligning
        } else {
            stopSTM32();
        }

        // ask STM32 for line sensor status via status request
        // when STM32 reports NFC checkpoint, we know we're on track
        if (g_newCheckpoint) {
            g_newCheckpoint = false;
            stopSTM32();
            Serial.printf("[RECOVERY] on line, CP=%u\n", g_lastCheckpointId);
            phase = REC_READ_CHECKPOINT;
        }

        oledRecovery("Aligning to line...");
    } break;

    // ── Phase 3: read NFC checkpoint ────────────────────────────────
    case REC_READ_CHECKPOINT:
        // we already have a checkpoint from align phase
        if (g_lastCheckpointId != 0) {
            mqttPublishReturnRequest(g_lastCheckpointId);
            routeTimeout = now;
            phase = REC_WAIT_ROUTE;
            oledRecovery("Requesting route...");
            Serial.printf("[RECOVERY] requesting return from CP %u\n", g_lastCheckpointId);
        } else {
            // wait for STM32 to read NFC
            cpTimeout = now;
            oledRecovery("Reading NFC...");
            if (g_newCheckpoint) {
                g_newCheckpoint = false;
                mqttPublishReturnRequest(g_lastCheckpointId);
                routeTimeout = now;
                phase = REC_WAIT_ROUTE;
            }
            if (now - cpTimeout > 30000) {
                buzzerBeepN(5);
                oledError("NFC timeout!");
                // stay in recovery, keep trying
                cpTimeout = now;
            }
        }
        break;

    // ── Phase 4: wait for return route from backend ─────────────────
    case REC_WAIT_ROUTE:
        oledRecovery("Waiting for route...");
        // MQTT callback will set g_autoState = AUTO_RETURNING when route arrives
        if (g_autoState == AUTO_RETURNING) {
            phase = REC_DONE;
        }
        if (now - routeTimeout > 15000) {
            // retry
            mqttPublishReturnRequest(g_lastCheckpointId);
            routeTimeout = now;
            Serial.println("[RECOVERY] route timeout, retrying");
        }
        break;

    // ── Phase 5: switch to Auto mode to execute return route ────────
    case REC_DONE:
        relaySetAuto();
        servoSetX(SERVO_X_CENTER);
        servoSetY(SERVO_Y_LEVEL);
        g_mode = MODE_AUTO;
        // g_autoState already set to AUTO_RETURNING by MQTT callback
        Serial.println("[RECOVERY] → AUTO (returning)");
        buzzerBeep(100);
        break;

    default:
        phase = REC_FIND_LINE;
        break;
    }
}
