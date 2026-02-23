#pragma once

// ============================================================
// IMU Balance — Init, calibration, PD control loop
// ============================================================

void standStraight();
bool initIMU();
void calibrateReferenceAngles();
void readPitchRoll(float dt, float& pitchOut, float& rollOut,
                   float& pitchRate, float& rollRate);
void updateControl(float dt);
