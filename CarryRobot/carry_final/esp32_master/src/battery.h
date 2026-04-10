#pragma once
#include <Arduino.h>
#include "config.h"

void    batteryInit();
void    batteryRead();       // updates g_batteryPercent
uint8_t batteryGetPercent();
float   batteryGetVoltage();
bool    batteryOk();         // returns true if ≥ BATT_MIN_PERCENT
