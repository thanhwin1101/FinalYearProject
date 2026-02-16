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
enum RunState { IDLE_AT_MED, RUN_OUTBOUND, WAIT_AT_DEST, RUN_RETURN, WAIT_FOR_RETURN_ROUTE };

struct RoutePoint {
  String nodeId;
  String rfidUid;
  float x;
  float y;
  char action; 
};

// =========================================
// GLOBAL OBJECTS (extern declarations)
// =========================================
extern Adafruit_PN532 nfc;
extern VL53L0X tof;
extern bool tofOk;
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C oled;
extern Preferences prefs;

// MQTT Objects
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

// Topic buffers
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
// GLOBAL STATE VARIABLES
// =========================================
extern bool shouldSaveConfig;

extern unsigned long lastTelemetry;
extern unsigned long lastPoll;
extern unsigned long lastCancelPoll;
extern unsigned long lastObstacleBeep;
extern unsigned long lastOLED;
extern unsigned long lastWebOkAt;
extern unsigned long webOkUntil;

extern RunState state;
extern bool obstacleHold;

extern String activeMissionId;
extern String activeMissionStatus;
extern String patientName;
extern String bedId;

extern std::vector<RoutePoint> outbound;
extern std::vector<RoutePoint> retRoute;
extern int routeIndex;
extern bool haveSeenMED;

extern bool cargoRaw;
extern bool cargoStable;
extern unsigned long cargoLastChange;
extern String lastNfcUid;
extern unsigned long lastNfcAt;
extern bool cancelPending;
extern bool destUturnedBeforeWait;

extern unsigned long nfcIgnoreUntil;
extern char lastTurnChar;
extern unsigned long turnOverlayUntil;

// Position tracking for cancel/return
extern String cancelAtNodeId;
extern bool waitingForReturnRoute;
extern unsigned long waitingReturnRouteStartTime;

#endif // GLOBALS_H
