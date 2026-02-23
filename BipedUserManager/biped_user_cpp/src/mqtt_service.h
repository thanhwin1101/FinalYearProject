/*
 * ============================================================
 * BIPED USER MANAGER — MQTT SERVICE (Header)
 * ============================================================
 * WiFi + MQTT cho giao tiếp với Dashboard
 * ============================================================
 */

#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <Arduino.h>

// WiFi
void wifiInit(bool forcePortal = false);
bool isWiFiConnected();
void startWiFiPortal();

// MQTT
void mqttBuildTopics();
void mqttInit();
void mqttLoop();
void mqttReconnect();
bool isMqttConnected();

// Publish
void mqttSendTelemetry();
void mqttSendSessionStart();
void mqttSendSessionUpdate();
void mqttSendSessionEnd(const char* status);
void mqttSendCheckpoint(const char* checkpointId);

// MQTT callback
void mqttCallback(char* topic, byte* payload, unsigned int length);

#endif // MQTT_SERVICE_H
