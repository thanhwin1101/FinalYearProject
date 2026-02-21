#ifndef MISSION_H
#define MISSION_H

#include <Arduino.h>
#include <vector>
#include "globals.h"

// =========================================
// WIFI
// =========================================
void wifiInit(bool forcePortal = false);
bool isWiFiConnected();

// =========================================
// MQTT
// =========================================
void mqttInit();
void mqttLoop();
void mqttReconnect();
bool isMqttConnected();
void buildTopics();
void mqttPublish(const char* topic, const String& payload, bool retained = false);
void sendTelemetry();
void sendProgress(const char* statusText, const String& nodeId, const char* note = nullptr);
void sendComplete(const char* result = "ok");
void sendReturned(const char* note = nullptr);
void sendPositionWaitingReturn(const String& currentNodeId);
bool mqttOk();

void parseMissionPayload(const char* payload);
void parseCancelPayload(const char* payload);
void parseReturnRoutePayload(const char* payload);
void parseCommandPayload(const char* payload);

// =========================================
// ROUTE LOGIC
// =========================================
const std::vector<RoutePoint>& currentRoute();
String expectedNextUid();
String currentNodeIdSafe();
char upcomingTurnAtNextNode();
const char* turnCharLabel(char a);
char invertTurn(char a);
void buildReturnFromVisited();

// =========================================
// STATE MACHINE
// =========================================
void startOutbound();
void enterWaitAtDest();
void startReturn(const char* note, bool doUturn);
void goIdleReset();
void handleCheckpointHit(const String& uid);

#endif // MISSION_H
