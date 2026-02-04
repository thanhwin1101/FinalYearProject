#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <Arduino.h>

// =========================================
// WIFI FUNCTIONS
// =========================================
void wifiInit();
bool isWiFiConnected();

// =========================================
// API FUNCTIONS
// =========================================
bool fetchNextMission();
bool updateMissionProgress(const char* missionId, const char* currentNode, const char* status, int progress);
bool completeMission(const char* missionId);
bool sendTelemetry();

#endif
