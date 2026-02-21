#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <vector>
#include <Adafruit_PN532.h>
#include <VL53L0X.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>

// =========================================
// ENUMS & STRUCTS
// =========================================
enum RunState {
  ST_BOOT, ST_PORTAL, ST_CONNECTING, ST_IDLE, ST_GET_MISSION,
  ST_OUTBOUND, ST_CANCEL, ST_WAIT_AT_DEST, ST_BACK, ST_WAIT_RETURN_ROUTE
};

struct RoutePoint {
  String nodeId;
  String rfidUid;
  float x, y;
  char action;
};

// =========================================
// GLOBAL OBJECTS
// =========================================
extern Adafruit_PN532 nfc;
extern VL53L0X tof;
extern bool tofOk;
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C oled;
extern Preferences prefs;
extern WiFiClient espClient;
extern PubSubClient mqttClient;

// =========================================
// MQTT CONFIGURATION
// =========================================
extern char mqttServer[64];
extern int mqttPort;
extern char mqttUser[32];
extern char mqttPass[32];
extern bool mqttConnected;
extern unsigned long lastMqttReconnect;

extern char topicTelemetry[64];
extern char topicMissionAssign[64];
extern char topicMissionProgress[64];
extern char topicMissionComplete[64];
extern char topicMissionReturned[64];
extern char topicMissionCancel[64];
extern char topicMissionReturnRoute[64];
extern char topicPositionWaitingReturn[64];
extern char topicCommand[64];

// =========================================
// STATE VARIABLES
// =========================================
extern bool shouldSaveConfig;
extern unsigned long lastTelemetry, lastPoll, lastCancelPoll, lastObstacleBeep, lastOLED;

extern RunState state;
extern bool obstacleHold;

extern String activeMissionId, activeMissionStatus, patientName, bedId;
extern std::vector<RoutePoint> outbound, retRoute;
extern int routeIndex;

extern bool swRaw, swStable;
extern unsigned long swLastChange;
extern String currentCheckpoint, lastNfcUid;
extern unsigned long lastNfcAt;
extern bool cancelPending, destUturnedBeforeWait;

extern unsigned long nfcIgnoreUntil;
extern char lastTurnChar;
extern unsigned long turnOverlayUntil;

extern String cancelAtNodeId;
extern bool waitingForReturnRoute;
extern unsigned long waitingReturnRouteStartTime;

// =========================================
// UTILITY FUNCTIONS (merged from helpers)
// =========================================
String truncStr(const String& s, size_t maxLen);
void updateSW();
bool swHeld();
void ignoreNfcFor(unsigned long ms);
bool nfcAllowed();
bool isNfcReady();
void markNfcRead();
void buzzerInit();
void toneOff();
void beepOnce(int ms = 80, int freq = 2200);
void beepArrivedPattern();

#endif // GLOBALS_H
