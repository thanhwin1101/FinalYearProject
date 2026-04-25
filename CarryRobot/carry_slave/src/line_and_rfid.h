#pragma once
// ====================================================================
//  Carry Robot – STM32 Slave – 3-eye line follower + PN532 RFID (SPI)
//  Pins & constants come from config.h
// ====================================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include "config.h"
#include "checkpoint_map.h"

// --------------------------------------------------------------------
//  Line follower with simple PD from 3 digital sensors
// --------------------------------------------------------------------
class LineFollower {
public:
    void begin() {
        pinMode(PIN_LINE_L, INPUT);
        pinMode(PIN_LINE_C, INPUT);
        pinMode(PIN_LINE_R, INPUT);
    }

    // Returns error in range [-2..+2] (left negative, right positive)
    int readError() {
        int l = (digitalRead(PIN_LINE_L) == LOW) ? 1 : 0;
        int c = (digitalRead(PIN_LINE_C) == LOW) ? 1 : 0;
        int r = (digitalRead(PIN_LINE_R) == LOW) ? 1 : 0;
        int mask = (l << 2) | (c << 1) | r;
        switch (mask) {
            case 0b010: return  0;   // only center
            case 0b110: return -1;   // L+C
            case 0b100: return -2;   // only L
            case 0b011: return +1;   // C+R
            case 0b001: return +2;   // only R
            case 0b111: return  0;   // all (intersection)
            default:    return _lastErr;
        }
    }

    // Compute left/right motor speeds from PD controller.
    void step(int& outL, int& outR) {
        int err = readError();
        int d   = err - _lastErr;
        _lastErr = err;
        int corr = LF_KP * err + LF_KD * d;
        outL = LF_BASE_PWM + corr;
        outR = LF_BASE_PWM - corr;
        outL = constrain(outL, 0, 230);
        outR = constrain(outR, 0, 230);
    }

private:
    int _lastErr = 0;
};

// --------------------------------------------------------------------
//  PN532 – read checkpoint UID → hex string
// --------------------------------------------------------------------
class RfidCheckpoint {
public:
    bool begin() {
        // Hardware-SPI constructor: only CS pin needed; SPI uses PA5/PA6/PA7.
        if (!_pn532) _pn532 = new Adafruit_PN532(PN532_SS);
        SPI.begin();
        _pn532->begin();
        uint32_t v = _pn532->getFirmwareVersion();
        if (!v) return false;
        _pn532->SAMConfig();
        _ready = true;
        return true;
    }

    // Re-initialise after the PN532 has been power-cycled by the relay.
    // Tries a few times because the module takes a few ms to boot.
    bool reinit() {
        _ready = false;
        _lastId = ""; _lastMs = 0;
        for (int i = 0; i < 3; ++i) {
            if (begin()) return true;
            delay(50);
        }
        return false;
    }

    bool ready() const { return _ready; }

    // Returns non-empty *nodeId* string when a NEW recognised tag is read.
    // Same-tag duplicates within NFC_REPEAT_MS are suppressed.
    // Unknown UIDs (not in CHECKPOINT_MAP) are silently dropped.
    String poll() {
        if (!_pn532 || !_ready) return "";
        uint8_t uid[7]; uint8_t uidLen = 0;
        bool ok = _pn532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, NFC_POLL_TIMEOUT_MS);
        if (!ok || uidLen == 0) return "";

        char hex[16] = {0};
        for (uint8_t i = 0; i < uidLen && i < 4; ++i) {
            snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02X", uid[i]);
        }

        const char* nodeId = uidToNodeId(hex);
        uint32_t now = millis();
        // Build sentinel "?HEX" for unknown UID so the FSM can detect
        // an off-route checkpoint instead of dropping it silently.
        String id;
        if (nodeId && *nodeId) id = nodeId;
        else                   { id = "?"; id += hex; }
        if (id == _lastId && now - _lastMs < NFC_REPEAT_MS) return "";
        _lastId = id; _lastMs = now;
        return id;
    }

private:
    Adafruit_PN532* _pn532  = nullptr;
    String          _lastId = "";
    uint32_t        _lastMs = 0;
    bool            _ready  = false;
};
