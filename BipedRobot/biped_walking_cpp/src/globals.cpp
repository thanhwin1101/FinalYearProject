#include "globals.h"

// ============================================================
// GLOBALS — Instantiation
// ============================================================

ServoController servoCtrl;
MPU6500_WE      imu(&Wire);
Kalman          kPitch, kRoll;
HardwareSerial  UserSerial(2);

bool balanceEnabled = true;

uint32_t      stepCount         = 0;
uint32_t      lastStepSentCount = 0;
unsigned long lastStepSendMs    = 0;

MoveCommand currentCmd = CMD_NONE;
uint8_t     moveSpeed  = 50;

char uartBuffer[64];
int  uartBufIdx = 0;

float pitchRef = 0.0f;
float rollRef  = 0.0f;

float baseAngle[JOINT_COUNT] = { 0 };

unsigned long lastMs = 0;

float pitchFilt = 0.0f;
float rollFilt  = 0.0f;

unsigned long balanceStartMs = 0;
float         strength       = 0.0f;

float lastHipP   = 0.0f;
float lastKneeP  = 0.0f;
float lastHipRL  = 0.0f;
float lastHipRR  = 0.0f;
float lastAnkleP = 0.0f;
