#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================
// 1. PINOUT DEFINITIONS (STRICTLY APPLIED)
// =========================================

// --- I2C BUS (OLED, VL53L0X) ---
#define I2C_SDA 21
#define I2C_SCL 22

// --- PN532 (VSPI) ---
#define PN532_SCK  18
#define PN532_MISO 19
#define PN532_MOSI 23
#define PN532_SS   5

// --- MOTORS (L298N) ---
// Left Side (Front Left + Rear Left)
#define EN_LEFT   17  
#define FL_IN1    32
#define FL_IN2    33
#define RL_IN1    25
#define RL_IN2    26

// Right Side (Front Right + Rear Right)
#define EN_RIGHT  16  
#define FR_IN1    27
#define FR_IN2    14
#define RR_IN1    13
#define RR_IN2    4

// --- SRF05 (Ultrasonic) ---
#define TRIG_LEFT  1   
#define ECHO_LEFT  34
#define TRIG_RIGHT 3   
#define ECHO_RIGHT 35

// --- SENSORS & UI ---
#define CARGO_SWITCH_PIN 15
#define BUZZER_PIN       2

// =========================================
// 2. CONFIGURATION
// =========================================
static const char* ROBOT_ID      = "CARRY-01";
static const char* HOME_MED_UID  = "45:54:80:83";

// Tắt Serial Debug để dùng chân TX/RX cho SRF05
#define SERIAL_DEBUG 0 

// WiFiManager
static const int WIFI_PORTAL_TIMEOUT_S  = 180;
static const int WIFI_CONNECT_TIMEOUT_S = 25;
static const unsigned long CFG_RESET_HOLD_MS = 5000;

// Motor Inversion (tuned)
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

// Motion PWM (tuned)
const int PWM_FWD   = 165;
const int PWM_TURN  = 168;
const int PWM_BRAKE = 150;

// Time-based Turn Config (tuned values)
const unsigned long TURN_90_MS  = 974;
const unsigned long TURN_180_MS = 1980;

// Turn Speed Zones
const int PWM_TURN_SLOW = 120;
const int PWM_TURN_FINE = 90;
const float SLOW_ZONE_RATIO = 0.25;
const float FINE_ZONE_RATIO = 0.10;

// Gain (Cân bằng động cơ khi chạy thẳng)
static float leftGain  = 1.00f;
static float rightGain = 1.011f;

// PWM Properties
const int MOTOR_PWM_FREQ = 20000;
const int MOTOR_PWM_RES  = 8;

// Obstacle (VL53L0X)
const int OBSTACLE_MM             = 220;
const int OBSTACLE_RESUME_MM      = 300;
const int OBSTACLE_BEEP_PERIOD_MS = 600;

// Timing
const unsigned long TELEMETRY_MS    = 2000;
const unsigned long POLL_MS         = 1500;
const unsigned long CANCEL_POLL_MS  = 2500;
const unsigned long OLED_MS         = 200;
const unsigned long WEB_OK_SHOW_MS  = 3000;
const unsigned long WEB_OK_ALIVE_MS = 10000;
const unsigned long SWITCH_DEBOUNCE_MS = 60;
const unsigned long NFC_REPEAT_GUARD_MS = 700;

#endif // CONFIG_H
