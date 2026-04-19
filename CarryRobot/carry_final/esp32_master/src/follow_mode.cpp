#include "follow_mode.h"
#include "globals.h"
#include "config.h"
#include "relay_control.h"
#include "uart_protocol.h"
#include "servo_control.h"
#include "oled_display.h"
#include "buzzer.h"

extern HardwareSerial Serial2;

#define HUSKY_CY  120      // screen center Y for servo adjustment

// ──────────────────────────────────────────────────────────────────
void followModeInit() {
    if (!relayGetVision()) {
        relaySetFollow();       // R1 ON, R2 OFF, 5s delay (only if needed)
    }
    uint8_t m = MODE_FOLLOW;
    uartSendFrame(Serial2, CMD_SET_MODE, &m, 1);

    servoSetY(SERVO_Y_TILT_DOWN);
    oledFollowEnter(true);
    Serial.println("[FOLLOW] init → STM32");
}

void followModeLoop() {
    if (g_mode != MODE_FOLLOW) return;

    static uint32_t s_tagLostAt    = 0;
    static uint32_t s_beepToggle   = 0;
    static bool     s_beepOn       = false;
    static uint32_t s_battWarnAt   = 0;   // millis() when low-battery warning started (0=none)
    static uint32_t s_battBeepTgl  = 0;
    static bool     s_battBeepOn   = false;

    uint32_t now = millis();

    // ── double click → Recovery (cancel any countdown) ─────────────
    if (g_btnDoubleClick) {
        g_btnDoubleClick = false;
        buzzerOff();
        s_tagLostAt  = 0;
        s_battWarnAt = 0;
        g_mode = MODE_RECOVERY;
        g_modeChangeReq = true;
        Serial.println("[FOLLOW] double click → RECOVERY");
        return;
    }

    // ── Battery low warning: ≤30% → 10s countdown then recovery ────
    if (g_batteryPercent <= BATT_MIN_PERCENT) {
        if (s_battWarnAt == 0) {
            s_battWarnAt  = now;
            s_battBeepTgl = now;
            s_battBeepOn  = false;
            Serial.printf("[FOLLOW] battery low (%u%%) → 10s warning\n", g_batteryPercent);
        }
        // fast beep 200ms on / 100ms off
        if (s_battBeepOn && now - s_battBeepTgl >= 200) {
            buzzerOff(); s_battBeepOn = false; s_battBeepTgl = now;
        } else if (!s_battBeepOn && now - s_battBeepTgl >= 100) {
            buzzerOn(); s_battBeepOn = true; s_battBeepTgl = now;
        }
        // OLED shows battery low + countdown
        static uint32_t s_lastBattOled = 0;
        if (now - s_lastBattOled >= 500) {
            s_lastBattOled = now;
            uint32_t elapsed = now - s_battWarnAt;
            uint8_t secsLeft = (elapsed < (uint32_t)BATT_FOLLOW_WARN_S * 1000UL)
                               ? (uint8_t)((BATT_FOLLOW_WARN_S * 1000UL - elapsed + 999) / 1000)
                               : 0;
            oledBatteryLow(g_batteryPercent);   // shows battery %
            (void)secsLeft;                     // already visible from buzzer urgency
        }
        // 10 seconds elapsed → enter recovery
        if (now - s_battWarnAt >= (uint32_t)BATT_FOLLOW_WARN_S * 1000UL) {
            buzzerOff();
            s_battWarnAt = 0;
            g_mode = MODE_RECOVERY;
            g_modeChangeReq = true;
            Serial.println("[FOLLOW] battery low 10s → RECOVERY");
            return;
        }
    } else {
        // battery recovered above threshold → cancel warning
        if (s_battWarnAt != 0) {
            buzzerOff();
            s_battWarnAt = 0;
        }
    }

    // ── STM32 reports tag lost → start 30s countdown ─────────────────
    if (g_stm32TagLost) {
        g_stm32TagLost = false;
        if (s_tagLostAt == 0) {
            s_tagLostAt  = now;
            s_beepToggle = now;
            s_beepOn     = false;
            Serial.println("[FOLLOW] tag lost → 30s countdown");
        }
    }

    // ── Tag found again → cancel countdown ──────────────────────────
    // g_stm32TagFound: explicit signal sent by STM32 as soon as tag is re-detected
    // g_huskyDetected: polled via CMD_HUSKY_STATUS every 200ms (fallback)
    {
        bool tagBack = g_huskyDetected || g_stm32TagFound;
        if (g_stm32TagFound) g_stm32TagFound = false;  // always consume
        if (s_tagLostAt != 0 && tagBack) {
            buzzerOff();
            s_tagLostAt = 0;
            Serial.println("[FOLLOW] tag regained → cancel countdown");
        }
    }

    // ── Countdown active: beep 300ms on / 150ms off continuously ────
    if (s_tagLostAt != 0) {
        if (s_beepOn && now - s_beepToggle >= 300) {
            buzzerOff();
            s_beepOn     = false;
            s_beepToggle = now;
        } else if (!s_beepOn && now - s_beepToggle >= 150) {
            buzzerOn();
            s_beepOn     = true;
            s_beepToggle = now;
        }

        // OLED countdown (update every 500ms to reduce flicker)
        static uint32_t s_lastOledLost = 0;
        if (now - s_lastOledLost >= 500) {
            s_lastOledLost = now;
            uint32_t elapsed = now - s_tagLostAt;
            uint8_t secsLeft = (elapsed < 30000UL)
                               ? (uint8_t)((30000UL - elapsed + 999) / 1000)
                               : 0;
            oledFollowTagLost(secsLeft);
        }

        // 30 seconds elapsed → enter recovery automatically
        if (now - s_tagLostAt >= 30000UL) {
            buzzerOff();
            s_tagLostAt = 0;
            g_mode = MODE_RECOVERY;
            g_modeChangeReq = true;
            Serial.println("[FOLLOW] tag lost 30s → RECOVERY");
            return;
        }
    }

    // ── Update servo Y + OLED from HuskyLens data ──────────────────
    if (g_huskyNew) {
        g_huskyNew = false;

        if (g_huskyDetected) {
            // adjust servo Y based on tag Y position
            float errY = (float)(g_huskyYCenter - HUSKY_CY);
            int newY = servoGetY() - (int)(errY * 0.05f);
            servoSetY(constrain(newY, 20, 150));
        }

        // OLED update
        static uint32_t lastOled = 0;
        if (millis() - lastOled > OLED_UPDATE_MS) {
            lastOled = millis();
            int16_t area = g_huskyWidth * g_huskyHeight;
            oledFollowMode(g_huskyXCenter, g_huskyYCenter, area);
        }
    }
}
