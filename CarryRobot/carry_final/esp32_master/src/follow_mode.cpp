#include "follow_mode.h"
#include "globals.h"
#include "config.h"
#include "relay_control.h"
#include "uart_protocol.h"
#include "huskylens_uart.h"
#include "servo_control.h"
#include "sr05.h"
#include "oled_display.h"
#include "buzzer.h"

extern HardwareSerial Serial2;

// ── PID-like follow constants ───────────────────────────────────────
#define HUSKY_CX          160      // screen center X (320/2)
#define HUSKY_CY          120      // screen center Y (240/2)
#define TARGET_AREA       4000     // desired tag area (distance proxy)
#define VR_GAIN           0.5f
#define VY_GAIN           0.3f
#define VX_WALL_GAIN      1.5f
#define TAG_LOST_MS       10000    // ms before switching to FIND (10 s)

static uint32_t lastTagTime = 0;

// ── send velocity to STM32 ──────────────────────────────────────────
static void sendVel(int16_t vx, int16_t vy, int16_t vr) {
    uint8_t buf[6];
    buf[0] = (vx >> 8) & 0xFF; buf[1] = vx & 0xFF;
    buf[2] = (vy >> 8) & 0xFF; buf[3] = vy & 0xFF;
    buf[4] = (vr >> 8) & 0xFF; buf[5] = vr & 0xFF;
    uartSendFrame(Serial2, CMD_DIRECT_VEL, buf, 6);
}

static void stopSTM32() { sendVel(0, 0, 0); }

// ──────────────────────────────────────────────────────────────────
void followModeInit() {
    relaySetFollow();
    huskyReconnect();   // re-init after relay vision powered on
    uint8_t m = MODE_FOLLOW;
    uartSendFrame(Serial2, CMD_SET_MODE, &m, 1);

    servoSetX(SERVO_X_CENTER);
    servoSetY(SERVO_Y_TILT_DOWN);
    huskySetTagMode();
    lastTagTime = millis();
    Serial.println("[FOLLOW] init");
}

void followModeLoop() {
    if (g_mode != MODE_FOLLOW) return;

    // ── double click → Recovery ─────────────────────────────────────
    if (g_btnDoubleClick) {
        g_btnDoubleClick = false;
        stopSTM32();
        g_mode = MODE_RECOVERY;
        Serial.println("[FOLLOW] double click → RECOVERY");
        return;
    }

    // ── read HuskyLens ──────────────────────────────────────────────
    HuskyResult tag = huskyRead();

    if (tag.detected) {
        lastTagTime = millis();

        // rotation: tag X offset → Vr
        float errX = (float)(tag.xCenter - HUSKY_CX);
        int16_t vr = (int16_t)(errX * VR_GAIN);

        // forward/backward: area → Vy
        int16_t area = tag.width * tag.height;
        float errArea = (float)(TARGET_AREA - area);
        int16_t vy = (int16_t)(errArea * VY_GAIN);
        vy = constrain(vy, -200, 200);

        // lateral wall avoidance
        int16_t vx = 0;
        float wl = sr05ReadLeft();
        float wr = sr05ReadRight();
        if (wl > 0 && wl < SR05_WALL_WARN_CM) vx += (int16_t)((SR05_WALL_WARN_CM - wl) * VX_WALL_GAIN);
        if (wr > 0 && wr < SR05_WALL_WARN_CM) vx -= (int16_t)((SR05_WALL_WARN_CM - wr) * VX_WALL_GAIN);

        // adjust servo Y based on tag Y position
        float errY = (float)(tag.yCenter - HUSKY_CY);
        int newY = servoGetY() - (int)(errY * 0.05f);
        servoSetY(constrain(newY, 20, 150));

        sendVel(vx, vy, vr);

        // OLED
        static uint32_t lastOled = 0;
        if (millis() - lastOled > OLED_UPDATE_MS) {
            oledFollowMode(tag.xCenter, tag.yCenter, area, wl, wr);
            lastOled = millis();
        }

    } else {
        // tag not seen
        if (millis() - lastTagTime > TAG_LOST_MS) {
            stopSTM32();            buzzerBeepN(2, 250, 100);  // ⚠ target lost            g_mode = MODE_FIND;
            Serial.println("[FOLLOW] tag lost → FIND");
        }
    }
}
