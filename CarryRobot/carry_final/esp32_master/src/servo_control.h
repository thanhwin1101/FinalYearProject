#pragma once
#include <Arduino.h>
#include "config.h"

void  servoInit();
void  servoSetX(int angle);
void  servoSetY(int angle);
int   servoGetX();
int   servoGetY();
int   servoReadXFeedback();   // raw ADC feedback for servo X
float servoReadXAngle();      // estimated angle from feedback
