/*  servo_gimbal.h  –  Pan (X) / Tilt (Y) servo control with analog feedback
 */
#pragma once
#include <Arduino.h>

void  gimbalInit();

void  gimbalSetX(float angleDeg);          // 0–180
void  gimbalSetY(float angleDeg);          // 0–180
float gimbalReadXAngle();                  // Read from analog feedback pin
void  gimbalLockX(bool lock);              // Lock / unlock servo-X at centre (90°)

float gimbalGetTargetX();
float gimbalGetTargetY();
