#include <Wire.h>
#include <MPU6500_WE.h>

// === Custom headers ===
#include "config.h"
#include "ServoController.h"
#include "Kalman.h"
#include "PID.h"
#include "Kinematics.h"

// ----------------------------------------------------------
// Fallback ID & limits nếu thiếu trong config.h
// ----------------------------------------------------------
#ifndef HIP_PITCH_L
  #define HIP_PITCH_L     0
#endif
#ifndef HIP_ROLL_L
  #define HIP_ROLL_L      1
#endif
#ifndef KNEE_PITCH_L
  #define KNEE_PITCH_L    2
#endif
#ifndef ANKLE_PITCH_L
  #define ANKLE_PITCH_L   3
#endif
#ifndef ANKLE_ROLL_L
  #define ANKLE_ROLL_L    4
#endif
#ifndef HIP_PITCH_R
  #define HIP_PITCH_R     5
#endif
#ifndef HIP_ROLL_R
  #define HIP_ROLL_R      6
#endif
#ifndef KNEE_PITCH_R
  #define KNEE_PITCH_R    7
#endif
#ifndef ANKLE_PITCH_R
  #define ANKLE_PITCH_R   8
#endif
#ifndef ANKLE_ROLL_R
  #define ANKLE_ROLL_R    9
#endif

#ifndef HIP_PITCH_MIN_ANGLE
  #define HIP_PITCH_MIN_ANGLE   -45.0f
  #define HIP_PITCH_MAX_ANGLE    45.0f
#endif
#ifndef HIP_ROLL_MIN_ANGLE
  #define HIP_ROLL_MIN_ANGLE    -20.0f
  #define HIP_ROLL_MAX_ANGLE     20.0f
#endif
#ifndef KNEE_PITCH_MIN_ANGLE
  #define KNEE_PITCH_MIN_ANGLE  -10.0f
  #define KNEE_PITCH_MAX_ANGLE   70.0f
#endif
#ifndef ANKLE_PITCH_MIN_ANGLE
  #define ANKLE_PITCH_MIN_ANGLE -25.0f
  #define ANKLE_PITCH_MAX_ANGLE  25.0f
#endif
#ifndef ANKLE_ROLL_MIN_ANGLE
  #define ANKLE_ROLL_MIN_ANGLE  -15.0f
  #define ANKLE_ROLL_MAX_ANGLE   15.0f
#endif

// ----------------------------------------------------------
// Đối tượng & trạng thái
// ----------------------------------------------------------
#define MPU6500_ADDR 0x68
MPU6500_WE imu(MPU6500_ADDR);
ServoController servos;
Kinematics kinematics;
Kalman kalmanPitch, kalmanRoll;
PID pidPitch, pidRoll;

enum RobotState { STATE_IDLE, STATE_BALANCING };
RobotState currentState = STATE_IDLE;

#define BUTTON_PIN 4
bool buttonState = HIGH, lastReading = HIGH;
unsigned long lastDebounceTime = 0, debounceDelay = 50;

// ----------------------------------------------------------
// Thời gian: vòng lặp cố định 300 Hz
// ----------------------------------------------------------
#define LOOP_HZ          300
#define LOOP_PERIOD_US   (1000000UL / LOOP_HZ)
unsigned long loopNextTick = 0;
float dt = 0.0f;

// ----------------------------------------------------------
// PID + Kalman + Tuning
// ----------------------------------------------------------
float KALMAN_Q_ANGLE = 0.001f, KALMAN_Q_BIAS = 0.003f, KALMAN_R_MEASURE = 0.03f;

// Khởi đầu hơi cao KD để giảm overshoot
float PITCH_KP = 1.5f, PITCH_KI = 0.0f, PITCH_KD = 0.05f;
float ROLL_KP  = 1.5f, ROLL_KI  = 0.0f, ROLL_KD  = 0.05f;

float P_STEP = 0.1f, I_STEP = 0.01f, D_STEP = 0.02f;

float current_pitch = 0.0f, current_roll = 0.0f;
float setpoint_pitch = 0.0f, setpoint_roll = 0.0f;

// ----------------------------------------------------------
// Phân bổ correction
// ----------------------------------------------------------
const float MAP_PITCH_TO_ANKLE = 0.80f;
const float MAP_PITCH_TO_HIP   = 0.20f;
const float MAP_ROLL_TO_ANKLE  = 0.90f;
const float MAP_ROLL_TO_HIP    = 0.10f;

// ----------------------------------------------------------
// Deadband + Low-pass (giảm trễ hơn để nhanh hơn)
// ----------------------------------------------------------
float DEAD_BAND_DEG = 0.20f;  // từ 0.30 -> 0.20 để nhanh hơn
float LPF_ALPHA     = 0.30f;  // từ 0.20 -> 0.30 để nhanh hơn

// Lưu lệnh đã lọc
float filt_ankle_pitch_cmd = 0.0f;
float filt_hip_pitch_cmd   = 0.0f;
float filt_ankle_roll_L    = 0.0f;
float filt_ankle_roll_R    = 0.0f;
float filt_hip_roll_L      = 0.0f;
float filt_hip_roll_R      = 0.0f;

// ----------------------------------------------------------
// NEW: Output limits nhỏ hơn để hạn chế vọt
// ----------------------------------------------------------
float OUTLIM_ANKLE = 8.0f;  // ±8°
float OUTLIM_HIP   = 6.0f;  // ±6°

// ----------------------------------------------------------
// NEW: Slew-rate limit (°/s) để tránh giật và overshoot
// ----------------------------------------------------------
float MAX_RATE_ANKLE = 120.0f; // 120°/s
float MAX_RATE_HIP   = 90.0f;  // 90°/s

// ----------------------------------------------------------
// NEW: Soft-saturation (đệm mềm gần biên) – hệ số mềm hóa
// ----------------------------------------------------------
float SOFT_SAT_K = 0.6f; // 0..1 (1: gắt, 0.3..0.6: mềm)

// ----------------------------------------------------------
// Debug in
// ----------------------------------------------------------
unsigned long lastStatusPrintTime = 0;
const unsigned long statusPrintInterval = 1000;

// =================== Helpers ===================
inline float constrainf(float x, float a, float b){ return (x < a) ? a : (x > b ? b : x); }

inline float applyDeadband(float x, float db) { return (fabs(x) < db) ? 0.0f : x; }

inline float lpf(float prev, float current, float alpha) {
  return (1.0f - alpha) * prev + alpha * current;
}

// soft-saturation: scale mềm dần khi gần biên (dùng tanh-like)
inline float softSaturate(float x, float limit, float k) {
  if (limit <= 0) return 0;
  float n = x / limit;             // -1..1 là vùng an toàn
  float s = tanh(k * n);           // nén mềm
  return s * limit;
}

// Slew-rate limiter: giới hạn tốc độ thay đổi (°/s)
inline float rateLimit(float prev, float target, float maxRateDegPerSec, float dt) {
  float maxStep = maxRateDegPerSec * dt;
  float delta   = target - prev;
  if (delta >  maxStep) delta =  maxStep;
  if (delta < -maxStep) delta = -maxStep;
  return prev + delta;
}

// Set + clamp theo giới hạn cơ khí
inline void setJointClamped(int idx, float logical_deg, float min_deg, float max_deg) {
  servos.setAngle(idx, constrainf(logical_deg, min_deg, max_deg));
}

// =================== Button ===================
void checkButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastReading) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        Serial.println("Nut bam da duoc nhan!");
        if (currentState == STATE_IDLE) {
          currentState = STATE_BALANCING;
          Serial.println("Chuyen sang che do THANG BANG");
          pidPitch.reset(); 
          pidRoll.reset();
          servos.setHomePosition();  // đảm bảo ở HOME trước khi bám
          loopNextTick = micros() + LOOP_PERIOD_US; // reset scheduler
        } else {
          currentState = STATE_IDLE;
          Serial.println("Chuyen sang che do IDLE (Dung yen)");
          servos.setHomePosition();
        }
      }
    }
  }
  lastReading = reading;
}

// =================== Setup ===================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!imu.init()) {
    Serial.println("MPU6500 not found!");
    while(1) delay(10);
  }
  Serial.println("MPU6500 Found.");
  imu.setAccRange(MPU6500_ACC_RANGE_8G);
  imu.setGyrRange(MPU6500_GYRO_RANGE_500);
  imu.setAccDLPF(MPU6500_DLPF_6);
  imu.setGyrDLPF(MPU6500_DLPF_6);
  Serial.println("Dang do offset Gyro... Giu yen!");
  imu.autoOffsets();
  Serial.println("Do offset xong.");

  servos.begin();
  delay(500);
  servos.setHomePosition();
  Serial.println("Servos at HOME. Giu yen robot de do Setpoint...");
  delay(2000);

  kalmanPitch.setQangle(KALMAN_Q_ANGLE);
  kalmanPitch.setQbias(KALMAN_Q_BIAS);
  kalmanPitch.setRmeasure(KALMAN_R_MEASURE);
  kalmanRoll.setQangle(KALMAN_Q_ANGLE);
  kalmanRoll.setQbias(KALMAN_Q_BIAS);
  kalmanRoll.setRmeasure(KALMAN_R_MEASURE);

  // Auto-measure setpoint (100 mẫu cuối)
  Serial.println("Dang do Setpoint thuc te (Kalman)...");
  float sumP=0, sumR=0, mp=0, mr=0;
  float _dt = 0.005f;
  for (int i=0;i<200;i++) {
    unsigned long t0 = micros();
    xyzFloat acc = imu.getGValues();
    xyzFloat gyr = imu.getGyrValues();
    float a_roll  = atan2(acc.y, acc.z) * 180.0f / PI;
    float a_pitch = atan2(-acc.x, sqrt(acc.y*acc.y + acc.z*acc.z)) * 180.0f / PI;
    mr = kalmanRoll.getAngle(a_roll,  gyr.x, _dt);
    mp = kalmanPitch.getAngle(a_pitch, gyr.y, _dt);
    if (i>=100){ sumP += mp; sumR += mr; }
    unsigned long t1 = micros();
    _dt = (t1 - t0) / 1e6f; if (_dt <= 0) _dt = 0.005f;
    delayMicroseconds(1000); // giữ ~1ms giữa các mẫu (nhẹ nhàng)
  }
  setpoint_pitch = sumP / 100.0f;
  setpoint_roll  = sumR / 100.0f;

  Serial.println("---------------------------------------------");
  Serial.print("  Setpoint tu dong: Pitch = "); Serial.println(setpoint_pitch, 3);
  Serial.print("  Setpoint tu dong: Roll  = "); Serial.println(setpoint_roll, 3);
  Serial.println("---------------------------------------------");

  pidPitch.setOutputLimits(-10.0f, 10.0f);
  pidRoll .setOutputLimits(-10.0f, 10.0f);
  pidPitch.setTunings(PITCH_KP, PITCH_KI, PITCH_KD);
  pidRoll .setTunings(ROLL_KP,  ROLL_KI,  ROLL_KD);

  filt_ankle_pitch_cmd = filt_hip_pitch_cmd = 0.0f;
  filt_ankle_roll_L = filt_ankle_roll_R = 0.0f;
  filt_hip_roll_L = filt_hip_roll_R = 0.0f;

  loopNextTick = micros() + LOOP_PERIOD_US;

  Serial.println("=============================================");
  Serial.println("Robot o che do IDLE. Nhan nut de THANG BANG.");
  Serial.println("Pitch: [p/l] (P), [i/k] (I), [d/f] (D). Setpoint: [w/s]=Pitch -/+, [q/a]=Roll -/+");
  Serial.println("Deadband: (]) +0.05°, ([) -0.05° | LPF alpha: (=) +0.05, (-) -0.05");
  Serial.println("Slew Ankle: (>) +10°/s, (<) -10°/s | Slew Hip: (}) +10°/s, ({) -10°/s");
  Serial.println("OutLim Ankle: (.) +0.5°, (,) -0.5° | OutLim Hip: (:) +0.5°, (;) -0.5°");
  Serial.println("=============================================");
}

// =================== Logic cân bằng ===================
void runBalancingLogic(float dt) {
  // B) IMU
  xyzFloat acc = imu.getGValues();
  xyzFloat gyr = imu.getGyrValues();

  // C) acc angles
  float a_roll  = atan2(acc.y, acc.z) * 180.0f / PI;
  float a_pitch = atan2(-acc.x, sqrt(acc.y*acc.y + acc.z*acc.z)) * 180.0f / PI;

  // D) Kalman
  current_roll  = kalmanRoll.getAngle (a_roll,  gyr.x, dt);
  current_pitch = kalmanPitch.getAngle(a_pitch, gyr.y, dt);

  // E) PID
  float pitch_corr = pidPitch.compute(setpoint_pitch, current_pitch, dt);
  float roll_corr  = pidRoll .compute(setpoint_roll,  current_roll,  dt);

  // E2) Deadband
  pitch_corr = applyDeadband(pitch_corr, DEAD_BAND_DEG);
  roll_corr  = applyDeadband(roll_corr,  DEAD_BAND_DEG);

  // F) Map
  float ankle_pitch_cmd = MAP_PITCH_TO_ANKLE * pitch_corr;
  float hip_pitch_cmd   = MAP_PITCH_TO_HIP   * pitch_corr;

  float ankle_roll_cmd_L =  MAP_ROLL_TO_ANKLE * roll_corr;
  float ankle_roll_cmd_R = -MAP_ROLL_TO_ANKLE * roll_corr;
  float hip_roll_cmd_L   =  MAP_ROLL_TO_HIP   * roll_corr;
  float hip_roll_cmd_R   = -MAP_ROLL_TO_HIP   * roll_corr;

  // F2) LPF
  float raw_ap = lpf(filt_ankle_pitch_cmd, ankle_pitch_cmd, LPF_ALPHA);
  float raw_hp = lpf(filt_hip_pitch_cmd,   hip_pitch_cmd,   LPF_ALPHA);
  float raw_arL= lpf(filt_ankle_roll_L,    ankle_roll_cmd_L,LPF_ALPHA);
  float raw_arR= lpf(filt_ankle_roll_R,    ankle_roll_cmd_R,LPF_ALPHA);
  float raw_hrL= lpf(filt_hip_roll_L,      hip_roll_cmd_L,  LPF_ALPHA);
  float raw_hrR= lpf(filt_hip_roll_R,      hip_roll_cmd_R,  LPF_ALPHA);

  // G) Soft-saturation theo OutLim nhóm khớp
  raw_ap = softSaturate(raw_ap,  OUTLIM_ANKLE, SOFT_SAT_K);
  raw_arL= softSaturate(raw_arL, OUTLIM_ANKLE, SOFT_SAT_K);
  raw_arR= softSaturate(raw_arR, OUTLIM_ANKLE, SOFT_SAT_K);

  raw_hp = softSaturate(raw_hp,  OUTLIM_HIP,   SOFT_SAT_K);
  raw_hrL= softSaturate(raw_hrL, OUTLIM_HIP,   SOFT_SAT_K);
  raw_hrR= softSaturate(raw_hrR, OUTLIM_HIP,   SOFT_SAT_K);

  // H) Slew-rate limit
  filt_ankle_pitch_cmd = rateLimit(filt_ankle_pitch_cmd, raw_ap,  MAX_RATE_ANKLE, dt);
  filt_ankle_roll_L    = rateLimit(filt_ankle_roll_L,    raw_arL, MAX_RATE_ANKLE, dt);
  filt_ankle_roll_R    = rateLimit(filt_ankle_roll_R,    raw_arR, MAX_RATE_ANKLE, dt);

  filt_hip_pitch_cmd   = rateLimit(filt_hip_pitch_cmd,   raw_hp,  MAX_RATE_HIP,   dt);
  filt_hip_roll_L      = rateLimit(filt_hip_roll_L,      raw_hrL, MAX_RATE_HIP,   dt);
  filt_hip_roll_R      = rateLimit(filt_hip_roll_R,      raw_hrR, MAX_RATE_HIP,   dt);

  // I) Gửi lệnh (các khớp khác = 0°)
  setJointClamped(HIP_PITCH_L, filt_hip_pitch_cmd,  HIP_PITCH_MIN_ANGLE,  HIP_PITCH_MAX_ANGLE);
  setJointClamped(HIP_PITCH_R, filt_hip_pitch_cmd,  HIP_PITCH_MIN_ANGLE,  HIP_PITCH_MAX_ANGLE);

  setJointClamped(KNEE_PITCH_L, 0.0f, KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);
  setJointClamped(KNEE_PITCH_R, 0.0f, KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);

  setJointClamped(ANKLE_PITCH_L, filt_ankle_pitch_cmd, ANKLE_PITCH_MIN_ANGLE, ANKLE_PITCH_MAX_ANGLE);
  setJointClamped(ANKLE_PITCH_R, filt_ankle_pitch_cmd, ANKLE_PITCH_MIN_ANGLE, ANKLE_PITCH_MAX_ANGLE);

  setJointClamped(HIP_ROLL_L, filt_hip_roll_L,  HIP_ROLL_MIN_ANGLE,  HIP_ROLL_MAX_ANGLE);
  setJointClamped(HIP_ROLL_R, filt_hip_roll_R,  HIP_ROLL_MIN_ANGLE,  HIP_ROLL_MAX_ANGLE);

  setJointClamped(ANKLE_ROLL_L, filt_ankle_roll_L, ANKLE_ROLL_MIN_ANGLE, ANKLE_ROLL_MAX_ANGLE);
  setJointClamped(ANKLE_ROLL_R, filt_ankle_roll_R, ANKLE_ROLL_MIN_ANGLE, ANKLE_ROLL_MAX_ANGLE);
}

// =================== Serial tuning ===================
void checkSerialTuning() {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  // PID pitch
  if (cmd=='p') PITCH_KP += P_STEP;
  else if (cmd=='l') PITCH_KP -= P_STEP;
  else if (cmd=='i') PITCH_KI += I_STEP;
  else if (cmd=='k') PITCH_KI -= I_STEP;
  else if (cmd=='d') PITCH_KD += D_STEP;
  else if (cmd=='f') PITCH_KD -= D_STEP;

  // PID roll
  else if (cmd=='P') ROLL_KP += P_STEP;
  else if (cmd=='L') ROLL_KP -= P_STEP;
  else if (cmd=='I') ROLL_KI += I_STEP;
  else if (cmd=='K') ROLL_KI -= I_STEP;
  else if (cmd=='D') ROLL_KD += D_STEP;
  else if (cmd=='F') ROLL_KD -= D_STEP;

  // Setpoint
  else if (cmd=='s') setpoint_pitch += 0.05f;
  else if (cmd=='w') setpoint_pitch -= 0.05f;
  else if (cmd=='a') setpoint_roll  += 0.05f;
  else if (cmd=='q') setpoint_roll  -= 0.05f;

  // Deadband & LPF
  else if (cmd==']') DEAD_BAND_DEG += 0.05f;
  else if (cmd=='[') { DEAD_BAND_DEG -= 0.05f; if (DEAD_BAND_DEG < 0) DEAD_BAND_DEG = 0; }
  else if (cmd=='=') { LPF_ALPHA += 0.05f; if (LPF_ALPHA > 1.0f) LPF_ALPHA = 1.0f; }
  else if (cmd=='-') { LPF_ALPHA -= 0.05f; if (LPF_ALPHA < 0.0f) LPF_ALPHA = 0.0f; }

  // NEW: Slew-rate (ankle/hip)
  else if (cmd=='>') MAX_RATE_ANKLE += 10.0f;
  else if (cmd=='<') { MAX_RATE_ANKLE -= 10.0f; if (MAX_RATE_ANKLE < 10.0f) MAX_RATE_ANKLE = 10.0f; }
  else if (cmd=='}') MAX_RATE_HIP   += 10.0f;
  else if (cmd=='{') { MAX_RATE_HIP   -= 10.0f; if (MAX_RATE_HIP   < 10.0f) MAX_RATE_HIP   = 10.0f; }

  // NEW: Output limit nhóm khớp
  else if (cmd=='.') OUTLIM_ANKLE += 0.5f;
  else if (cmd==',') { OUTLIM_ANKLE -= 0.5f; if (OUTLIM_ANKLE < 2.0f) OUTLIM_ANKLE = 2.0f; }
  else if (cmd==':') OUTLIM_HIP   += 0.5f;
  else if (cmd==';') { OUTLIM_HIP   -= 0.5f; if (OUTLIM_HIP   < 2.0f) OUTLIM_HIP   = 2.0f; }

  // Kẹp không âm cho PID
  if (PITCH_KP < 0) PITCH_KP = 0;
  if (PITCH_KI < 0) PITCH_KI = 0;
  if (PITCH_KD < 0) PITCH_KD = 0;
  if (ROLL_KP  < 0) ROLL_KP  = 0;
  if (ROLL_KI  < 0) ROLL_KI  = 0;
  if (ROLL_KD  < 0) ROLL_KD  = 0;

  pidPitch.setTunings(PITCH_KP, PITCH_KI, PITCH_KD);
  pidRoll .setTunings(ROLL_KP,  ROLL_KI,  ROLL_KD);

  Serial.println("--- TUNING ---");
  Serial.print("Pitch: KP="); Serial.print(PITCH_KP,3);
  Serial.print(" KI=");       Serial.print(PITCH_KI,3);
  Serial.print(" KD=");       Serial.print(PITCH_KD,3);
  Serial.print(" SP=");       Serial.println(setpoint_pitch,3);
  Serial.print("Roll : KP="); Serial.print(ROLL_KP,3);
  Serial.print(" KI=");       Serial.print(ROLL_KI,3);
  Serial.print(" KD=");       Serial.print(ROLL_KD,3);
  Serial.print(" SP=");       Serial.println(setpoint_roll,3);
  Serial.print("Deadband=");  Serial.print(DEAD_BAND_DEG,3);
  Serial.print(" | LPF alpha="); Serial.print(LPF_ALPHA,3);
  Serial.print(" | Slew Ankle="); Serial.print(MAX_RATE_ANKLE,1);
  Serial.print(" deg/s, Hip=");   Serial.print(MAX_RATE_HIP,1);
  Serial.print(" | OutLim Ankle="); Serial.print(OUTLIM_ANKLE,2);
  Serial.print("°, Hip=");         Serial.println(OUTLIM_HIP,2);
}

// =================== Loop cố định tần ===================
void loop() {
  checkButton();

  // Scheduler: chờ đúng tick (ổn định tần)
  unsigned long now = micros();
  if ((long)(now - loopNextTick) < 0) return; // chưa tới thời điểm
  // cập nhật tick kế
  loopNextTick += LOOP_PERIOD_US;

  // Tính dt thực tế
  static unsigned long lastMicros = now;
  dt = (now - lastMicros) / 1e6f; 
  lastMicros = now;
  if (dt <= 0) dt = 1.0f / LOOP_HZ;

  switch (currentState) {
    case STATE_IDLE:
      if (millis() - lastStatusPrintTime > statusPrintInterval) {
        lastStatusPrintTime = millis();
        Serial.println("Trang thai: IDLE (Dang nghi...)");
      }
      break;

    case STATE_BALANCING:
      checkSerialTuning();
      runBalancingLogic(dt);
      break;
  }

  // Debug 5 Hz
  static unsigned long lastDbg = 0;
  if (millis() - lastDbg > 200) {
    lastDbg = millis();
    Serial.print("Pitch(In,Set): "); Serial.print(current_pitch, 2);
    Serial.print(","); Serial.print(setpoint_pitch, 2); Serial.print(" | ");
    Serial.print("Roll(In,Set): ");  Serial.print(current_roll, 2);
    Serial.print(","); Serial.println(setpoint_roll, 2);-
  }
}
