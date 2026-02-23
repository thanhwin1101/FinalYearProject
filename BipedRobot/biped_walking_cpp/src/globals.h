#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <MPU6500_WE.h>
#include "config.h"
#include "ServoController.h"
#include "Kalman.h"

// ============================================================
// GLOBALS — Shared state for Biped Walking Controller
// ============================================================

// ---------- Movement Commands ----------
enum MoveCommand {
  CMD_NONE = 0,
  CMD_FORWARD,
  CMD_BACKWARD,
  CMD_LEFT,
  CMD_RIGHT,
  CMD_STOP
};

// ---------- Hardware Objects ----------
extern ServoController servoCtrl;
extern MPU6500_WE      imu;
extern Kalman          kPitch, kRoll;
extern HardwareSerial  UserSerial;  // Serial2

// ---------- Mode ----------
extern bool balanceEnabled;

// ---------- Step Counter ----------
extern uint32_t      stepCount;
extern uint32_t      lastStepSentCount;
extern unsigned long lastStepSendMs;

// ---------- Command from User Manager ----------
extern MoveCommand currentCmd;
extern uint8_t     moveSpeed;

// ---------- UART Buffer ----------
extern char uartBuffer[64];
extern int  uartBufIdx;

// ---------- IMU Reference (standing straight) ----------
extern float pitchRef;
extern float rollRef;

// ---------- Base Pose ----------
extern float baseAngle[JOINT_COUNT];

// ---------- Control Timing ----------
extern unsigned long lastMs;

// ---------- Filtered State ----------
extern float pitchFilt;
extern float rollFilt;

// ---------- Soft-Start ----------
extern unsigned long balanceStartMs;
extern float         strength;

// ---------- Last Servo Commands ----------
extern float lastHipP;
extern float lastKneeP;
extern float lastHipRL;
extern float lastHipRR;
extern float lastAnkleP;

// ---------- Inline Helpers ----------
static inline float clampf(float x, float a, float b) {
  return (x < a) ? a : (x > b) ? b : x;
}

static inline float lowPass(float current, float target, float tau, float dt) {
  float alpha = dt / (tau + dt);
  return current + alpha * (target - current);
}

static inline float slewLimit(float current, float target, float maxRateDps, float dt) {
  float maxDelta = maxRateDps * dt;
  float d = target - current;
  if (d >  maxDelta) d =  maxDelta;
  if (d < -maxDelta) d = -maxDelta;
  return current + d;
}

static inline void setAngleIfChanged(ServoController& sc, int joint, float cmd, float& lastCmd) {
  if (fabsf(cmd - lastCmd) >= SEND_EPS_DEG) {
    sc.setAngle(joint, cmd);
    lastCmd = cmd;
  }
}
