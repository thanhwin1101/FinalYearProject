#include "find_mode.h"
#include "globals.h"
#include "config.h"
#include "uart_protocol.h"
#include "huskylens_uart.h"
#include "servo_control.h"
#include "sr05.h"
#include "oled_display.h"
#include "buzzer.h"

extern HardwareSerial Serial2;

#define FIND_SLOW_VY    100      // mm/s – crawl forward
#define FIND_TURN_VR    80       // rotation speed for search
#define FIND_TURN_MS    1200     // duration of each search turn
#define FIND_MAX_TRIES  3
#define FIND_WALL_CHK   100      // ms between wall checks

static uint8_t  attempts   = 0;
static bool     prevWallL  = false;
static bool     prevWallR  = false;
static uint32_t lastWallMs = 0;
static bool     turning    = false;
static int16_t  turnDir    = 0;
static uint32_t turnStart  = 0;

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
void findModeInit() {
    attempts  = 0;
    turning   = false;
    prevWallL = sr05WallLeft();
    prevWallR = sr05WallRight();
    lastWallMs = millis();

    // creep forward
    sendVel(0, FIND_SLOW_VY, 0);
    Serial.println("[FIND] init – creeping forward");
}

void findModeLoop() {
    if (g_mode != MODE_FIND) return;
    uint32_t now = millis();

    // ── double click → Recovery ─────────────────────────────────────
    if (g_btnDoubleClick) {
        g_btnDoubleClick = false;
        stopSTM32();
        g_mode = MODE_RECOVERY;
        Serial.println("[FIND] double click → RECOVERY");
        return;
    }

    // ── check HuskyLens while turning or moving ─────────────────────
    HuskyResult tag = huskyRead();
    if (tag.detected) {
        stopSTM32();
        servoSetX(SERVO_X_CENTER);        buzzerBeepN(3, 100, 50);  // ♪ target re-acquired        g_mode = MODE_FOLLOW;
        Serial.println("[FIND] tag found → FOLLOW");
        return;
    }

    // ── turning phase ───────────────────────────────────────────────
    if (turning) {
        if (now - turnStart >= FIND_TURN_MS) {
            // turn finished, no tag found
            turning = false;
            attempts++;
            if (attempts >= FIND_MAX_TRIES) {
                buzzerBeepN(3);
                Serial.println("[FIND] max tries – continuing straight");
            }
            // resume forward crawl
            sendVel(0, FIND_SLOW_VY, 0);
        }
        return;
    }

    // ── wall change detection ───────────────────────────────────────
    if (now - lastWallMs >= FIND_WALL_CHK) {
        lastWallMs = now;
        bool wl = sr05WallLeft();
        bool wr = sr05WallRight();

        bool changed = (wl != prevWallL) || (wr != prevWallR);
        prevWallL = wl;
        prevWallR = wr;

        if (changed && attempts < FIND_MAX_TRIES) {
            stopSTM32();
            // pick direction: prefer turning toward open side
            if (wl != prevWallL) turnDir = -FIND_TURN_VR;   // turn left
            else                 turnDir =  FIND_TURN_VR;   // turn right
            if (!wl && wr)       turnDir = -FIND_TURN_VR;   // left open
            if (wl && !wr)       turnDir =  FIND_TURN_VR;   // right open

            sendVel(0, 0, turnDir);
            turning   = true;
            turnStart = now;
            Serial.printf("[FIND] wall change → turn %s (attempt %u)\n",
                          turnDir < 0 ? "LEFT" : "RIGHT", attempts + 1);
        }
    }

    oledFindMode(attempts);
}
