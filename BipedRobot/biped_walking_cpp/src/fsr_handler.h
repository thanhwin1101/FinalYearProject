#pragma once
#include <Arduino.h>

struct FootSensor {
    int front, heel, left, right, total;
    bool isGrounded;
    float copX; // Dọc: dương = mũi chân
    float copY; // Ngang: dương = mép trái
};

extern FootSensor footL;
extern FootSensor footR;

void fsrInit();
void fsrUpdate();