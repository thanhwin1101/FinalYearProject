#pragma once
#include <Arduino.h>
#include "config.h"

void lineInit();

// returns error for PID:  negative=left, 0=center, positive=right
float lineReadError();

// raw sensor states (true = line detected, active low)
bool lineLeft();
bool lineCenter();
bool lineRight();

// returns true if at least one sensor sees line
bool lineDetected();

// ── health check (PN532-like) ───────────────────────────────────────
uint16_t lineConsecLost();     // consecutive reads with no line
void     lineHealthReset();    // reset counter (call on mode init)
