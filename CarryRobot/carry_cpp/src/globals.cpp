#include "globals.h"
#include "config.h"

// =========================================
// GLOBAL OBJECTS (definitions)
// =========================================
Adafruit_PN532 nfc(PN532_SS);
VL53L0X tof;
bool tofOk = false;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
Preferences prefs;

// MQTT Objects
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

// Topic buffers
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
// GLOBAL STATE VARIABLES (definitions)
// =========================================
static const char* PREF_NS = "carrycfg";
bool shouldSaveConfig = false;

unsigned long lastTelemetry = 0;
unsigned long lastPoll = 0;
unsigned long lastCancelPoll = 0;
unsigned long lastObstacleBeep = 0;
unsigned long lastOLED = 0;
unsigned long lastWebOkAt = 0;
unsigned long webOkUntil = 0;

RunState state = IDLE_AT_MED;
bool obstacleHold = false;

String activeMissionId = "";
String activeMissionStatus = "";
String patientName = "";
String bedId = "";

std::vector<RoutePoint> outbound;
std::vector<RoutePoint> retRoute;
int routeIndex = 0;
bool haveSeenMED = false;

bool cargoRaw = true;
bool cargoStable = true;
unsigned long cargoLastChange = 0;
String lastNfcUid = "";
unsigned long lastNfcAt = 0;
bool cancelPending = false;
bool destUturnedBeforeWait = false;

unsigned long nfcIgnoreUntil = 0;
char lastTurnChar = 'F';
unsigned long turnOverlayUntil = 0;

// Position tracking for cancel/return
String cancelAtNodeId = "";
bool waitingForReturnRoute = false;
unsigned long waitingReturnRouteStartTime = 0;
