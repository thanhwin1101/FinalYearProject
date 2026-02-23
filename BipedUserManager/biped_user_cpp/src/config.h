/*
 * ============================================================
 * BIPED USER MANAGER — CONFIG
 * ============================================================
 * Cấu hình pin, MQTT, timing cho ESP32 User Manager
 * Giao tiếp với Walking Controller qua UART
 * Giao tiếp với Dashboard qua MQTT
 * ============================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================
// 1. PIN DEFINITIONS
// =========================================

// ----- RFID RC522 (SPI) -----
#define RFID_SS_PIN     5
#define RFID_RST_PIN    4
#define RFID_SCK_PIN    18
#define RFID_MISO_PIN   19
#define RFID_MOSI_PIN   23

// ----- OLED SSD1306 0.96" (I2C) -----
#define OLED_SDA_PIN    21
#define OLED_SCL_PIN    22
#define OLED_ADDRESS    0x3C

// ----- Control Buttons (INPUT_PULLUP, active LOW) -----
#define BTN_FORWARD_PIN   32
#define BTN_BACKWARD_PIN  33
#define BTN_LEFT_PIN      25
#define BTN_RIGHT_PIN     26
#define BTN_STOP_PIN      27    // Dừng / Giữ 2s = kết thúc session

// ----- Rotary Encoder -----
#define ENC_CLK_PIN     34      // Cần external pull-up
#define ENC_DT_PIN      35      // Cần external pull-up
#define ENC_SW_PIN      39      // Encoder button

// ----- UART to Walking Controller -----
#define UART_TX_PIN     17
#define UART_RX_PIN     16
#define UART_BAUD       115200

// ----- Status LED -----
#define LED_STATUS_PIN  2       // Built-in LED

// ----- Buzzer (Optional) -----
#define BUZZER_PIN      12
#define BUZZER_CHANNEL  2

// =========================================
// 2. ROBOT IDENTIFICATION
// =========================================
static const char* ROBOT_ID   = "BIPED-001";
static const char* ROBOT_NAME = "Biped Robot 1";
static const char* ROBOT_TYPE = "biped";

// =========================================
// 3. WIFI CONFIGURATION
// =========================================
#define WIFI_PORTAL_SSID      "BipedRobot-Setup"
#define WIFI_PORTAL_PASS      "biped123"
#define WIFI_PORTAL_TIMEOUT_S 300
#define WIFI_CONNECT_TIMEOUT_S 25

// =========================================
// 4. MQTT CONFIGURATION
// =========================================
#define MQTT_DEFAULT_SERVER   "192.168.0.102"
#define MQTT_DEFAULT_PORT     1883
#define MQTT_DEFAULT_USER     "hospital_robot"
#define MQTT_DEFAULT_PASS     "123456"

// MQTT Topics (format strings, %s = ROBOT_ID)
// --- Publish ---
#define TOPIC_TELEMETRY         "hospital/robots/%s/telemetry"
#define TOPIC_SESSION_START     "hospital/robots/%s/session/start"
#define TOPIC_SESSION_UPDATE    "hospital/robots/%s/session/update"
#define TOPIC_SESSION_END       "hospital/robots/%s/session/end"

// --- Subscribe ---
#define TOPIC_COMMAND           "hospital/robots/%s/command"
#define TOPIC_SESSION_ACK       "hospital/robots/%s/session/ack"

// MQTT reconnect timing
static const unsigned long MQTT_RECONNECT_MS = 5000;

// =========================================
// 5. TIMING CONFIGURATION (ms)
// =========================================
#define DEBOUNCE_MS              50
#define RFID_SCAN_INTERVAL       500
#define TELEMETRY_INTERVAL       5000   // Heartbeat / telemetry mỗi 5s
#define STEP_UPDATE_INTERVAL     2000   // Gửi step count mỗi 2s
#define DISPLAY_UPDATE_INTERVAL  200    // OLED refresh 5Hz
#define WIFI_RECONNECT_INTERVAL  10000
#define SESSION_END_HOLD_TIME    2000   // Giữ nút Stop 2s = kết thúc session
#define WIFI_SETUP_HOLD_TIME     3000   // Giữ Forward 3s = WiFi setup

// =========================================
// 6. SPEED CONFIGURATION
// =========================================
#define SPEED_MIN       10
#define SPEED_MAX       100
#define SPEED_DEFAULT   50
#define SPEED_STEP      5

// =========================================
// 7. UART PROTOCOL — TO Walking Controller
// =========================================
#define CMD_FWD_STR        "CMD:FWD"
#define CMD_BACK_STR       "CMD:BACK"
#define CMD_LEFT_STR       "CMD:LEFT"
#define CMD_RIGHT_STR      "CMD:RIGHT"
#define CMD_STOP_STR       "STOP"
#define CMD_SPEED_PREFIX   "SPEED:"
#define CMD_BALANCE_ON     "BALANCE:ON"
#define CMD_BALANCE_OFF    "BALANCE:OFF"
#define CMD_CALIBRATE_STR  "CALIBRATE"

// =========================================
// 8. UART PROTOCOL — FROM Walking Controller
// =========================================
#define MSG_STEP_PREFIX      "STEP:"
#define MSG_BALANCE_PREFIX   "BALANCE:"
#define MSG_ERROR_PREFIX     "ERROR:"
#define MSG_ACK_PREFIX       "ACK:"
#define MSG_STATUS_PREFIX    "STATUS:"

// =========================================
// 9. DISPLAY STRINGS (Vietnamese)
// =========================================
#define STR_TITLE           "BIPED ROBOT"
#define STR_READY           "San sang su dung"
#define STR_SCAN_CARD       "-> Quet the bat dau"
#define STR_SESSION_ACTIVE  "Phien tap luyen"
#define STR_STEPS_LABEL     "buoc"
#define STR_SPEED_LABEL     "Toc do"
#define STR_END_SESSION     "KET THUC!"
#define STR_GOODBYE         "Hen gap lai!"
#define STR_INVALID_CARD    "KHONG HOP LE!"
#define STR_CARD_NOT_REG    "The chua dang ky."
#define STR_CONTACT_STAFF   "Lien he nhan vien."
#define STR_CONNECTING      "Dang ket noi..."
#define STR_CHECKING        "Dang kiem tra..."
#define STR_CONN_ERROR      "Loi ket noi!"
#define STR_RETRY_LATER     "Thu lai sau."

// =========================================
// 10. CHECKPOINT DATABASE (RFID → Location)
// =========================================
struct CheckpointEntry {
  uint8_t uid[4];
  const char* checkpointId;
  const char* description;
};

// Placeholder UIDs — cần cập nhật theo thực tế
static const CheckpointEntry CHECKPOINT_DB[] = {
  {{0xC1, 0xC2, 0xC3, 0xC4}, "CP_LOBBY", "Sanh chinh"},
  {{0xD1, 0xD2, 0xD3, 0xD4}, "CP_R1",    "Phong 1"},
  {{0xE1, 0xE2, 0xE3, 0xE4}, "CP_R2",    "Phong 2"},
  {{0xF1, 0xF2, 0xF3, 0xF4}, "CP_R3",    "Phong 3"},
  {{0xA1, 0xA2, 0xA3, 0xA4}, "CP_R4",    "Phong 4"},
  {{0xB1, 0xB2, 0xB3, 0xB4}, "CP_HALL",  "Hanh lang"},
};
static const int CHECKPOINT_COUNT = sizeof(CHECKPOINT_DB) / sizeof(CHECKPOINT_DB[0]);

#endif // CONFIG_H
