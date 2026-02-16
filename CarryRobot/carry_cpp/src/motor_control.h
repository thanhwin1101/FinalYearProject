#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include "config.h"

// Initialization
void motorPwmInit();
void motorsStop();

// Movement
void driveForward(int pwm);
void driveBackward(int pwm);

// Direction setup for turning
void setMotorDirLeft();
void setMotorDirRight();

// Braking (with defaults)
void applyForwardBrake(int brakePwm = PWM_BRAKE, int brakeMs = BRAKE_FORWARD_MS);
void applyHardBrake(bool wasTurningLeft, int brakePwm = PWM_BRAKE, int brakeMs = 50);

// Turning
void rotateByTime(unsigned long totalMs, bool isLeft);
void turnByAction(char a);

#endif // MOTOR_CONTROL_H
