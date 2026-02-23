// --- Ghi đè vào biped_walking_cpp/src/config.h ---
#pragma once

#include <Arduino.h>

// ---------- FSR Sensor Pins (ADC1 & ADC2 an toàn vì tắt WiFi) ----------
// LEFT FOOT
#define FSR_L_FRONT 32
#define FSR_L_HEEL  33
#define FSR_L_LEFT  34
#define FSR_L_RIGHT 35

// RIGHT FOOT
#define FSR_R_FRONT 36
#define FSR_R_HEEL  39
#define FSR_R_LEFT  26
#define FSR_R_RIGHT 27

#define FSR_CONTACT_THRES 500  // Ngưỡng chạm đất

// ---------- UART ----------
#define USER_UART_RX      16
#define USER_UART_TX      17
#define USER_UART_BAUD    115200

// ---------- Joint Count ----------
#define JOINT_COUNT 10
enum JointIndex {
  HIP_PITCH_L = 0, HIP_ROLL_L = 1, KNEE_PITCH_L = 2, ANKLE_PITCH_L = 3, ANKLE_ROLL_L = 4,
  HIP_PITCH_R = 5, HIP_ROLL_R = 6, KNEE_PITCH_R = 7, ANKLE_PITCH_R = 8, ANKLE_ROLL_R = 9
};

struct JointCfg { const char* name; uint8_t pin; float zero_offset; bool inverted; };

// Hãy thay đổi zero_offset cho phù hợp với phần cứng thực tế của bạn
static const JointCfg joints[JOINT_COUNT] = {
  { "HIP_PITCH_L",   0,  90.0f, false },
  { "HIP_YAW_L",     1,  90.0f, false }, // Tên cũ HIP_ROLL
  { "KNEE_PITCH_L",  2,  90.0f, false },
  { "ANKLE_PITCH_L", 3,  90.0f, false },
  { "ANKLE_ROLL_L",  4,  90.0f, false },
  { "HIP_PITCH_R",   5,  90.0f, true  },
  { "HIP_YAW_R",     6,  90.0f, true  }, 
  { "KNEE_PITCH_R",  7,  90.0f, true  },
  { "ANKLE_PITCH_R", 8,  90.0f, true  },
  { "ANKLE_ROLL_R",  9,  90.0f, true  }
};

// ---------- Kinematics & Walking ----------
#define THIGH_LENGTH_MM    60.0f
#define SHANK_LENGTH_MM    70.0f

#define STAND_HEIGHT_MM    110.0f // Chiều cao lúc đứng
#define STEP_CLEARANCE_MM  25.0f  // Độ cao nhấc chân
#define STEP_LENGTH_MM     35.0f  // Độ dài bước
#define BODY_TILT_DEG      12.0f  // Độ nghiêng Ankle Roll để dồn trọng tâm
#define TURN_ANGLE_DEG     15.0f  // Góc xoay chữ V khi rẽ trái/phải

#define HIP_PITCH_MIN_ANGLE   -45.0f
#define HIP_PITCH_MAX_ANGLE    45.0f
#define HIP_ROLL_MIN_ANGLE    -30.0f  // Hip Yaw
#define HIP_ROLL_MAX_ANGLE     30.0f
#define KNEE_PITCH_MIN_ANGLE   -5.0f
#define KNEE_PITCH_MAX_ANGLE  140.0f
#define ANKLE_PITCH_MIN_ANGLE -30.0f
#define ANKLE_PITCH_MAX_ANGLE  30.0f
#define ANKLE_ROLL_MIN_ANGLE  -30.0f
#define ANKLE_ROLL_MAX_ANGLE   30.0f

// ---------- Control Params ----------
#define SERVO_FREQ        50
#define SERVOMIN_PULSE    150
#define SERVOMAX_PULSE    600
#define UPDATE_INTERVAL_MS 20
#define IMU_TAU_S         0.12f

// PD Balance
#define KP_PITCH            0.40f
#define KD_PITCH_RATE       0.06f
#define KP_ROLL             0.50f
#define KD_ROLL_RATE        0.05f

#define PITCH_DIR           1.0f
#define ROLL_DIR            1.0f
#define MAX_HIP_PITCH_CORR  8.0f
#define MAX_HIP_ROLL_CORR   8.0f
#define ANKLE_DIR           -1.0f
#define ANKLE_LEVEL_GAIN     0.50f
#define SEND_EPS_DEG         0.20f
#define STEP_SEND_INTERVAL   1000