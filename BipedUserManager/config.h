/*
 * ============================================================
 * BIPED USER MANAGER - CONFIGURATION
 * ============================================================
 * Cấu hình pin, thông số và database cho User Manager ESP32
 * ============================================================
 */

#pragma once

// ============================================================
// HARDWARE PIN DEFINITIONS
// ============================================================

// ----- RFID RC522 (SPI) -----
#define RFID_SS_PIN     5     // SDA/SS
#define RFID_RST_PIN    4     // RST
#define RFID_SCK_PIN    18    // SCK
#define RFID_MISO_PIN   19    // MISO
#define RFID_MOSI_PIN   23    // MOSI

// ----- OLED SSD1306 0.96" (I2C) -----
#define OLED_SDA_PIN    21
#define OLED_SCL_PIN    22
#define OLED_ADDRESS    0x3C  // Default I2C address

// ----- Control Buttons -----
#define BTN_FORWARD_PIN   32  // Di tien
#define BTN_BACKWARD_PIN  33  // Di lui  
#define BTN_LEFT_PIN      25  // Re trai
#define BTN_RIGHT_PIN     26  // Re phai
#define BTN_STOP_PIN      27  // Dung khan cap / Ket thuc session (nhan giu)

// ----- Rotary Encoder -----
#define ENC_CLK_PIN     34    // Clock (A)
#define ENC_DT_PIN      35    // Data (B)
#define ENC_SW_PIN      39    // Switch/Button

// ----- UART to Walking Controller -----
#define UART_TX_PIN     17    // TX2 -> RX of Walking ESP32
#define UART_RX_PIN     16    // RX2 <- TX of Walking ESP32
#define UART_BAUD       115200

// ----- Status LED -----
#define LED_STATUS_PIN  2     // Built-in LED (or external)
#define LED_ERROR_PIN   15    // Optional error LED

// ----- Buzzer (Optional) -----
#define BUZZER_PIN      12    // For audio feedback

// ============================================================
// ROBOT IDENTIFICATION
// ============================================================

#define ROBOT_ID        "BIPED-001"
#define ROBOT_NAME      "Biped Robot 1"
#define ROBOT_TYPE      "biped"

// ============================================================
// NETWORK CONFIGURATION
// ============================================================

// Default WiFi credentials (can be changed via config portal)
#define DEFAULT_SSID        "Hospital_WiFi"
#define DEFAULT_PASSWORD    "hospital123"

// API Server
#define DEFAULT_API_URL     "http://192.168.1.100:3000/api"
#define API_TIMEOUT_MS      5000

// ============================================================
// TIMING CONFIGURATION (milliseconds)
// ============================================================

#define DEBOUNCE_MS             50    // Button debounce
#define RFID_SCAN_INTERVAL      500   // RFID polling interval
#define HEARTBEAT_INTERVAL      5000  // API heartbeat
#define STEP_UPDATE_INTERVAL    2000  // Step count update to API
#define DISPLAY_UPDATE_INTERVAL 200   // OLED refresh rate
#define WIFI_RECONNECT_INTERVAL 10000 // WiFi reconnect attempt
#define SESSION_END_HOLD_TIME   2000  // Hold time to end session

// ============================================================
// SPEED CONFIGURATION
// ============================================================

#define SPEED_MIN       10    // Minimum speed %
#define SPEED_MAX       100   // Maximum speed %
#define SPEED_DEFAULT   50    // Default speed %
#define SPEED_STEP      5     // Encoder step increment

// ============================================================
// UART PROTOCOL DEFINITIONS
// ============================================================

// Commands TO Walking Controller
#define CMD_FORWARD     "CMD:FWD"
#define CMD_BACKWARD    "CMD:BACK"
#define CMD_LEFT        "CMD:LEFT"
#define CMD_RIGHT       "CMD:RIGHT"
#define CMD_STOP        "STOP"
#define CMD_SPEED       "SPEED:"      // + value (0-100)
#define CMD_BALANCE_ON  "BALANCE:ON"
#define CMD_BALANCE_OFF "BALANCE:OFF"
#define CMD_CALIBRATE   "CALIBRATE"

// Messages FROM Walking Controller
#define MSG_STEP        "STEP:"       // + step count
#define MSG_BALANCE     "BALANCE:"    // + OK/WARN/ERROR
#define MSG_ERROR       "ERROR:"      // + error message
#define MSG_ACK         "ACK:"        // + acknowledged command
#define MSG_STATUS      "STATUS:"     // + status info

// ============================================================
// DISPLAY STRINGS (Vietnamese)
// ============================================================

#define STR_TITLE           "BIPED ROBOT"
#define STR_READY           "San sang"
#define STR_CONNECTING      "Dang ket noi..."
#define STR_WIFI_OK         "WiFi: OK"
#define STR_WIFI_FAIL       "WiFi: Mat ket noi"
#define STR_SCAN_CARD       "Quet the de bat dau"
#define STR_SESSION_ACTIVE  "Phien tap luyen"
#define STR_STEPS           "buoc"
#define STR_SPEED           "Toc do"
#define STR_MOVING          "DANG DI"
#define STR_DIRECTION       "Huong"
#define STR_FORWARD         "TIEN"
#define STR_BACKWARD        "LUI"
#define STR_LEFT            "TRAI"
#define STR_RIGHT           "PHAI"
#define STR_ERROR           "LOI!"
#define STR_USER            "User"
#define STR_DURATION        "Thoi gian"

// ============================================================
// ERROR CODES
// ============================================================

enum ErrorCode {
  ERR_NONE = 0,
  ERR_RFID_INIT,
  ERR_OLED_INIT,
  ERR_WIFI_CONNECT,
  ERR_API_CONNECT,
  ERR_UART_TIMEOUT,
  ERR_BALANCE_FAIL,
  ERR_UNKNOWN_CARD,
  ERR_SESSION_START,
  ERR_SESSION_END
};

// ============================================================
// SYSTEM STATES
// ============================================================

enum SystemState {
  STATE_INIT,           // Initializing
  STATE_IDLE,           // Waiting for user
  STATE_SESSION_ACTIVE, // Rehabilitation session active
  STATE_MOVING,         // Robot is moving
  STATE_PAUSED,         // Session paused
  STATE_ERROR,          // Error state
  STATE_CHARGING        // Robot charging (future)
};

// ============================================================
// DISPLAY SCREENS
// ============================================================

enum DisplayScreen {
  SCREEN_BOOT,          // Boot/splash screen
  SCREEN_CONNECTING,    // WiFi connecting
  SCREEN_IDLE,          // Idle/ready screen
  SCREEN_SESSION,       // Session info display
  SCREEN_MOVING,        // Moving indicator
  SCREEN_SPEED_ADJUST,  // Speed adjustment overlay
  SCREEN_ERROR,         // Error display
  SCREEN_MENU           // Settings menu (future)
};

// ============================================================
// SESSION STATUS
// ============================================================

enum SessionStatus {
  SESSION_NONE,
  SESSION_ACTIVE,
  SESSION_COMPLETED,
  SESSION_INTERRUPTED,
  SESSION_PAUSED
};

// ============================================================
// BUTTON INDICES
// ============================================================

enum ButtonIndex {
  BTN_IDX_FORWARD = 0,
  BTN_IDX_BACKWARD = 1,
  BTN_IDX_LEFT = 2,
  BTN_IDX_RIGHT = 3,
  BTN_IDX_STOP = 4,
  BTN_COUNT = 5
};
