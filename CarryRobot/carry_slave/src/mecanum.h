/*  mecanum.h  –  4-wheel Mecanum drive via 2× L298N
 *
 *  Wheel layout:
 *    FL ─── FR       L298N #1 (Left):  FL=ENA, BL=ENB
 *    |       |       L298N #2 (Right): FR=ENA, BR=ENB
 *    BL ─── BR
 *
 *  Inverse kinematics:
 *    FL = vX + vY + vR
 *    FR = vX - vY - vR
 *    BL = vX - vY + vR
 *    BR = vX + vY - vR
 */
#pragma once
#include <Arduino.h>
#include "config.h"

void mecanumInit();
void mecanumDrive(float vX, float vY, float vR);   // each -255 … +255
void mecanumStop();
void mecanumHardBrake(uint8_t pwm = PWM_BRAKE, uint16_t brakeMs = BRAKE_MS);

// Timed manoeuvres (blocking)
void mecanumTurnLeft90();
void mecanumTurnRight90();
void mecanumUturn();
