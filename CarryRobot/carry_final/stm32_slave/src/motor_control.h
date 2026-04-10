#pragma once
#include <Arduino.h>
#include "config.h"

void motorInit();

// set individual motor PWM (-255 … +255)
void motorSet(int fl, int fr, int bl, int br);

// stop all motors immediately
void motorStop();

// brake: brief reverse pulse then stop
void motorBrake();
