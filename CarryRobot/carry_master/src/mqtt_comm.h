/*  mqtt_comm.h  –  MQTT connection + pub/sub for Master ESP32
 */
#pragma once
#include <Arduino.h>

void mqttSetServerHost(const char* host);
const char* mqttGetServerHost();

void mqttInit();
void mqttLoop();            // call in loop() – reconnect + yield
bool mqttConnected();

// Publish
void mqttSendTelemetry(const char* state, const char* nodeId, const char* destBed);
void mqttSendProgress(const char* missionId, const char* nodeId, uint8_t idx, uint8_t total);
void mqttSendComplete(const char* missionId);
void mqttSendReturned(const char* missionId);
void mqttSendWaitingReturn(const char* nodeId, const char* previousNodeId = nullptr);
