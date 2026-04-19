#pragma once
#include <Arduino.h>
#include "config.h"

// Servo Y angle tracked on ESP32 and sent to STM32 via CMD_SERVO_SET (0x07).
void  servoInit();
void  servoSetY(int angle);
int   servoGetY();
