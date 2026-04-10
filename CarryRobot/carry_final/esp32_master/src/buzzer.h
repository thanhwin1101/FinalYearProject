#pragma once
#include <Arduino.h>
#include "config.h"

void buzzerInit();
void buzzerBeep(uint16_t ms = 100);
void buzzerBeepN(uint8_t n, uint16_t onMs = 100, uint16_t offMs = 100);
void buzzerTone(uint16_t freq, uint16_t ms);
void buzzerOff();            // silence immediately
