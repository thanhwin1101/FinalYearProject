#pragma once
#include <Arduino.h>
#include "config.h"

void motorInit();

// set individual motor PWM (-255 … +255)
void motorSet(int fl, int fr, int bl, int br);

// stop all motors immediately
void motorStop();

// brake: soft ramp-down then stop (bảo vệ bánh răng nhựa)
void motorBrake();

// soft-stop: hạ PWM từ từ về 0 (không ngắt đột ngột)
void motorSoftStop();

// kick-start: cấp PWM cao trong MOTOR_KICK_MS, rồi xuống target
// Dùng cho điểm bắt đầu chuyển động từ trạng thái đứng yên
void motorKickThenRun(int fl, int fr, int bl, int br);
