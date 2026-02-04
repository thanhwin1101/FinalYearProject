#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>

// Initialization
void motorPwmInit();
void motorsStop();

// Movement
void driveForward(int pwm);
void driveBackward(int pwm);

// Direction setup for turning
void setMotorDirLeft();
void setMotorDirRight();

// Braking
void applyForwardBrake(int brakePwm, int brakeMs);
void applyHardBrake(bool wasTurningLeft, int brakePwm, int brakeMs);

// Turning
void rotateByTime(unsigned long totalMs, bool isLeft);
void turnByAction(char a);

#endif // MOTOR_CONTROL_H
