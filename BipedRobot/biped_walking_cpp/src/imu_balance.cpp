// --- biped_walking_cpp/src/imu_balance.cpp ---
#include "gait_generator.h"
#include "fsr_handler.h"
#include "imu_balance.h"
#include "globals.h"

// ============================================================
// Stand Straight — all joints to 0°
// ============================================================
void standStraight() {
  for (int i = 0; i < JOINT_COUNT; i++) {
    baseAngle[i] = 0.0f;
    servoCtrl.setAngle(i, 0.0f);
    delay(60);
  }
  lastHipP = lastKneeP = lastHipRL = lastHipRR = lastAnkleP = 0.0f;
}

// ============================================================
// IMU Initialization
// ============================================================
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

// ============================================================
// Calibrate Reference Angles (80 samples, ~0.8s)
// ============================================================
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

// ============================================================
// Read Pitch/Roll from IMU (Kalman + low-pass)
// ============================================================
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

  rollRate  = gyro.x;
  pitchRate = gyro.y;
}

// ============================================================
// Update Control — Gait + IMU Balance + FSR feedback
// ============================================================
void updateControl(float dt) {
  // 1. Tính toán quỹ đạo bước đi
  gaitUpdate(dt);

  // 2. Đọc IMU
  float pitch, roll, pitchRate, rollRate;
  readPitchRoll(dt, pitch, roll, pitchRate, rollRate);

  float pitchErr = pitchRef - pitch;
  float rollErr  = rollRef  - roll;

  // 3. PD Balance (Cơ thể bù ngã)
  float corrPitch = (KP_PITCH * pitchErr - KD_PITCH_RATE * pitchRate) * PITCH_DIR;
  float corrRoll  = (KP_ROLL  * rollErr  - KD_ROLL_RATE  * rollRate)  * ROLL_DIR;

  corrPitch = clampf(corrPitch, -MAX_HIP_PITCH_CORR, MAX_HIP_PITCH_CORR);
  corrRoll  = clampf(corrRoll,  -MAX_HIP_ROLL_CORR,  MAX_HIP_ROLL_CORR);

  // 4. Trộn tín hiệu vào Servo
  float targetHipP_L = clampf(gaitAngles[HIP_PITCH_L] + corrPitch, HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
  float targetHipP_R = clampf(gaitAngles[HIP_PITCH_R] + corrPitch, HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
  
  // Chú ý: Cân bằng Roll ngang ngã trái/phải tác động vào Ankle Roll thay vì Hip (do không có Hip Roll)
  float targetAnkleR_L = clampf(gaitAngles[ANKLE_ROLL_L] + corrRoll, ANKLE_ROLL_MIN_ANGLE, ANKLE_ROLL_MAX_ANGLE);
  float targetAnkleR_R = clampf(gaitAngles[ANKLE_ROLL_R] + corrRoll, ANKLE_ROLL_MIN_ANGLE, ANKLE_ROLL_MAX_ANGLE);

  float targetAnkleP_L = gaitAngles[ANKLE_PITCH_L];
  float targetAnkleP_R = gaitAngles[ANKLE_PITCH_R];

  float ankleLevel = (pitchRef - pitch) * ANKLE_LEVEL_GAIN * ANKLE_DIR;

  // Feedback FSR (chỉ bù cân bằng nếu chân đang tiếp đất)
  if (footL.isGrounded) {
    targetAnkleP_L += ankleLevel;
    targetAnkleP_L += (footL.copX * 3.0f); // Nghiêng mũi/gót bù
    targetAnkleR_L += (footL.copY * 2.0f); // Nghiêng trái/phải bù
  }
  if (footR.isGrounded) {
    targetAnkleP_R += ankleLevel;
    targetAnkleP_R += (footR.copX * 3.0f);
    targetAnkleR_R += (footR.copY * 2.0f);
  }

  // Xuất ra PWM Servo
  servoCtrl.setAngle(HIP_PITCH_L, targetHipP_L);
  servoCtrl.setAngle(HIP_PITCH_R, targetHipP_R);
  
  // Hip Yaw (xoay chữ V) chỉ nhận từ Gait, KHÔNG nhận từ IMU
  servoCtrl.setAngle(HIP_ROLL_L, gaitAngles[HIP_ROLL_L]); 
  servoCtrl.setAngle(HIP_ROLL_R, gaitAngles[HIP_ROLL_R]); 
  
  servoCtrl.setAngle(KNEE_PITCH_L, gaitAngles[KNEE_PITCH_L]);
  servoCtrl.setAngle(KNEE_PITCH_R, gaitAngles[KNEE_PITCH_R]);
  
  servoCtrl.setAngle(ANKLE_PITCH_L, targetAnkleP_L);
  servoCtrl.setAngle(ANKLE_PITCH_R, targetAnkleP_R);
  
  servoCtrl.setAngle(ANKLE_ROLL_L, targetAnkleR_L);
  servoCtrl.setAngle(ANKLE_ROLL_R, targetAnkleR_R);
}