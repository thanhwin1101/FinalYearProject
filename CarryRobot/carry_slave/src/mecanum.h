#pragma once
#include <Arduino.h>
#include "config.h"

void mecanumInit();
void mecanumDrive(float vX, float vY, float vR);
void mecanumStop();
void mecanumHardBrake(uint8_t pwm = PWM_BRAKE, uint16_t brakeMs = BRAKE_MS);

void mecanumTurnLeft90();
void mecanumTurnRight90();
void mecanumUturn();
