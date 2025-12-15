#include <Wire.h>
#include <MPU6500_WE.h>

#include "config.h"
#include "ServoController.h"
#include "PID.h"
#include "Kalman.h"

ServoController servoCtrl;

// ================= IMU =================
MPU6500_WE imu(&Wire);
Kalman kPitch, kRoll;

// ================= PID =================
PID pidPitch, pidRoll;

// ====== Auto-balance enable ======
bool balanceEnabled = true;

// “Góc cũ” (reference) ngay sau khi đứng thẳng
float pitchRef = 0.0f;
float rollRef  = 0.0f;

// Base pose: giữ nguyên dáng đứng (logical = 0)
float baseAngle[JOINT_COUNT];

// ============ Tuning nhanh ============
static const float UPDATE_DT_MIN = 0.01f;   // ~100Hz
static const float DEADBAND_DEG  = 0.6f;    // tránh rung nhỏ

// Giới hạn bù (nhỏ để không làm đổi dáng)
static const float MAX_HIP_PITCH_CORR  = 8.0f;
static const float MAX_KNEE_PITCH_CORR = 5.0f;
static const float MAX_HIP_ROLL_CORR   = 10.0f;

// Tỷ lệ phân bổ bù
static const float HIP_PITCH_GAIN  = 1.0f;
static const float KNEE_PITCH_GAIN = 0.5f;
static const float HIP_ROLL_GAIN   = 1.0f;

// Nếu chiều bù bị ngược thì đổi dấu tại đây (dễ nhất)
static const float PITCH_DIR = 1.0f;
static const float ROLL_DIR  = 1.0f;

// Nếu roll giữa L/R bị “ngược cơ khí” (do HIP_ROLL_R không inverted),
// bạn chỉnh 2 dấu này sau khi test thực tế:
static const float ROLL_L_SIGN = 1.0f;
static const float ROLL_R_SIGN = 1.0f;

// Low-pass cho lệnh servo để mượt (giảm giật)
float cmdHipPitch = 0, cmdKneePitch = 0, cmdHipRollL = 0, cmdHipRollR = 0;
static const float SMOOTH_ALPHA = 0.25f; // 0.1 mượt hơn, 0.3 phản ứng nhanh hơn

// timing
unsigned long lastMs = 0;

// ===== prototypes =====
void standStraight();
bool initIMU();
void calibrateReferenceAngles();
void balanceUpdate(float dt);
void readPitchRoll(float dt, float &pitchOut, float &rollOut);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  servoCtrl.begin();
  Serial.println("Robot khoi dong...");

  // 1) Stand straight (home pose)
  standStraight();
  Serial.println("Da ve vi tri dung thang (Home Position)");

  // 2) Init IMU
  if (!initIMU()) {
    Serial.println("IMU init failed!");
    while (1) delay(10);
  }

  // 3) Calibrate “góc cũ” = pitchRef/rollRef ngay khi đang đứng thẳng
  calibrateReferenceAngles();
  Serial.print("Reference pitch="); Serial.print(pitchRef, 2);
  Serial.print(" roll="); Serial.println(rollRef, 2);

  // 4) PID tune ban đầu (bạn có thể tăng/giảm)
  pidPitch.setTunings(1.2f, 0.0f, 0.06f);
  pidRoll.setTunings(1.4f, 0.0f, 0.07f);

  pidPitch.setOutputLimits(-MAX_HIP_PITCH_CORR, MAX_HIP_PITCH_CORR);
  pidRoll.setOutputLimits(-MAX_HIP_ROLL_CORR,  MAX_HIP_ROLL_CORR);

  pidPitch.reset();
  pidRoll.reset();

  lastMs = millis();
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastMs) / 1000.0f;
  if (dt <= 0) return;
  lastMs = now;

  // Update ~100Hz
  static float acc = 0;
  acc += dt;
  if (acc < UPDATE_DT_MIN) return;
  dt = acc;
  acc = 0;

  // Optional: bật/tắt bằng Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') {
      balanceEnabled = !balanceEnabled;
      Serial.print("balanceEnabled = "); Serial.println(balanceEnabled);
      // khi bật lại, lấy ref mới để đúng “góc cũ”
      if (balanceEnabled) calibrateReferenceAngles();
    }
  }

  if (balanceEnabled) balanceUpdate(dt);
}

/**
 * Đưa robot về pose mặc định: logical = 0 cho tất cả.
 * Offset + inverted sẽ được ServoController xử lý.
 */
void standStraight() {
  for (int i = 0; i < JOINT_COUNT; i++) {
    baseAngle[i] = 0.0f;
    servoCtrl.setAngle(i, baseAngle[i]);
    delay(80);
  }

  // set các biến cmd về 0 (để smooth bắt đầu đúng)
  cmdHipPitch = cmdKneePitch = cmdHipRollL = cmdHipRollR = 0;
}

bool initIMU() {
  if (!imu.init()) return false;

  // Robot nên đứng yên khi autoOffsets
  imu.autoOffsets();
  imu.setAccRange(MPU6500_ACC_RANGE_2G);
  imu.setGyrRange(MPU6500_GYRO_RANGE_250);

  // set góc ban đầu cho Kalman từ accel
  xyzFloat g = imu.getGValues();
  float rollAcc  = atan2(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2(-g.x, sqrt(g.y*g.y + g.z*g.z)) * 180.0f / PI;

  kRoll.setAngle(rollAcc);
  kPitch.setAngle(pitchAcc);

  return true;
}

/**
 * Lấy “góc cũ” = pitch/roll hiện tại ở dáng đứng thẳng.
 * Lấy trung bình nhiều mẫu để ổn định.
 */
void calibrateReferenceAngles() {
  float sumP = 0, sumR = 0;
  const int N = 120;

  // reset Kalman nhẹ (đỡ drift)
  xyzFloat g = imu.getGValues();
  float rollAcc  = atan2(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2(-g.x, sqrt(g.y*g.y + g.z*g.z)) * 180.0f / PI;
  kRoll.setAngle(rollAcc);
  kPitch.setAngle(pitchAcc);

  for (int i = 0; i < N; i++) {
    float p, r;
    readPitchRoll(0.01f, p, r);
    sumP += p;
    sumR += r;
    delay(10);
  }
  pitchRef = sumP / N;
  rollRef  = sumR / N;
}

/**
 * Đọc pitch/roll (Kalman).
 * Nếu trục gyro bị lệch (tuỳ bạn gắn IMU), bạn chỉ cần đổi gyro.x/gyro.y.
 */
void readPitchRoll(float dt, float &pitchOut, float &rollOut) {
  xyzFloat g = imu.getGValues();
  xyzFloat gyro = imu.getGyrValues(); // deg/s (tuỳ lib)

  float rollAcc  = atan2(g.y, g.z) * 180.0f / PI;
  float pitchAcc = atan2(-g.x, sqrt(g.y*g.y + g.z*g.z)) * 180.0f / PI;

  float roll  = kRoll.getAngle(rollAcc,   gyro.x, dt);
  float pitch = kPitch.getAngle(pitchAcc, gyro.y, dt);

  rollOut  = roll;
  pitchOut = pitch;
}

/**
 * Auto-balance: chỉ tác động 6 servo:
 *  HIP_PITCH_L/R, HIP_ROLL_L/R, KNEE_PITCH_L/R
 * Cổ chân giữ nguyên (0).
 */
void balanceUpdate(float dt) {
  float pitch, roll;
  readPitchRoll(dt, pitch, roll);

  // Fail-safe: nghiêng quá thì không cố bù mạnh (tránh gãy)
  if (fabs(pitch - pitchRef) > 25.0f || fabs(roll - rollRef) > 25.0f) {
    Serial.println("Tilt too large -> hold pose");
    // giữ 6 servo về base (0)
    servoCtrl.setAngle(HIP_PITCH_L,  baseAngle[HIP_PITCH_L]);
    servoCtrl.setAngle(HIP_PITCH_R,  baseAngle[HIP_PITCH_R]);
    servoCtrl.setAngle(KNEE_PITCH_L, baseAngle[KNEE_PITCH_L]);
    servoCtrl.setAngle(KNEE_PITCH_R, baseAngle[KNEE_PITCH_R]);
    servoCtrl.setAngle(HIP_ROLL_L,   baseAngle[HIP_ROLL_L]);
    servoCtrl.setAngle(HIP_ROLL_R,   baseAngle[HIP_ROLL_R]);
    return;
  }

  // Error so với “góc cũ”
  float pitchErr = (pitchRef - pitch);
  float rollErr  = (rollRef  - roll);

  // Deadband để giảm rung
  if (fabs(pitchErr) < DEADBAND_DEG) pitchErr = 0;
  if (fabs(rollErr)  < DEADBAND_DEG) rollErr  = 0;

  // PID: setpoint = 0, input = -error cũng được; ở đây dùng trực tiếp error như input bằng cách:
  // compute(setpoint, input, dt) => muốn output theo error, ta cho input = -error
  float corrPitch = pidPitch.compute(0.0f, -pitchErr, dt) * PITCH_DIR;
  float corrRoll  = pidRoll.compute(0.0f, -rollErr,  dt) * ROLL_DIR;

  // Clamp riêng cho knee (nhỏ hơn hip)
  float corrKneePitch = constrain(corrPitch * KNEE_PITCH_GAIN, -MAX_KNEE_PITCH_CORR, MAX_KNEE_PITCH_CORR);
  float corrHipPitch  = constrain(corrPitch * HIP_PITCH_GAIN,  -MAX_HIP_PITCH_CORR,  MAX_HIP_PITCH_CORR);

  float targetHipPitch = baseAngle[HIP_PITCH_L] + corrHipPitch;
  float targetKneePitch= baseAngle[KNEE_PITCH_L] + corrKneePitch;

  float targetHipRollL = baseAngle[HIP_ROLL_L] + (corrRoll * HIP_ROLL_GAIN * ROLL_L_SIGN);
  float targetHipRollR = baseAngle[HIP_ROLL_R] + (corrRoll * HIP_ROLL_GAIN * ROLL_R_SIGN);

  // Giới hạn theo biên độ logic trong config.h
  targetHipPitch  = constrain(targetHipPitch,  HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
  targetKneePitch = constrain(targetKneePitch, KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);
  targetHipRollL  = constrain(targetHipRollL,  HIP_ROLL_MIN_ANGLE,  HIP_ROLL_MAX_ANGLE);
  targetHipRollR  = constrain(targetHipRollR,  HIP_ROLL_MIN_ANGLE,  HIP_ROLL_MAX_ANGLE);

  // Smooth command (low-pass)
  cmdHipPitch  = cmdHipPitch  + SMOOTH_ALPHA * (targetHipPitch  - cmdHipPitch);
  cmdKneePitch = cmdKneePitch + SMOOTH_ALPHA * (targetKneePitch - cmdKneePitch);
  cmdHipRollL  = cmdHipRollL  + SMOOTH_ALPHA * (targetHipRollL  - cmdHipRollL);
  cmdHipRollR  = cmdHipRollR  + SMOOTH_ALPHA * (targetHipRollR  - cmdHipRollR);

  // Gửi lệnh: dùng đúng enum JointIndex bạn đưa
  servoCtrl.setAngle(HIP_PITCH_L,  cmdHipPitch);
  servoCtrl.setAngle(HIP_PITCH_R,  cmdHipPitch);

  servoCtrl.setAngle(KNEE_PITCH_L, cmdKneePitch);
  servoCtrl.setAngle(KNEE_PITCH_R, cmdKneePitch);

  servoCtrl.setAngle(HIP_ROLL_L,   cmdHipRollL);
  servoCtrl.setAngle(HIP_ROLL_R,   cmdHipRollR);

  // Debug nhẹ
  static int cnt = 0;
  if (++cnt % 30 == 0) {
    Serial.print("p="); Serial.print(pitch, 2);
    Serial.print(" r="); Serial.print(roll, 2);
    Serial.print(" ep="); Serial.print(pitchErr, 2);
    Serial.print(" er="); Serial.print(rollErr, 2);
    Serial.print(" cP="); Serial.print(corrPitch, 2);
    Serial.print(" cR="); Serial.println(corrRoll, 2);
  }
}
