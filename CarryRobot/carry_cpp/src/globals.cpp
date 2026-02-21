#include "globals.h"
#include "config.h"

// =========================================
// GLOBAL OBJECTS
// =========================================
Adafruit_PN532 nfc(PN532_SS);
VL53L0X tof;
bool tofOk = false;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
Preferences prefs;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =========================================
// MQTT CONFIGURATION
// =========================================
char mqttServer[64] = MQTT_DEFAULT_SERVER;
int mqttPort = MQTT_DEFAULT_PORT;
char mqttUser[32] = MQTT_DEFAULT_USER;
char mqttPass[32] = MQTT_DEFAULT_PASS;
bool mqttConnected = false;
unsigned long lastMqttReconnect = 0;

char topicTelemetry[64];
char topicMissionAssign[64];
char topicMissionProgress[64];
char topicMissionComplete[64];
char topicMissionReturned[64];
char topicMissionCancel[64];
char topicMissionReturnRoute[64];
char topicPositionWaitingReturn[64];
char topicCommand[64];

// =========================================
// STATE VARIABLES
// =========================================
bool shouldSaveConfig = false;

unsigned long lastTelemetry = 0;
unsigned long lastPoll = 0;
unsigned long lastCancelPoll = 0;
unsigned long lastObstacleBeep = 0;
unsigned long lastOLED = 0;

RunState state = ST_BOOT;
bool obstacleHold = false;

String activeMissionId = "";
String activeMissionStatus = "";
String patientName = "";
String bedId = "";

std::vector<RoutePoint> outbound;
std::vector<RoutePoint> retRoute;
int routeIndex = 0;

bool swRaw = true;
bool swStable = true;
unsigned long swLastChange = 0;
// Khởi tạo trạng thái ban đầu là rỗng (chưa quét thẻ)
String currentCheckpoint = ""; 
String lastNfcUid = "";
unsigned long lastNfcAt = 0;
bool cancelPending = false;
bool destUturnedBeforeWait = false;

unsigned long nfcIgnoreUntil = 0;
char lastTurnChar = 'F';
unsigned long turnOverlayUntil = 0;

String cancelAtNodeId = "";
bool waitingForReturnRoute = false;
unsigned long waitingReturnRouteStartTime = 0;

// =========================================
// UTILITY FUNCTIONS (merged from helpers)
// =========================================
String truncStr(const String& s, size_t maxLen) {
  if (s.length() <= (int)maxLen) return s;
  return s.substring(0, maxLen);
}

void updateSW() {
  bool r = digitalRead(SW_PIN);
  if (r != swRaw) { swRaw = r; swLastChange = millis(); }
  if ((millis() - swLastChange) >= SWITCH_DEBOUNCE_MS) swStable = swRaw;
}

bool swHeld() { return swStable == LOW; }

void ignoreNfcFor(unsigned long ms) { nfcIgnoreUntil = millis() + ms; }
bool nfcAllowed() { return millis() >= nfcIgnoreUntil; }
bool isNfcReady() { return nfcAllowed(); }
void markNfcRead() { ignoreNfcFor(400); }

#define BUZZER_PWM_CHANNEL 2

void buzzerInit() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach((uint8_t)BUZZER_PIN, 2000, 8);
#else
  ledcSetup(BUZZER_PWM_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CHANNEL);
#endif
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

void toneOff() { ledcWriteTone((uint8_t)BUZZER_PIN, 0); }

void beepOnce(int ms, int freq) {
  ledcWriteTone((uint8_t)BUZZER_PIN, (uint32_t)freq);
  delay(ms);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

void beepArrivedPattern() {
  for (int i = 0; i < 3; i++) { beepOnce(140, 1800); delay(90); }
}