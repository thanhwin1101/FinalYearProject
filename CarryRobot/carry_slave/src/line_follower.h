/*  line_follower.h  –  3-sensor line follower with PID
 */
#pragma once
#include <Arduino.h>

void lineInit();

// Read sensors, compute error, return PID correction for vR
// Returns rotation correction  (negative = turn left, positive = turn right)
float lineUpdate(float dt);

// Raw readings
uint8_t  lineGetBits();          // 3-bit bitmask  (bit 2 = left, bit 1 = centre, bit 0 = right)
int16_t  lineGetError();         // -2000 … +2000 (left … right)
bool     lineDetected();         // any sensor sees line
bool     lineCentred();          // centre sensor sees line

void     lineResetPID();
