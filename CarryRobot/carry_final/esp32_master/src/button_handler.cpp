#include "button_handler.h"
#include "globals.h"

static bool     lastStable   = HIGH;
static bool     lastRaw      = HIGH;
static uint32_t debounceTime = 0;
static uint32_t releaseTime  = 0;
static uint32_t pressTime    = 0;
static uint8_t  clickCount   = 0;
static bool     longFired    = false;

void buttonInit() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);   // GPIO15 with internal pull-up
}

void buttonLoop() {
    bool raw = digitalRead(PIN_BUTTON);
    uint32_t now = millis();

    // debounce
    if (raw != lastRaw) { debounceTime = now; lastRaw = raw; }
    if ((now - debounceTime) < BTN_DEBOUNCE_MS) return;

    bool stable = raw;

    // press edge → record start
    if (stable == LOW && lastStable == HIGH) {
        pressTime = now;
        longFired = false;
    }

    // long press — fire once while held
    if (stable == LOW && !longFired && (now - pressTime) >= BTN_LONG_MS) {
        longFired = true;
        clickCount = 0;   // discard any pending click
        g_btnLongPress = true;
    }

    // release edge → count click (skip if long press fired)
    if (stable == HIGH && lastStable == LOW) {
        if (!longFired) {
            releaseTime = now;
            clickCount++;
        }
    }

    // evaluate clicks after double-click window
    if (clickCount > 0 && stable == HIGH && (now - releaseTime) > BTN_DOUBLE_MS) {
        if (clickCount >= 2) {
            g_btnDoubleClick = true;
        } else {
            g_btnSingleClick = true;
        }
        clickCount = 0;
    }

    lastStable = stable;
}
