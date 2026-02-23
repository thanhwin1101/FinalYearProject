#pragma once
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "config.h"

// ============================================================
// ServoController — PCA9685 wrapper with logical angle mapping
// ============================================================
class ServoController {
public:
  ServoController() : pca(0x40) {}

  void begin() {
    pca.begin();
    pca.setOscillatorFrequency(27000000);
    pca.setPWMFreq(SERVO_FREQ);
    delay(10);
  }

  /**
   * Set joint to a logical angle (degrees).
   * Logical 0 = standing straight. Applies zero-offset and inversion.
   */
  void setAngle(int jointIndex, float logicalAngle) {
    if (jointIndex < 0 || jointIndex >= JOINT_COUNT) return;

    const JointCfg& j = joints[jointIndex];
    float servoAngle = j.zero_offset;

    if (j.inverted) {
      servoAngle -= logicalAngle;
    } else {
      servoAngle += logicalAngle;
    }

    servoAngle = constrain(servoAngle, 0.0f, 180.0f);

    uint16_t pulse = map((long)(servoAngle * 10), 0, 1800,
                         SERVOMIN_PULSE, SERVOMAX_PULSE);
    pca.setPWM(j.pin, 0, pulse);
  }

  /**
   * Move all joints to logical 0 (standing straight).
   */
  void setHomePosition() {
    for (int i = 0; i < JOINT_COUNT; i++) {
      setAngle(i, 0.0f);
      delay(60);
    }
  }

private:
  Adafruit_PWMServoDriver pca;
};
