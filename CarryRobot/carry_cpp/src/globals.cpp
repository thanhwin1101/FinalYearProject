#include "globals.h"
#include "config.h"

Adafruit_PN532 nfc(NFC_SS);
VL53L0X tof;
bool tofOk = false;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
Preferences prefs;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
HUSKYLENS huskylens;

char mqttServer[64] = MQTT_DEFAULT_SERVER;
int mqttPort = MQTT_DEFAULT_PORT;
char mqttUser[32] = MQTT_DEFAULT_USER;
char mqttPass[32] = MQTT_DEFAULT_PASS;
bool mqttConnected = false;
unsigned long lastMqttReconnect = 0;

char topicTelemetry[64], topicMissionAssign[64], topicMissionProgress[64];
char topicMissionComplete[64], topicMissionReturned[64], topicMissionCancel[64];
char topicMissionReturnRoute[64], topicPositionWaitingReturn[64], topicCommand[64];

bool shouldSaveConfig = false;
unsigned long lastTelemetry = 0, lastObstacleBeep = 0, lastOLED = 0;
RunState state = ST_BOOT;
bool obstacleHold = false;

String activeMissionId = "", activeMissionStatus = "", patientName = "", bedId = "";
std::vector<RoutePoint> outbound, retRoute;
int routeIndex = 0;

String currentCheckpoint = "MED";
String lastNfcUid = "";
unsigned long lastNfcAt = 0;
bool cancelPending = false, destUturnedBeforeWait = false;

unsigned long nfcIgnoreUntil = 0;
char lastTurnChar = 'F';
unsigned long turnOverlayUntil = 0;
String cancelAtNodeId = "";
bool waitingForReturnRoute = false;
unsigned long waitingReturnRouteStartTime = 0;

bool flagSingleClick = false;
bool flagDoubleClick = false;
unsigned long btnPressTime = 0;
int btnClicks = 0;
bool btnLastState = HIGH; 
unsigned long lastDebounceTime = 0;
bool btnState = HIGH;

void processButton() {
  flagSingleClick = false;
  flagDoubleClick = false;

  int reading = digitalRead(SW_PIN);
  if (reading != btnLastState) { lastDebounceTime = millis(); }
  
  if ((millis() - lastDebounceTime) > 50) {
    if (reading != btnState) {
      btnState = reading;
      if (btnState == LOW) {
        btnPressTime = millis();
        btnClicks++;
      }
    }
  }
  btnLastState = reading;

  if (btnClicks > 0 && (millis() - btnPressTime) > 350) {
    if (btnClicks == 1) flagSingleClick = true;
    else if (btnClicks >= 2) flagDoubleClick = true;
    btnClicks = 0;
  }
}

String truncStr(const String& s, size_t maxLen) {
  if (s.length() <= (int)maxLen) return s;
  return s.substring(0, maxLen);
}

void ignoreNfcFor(unsigned long ms) { nfcIgnoreUntil = millis() + ms; }
bool nfcAllowed() { return millis() >= nfcIgnoreUntil; }
void markNfcRead() { ignoreNfcFor(400); }

void buzzerInit() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach((uint8_t)BUZZER_PIN, 2000, 8);
#else
  ledcSetup(2, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 2);
#endif
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}
void toneOff() { ledcWriteTone((uint8_t)BUZZER_PIN, 0); }
void beepOnce(int ms, int freq) {
  ledcWriteTone((uint8_t)BUZZER_PIN, (uint32_t)freq); delay(ms); ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}
void beepArrivedPattern() {
  for (int i = 0; i < 3; i++) { beepOnce(140, 1800); delay(90); }
}