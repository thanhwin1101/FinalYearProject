#pragma once
#include <Arduino.h>

void  gimbalInit();

void  gimbalSetX(float angleDeg);
void  gimbalSetY(float angleDeg);
float gimbalReadXAngle();
void  gimbalLockX(bool lock);

float gimbalGetTargetX();
float gimbalGetTargetY();
