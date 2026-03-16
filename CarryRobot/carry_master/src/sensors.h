/*  sensors.h  –  VL53L0X ToF + dual ultrasonic
 */
#pragma once
#include <Arduino.h>

void sensorsInit();

// VL53L0X  (front obstacle)
bool tofRead(uint16_t &distMm);

// Ultrasonic  (side clearance)
long usReadMm(uint8_t trigPin, uint8_t echoPin);
long usLeftMm();
long usRightMm();
