#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>

// --- I2C BUS (OLED, VL53L0X, HuskyLens) ---
#define I2C_SDA 21
#define I2C_SCL 22

// --- PN532 (SPI) ---
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define NFC_SS    5

// --- UART GIAO TIẾP VỚI NANO ---
#define RX_NANO 16
#define TX_NANO 17

// --- SIÊU ÂM TRÊN ESP32 ---
#define TRIG_LEFT  25
#define ECHO_LEFT  26
#define TRIG_RIGHT 27
#define ECHO_RIGHT 14

// --- SERVO CAMERA PAN/TILT (X/Y) ---
#define SERVO_PAN_PIN   13 // Servo X (Quay trái/phải)
#define SERVO_TILT_PIN  32 // Servo Y (Gật gù lên/xuống)
#define SERVO_FB_PIN    36 // Analog Feedback cho Servo X

// --- SENSORS & UI ---
#define SW_PIN     15
#define BUZZER_PIN 2

#define SERIAL_DEBUG 1

static const char* ROBOT_ID     = "CARRY-01";
static const char* DEVICE_NAME  = "Carry-01";

static const int WIFI_PORTAL_TIMEOUT_S  = 180;
static const int WIFI_CONNECT_TIMEOUT_S = 25;

#define MQTT_DEFAULT_SERVER "192.168.0.102"
#define MQTT_DEFAULT_PORT   1883
#define MQTT_DEFAULT_USER   "hospital_robot"
#define MQTT_DEFAULT_PASS   "123456"

#define TOPIC_TELEMETRY              "hospital/robots/%s/telemetry"
#define TOPIC_MISSION_ASSIGN         "hospital/robots/%s/mission/assign"
#define TOPIC_MISSION_PROGRESS       "hospital/robots/%s/mission/progress"
#define TOPIC_MISSION_COMPLETE       "hospital/robots/%s/mission/complete"
#define TOPIC_MISSION_RETURNED       "hospital/robots/%s/mission/returned"
#define TOPIC_MISSION_CANCEL         "hospital/robots/%s/mission/cancel"
#define TOPIC_MISSION_RETURN_ROUTE   "hospital/robots/%s/mission/return_route"
#define TOPIC_POSITION_WAITING_RETURN "hospital/robots/%s/position/waiting_return"
#define TOPIC_COMMAND                "hospital/robots/%s/command"

const unsigned long MQTT_RECONNECT_MS = 5000;

// THÔNG SỐ VẬN HÀNH
const int PWM_FWD   = 165; 
const int PWM_TURN  = 168; // Tốc độ xoay (Trục Z)
const int PWM_BRAKE = 150;
const int BRAKE_FORWARD_MS = 80;
const unsigned long TURN_90_MS  = 974;
const unsigned long TURN_180_MS = 1980;

const int OBSTACLE_MM             = 220;
const int OBSTACLE_RESUME_MM      = 300;
const int OBSTACLE_BEEP_PERIOD_MS = 600;
const int TOF_STOP_DIST           = OBSTACLE_MM;
const int TOF_RESUME_DIST         = OBSTACLE_RESUME_MM;
const unsigned long TOF_INTERVAL  = 50;

const unsigned long TELEMETRY_MS        = 2000;
const unsigned long TELEMETRY_INTERVAL  = TELEMETRY_MS;
const unsigned long OLED_MS             = 200;
const unsigned long SWITCH_DEBOUNCE_MS  = 60;
const unsigned long NFC_REPEAT_GUARD_MS = 700;
const unsigned long RETURN_ROUTE_TIMEOUT_MS = 5000;

#endif // CONFIG_H