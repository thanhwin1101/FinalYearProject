#pragma once
#include <Arduino.h>
#include "config.h"
#include "HUSKYLENS.h"

// ── HuskyLens result ────────────────────────────────────────────────
struct HuskyResult {
    bool    detected;
    int16_t xCenter;
    int16_t yCenter;
    int16_t width;
    int16_t height;
    int16_t id;
};

void        huskyInit(HardwareSerial &port);
void        huskyReconnect();      // re-init after relay power cycle
bool        huskyConnected();
void        huskySetTagMode();
void        huskySetLineMode();
HuskyResult huskyRead();           // poll once; returns latest result
