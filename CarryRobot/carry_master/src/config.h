/*  config.h  –  Master ESP32 pin map, constants, PID gains
 *  Carry Robot – Dual-ESP32 architecture
 */
#pragma once

// ======================= Robot Identity =======================
#define ROBOT_ID              "CARRY-01"
#define ROBOT_NAME            "Carry-01"
#define FW_VERSION            "carry-dual-v1"

// ======================= Pin Map – Master ESP32 ===============

// HuskyLens  (UART2)
#define HUSKY_RX_PIN          16        // ESP32 RX2  ← HuskyLens TX
#define HUSKY_TX_PIN          17        // ESP32 TX2  → HuskyLens RX
#define HUSKY_BAUD            9600

// I2C  (shared: OLED SSD1306 + VL53L0X ToF)
#define I2C_SDA               21
#define I2C_SCL               22
#define OLED_I2C_ADDR         0x3C

// Ultrasonic – Left
#define US_LEFT_TRIG          25
#define US_LEFT_ECHO          26

// Ultrasonic – Right
#define US_RIGHT_TRIG         32
#define US_RIGHT_ECHO         33

// Servo Gimbal
#define SERVO_X_PIN           13        // Pan  (horizontal)
#define SERVO_Y_PIN           12        // Tilt (vertical)
#define SERVO_X_FB_PIN        34        // Analog feedback from servo X potentiometer

// Buzzer
#define BUZZER_PIN            14

// Mode switch button  (đổi chân tại đây: 4, 0, 2, 15, 32, 33...)
#define SW_PIN                4

// ======================= Servo Angle Presets ==================
#define SERVO_X_CENTER        90        // Forward-facing
#define SERVO_Y_LEVEL         90        // Level (follow mode)
#define SERVO_Y_TILT_DOWN     45        // Recovery: look at floor
#define SERVO_Y_LOOK_UP      115        // Follow: tilt up to search for person
#define SERVO_X_MIN           0
#define SERVO_X_MAX           180
#define SERVO_Y_MIN           0
#define SERVO_Y_MAX           180

// Servo X feedback ADC calibration  (tune on real hardware)
#define SERVO_FB_ADC_MIN      300       // ADC value at 0°
#define SERVO_FB_ADC_MAX      3700      // ADC value at 180°

// ======================= HuskyLens ============================
#define HUSKY_IMG_W           320
#define HUSKY_IMG_H           240
#define HUSKY_CENTER_X        (HUSKY_IMG_W / 2)   // 160
#define HUSKY_CENTER_Y        (HUSKY_IMG_H / 2)   // 120
#define HUSKY_INIT_TIMEOUT_MS 7000
#define HUSKY_RECONNECT_INTERVAL_MS 700
#define HUSKY_FACE_AUTH_ID 1                 // Face ID trained on HuskyLens that is allowed to start follow
#define HUSKY_FACE_AUTH_STREAK 3             // require N consecutive detections to avoid false unlock

// ======================= Obstacle Detection ===================
// VL53L0X  (front)
#define TOF_STOP_DIST         220       // mm – hard stop
#define TOF_RESUME_DIST       300       // mm – resume driving
#define TOF_MEAS_BUDGET_US    20000     // 20 ms

// Ultrasonic  (sides)
#define US_SIDE_WARN_MM       150       // mm – side proximity warning
#define US_TIMEOUT_US         25000     // ~4.3 m max range

// ======================= Follow Mode PID ======================
// Inner loop: pixel error → Servo X angle
#define KP_CAM                0.15f
#define KI_CAM                0.001f
#define KD_CAM                0.05f
#define CAM_I_MAX             30.0f     // anti-windup integrator limit

// Outer loop: servo-X angle error → Mecanum strafe vY  (+ slight vR)
#define KP_ANGLE              2.0f
#define KD_ANGLE              0.3f

// Distance loop: ToF mm error → vX
#define KP_DIST               0.5f
#define KD_DIST               0.15f
#define FOLLOW_TARGET_MM      500       // desired follow distance

// ======================= Line-following base speed ============
#define LINE_BASE_SPEED       165       // Tuned forward PWM

// ======================= Timing (ms) ==========================
#define TELEMETRY_INTERVAL    1000
#define OLED_INTERVAL         200
#define TOF_INTERVAL          50
#define US_INTERVAL           100
#define HUSKY_INTERVAL        50
#define ESPNOW_TX_INTERVAL    50        // 20 Hz
#define ESPNOW_RELOCK_BEACON_INTERVAL_MS 10   // fast beacon while re-locking slave channel
#define ESPNOW_RELOCK_BEACON_MS 5000          // keep fast beacon for 5s after WiFi reconnect
#define ESPNOW_BOOT_WAIT_MS   5000      // wait up to 5s for first slave packet before WiFi setup
#define WIFI_LINK_CHECK_INTERVAL_MS 300       // WiFi link polling cadence
#define MQTT_RECONNECT_MS     2000
#define SYSTEM_READY_STABLE_MS 2000      // all links must remain OK for this long before entering IDLE
#define SYSTEM_READY_MAX_WAIT_MS 30000   // after this, allow partial startup if Slave link exists
#define OBSTACLE_BEEP_MS      600

// ======================= Button ===============================
#define DEBOUNCE_MS           50
#define LONG_PRESS_MS         3000
#define DOUBLE_CLICK_MS       400   // max ms between two clicks to count as double-click
#define MONITOR_SERIAL        1     // 1 = log [MON] button/state/OLED to Serial (COM port) for debugging
// 1 = pressed when pin reads LOW (button to GND). 0 = pressed when pin reads HIGH (button to 3.3V).
#define SW_ACTIVE_LOW         1

// ======================= MQTT =================================
#define MQTT_DEFAULT_SERVER   "192.168.137.1"
#define MQTT_DEFAULT_PORT     1883
#define MQTT_DEFAULT_USER     "hospital_robot"
#define MQTT_DEFAULT_PASS     "123456"
#define MQTT_BUFFER_SIZE      4096
#define MQTT_PAYLOAD_VERSION  2
// Hard-clean phase: legacy keys are fully disabled.
#define MQTT_SEND_LEGACY_FIELDS 0

// ======================= WiFi / WiFiManager ===================
#define WIFI_DEFAULT_SSID    "Test"
#define WIFI_DEFAULT_PASS    "123456789"
#define WIFI_PORTAL_SSID      "CarryMaster-Setup"
#define WIFI_PORTAL_PASS      "carry123"
#define WIFI_PORTAL_TIMEOUT   180       // seconds

// ======================= ESP-NOW ==============================
// Slave MAC – from latest slave upload log (COM13)
static const uint8_t SLAVE_MAC[6] = {0xB0,0xCB,0xD8,0xC9,0x9F,0x14};
#define ESPNOW_CHANNEL 7             // fixed ESP-NOW channel (must match slave)
#define ESPNOW_SLAVE_TIMEOUT_MS 1500      // no slave packet in this window => link lost

// ======================= Turn timing (Slave execution) ========
#define TURN_90_MS            974
#define TURN_180_MS           1980

// ======================= NFC UID map re-use ===================
// Home station UID
#define HOME_MED_UID          "45:54:80:83"
