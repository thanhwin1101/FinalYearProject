#pragma once
// ====================================================================
//  carry_final  –  ESP32 Master  –  Pin & Constant Configuration
// ====================================================================

// ── UART to STM32 — dùng Serial2 toàn bộ (main.cpp gọi Serial2.begin) ──
#define PIN_STM32_TX        17        // ESP32 TX → STM32 RX
#define PIN_STM32_RX        16        // ESP32 RX ← STM32 TX
#define STM32_BAUD          115200

// ── HuskyLens (Serial1) ────────────────────────────────────────────
#define PIN_HUSKY_TX        4         // ESP32 TX → HuskyLens RX
#define PIN_HUSKY_RX        5         // ESP32 RX ← HuskyLens TX
#define HUSKY_BAUD          9600

// ── Servo Gimbal ────────────────────────────────────────────────────
#define PIN_SERVO_X         13        // PWM
#define PIN_SERVO_X_FB      34        // ADC1_CH6  (analog feedback)
#define PIN_SERVO_Y         14        // PWM

#define SERVO_X_CENTER      100
#define SERVO_Y_LEVEL       100
#define SERVO_Y_TILT_DOWN   45
#define SERVO_Y_LOOK_UP     115
#define SERVO_MIN           0
#define SERVO_MAX           180

// ── SR05 Ultrasonic ─────────────────────────────────────────────────
#define PIN_SR05_L_TRIG     26
#define PIN_SR05_L_ECHO     27
#define PIN_SR05_R_TRIG     32
#define PIN_SR05_R_ECHO     33
#define SR05_WALL_WARN_CM   30        // warning threshold  (cm)
#define SR05_TIMEOUT_US     25000

// ── OLED (SH1106 128×64, I²C) ──────────────────────────────────────
#define PIN_OLED_SDA        21
#define PIN_OLED_SCL        22

// ── Buzzer ──────────────────────────────────────────────────────────
#define PIN_BUZZER          25

// ── Button ──────────────────────────────────────────────────────────
#define PIN_BUTTON          15        // GPIO15, internal pull-up
#define BTN_DEBOUNCE_MS     50
#define BTN_DOUBLE_MS       400       // max gap for double-click
#define BTN_LONG_MS         3000      // long-press → portal

// ── Relays (active HIGH = power ON) ─────────────────────────────────
#define PIN_RELAY_VISION    18        // R1  HuskyLens + servo + SR05
#define PIN_RELAY_LINE_NFC  23        // R2  Line sensors + PN532

// ── Battery ADC (tạm comment) ───────────────────────────────────────
// #define PIN_BATTERY         35        // ADC1_CH7  (voltage divider)
// #define BATT_ADC_FULL_V     3.25f     // ADC voltage when battery full
// #define BATT_ADC_EMPTY_V    1.70f     // ADC voltage when battery empty
// #define BATT_MIN_PERCENT    30        // block commands below this

// ── MQTT (topics defined in mqtt_client.cpp) ───────────────────────────
#define MQTT_DEFAULT_SERVER "192.168.137.1"
#define MQTT_DEFAULT_PORT   1883
#define MQTT_DEFAULT_USER   "hospital_robot"
#define MQTT_DEFAULT_PASS   "123456"
#define MQTT_BUFFER_SIZE    4096
#define MQTT_RECONNECT_MS   3000

// ── WiFiManager ─────────────────────────────────────────────────────
#define WM_AP_NAME          "Robot_Setup"
#define WM_AP_PASS          ""          // open AP
#define WM_PORTAL_TIMEOUT   0           // infinite

// ── NVS ─────────────────────────────────────────────────────────────
#define NVS_NAMESPACE       "robotcfg"

// ── Timing (ms) ─────────────────────────────────────────────────────
#define OLED_UPDATE_MS      200
#define TELEMETRY_MS        5000
// #define BATTERY_READ_MS     5000
#define HUSKY_POLL_MS       50
#define SR05_POLL_MS        100
#define UART_POLL_MS        2

// ── Route ───────────────────────────────────────────────────────────
#define MAX_ROUTE_LEN       30
#define MED_CHECKPOINT_ID   0x8083      // from UID "45:54:80:83" last 2 bytes

// ── UART Protocol ───────────────────────────────────────────────────
#define UART_STX            0x7E
#define UART_MAX_FRAME      128
