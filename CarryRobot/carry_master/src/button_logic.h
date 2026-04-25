#pragma once
// ====================================================================
//  Carry Robot – Master – Button debouncer (short / long press)
// ====================================================================
#include <Arduino.h>
#include "config.h"

enum class BtnEvent { NONE, SHORT_PRESS, LONG_PRESS };

class Button {
public:
    void begin(uint8_t pin) {
        _pin = pin;
        pinMode(_pin, INPUT_PULLUP);
        _lastLevel    = HIGH;
        _stableLevel  = HIGH;
        _lastChangeMs = 0;
        _pressedMs    = 0;
        _longFired    = false;
    }

    // Call every loop; returns SHORT/LONG/NONE.
    BtnEvent poll() {
        uint32_t now = millis();
        int lvl = digitalRead(_pin);

        if (lvl != _lastLevel) {
            _lastLevel    = lvl;
            _lastChangeMs = now;
        }

        if ((now - _lastChangeMs) >= BTN_DEBOUNCE_MS && lvl != _stableLevel) {
            _stableLevel = lvl;
            if (_stableLevel == LOW) {                    // pressed
                _pressedMs = now;
                _longFired = false;
            } else {                                      // released
                if (!_longFired && _pressedMs) {
                    _pressedMs = 0;
                    return BtnEvent::SHORT_PRESS;
                }
                _pressedMs = 0;
            }
        }

        if (_stableLevel == LOW && !_longFired && _pressedMs
            && (now - _pressedMs) >= BTN_LONG_MS) {
            _longFired = true;
            return BtnEvent::LONG_PRESS;
        }
        return BtnEvent::NONE;
    }

private:
    uint8_t  _pin          = 0;
    int      _lastLevel    = HIGH;
    int      _stableLevel  = HIGH;
    uint32_t _lastChangeMs = 0;
    uint32_t _pressedMs    = 0;
    bool     _longFired    = false;
};
