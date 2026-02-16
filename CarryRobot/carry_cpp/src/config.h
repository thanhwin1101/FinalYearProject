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
#define CARGO_BTN        15    // Alias
#define BUZZER_PIN       2

// --- SPI for PN532 (aliases) ---
#define SPI_SCK   PN532_SCK
#define SPI_MISO  PN532_MISO
#define SPI_MOSI  PN532_MOSI
#define NFC_SS    PN532_SS

// =========================================
// 2. CONFIGURATION
// =========================================
static const char* ROBOT_ID = "CARRY-01";

// Tắt Serial Debug để dùng chân TX/RX cho SRF05
#define SERIAL_DEBUG 0 

// WiFiManager
static const int WIFI_PORTAL_TIMEOUT_S  = 180;
static const int WIFI_CONNECT_TIMEOUT_S = 25;
static const unsigned long CFG_RESET_HOLD_MS = 5000;

// =========================================
// MQTT CONFIGURATION
// =========================================
#define MQTT_DEFAULT_SERVER "192.168.0.102"
#define MQTT_DEFAULT_PORT   1883
#define MQTT_DEFAULT_USER   "hospital_robot"
#define MQTT_DEFAULT_PASS   "123456"

// MQTT Topics (format strings)
#define TOPIC_TELEMETRY              "hospital/robots/%s/telemetry"
#define TOPIC_MISSION_ASSIGN         "hospital/robots/%s/mission/assign"
#define TOPIC_MISSION_PROGRESS       "hospital/robots/%s/mission/progress"
#define TOPIC_MISSION_COMPLETE       "hospital/robots/%s/mission/complete"
#define TOPIC_MISSION_RETURNED       "hospital/robots/%s/mission/returned"
#define TOPIC_MISSION_CANCEL         "hospital/robots/%s/mission/cancel"
#define TOPIC_MISSION_RETURN_ROUTE   "hospital/robots/%s/mission/return_route"
#define TOPIC_POSITION_WAITING_RETURN "hospital/robots/%s/position/waiting_return"
#define TOPIC_COMMAND                "hospital/robots/%s/command"

// MQTT reconnect timing
const unsigned long MQTT_RECONNECT_MS = 5000;

// Motor Inversion (tuned)
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

// Motion PWM (tuned)
const int PWM_FWD   = 165;
const int PWM_TURN  = 168;
const int PWM_BRAKE = 150;

// Braking
const int BRAKE_FORWARD_MS = 80;

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
const int TOF_STOP_DIST           = OBSTACLE_MM;
const int TOF_RESUME_DIST         = OBSTACLE_RESUME_MM;
const unsigned long TOF_INTERVAL  = 50;

// Timing
const unsigned long TELEMETRY_MS        = 2000;
const unsigned long TELEMETRY_INTERVAL  = TELEMETRY_MS;
const unsigned long POLL_MS             = 1500;
const unsigned long CANCEL_POLL_MS      = 2500;
const unsigned long OLED_MS             = 200;
const unsigned long WEB_OK_SHOW_MS      = 3000;
const unsigned long WEB_OK_ALIVE_MS     = 10000;
const unsigned long SWITCH_DEBOUNCE_MS  = 60;
const unsigned long NFC_REPEAT_GUARD_MS = 700;

// Return route waiting timeout (5 seconds)
const unsigned long RETURN_ROUTE_TIMEOUT_MS = 5000;

#endif // CONFIG_H
