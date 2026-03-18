#pragma once
#include <Arduino.h>

void sensorsInit();

bool tofRead(uint16_t &distMm);

long usReadMm(uint8_t trigPin, uint8_t echoPin);
long usLeftMm();
long usRightMm();

// Task_Sensors helper (50ms): update global sensor cache in globals.cpp
void sensorsUpdateCache50ms();
