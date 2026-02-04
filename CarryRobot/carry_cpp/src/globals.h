#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <vector>
#include <Adafruit_PN532.h>
#include <VL53L0X.h>
#include <U8g2lib.h>
#include <Preferences.h>

// =========================================
// ENUMS & STRUCTS
// =========================================
enum RunState { IDLE_AT_MED, RUN_OUTBOUND, WAIT_AT_DEST, RUN_RETURN };

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

// =========================================
// GLOBAL STATE VARIABLES
// =========================================
extern char apiBase[96];
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

#endif // GLOBALS_H
