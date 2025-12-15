#include <Wire.h>
#include <MPU6500_WE.h>

#include "config.h"           // joints[], JOINT_COUNT, JointIndex enum, min/max angles
#include "ServoController.h"  // servoCtrl.setAngle(jointIndex, logicalDeg)
#include "PID.h"
#include "Kalman.h"

// =====================================================
//  ROBOT BALANCE (Smooth + Slow) - Only: 4 Hip + 2 Knee
// =====================================================

ServoController servoCtrl;

// ---------- IMU ----------
MPU6500_WE imu(&Wire);
Kalman kPitch, kRoll;

// ---------- PID ----------
PID pidPitch, pidRoll;

// ---------- Mode ----------
bool balanceEnabled = true;

// “Góc cũ” (reference) ngay sau khi đứng thẳng
float pitchRef = 0.0f;
float rollRef  = 0.0f;

// Base pose (giữ dáng đứng): logical = 0 cho toàn bộ
float baseAngle[JOINT_COUNT];

// ---------- Timing ----------
unsigned long lastMs = 0;
static const uint32_t UPDATE_INTERVAL_MS = 20; // 50Hz -> chậm & mượt hơn

// ---------- Smooth / Slow parameters ----------
static const float DEADBAND_DEG = 1.0f;     // tăng deadband để giảm rung
static const float SOFTSTART_S  = 1.5f;     // tăng dần lực cân bằng trong 1.5s

// Giới hạn bù nhỏ để không “đổi dáng”
static const float MAX_HIP_PITCH_CORR  = 6.0f;
static const float MAX_KNEE_PITCH_CORR = 4.0f;
static const float MAX_HIP_ROLL_CORR   = 8.0f;

// Phân bổ bù
static const float HIP_PITCH_GAIN  = 1.0f;
static const float KNEE_PITCH_GAIN = 0.5f;
static const float HIP_ROLL_GAIN   = 1.0f;

// Nếu chiều bù bị ngược, đổi dấu ở đây
static const float PITCH_DIR = 1.0f;
static const float ROLL_DIR  = 1.0f;

// Vì HIP_ROLL_R của bạn đang inverted=false (theo mapping bạn gửi),
// có thể cần đảo riêng từng bên roll nếu cơ khí gương trái-phải.
// Nếu bị “nghiêng thêm”, đổi dấu một trong hai cái này:
static const float ROLL_L_SIGN = 1.0f;
static const float ROLL_R_SIGN = 1.0f;

// Low-pass filter cho IMU output (sau Kalman) để giảm jitter
static const float IMU_TAU_S = 0.12f;   // time-constant (giảm -> nhạy hơn, tăng -> mượt hơn)
float pitchFilt = 0.0f, rollFilt = 0.0f;

// Slew-rate limit (deg/s): giới hạn tốc độ đổi góc để bớt “đập”
static const float HIP_PITCH_MAX_RATE_DPS = 25.0f;
static const float KNEE_PITCH_MAX_RATE_DPS = 18.0f;
static const float HIP_ROLL_MAX_RATE_DPS = 28.0f;

// Chỉ gửi lệnh khi thay đổi đủ lớn (giảm noise)
static const float SEND_EPS_DEG = 0.20f;

// Lưu lệnh cuối để so sánh
float lastCmdHipPitch = 0.0f;
float lastCmdKneePitch = 0.0f;
float lastCmdHipRollL = 0.0f;
float lastCmdHipRollR = 0.0f;

// Soft-start
float balanceStrength = 0.0f;
unsigned long balanceStartMs = 0;

// ----------------- Helpers -----------------
static inline float clampf(float x, float a, float b) { return (x < a) ? a : (x > b) ? b : x; }

static inline float lowPass(float current, float target, float tau, float dt) {
  // alpha = dt/(tau+dt)
  float alpha = dt / (tau + dt);
  return current + alpha * (target - current);
}

static inline float slewLimit(float current, float target, float maxRateDps, float dt) {
  float maxDelta = maxRateDps * dt;
  float delta = target - current;
  if (delta >  maxDelta) delta =  maxDelta;
  if (delta < -maxDelta) delta = -maxDelta;
  return current + delta;
}

static inline void setAngleIfChanged(ServoController &sc, int joint, float cmd, float &lastCmd) {
  if (fabs(cmd - lastCmd) >= SEND_EPS_DEG) {
    sc.setAngle(joint, cmd);
    lastCmd = cmd;
  }
}

// ----------------- Core prototypes -----------------
void standStraight();
bool initIMU();
void calibrateReferenceAngles();
void readPitchRoll(float dt, float &pitchOut, float &rollOut);
void balanceUpdate(float dt);

// =====================================================

void setup() {
  Serial.begin(115200);
  Wire.begin();

  servoCtrl.begin();
  Serial.println("Robot khoi dong...");

  // 1) Đứng thẳng (home)
  standStraight();
  Serial.println("Da ve vi tri dung thang (Home Position)");

  // 2) IMU init
  if (!initIMU()) {
    Serial.println("IMU init failed!");
    while (1) delay(10);
  }

  // 3) Lấy “góc cũ” ngay khi đang đứng thẳng
  calibrateReferenceAngles();
  Serial.print("Reference pitch="); Serial.print(pitchRef, 2);
  Serial.print(" roll="); Serial.println(rollRef, 2);

  // 4) PID tune (GIẢM Kp + tăng damping nhẹ để mượt)
  // Nếu vẫn rung: giảm Kp thêm 10–20% hoặc tăng IMU_TAU_S lên 0.15–0.20
  pidPitch.setTunings(0.9f, 0.0f, 0.08f);
  pidRoll.setTunings(1.0f, 0.0f, 0.10f);

  pidPitch.setOutputLimits(-MAX_HIP_PITCH_CORR, MAX_HIP_PITCH_CORR);
  pidRoll.setOutputLimits(-MAX_HIP_ROLL_CORR,  MAX_HIP_ROLL_CORR);

  pidPitch.reset();
  pidRoll.reset();

  // Soft-start
  balanceStrength = 0.0f;
  balanceStartMs = millis();

  lastMs = millis();
}

void loop() {
  // ---- Update rate control ----
  unsigned long now = millis();
  if (now - lastMs < UPDATE_INTERVAL_MS) return;
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;

  // ---- Serial control (optional) ----
  // B/b: toggle balance
  // R/r: recalibrate reference angle (góc cũ)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') {
      balanceEnabled = !balanceEnabled;
      Serial.print("balanceEnabled = "); Serial.println(balanceEnabled);

      if (balanceEnabled) {
        pidPitch.reset();
        pidRoll.reset();
        calibrateReferenceAngles();
        balanceStrength = 0.0f;
        balanceStartMs = millis();
      } else {
        // khi tắt, giữ lại dáng đứng (base = 0)
        servoCtrl.setAngle(HIP_PITCH_L, baseAngle[HIP_PITCH_L]);
        servoCtrl.setAngle(HIP_PITCH_R, baseAngle[HIP_PITCH_R]);
        servoCtrl.setAngle(KNEE_PITCH_L, baseAngle[KNEE_PITCH_L]);
        servoCtrl.setAngle(KNEE_PITCH_R, baseAngle[KNEE_PITCH_R]);
        servoCtrl.setAngle(HIP_ROLL_L, baseAngle[HIP_ROLL_L]);
        servoCtrl.setAngle(HIP_ROLL_R, baseAngle[HIP_ROLL_R]);
      }
    }
    if (c == 'r' || c == 'R') {
      calibrateReferenceAngles();
      pidPitch.reset();
      pidRoll.reset();
      balanceStrength = 0.0f;
      balanceStartMs = millis();
      Serial.println("Recalibrated reference angles.");
    }
  }

  if (balanceEnabled) balanceUpdate(dt);
}

// =====================================================
//  Stand straight = base pose (logical 0) for all joints
// =====================================================
void standStraight() {
  for (int i = 0; i < JOINT_COUNT; i++) {
    baseAngle[i] = 0.0f;
    servoCtrl.setAngle(i, baseAngle[i]);
    delay(80);
  }

  // reset last cmd
  lastCmdHipPitch = 0.0f;
  lastCmdKneePitch = 0.0f;
  lastCmdHipRollL = 0.0f;
  lastCmdHipRollR = 0.0f;
}

// =====================================================
//  IMU init
// =====================================================
bool initIMU() {
  if (!imu.init()) return false;

  // robot đứng yên khi autoOffsets
  imu.autoOffsets();
  imu.setAccRange(MPU6500_ACC_RANGE_2G);
  imu.setGyrRange(MPU6500_GYRO_RANGE_250);

  // set Kalman initial angles from accel
  xyzFloat g = imu.getGValues();
  float rollAcc  = atan2(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2(-g.x, sqrt(g.y * g.y + g.z * g.z)) * 180.0f / PI;

  kRoll.setAngle(rollAcc);
  kPitch.setAngle(pitchAcc);

  // init filter state
  pitchFilt = pitchAcc;
  rollFilt  = rollAcc;

  return true;
}

// =====================================================
//  Reference angle = “góc cũ” ngay khi đứng thẳng
// =====================================================
void calibrateReferenceAngles() {
  float sumP = 0, sumR = 0;
  const int N = 100;

  // reset Kalman nhẹ
  xyzFloat g = imu.getGValues();
  float rollAcc  = atan2(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2(-g.x, sqrt(g.y * g.y + g.z * g.z)) * 180.0f / PI;
  kRoll.setAngle(rollAcc);
  kPitch.setAngle(pitchAcc);

  pitchFilt = pitchAcc;
  rollFilt  = rollAcc;

  for (int i = 0; i < N; i++) {
    float p, r;
    readPitchRoll(0.02f, p, r); // giả dt
    sumP += p;
    sumR += r;
    delay(10);
  }

  pitchRef = sumP / N;
  rollRef  = sumR / N;
}

// =====================================================
//  Read pitch/roll (Kalman + low-pass for smooth)
//  NOTE: nếu trục gyro khác do hướng gắn IMU, đổi gyro.x/gyro.y
// =====================================================
void readPitchRoll(float dt, float &pitchOut, float &rollOut) {
  xyzFloat g = imu.getGValues();
  xyzFloat gyro = imu.getGyrValues();   // deg/s (tuỳ lib)

  float rollAcc  = atan2(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2(-g.x, sqrt(g.y * g.y + g.z * g.z)) * 180.0f / PI;

  float roll  = kRoll.getAngle(rollAcc,   gyro.x, dt);
  float pitch = kPitch.getAngle(pitchAcc, gyro.y, dt);

  // Low-pass sau Kalman để giảm jitter
  rollFilt  = lowPass(rollFilt,  roll,  IMU_TAU_S, dt);
  pitchFilt = lowPass(pitchFilt, pitch, IMU_TAU_S, dt);

  rollOut  = rollFilt;
  pitchOut = pitchFilt;
}

// =====================================================
//  Balance update (slow + smooth)
//  Use only 6 joints: HIP_PITCH_L/R, HIP_ROLL_L/R, KNEE_PITCH_L/R
// =====================================================
void balanceUpdate(float dt) {
  // Soft-start strength
  float t = (millis() - balanceStartMs) / 1000.0f;
  balanceStrength = clampf(t / SOFTSTART_S, 0.0f, 1.0f);

  float pitch, roll;
  readPitchRoll(dt, pitch, roll);

  // Fail-safe: nghiêng quá thì giữ pose (tránh bù quá mạnh)
  if (fabs(pitch - pitchRef) > 25.0f || fabs(roll - rollRef) > 25.0f) {
    servoCtrl.setAngle(HIP_PITCH_L, baseAngle[HIP_PITCH_L]);
    servoCtrl.setAngle(HIP_PITCH_R, baseAngle[HIP_PITCH_R]);
    servoCtrl.setAngle(KNEE_PITCH_L, baseAngle[KNEE_PITCH_L]);
    servoCtrl.setAngle(KNEE_PITCH_R, baseAngle[KNEE_PITCH_R]);
    servoCtrl.setAngle(HIP_ROLL_L, baseAngle[HIP_ROLL_L]);
    servoCtrl.setAngle(HIP_ROLL_R, baseAngle[HIP_ROLL_R]);
    return;
  }

  // Error so với “góc cũ”
  float pitchErr = (pitchRef - pitch);
  float rollErr  = (rollRef  - roll);

  // Deadband để giảm rung
  if (fabs(pitchErr) < DEADBAND_DEG) pitchErr = 0.0f;
  if (fabs(rollErr)  < DEADBAND_DEG) rollErr  = 0.0f;

  // PID output (dùng input = -error để output ~ error)
  float corrPitch = pidPitch.compute(0.0f, -pitchErr, dt) * PITCH_DIR;
  float corrRoll  = pidRoll.compute(0.0f, -rollErr,  dt) * ROLL_DIR;

  // Scale theo soft-start
  corrPitch *= balanceStrength;
  corrRoll  *= balanceStrength;

  // Clamp & split knee/hip pitch
  float corrHipPitch = clampf(corrPitch * HIP_PITCH_GAIN, -MAX_HIP_PITCH_CORR, MAX_HIP_PITCH_CORR);
  float corrKneePitch = clampf(corrPitch * KNEE_PITCH_GAIN, -MAX_KNEE_PITCH_CORR, MAX_KNEE_PITCH_CORR);

  // Targets (logical deg)
  float targetHipPitch  = baseAngle[HIP_PITCH_L] + corrHipPitch;
  float targetKneePitch = baseAngle[KNEE_PITCH_L] + corrKneePitch;

  float targetHipRollL  = baseAngle[HIP_ROLL_L] + (corrRoll * HIP_ROLL_GAIN * ROLL_L_SIGN);
  float targetHipRollR  = baseAngle[HIP_ROLL_R] + (corrRoll * HIP_ROLL_GAIN * ROLL_R_SIGN);

  // Respect logical limits from config.h
  targetHipPitch  = clampf(targetHipPitch,  HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
  targetKneePitch = clampf(targetKneePitch, KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);
  targetHipRollL  = clampf(targetHipRollL,  HIP_ROLL_MIN_ANGLE,   HIP_ROLL_MAX_ANGLE);
  targetHipRollR  = clampf(targetHipRollR,  HIP_ROLL_MIN_ANGLE,   HIP_ROLL_MAX_ANGLE);

  // --------- SLOW DOWN: slew-rate limit ----------
  // Lưu ý: chúng ta “ramp” từ lastCmd -> targetCmd theo maxRate deg/s
  float hipPitchCmd  = slewLimit(lastCmdHipPitch,  targetHipPitch,  HIP_PITCH_MAX_RATE_DPS, dt);
  float kneePitchCmd = slewLimit(lastCmdKneePitch, targetKneePitch, KNEE_PITCH_MAX_RATE_DPS, dt);
  float hipRollLCmd  = slewLimit(lastCmdHipRollL,  targetHipRollL,  HIP_ROLL_MAX_RATE_DPS, dt);
  float hipRollRCmd  = slewLimit(lastCmdHipRollR,  targetHipRollR,  HIP_ROLL_MAX_RATE_DPS, dt);

  // --------- Send only if changed (reduces jitter) ----------
  // Pitch: gửi cùng 1 cmd cho L/R (R sẽ tự inverted nếu config.h đặt true)
  setAngleIfChanged(servoCtrl, HIP_PITCH_L, hipPitchCmd, lastCmdHipPitch);
  servoCtrl.setAngle(HIP_PITCH_R, hipPitchCmd); // đồng bộ (không cần eps riêng)

  setAngleIfChanged(servoCtrl, KNEE_PITCH_L, kneePitchCmd, lastCmdKneePitch);
  servoCtrl.setAngle(KNEE_PITCH_R, kneePitchCmd);

  // Roll: có eps riêng từng bên để giảm rung
  setAngleIfChanged(servoCtrl, HIP_ROLL_L, hipRollLCmd, lastCmdHipRollL);
  setAngleIfChanged(servoCtrl, HIP_ROLL_R, hipRollRCmd, lastCmdHipRollR);

  // Debug nhẹ
  static int cnt = 0;
  if (++cnt % 25 == 0) {
    Serial.print("p="); Serial.print(pitch, 2);
    Serial.print(" r="); Serial.print(roll, 2);
    Serial.print(" eP="); Serial.print(pitchErr, 2);
    Serial.print(" eR="); Serial.print(rollErr, 2);
    Serial.print(" str="); Serial.println(balanceStrength, 2);
  }
}
