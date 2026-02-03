#include <Wire.h>
#include <MPU6500_WE.h>

#include "config.h"
#include "ServoController.h"
#include "Kalman.h"
#include <math.h>

ServoController servoCtrl;

// ---------- IMU ----------
MPU6500_WE imu(&Wire);
Kalman kPitch, kRoll;

// ---------- Mode ----------
bool balanceEnabled = true;

// ============================================================
// UART COMMUNICATION WITH USER MANAGER
// ============================================================
#define USER_SERIAL       Serial2
#define USER_UART_RX      16
#define USER_UART_TX      17
#define USER_UART_BAUD    115200

// Step counter
uint32_t stepCount = 0;
uint32_t lastStepSentCount = 0;
unsigned long lastStepSendMs = 0;
const unsigned long STEP_SEND_INTERVAL = 1000;  // Send step every 1s

// Command from User Manager
enum MoveCommand {
  CMD_NONE = 0,
  CMD_FORWARD,
  CMD_BACKWARD,
  CMD_LEFT,
  CMD_RIGHT,
  CMD_STOP
};
MoveCommand currentCmd = CMD_NONE;
uint8_t moveSpeed = 50;  // 0-100%

// UART buffer
char uartBuffer[64];
int uartBufIdx = 0;

// Reference (góc “đứng thẳng” đo từ IMU)
float pitchRef = 0.0f;
float rollRef  = 0.0f;

// Base pose: logical = 0
float baseAngle[JOINT_COUNT];

// ---------- Timing ----------
static const uint32_t UPDATE_INTERVAL_MS = 20; // 50Hz
unsigned long lastMs = 0;

// ---------- Filters ----------
static const float IMU_TAU_S = 0.12f; // low-pass sau Kalman
float pitchFilt = 0.0f, rollFilt = 0.0f;

// ---------- Deadband ----------
static const float DEADBAND_DEG = 1.0f;

// ---------- Soft-start ----------
static const float SOFTSTART_S = 2.0f;
unsigned long balanceStartMs = 0;
float strength = 0.0f;

// ---------- Limits (tăng một chút để tránh hip bị kẹp quá sớm) ----------
static const float MAX_HIP_PITCH_CORR  = 6.0f;
static const float MAX_KNEE_PITCH_CORR = 2.0f;
static const float MAX_HIP_ROLL_CORR   = 8.0f;

// ---------- Knee participation (để tránh “nhún”) ----------
static const float KNEE_PITCH_GAIN = 0.10f;

// ---------- Slew-rate (deg/s) ----------
static const float HIP_PITCH_MAX_RATE_DPS  = 12.0f;
static const float KNEE_PITCH_MAX_RATE_DPS = 8.0f;
static const float HIP_ROLL_MAX_RATE_DPS   = 18.0f;
static const float ANKLE_PITCH_MAX_RATE_DPS = 18.0f; // chậm để không “đập”

// ---------- Send threshold ----------
static const float SEND_EPS_DEG = 0.20f;

// ===============================
// PD damping using gyro rates
// ===============================
static const float KP_PITCH = 0.40f;      // range: 0.25 -> 0.60
static const float KD_PITCH_RATE = 0.06f; // range: 0.03 -> 0.10 (dập rung nhanh)

static const float KP_ROLL = 0.55f;
static const float KD_ROLL_RATE = 0.05f;

// Nếu chiều pitch/roll bị ngược, đổi dấu 2 cái này:
static const float PITCH_DIR = 1.0f;
static const float ROLL_DIR  = 1.0f;

// ===============================
// Foot leveling using ankle pitch
// ===============================
static const float ANKLE_DIR = -1.0f;  // <<< FIX CHÍNH: đảo chiều ankle (thử -1 trước)
static const float ANKLE_LEVEL_GAIN = 0.55f; // range: 0.35 -> 0.80 (giảm để bớt bão hoà)
static const float MAX_ANKLE_PITCH_CORR = 10.0f; // range: 6 -> 12

// Blend: khi |pitchErr| nhỏ -> ưu tiên “phẳng chân”
// khi |pitchErr| lớn -> ưu tiên “hồi phục” (PD theo gyro) để tránh ankle kẹp trần + rung
static const float LEVEL_WINDOW_DEG = 6.0f;  // trong ±6° thì cố giữ phẳng mạnh
static const float LEVEL_FADE_DEG   = 8.0f;  // từ 6→14° chuyển dần sang recovery

// Recovery ankle PD (dập rung + kéo về nhanh)
static const float KP_ANKLE_REC = 0.25f;       // range: 0.15 -> 0.35
static const float KD_ANKLE_RATE_REC = 0.04f;  // range: 0.02 -> 0.07

// ---------- Last commands ----------
float lastHipP = 0, lastKneeP = 0, lastHipRL = 0, lastHipRR = 0, lastAnkleP = 0;

// ---------- Helpers ----------
static inline float clampf(float x, float a, float b) { return (x < a) ? a : (x > b) ? b : x; }

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

static inline void setAngleIfChanged(ServoController &sc, int joint, float cmd, float &lastCmd) {
  if (fabsf(cmd - lastCmd) >= SEND_EPS_DEG) {
    sc.setAngle(joint, cmd);
    lastCmd = cmd;
  }
}

// ---------- Prototypes ----------
void standStraight();
bool initIMU();
void calibrateReferenceAngles();
void readPitchRoll(float dt, float &pitchOut, float &rollOut, float &pitchRate, float &rollRate);
void updateControl(float dt);
void handleUserUART();
void processUserCommand(const char* cmd);
void sendToUser(const char* msg);
void sendStepCount();
void sendBalanceStatus();

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Initialize UART to User Manager
  USER_SERIAL.begin(USER_UART_BAUD, SERIAL_8N1, USER_UART_RX, USER_UART_TX);
  Serial.println("UART to User Manager initialized");

  servoCtrl.begin();
  Serial.println("Robot khoi dong...");

  standStraight();

  if (!initIMU()) {
    Serial.println("IMU init failed!");
    sendToUser("ERROR:IMU_FAIL");
    while (1) delay(10);
  }

  calibrateReferenceAngles();
  Serial.print("Reference pitch="); Serial.print(pitchRef, 2);
  Serial.print(" roll="); Serial.println(rollRef, 2);
  
  // Notify User Manager that system is ready
  sendToUser("STATUS:READY");
  sendBalanceStatus();

  balanceStartMs = millis();
  lastMs = millis();
  lastStepSendMs = millis();
}

void loop() {
  unsigned long now = millis();
  
  // Handle UART from User Manager (always, regardless of timing)
  handleUserUART();
  
  // Send step count periodically
  if (now - lastStepSendMs >= STEP_SEND_INTERVAL) {
    lastStepSendMs = now;
    sendStepCount();
  }
  
  if (now - lastMs < UPDATE_INTERVAL_MS) return;
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;

  // Debug commands via USB Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') {
      balanceEnabled = !balanceEnabled;
      Serial.print("balanceEnabled="); Serial.println(balanceEnabled);
      sendToUser(balanceEnabled ? "BALANCE:ON" : "BALANCE:OFF");
      if (balanceEnabled) {
        calibrateReferenceAngles();
        balanceStartMs = millis();
      } else {
        standStraight();
      }
    }
    if (c == 'r' || c == 'R') {
      calibrateReferenceAngles();
      balanceStartMs = millis();
      Serial.println("Recalibrated reference angles.");
      sendToUser("STATUS:CALIBRATED");
    }
  }

  if (balanceEnabled) updateControl(dt);
}

void standStraight() {
  for (int i = 0; i < JOINT_COUNT; i++) {
    baseAngle[i] = 0.0f;
    servoCtrl.setAngle(i, 0.0f);
    delay(60);
  }
  lastHipP = lastKneeP = lastHipRL = lastHipRR = lastAnkleP = 0.0f;
}

bool initIMU() {
  if (!imu.init()) return false;

  imu.autoOffsets();
  imu.setAccRange(MPU6500_ACC_RANGE_2G);
  imu.setGyrRange(MPU6500_GYRO_RANGE_250);

  xyzFloat g = imu.getGValues();
  float rollAcc  = atan2f(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2f(-g.x, sqrtf(g.y*g.y + g.z*g.z)) * 180.0f / PI;

  kRoll.setAngle(rollAcc);
  kPitch.setAngle(pitchAcc);
  rollFilt = rollAcc;
  pitchFilt = pitchAcc;
  return true;
}

void calibrateReferenceAngles() {
  float sumP = 0, sumR = 0;
  const int N = 80;

  xyzFloat g = imu.getGValues();
  float rollAcc  = atan2f(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2f(-g.x, sqrtf(g.y*g.y + g.z*g.z)) * 180.0f / PI;
  kRoll.setAngle(rollAcc);
  kPitch.setAngle(pitchAcc);
  rollFilt = rollAcc;
  pitchFilt = pitchAcc;

  for (int i = 0; i < N; i++) {
    float p, r, pr, rr;
    readPitchRoll(0.02f, p, r, pr, rr);
    sumP += p;
    sumR += r;
    delay(10);
  }
  pitchRef = sumP / N;
  rollRef  = sumR / N;
}

void readPitchRoll(float dt, float &pitchOut, float &rollOut, float &pitchRate, float &rollRate) {
  xyzFloat g = imu.getGValues();
  xyzFloat gyro = imu.getGyrValues(); // deg/s

  float rollAcc  = atan2f(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2f(-g.x, sqrtf(g.y*g.y + g.z*g.z)) * 180.0f / PI;

  float roll  = kRoll.getAngle(rollAcc,   gyro.x, dt);
  float pitch = kPitch.getAngle(pitchAcc, gyro.y, dt);

  rollFilt  = lowPass(rollFilt,  roll,  IMU_TAU_S, dt);
  pitchFilt = lowPass(pitchFilt, pitch, IMU_TAU_S, dt);

  rollOut  = rollFilt;
  pitchOut = pitchFilt;

  // rates (để dập rung)
  rollRate  = gyro.x;
  pitchRate = gyro.y;
}

void updateControl(float dt) {
  // soft-start
  float t = (millis() - balanceStartMs) / 1000.0f;
  strength = clampf(t / SOFTSTART_S, 0.0f, 1.0f);

  float pitch, roll, pitchRate, rollRate;
  readPitchRoll(dt, pitch, roll, pitchRate, rollRate);

  float pitchErr = (pitchRef - pitch);
  float rollErr  = (rollRef  - roll);

  // deadband
  if (fabsf(pitchErr) < DEADBAND_DEG) pitchErr = 0;
  if (fabsf(rollErr)  < DEADBAND_DEG) rollErr  = 0;

  // ---------- PD balance (gyro damping) ----------
  float corrPitch = strength * (KP_PITCH * pitchErr - KD_PITCH_RATE * pitchRate) * PITCH_DIR;
  float corrRoll  = strength * (KP_ROLL  * rollErr  - KD_ROLL_RATE  * rollRate)  * ROLL_DIR;

  corrPitch = clampf(corrPitch, -MAX_HIP_PITCH_CORR, MAX_HIP_PITCH_CORR);
  corrRoll  = clampf(corrRoll,  -MAX_HIP_ROLL_CORR,  MAX_HIP_ROLL_CORR);

  // split knee
  float corrKnee = clampf(corrPitch * KNEE_PITCH_GAIN, -MAX_KNEE_PITCH_CORR, MAX_KNEE_PITCH_CORR);

  float targetHipP  = baseAngle[HIP_PITCH_L] + corrPitch;
  float targetKneeP = baseAngle[KNEE_PITCH_L] + corrKnee;

  targetHipP  = clampf(targetHipP,  HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
  targetKneeP = clampf(targetKneeP, KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);

  // slew
  float hipP  = slewLimit(lastHipP,  targetHipP,  HIP_PITCH_MAX_RATE_DPS, dt);
  float kneeP = slewLimit(lastKneeP, targetKneeP, KNEE_PITCH_MAX_RATE_DPS, dt);

  // ---------- ankle: blended leveling + recovery ----------
  // leveling term: ankle = pitchRef - pitch - hipP - kneeP
  float ankleLevel = (pitchRef - pitch - hipP - kneeP) * ANKLE_LEVEL_GAIN * ANKLE_DIR;

  // recovery term: giúp kéo về nhanh + dập rung, không phụ thuộc hip/knee
  float ankleRec = (KP_ANKLE_REC * pitchErr - KD_ANKLE_RATE_REC * pitchRate) * ANKLE_DIR;

  // blend factor based on |pitchErr|
  float ae = fabsf(pitchErr);
  float blend = 0.0f;
  if (ae <= LEVEL_WINDOW_DEG) blend = 0.0f;
  else if (ae >= (LEVEL_WINDOW_DEG + LEVEL_FADE_DEG)) blend = 1.0f;
  else blend = (ae - LEVEL_WINDOW_DEG) / LEVEL_FADE_DEG;

  float ankleCorr = (1.0f - blend) * ankleLevel + blend * ankleRec;
  ankleCorr = clampf(ankleCorr, -MAX_ANKLE_PITCH_CORR, MAX_ANKLE_PITCH_CORR);

  float targetAnkleP = baseAngle[ANKLE_PITCH_L] + ankleCorr;
  targetAnkleP = clampf(targetAnkleP, ANKLE_PITCH_MIN_ANGLE, ANKLE_PITCH_MAX_ANGLE);

  float ankleP = slewLimit(lastAnkleP, targetAnkleP, ANKLE_PITCH_MAX_RATE_DPS, dt);

  // ---------- send ----------
  setAngleIfChanged(servoCtrl, HIP_PITCH_L, hipP, lastHipP);
  servoCtrl.setAngle(HIP_PITCH_R, hipP);

  setAngleIfChanged(servoCtrl, KNEE_PITCH_L, kneeP, lastKneeP);
  servoCtrl.setAngle(KNEE_PITCH_R, kneeP);

  // roll giữ đơn giản (nếu roll chưa là vấn đề thì cứ giữ nhỏ)
  float targetHipRL = clampf(baseAngle[HIP_ROLL_L] + corrRoll, HIP_ROLL_MIN_ANGLE, HIP_ROLL_MAX_ANGLE);
  float targetHipRR = clampf(baseAngle[HIP_ROLL_R] + corrRoll, HIP_ROLL_MIN_ANGLE, HIP_ROLL_MAX_ANGLE);

  float hipRL = slewLimit(lastHipRL, targetHipRL, HIP_ROLL_MAX_RATE_DPS, dt);
  float hipRR = slewLimit(lastHipRR, targetHipRR, HIP_ROLL_MAX_RATE_DPS, dt);

  setAngleIfChanged(servoCtrl, HIP_ROLL_L, hipRL, lastHipRL);
  setAngleIfChanged(servoCtrl, HIP_ROLL_R, hipRR, lastHipRR);

  // ankle pitch giữ phẳng
  setAngleIfChanged(servoCtrl, ANKLE_PITCH_L, ankleP, lastAnkleP);
  servoCtrl.setAngle(ANKLE_PITCH_R, ankleP);

  // ankle roll giữ 0
  servoCtrl.setAngle(ANKLE_ROLL_L, 0.0f);
  servoCtrl.setAngle(ANKLE_ROLL_R, 0.0f);

  // ---------- debug ----------
  static int dbg = 0;
  if (++dbg % 10 == 0) {
    Serial.print("p="); Serial.print(pitch, 2);
    Serial.print(" rate="); Serial.print(pitchRate, 2);
    Serial.print(" hipP="); Serial.print(hipP, 2);
    Serial.print(" ankleP="); Serial.println(ankleP, 2);
  }
}

// ============================================================
// UART COMMUNICATION WITH USER MANAGER
// ============================================================

void handleUserUART() {
  while (USER_SERIAL.available()) {
    char c = USER_SERIAL.read();
    
    if (c == '\n' || c == '\r') {
      if (uartBufIdx > 0) {
        uartBuffer[uartBufIdx] = '\0';
        processUserCommand(uartBuffer);
        uartBufIdx = 0;
      }
    } else if (uartBufIdx < sizeof(uartBuffer) - 1) {
      uartBuffer[uartBufIdx++] = c;
    }
  }
}

void processUserCommand(const char* cmd) {
  Serial.print("RX from User Manager: ");
  Serial.println(cmd);

  // Parse command format: "KEY:VALUE" or just "KEY"
  char key[16], value[32];
  value[0] = '\0';
  
  char* colonPos = strchr(cmd, ':');
  if (colonPos) {
    int keyLen = colonPos - cmd;
    strncpy(key, cmd, keyLen);
    key[keyLen] = '\0';
    strncpy(value, colonPos + 1, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
  } else {
    strncpy(key, cmd, sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';
  }

  // Process commands
  if (strcmp(key, "CMD") == 0) {
    if (strcmp(value, "FWD") == 0) {
      currentCmd = CMD_FORWARD;
      stepCount++;  // Increment step for forward movement
      Serial.println("Command: FORWARD");
      sendToUser("ACK:FWD");
    } else if (strcmp(value, "BACK") == 0) {
      currentCmd = CMD_BACKWARD;
      stepCount++;  // Increment step for backward movement
      Serial.println("Command: BACKWARD");
      sendToUser("ACK:BACK");
    } else if (strcmp(value, "LEFT") == 0) {
      currentCmd = CMD_LEFT;
      Serial.println("Command: LEFT");
      sendToUser("ACK:LEFT");
    } else if (strcmp(value, "RIGHT") == 0) {
      currentCmd = CMD_RIGHT;
      Serial.println("Command: RIGHT");
      sendToUser("ACK:RIGHT");
    }
  } else if (strcmp(key, "STOP") == 0 || strcmp(cmd, "STOP") == 0) {
    currentCmd = CMD_STOP;
    Serial.println("Command: STOP");
    sendToUser("ACK:STOP");
  } else if (strcmp(key, "SPEED") == 0) {
    moveSpeed = atoi(value);
    if (moveSpeed > 100) moveSpeed = 100;
    Serial.print("Speed set to: ");
    Serial.println(moveSpeed);
    sendToUser("ACK:SPEED");
  } else if (strcmp(key, "BALANCE") == 0) {
    if (strcmp(value, "ON") == 0) {
      balanceEnabled = true;
      calibrateReferenceAngles();
      balanceStartMs = millis();
      Serial.println("Balance ENABLED");
      sendToUser("BALANCE:OK");
    } else if (strcmp(value, "OFF") == 0) {
      balanceEnabled = false;
      standStraight();
      Serial.println("Balance DISABLED");
      sendToUser("BALANCE:OFF");
    }
  } else if (strcmp(key, "CALIBRATE") == 0 || strcmp(cmd, "CALIBRATE") == 0) {
    calibrateReferenceAngles();
    balanceStartMs = millis();
    Serial.println("Calibration complete");
    sendToUser("STATUS:CALIBRATED");
  } else if (strcmp(key, "STATUS") == 0 || strcmp(cmd, "STATUS") == 0) {
    // Send current status
    sendStepCount();
    sendBalanceStatus();
  } else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
    sendToUser("ERROR:UNKNOWN_CMD");
  }
}

void sendToUser(const char* msg) {
  USER_SERIAL.println(msg);
  Serial.print("TX to User Manager: ");
  Serial.println(msg);
}

void sendStepCount() {
  if (stepCount != lastStepSentCount) {
    char buf[24];
    snprintf(buf, sizeof(buf), "STEP:%lu", stepCount);
    sendToUser(buf);
    lastStepSentCount = stepCount;
  }
}

void sendBalanceStatus() {
  if (balanceEnabled) {
    // Check if balance is within acceptable range
    float pitch, roll, pitchRate, rollRate;
    readPitchRoll(0.02f, pitch, roll, pitchRate, rollRate);
    
    float pitchErr = fabsf(pitchRef - pitch);
    float rollErr = fabsf(rollRef - roll);
    
    if (pitchErr < 5.0f && rollErr < 5.0f) {
      sendToUser("BALANCE:OK");
    } else if (pitchErr < 10.0f && rollErr < 10.0f) {
      sendToUser("BALANCE:WARN");
    } else {
      sendToUser("BALANCE:ERROR");
    }
  } else {
    sendToUser("BALANCE:OFF");
  }
}
