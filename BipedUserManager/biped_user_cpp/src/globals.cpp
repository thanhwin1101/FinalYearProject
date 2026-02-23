/*
 * ============================================================
 * BIPED USER MANAGER — GLOBALS (Implementation)
 * ============================================================
 */

#include "globals.h"
#include "config.h"

// =========================================
// GLOBAL OBJECTS
// =========================================
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
HardwareSerial WalkingSerial(2);   // UART2
Preferences prefs;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =========================================
// MQTT CONFIGURATION
// =========================================
char mqttServer[64]  = MQTT_DEFAULT_SERVER;
int  mqttPort        = MQTT_DEFAULT_PORT;
char mqttUser[32]    = MQTT_DEFAULT_USER;
char mqttPass[32]    = MQTT_DEFAULT_PASS;
bool mqttConnected   = false;
unsigned long lastMqttReconnect = 0;

char topicTelemetry[80];
char topicSessionStart[80];
char topicSessionUpdate[80];
char topicSessionEnd[80];
char topicCommand[80];
char topicSessionAck[80];

// =========================================
// STATE VARIABLES
// =========================================
SystemState currentState = STATE_BOOT;

UserInfo currentUser;
SessionData session;

uint8_t currentSpeed = SPEED_DEFAULT;
bool isMoving = false;
char lastCommand[16] = "";

char currentCheckpoint[16] = "UNKNOWN";
char balanceStatus[16] = "OK";

unsigned long lastRfidScan     = 0;
unsigned long lastTelemetry    = 0;
unsigned long lastStepUpdate   = 0;
unsigned long lastDisplayUpdate = 0;

ButtonState buttons[5] = {
  {BTN_FORWARD_PIN,  HIGH, 0, false},
  {BTN_BACKWARD_PIN, HIGH, 0, false},
  {BTN_LEFT_PIN,     HIGH, 0, false},
  {BTN_RIGHT_PIN,    HIGH, 0, false},
  {BTN_STOP_PIN,     HIGH, 0, false},
};

volatile int encoderPos    = SPEED_DEFAULT / SPEED_STEP;
int lastEncoderPos         = -1;

unsigned long forwardBtnPressStart = 0;
bool forwardLongPressTriggered     = false;

bool wifiOk = false;
bool shouldSaveConfig = false;

// =========================================
// UTILITY FUNCTIONS
// =========================================

String uidToHexString(byte* uid, byte size) {
  String result = "";
  for (byte i = 0; i < size; i++) {
    if (uid[i] < 0x10) result += "0";
    result += String(uid[i], HEX);
  }
  result.toUpperCase();
  return result;
}

const CheckpointEntry* findCheckpoint(byte* uid) {
  for (int i = 0; i < CHECKPOINT_COUNT; i++) {
    if (compareUID(uid, CHECKPOINT_DB[i].uid)) {
      return &CHECKPOINT_DB[i];
    }
  }
  return nullptr;
}

bool compareUID(const uint8_t* a, const uint8_t* b, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

void buzzerInit() {
#if defined(BUZZER_PIN) && BUZZER_PIN >= 0
  // ESP32 LEDC for buzzer
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(BUZZER_PIN, 2200, 8);
  #else
    ledcSetup(BUZZER_CHANNEL, 2200, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  #endif
  buzzerOff();
#endif
}

void buzzerBeep(int ms, int freq) {
#if defined(BUZZER_PIN) && BUZZER_PIN >= 0
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(BUZZER_PIN, freq);
  #else
    ledcWriteTone(BUZZER_CHANNEL, freq);
  #endif
  delay(ms);
  buzzerOff();
#endif
}

void buzzerOff() {
#if defined(BUZZER_PIN) && BUZZER_PIN >= 0
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(BUZZER_PIN, 0);
  #else
    ledcWriteTone(BUZZER_CHANNEL, 0);
  #endif
#endif
}
