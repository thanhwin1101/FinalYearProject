#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "HUSKYLENS.h"

// ── HuskyLens result (same struct as ESP32 version) ─────────────────
struct HuskyResult {
    bool    detected;
    int16_t xCenter;
    int16_t yCenter;
    int16_t width;
    int16_t height;
    int16_t id;
};

void        huskyInit(Stream &stream);
void        huskyReconnect();
bool        huskyConnected();
void        huskySetTagMode();
void        huskySetLineMode();
HuskyResult huskyRead();
