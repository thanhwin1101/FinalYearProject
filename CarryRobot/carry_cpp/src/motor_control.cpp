#include "motor_control.h"
#include "config.h"
#include "globals.h"

// =========================================
// HELPER FUNCTIONS
// =========================================
static inline uint8_t clampDuty(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static void setSideSpeed(int leftPwm, int rightPwm) {
  ledcWrite((uint8_t)EN_LEFT, clampDuty(leftPwm));
  ledcWrite((uint8_t)EN_RIGHT, clampDuty(rightPwm));
}

static inline int applyGainDuty(int pwm, float gain) {
  if (pwm <= 0) return 0;
  if (gain < 0.0f) gain = 0.0f;
  float v = (float)pwm * gain;
  if (v > 255.0f) v = 255.0f;
  return (int)(v + 0.5f);
}

// =========================================
// INITIALIZATION
// =========================================
void motorPwmInit() {
  ledcAttach((uint8_t)EN_LEFT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttach((uint8_t)EN_RIGHT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcWrite((uint8_t)EN_LEFT, 0);
  ledcWrite((uint8_t)EN_RIGHT, 0);
}

void motorsStop() {
  setSideSpeed(0, 0);
  digitalWrite(FL_IN1, LOW); digitalWrite(FL_IN2, LOW);
  digitalWrite(RL_IN1, LOW); digitalWrite(RL_IN2, LOW);
  digitalWrite(FR_IN1, LOW); digitalWrite(FR_IN2, LOW);
  digitalWrite(RR_IN1, LOW); digitalWrite(RR_IN2, LOW);
}

// =========================================
// FORWARD ACTIVE BRAKE
// =========================================
void applyForwardBrake(int brakePwm, int brakeMs) {
  setSideSpeed(brakePwm, brakePwm);
  
  // Left Side Backward (respects INVERT_LEFT)
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  // Right Side Backward (respects INVERT_RIGHT)
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
  
  delay(brakeMs);
  motorsStop();
}

// =========================================
// MOVEMENT
// =========================================
void driveForward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);

  // Left Side Forward (respects INVERT_LEFT)
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  // Right Side Forward (respects INVERT_RIGHT)
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

void driveBackward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);

  // Left Side Backward (respects INVERT_LEFT)
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  // Right Side Backward (respects INVERT_RIGHT)
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

// =========================================
// DIRECTION SETUP FOR TURNING
// =========================================
void setMotorDirLeft() {
  // Left Side Backward
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  // Right Side Forward
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

void setMotorDirRight() {
  // Left Side Forward
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  // Right Side Backward
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

// =========================================
// HARD BRAKE (FOR TURNING)
// =========================================
void applyHardBrake(bool wasTurningLeft, int brakePwm, int brakeMs) {
  if (wasTurningLeft) {
    setMotorDirRight();
  } else {
    setMotorDirLeft();
  }
  
  setSideSpeed(brakePwm, brakePwm);
  delay(brakeMs);
  motorsStop();
}

// =========================================
// TIME-BASED TURNING
// =========================================
void rotateByTime(unsigned long totalMs, bool isLeft) {
  motorsStop();
  delay(50);

  unsigned long slowZoneStart = (unsigned long)(totalMs * (1.0 - SLOW_ZONE_RATIO));
  unsigned long fineZoneStart = (unsigned long)(totalMs * (1.0 - FINE_ZONE_RATIO));
  
  if (isLeft) setMotorDirLeft(); else setMotorDirRight();
  
  unsigned long startTime = millis();
  int currentPwm = PWM_TURN;
  setSideSpeed(currentPwm, currentPwm);

  while (true) {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= totalMs) break;
    
    int newPwm;
    if (elapsed >= fineZoneStart) {
      newPwm = PWM_TURN_FINE;
    } else if (elapsed >= slowZoneStart) {
      float progress = (float)(elapsed - slowZoneStart) / (fineZoneStart - slowZoneStart);
      newPwm = PWM_TURN_SLOW - (int)((PWM_TURN_SLOW - PWM_TURN_FINE) * progress);
    } else {
      newPwm = PWM_TURN;
    }
    
    if (abs(newPwm - currentPwm) >= 8) {
      currentPwm = newPwm;
      setSideSpeed(currentPwm, currentPwm);
    }
    
    delay(5);
  }

  motorsStop();
  delay(15);
  
  int brakePwm = (currentPwm < PWM_TURN_SLOW) ? (PWM_BRAKE / 2) : PWM_BRAKE;
  int brakeMs = (currentPwm < PWM_TURN_SLOW) ? 40 : 60;
  applyHardBrake(isLeft, brakePwm, brakeMs);

#if SERIAL_DEBUG
  Serial.print("Turn "); Serial.print(isLeft ? "L" : "R");
  Serial.print(" time="); Serial.print(totalMs);
  Serial.println("ms");
#endif
}

void turnByAction(char a) {
  if (a == 'L') rotateByTime(TURN_90_MS, true);
  else if (a == 'R') rotateByTime(TURN_90_MS, false);
  else if (a == 'B') rotateByTime(TURN_180_MS, true);
}
