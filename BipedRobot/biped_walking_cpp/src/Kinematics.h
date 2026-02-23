// --- Ghi đè vào biped_walking_cpp/src/Kinematics.h ---
#pragma once
#include <math.h>
#include "config.h"

#define LEG_LEFT  0
#define LEG_RIGHT 1

class Kinematics {
public:
  float target_angles[JOINT_COUNT];

  Kinematics() {
    for (int i = 0; i < JOINT_COUNT; i++) target_angles[i] = 0.0f;
  }

  bool calculateLeg(int leg, float x, float y, float foot_pitch, float hip_yaw, float ankle_roll) {
    float D_sq = x * x + y * y;
    float D = sqrt(D_sq);

    if (D > (THIGH_LENGTH_MM + SHANK_LENGTH_MM)) return false;

    float beta_rad = acos((THIGH_LENGTH_MM * THIGH_LENGTH_MM + SHANK_LENGTH_MM * SHANK_LENGTH_MM - D_sq) / (2 * THIGH_LENGTH_MM * SHANK_LENGTH_MM));
    float knee_pitch_rad = PI - beta_rad;

    float alpha_rad = acos((THIGH_LENGTH_MM * THIGH_LENGTH_MM + D_sq - SHANK_LENGTH_MM * SHANK_LENGTH_MM) / (2 * THIGH_LENGTH_MM * D));
    float gamma_rad = atan2(x, -y);
    float hip_pitch_rad = gamma_rad - alpha_rad;

    float ankle_pitch_comp = -hip_pitch_rad - knee_pitch_rad;
    float ankle_pitch_rad = ankle_pitch_comp + foot_pitch;

    float ang[5];
    ang[0] = constrain(degrees(hip_pitch_rad), HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
    ang[1] = constrain(hip_yaw, HIP_ROLL_MIN_ANGLE, HIP_ROLL_MAX_ANGLE); // Joint này là YAW
    ang[2] = constrain(degrees(knee_pitch_rad), KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);
    ang[3] = constrain(degrees(ankle_pitch_rad), ANKLE_PITCH_MIN_ANGLE, ANKLE_PITCH_MAX_ANGLE);
    ang[4] = constrain(ankle_roll, ANKLE_ROLL_MIN_ANGLE, ANKLE_ROLL_MAX_ANGLE);

    if (leg == LEG_LEFT) {
      target_angles[HIP_PITCH_L] = ang[0]; target_angles[HIP_ROLL_L] = ang[1];
      target_angles[KNEE_PITCH_L] = ang[2]; target_angles[ANKLE_PITCH_L] = ang[3];
      target_angles[ANKLE_ROLL_L] = ang[4];
    } else {
      target_angles[HIP_PITCH_R] = ang[0]; target_angles[HIP_ROLL_R] = ang[1];
      target_angles[KNEE_PITCH_R] = ang[2]; target_angles[ANKLE_PITCH_R] = ang[3];
      target_angles[ANKLE_ROLL_R] = ang[4];
    }
    return true;
  }
};