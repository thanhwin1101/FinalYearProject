#pragma once
#include <Arduino.h>

void lineInit();

float lineUpdate(float dt);

uint8_t  lineGetBits();
int16_t  lineGetError();
bool     lineDetected();
bool     lineCentred();

void     lineResetPID();
