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

// ── Servo Gimbal (Y axis only) ───────────────────────────────────
#define PIN_SERVO_Y         14        // PWM

#define SERVO_Y_LEVEL       100
#define SERVO_Y_TILT_DOWN   45
#define SERVO_Y_LOOK_UP     115
#define SERVO_MIN           0
#define SERVO_MAX           180


// ── OLED (SH1106 128×64, I²C) ──────────────────────────────────────
#define PIN_OLED_SDA        21
#define PIN_OLED_SCL        22

// ── Buzzer ──────────────────────────────────────────────────────────
#define PIN_BUZZER          25

// ── Button ──────────────────────────────────────────────────────────
#define PIN_BUTTON          15        // GPIO15, internal pull-up
#define BTN_DEBOUNCE_MS     50
#define BTN_DOUBLE_MS       400       // max gap for double-click
#define BTN_LONG_MS         1500      // long-press → mode switch

// ── Relays (active HIGH = power ON) ─────────────────────────────────
#define PIN_RELAY_VISION    18        // R1  HuskyLens + servo
#define PIN_RELAY_LINE_NFC  23        // R2  Line sensors + PN532

// ── Battery ADC ─────────────────────────────────────────────────────
#define PIN_BATTERY         35        // ADC1_CH7  (voltage divider)
#define BATT_ADC_MIN_V      3.036f    // ADC voltage → BATT_PCT_AT_MIN% (đo thực tế khi pin thấp nhất)
#define BATT_ADC_MAX_V      3.6f      // ADC voltage → 100% (đo thực tế khi pin đầy)
#define BATT_PCT_AT_MIN     10        // percent displayed at BATT_ADC_MIN_V
#define BATT_MIN_PERCENT    30        // ≤30%: block auto mission, warn in follow
#define BATT_FOLLOW_WARN_S  10        // seconds to warn in follow before recovery

// ── MQTT (topics defined in mqtt_client.cpp) ───────────────────────────
#define MQTT_DEFAULT_SERVER "192.168.137.1"
#define MQTT_DEFAULT_PORT   1883
#define MQTT_DEFAULT_USER   "hospital_robot"
#define MQTT_DEFAULT_PASS   "123456"
#define MQTT_BUFFER_SIZE    4096
#define MQTT_RECONNECT_MS   3000

// ── WiFiManager ─────────────────────────────────────────────────────
#define WM_AP_NAME          "AGV_hospital"
#define WM_AP_PASS          ""          // open AP
#define WM_PORTAL_TIMEOUT   0           // infinite

// ── NVS ─────────────────────────────────────────────────────────────
#define NVS_NAMESPACE       "robotcfg"

// ── Timing (ms) ─────────────────────────────────────────────────────
#define OLED_UPDATE_MS      200
#define TELEMETRY_MS        5000
// #define BATTERY_READ_MS     5000
#define HUSKY_POLL_MS       50

// ── HuskyLens screen geometry ─────────────────────────────────
#define HUSKY_SCREEN_W      320
#define HUSKY_SCREEN_H      240
#define HUSKY_CX            160       // center X
#define HUSKY_TARGET_AREA_PCT 20      // target tag area % of screen
#define UART_POLL_MS        2

// ── Route ───────────────────────────────────────────────────────────
#define MAX_ROUTE_LEN       30
#define MED_CHECKPOINT_ID   0x8083      // from UID "45:54:80:83" last 2 bytes

// ── OTA (ESP32 ArduinoOTA via Wi-Fi) ─────────────────────────────────
#define OTA_HOSTNAME        "agv-esp32"
#define OTA_PASSWORD        "agv_ota_123"

// ── STM32 UART Bootloader (AN3155) ───────────────────────────────────
//  Requires 4 physical wires added to PCB:
//    GPIO26 → STM32 BOOT0        (set HIGH before reset → enters ROM bootloader)
//    GPIO27 → STM32 NRST         (open-drain: LOW resets STM32, pull-up to 3.3V)
//    GPIO32 → STM32 PA9  (USART1 RX) — bootloader UART RX
//    GPIO33 ← STM32 PA10 (USART1 TX) — bootloader UART TX
#define PIN_STM32_BOOT0     26
#define PIN_STM32_NRST      27
#define PIN_STM32_BL_TX     32        // ESP32 TX → STM32 PA9 (USART1 RX)
#define PIN_STM32_BL_RX     33        // ESP32 RX ← STM32 PA10 (USART1 TX)
#define STM32_BL_BAUD       115200
#define STM32_FLASH_BASE    0x08000000UL
#define STM32_MAX_BIN_SIZE  65536     // STM32F103C8 = 64 KB flash

// ── UART Protocol ───────────────────────────────────────────────────
#define UART_STX            0x7E
#define UART_MAX_FRAME      128
