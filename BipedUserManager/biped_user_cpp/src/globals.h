/*
 * ============================================================
 * BIPED USER MANAGER — GLOBALS (Header)
 * ============================================================
 * State variables, structs, global objects
 * ============================================================
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <MFRC522.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

// =========================================
// ENUMS
// =========================================
enum SystemState {
  STATE_BOOT,
  STATE_PORTAL,         // WiFiManager portal đang mở
  STATE_CONNECTING,     // Đang kết nối WiFi/MQTT
  STATE_IDLE,           // Chờ quét thẻ
  STATE_SESSION_ACTIVE, // Phiên tập luyện đang chạy
  STATE_ERROR           // Lỗi hệ thống
};

// =========================================
// STRUCTS
// =========================================
struct UserInfo {
  char cardUid[20];       // RFID UID dạng hex string
  char patientId[32];     // Patient ID từ database
  char userName[48];      // Tên bệnh nhân
  char roomBed[16];       // Phòng/Giường
  bool isValid;
};

struct SessionData {
  char sessionId[40];     // Session ID (format: BIPED-xxxx)
  char cardUid[20];       // UID thẻ người dùng
  char userName[48];      // Tên người dùng
  char patientId[32];     // Patient ID
  uint32_t stepCount;     // Tổng số bước
  unsigned long startTime;// millis() khi bắt đầu
  bool isActive;          // Session đang chạy
};

struct ButtonState {
  int pin;
  bool lastState;
  unsigned long lastDebounce;
  bool pressed;
};

// =========================================
// GLOBAL OBJECTS
// =========================================
extern MFRC522 rfid;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
extern HardwareSerial WalkingSerial;
extern Preferences prefs;
extern WiFiClient espClient;
extern PubSubClient mqttClient;

// =========================================
// MQTT CONFIGURATION (runtime)
// =========================================
extern char mqttServer[64];
extern int  mqttPort;
extern char mqttUser[32];
extern char mqttPass[32];
extern bool mqttConnected;
extern unsigned long lastMqttReconnect;

// MQTT topic buffers (built at runtime with ROBOT_ID)
extern char topicTelemetry[80];
extern char topicSessionStart[80];
extern char topicSessionUpdate[80];
extern char topicSessionEnd[80];
extern char topicCommand[80];
extern char topicSessionAck[80];

// =========================================
// STATE VARIABLES
// =========================================
extern SystemState currentState;

// User & session
extern UserInfo currentUser;
extern SessionData session;

// Movement
extern uint8_t currentSpeed;
extern bool isMoving;
extern char lastCommand[16];

// Location checkpoint
extern char currentCheckpoint[16];

// Balance status from Walking Controller
extern char balanceStatus[16];

// Timing
extern unsigned long lastRfidScan;
extern unsigned long lastTelemetry;
extern unsigned long lastStepUpdate;
extern unsigned long lastDisplayUpdate;

// Buttons (5 total: FWD, BACK, LEFT, RIGHT, STOP)
extern ButtonState buttons[5];
#define BTN_COUNT 5
#define BTN_IDX_FWD   0
#define BTN_IDX_BACK  1
#define BTN_IDX_LEFT  2
#define BTN_IDX_RIGHT 3
#define BTN_IDX_STOP  4

// Encoder
extern volatile int encoderPos;
extern int lastEncoderPos;

// Long-press WiFi setup
extern unsigned long forwardBtnPressStart;
extern bool forwardLongPressTriggered;

// WiFi connected flag
extern bool wifiOk;

// NVS saved config
extern bool shouldSaveConfig;

// =========================================
// UTILITY
// =========================================
String uidToHexString(byte* uid, byte size);
const CheckpointEntry* findCheckpoint(byte* uid);
bool compareUID(const uint8_t* a, const uint8_t* b, uint8_t len = 4);
void buzzerBeep(int ms = 80, int freq = 2200);
void buzzerInit();
void buzzerOff();

#endif // GLOBALS_H
