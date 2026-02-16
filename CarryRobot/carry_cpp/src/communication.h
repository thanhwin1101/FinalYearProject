#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <Arduino.h>

// =========================================
// WIFI FUNCTIONS
// =========================================
void wifiInit();
bool isWiFiConnected();

// =========================================
// MQTT FUNCTIONS
// =========================================
void mqttInit();
void mqttLoop();
void mqttReconnect();
bool isMqttConnected();
void buildTopics();

// =========================================
// MQTT PUBLISH FUNCTIONS
// =========================================
void mqttPublish(const char* topic, const String& payload, bool retained = false);
void sendTelemetry();
void sendProgress(const char* statusText, const String& nodeId, const char* note = nullptr);
void sendComplete(const char* result = "ok");
void sendReturned(const char* note = nullptr);
void sendPositionWaitingReturn(const String& currentNodeId);

// =========================================
// MQTT MESSAGE HANDLERS
// =========================================
void parseMissionPayload(const char* payload);
void parseCancelPayload(const char* payload);
void parseReturnRoutePayload(const char* payload);
void parseCommandPayload(const char* payload);

// =========================================
// CONNECTION STATUS
// =========================================
void markMqttOk();
bool mqttOk();
bool webConnected();

#endif // COMMUNICATION_H
