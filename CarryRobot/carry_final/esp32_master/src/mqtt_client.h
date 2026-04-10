#pragma once
#include <Arduino.h>
#include "config.h"

void mqttInit();
void mqttLoop();             // call frequently
bool mqttIsConnected();
void mqttPublishCheckpoint(uint16_t cpId);
void mqttPublishIdleScan(uint16_t cpId);
void mqttPublishBattery(uint8_t pct);
void mqttPublishReturnRequest(uint16_t cpId);
void mqttPublishStatus(const char *status);
void mqttPublishMissionDone(const char *missionId, bool success);
void mqttPublishEvent(const char *evt);     // generic sensor/system event
void mqttPublishTelemetry();   // periodic debug telemetry for test lab
