#include "communication.h"
#include "config.h"
#include "globals.h"
#include "helpers.h"
#include "route_logic.h"
#include "motor_control.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFiManager instance
WiFiManager wifiManager;

// =========================================
// CONNECTION STATUS
// =========================================
void markMqttOk() {
  lastWebOkAt = millis();
  webOkUntil = lastWebOkAt + WEB_OK_SHOW_MS;
}

bool mqttOk() {
  return mqttConnected && mqttClient.connected();
}

// =========================================
// WIFI FUNCTIONS
// =========================================
void wifiInit() {
  wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
  wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);
  
  WiFiManagerParameter customMqttServer("mqtt_server", "MQTT Server", mqttServer, sizeof(mqttServer));
  wifiManager.addParameter(&customMqttServer);
  
  bool res = wifiManager.autoConnect("CarryRobot-Setup", "carry123");
  
  if (res) {
#if SERIAL_DEBUG
    Serial.println("WiFi connected");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
#endif
    
    strncpy(mqttServer, customMqttServer.getValue(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
    
    prefs.begin("carrycfg", false);
    prefs.putString("mqtt_server", mqttServer);
    prefs.end();
    
#if SERIAL_DEBUG
    Serial.print("MQTT Server: "); Serial.println(mqttServer);
#endif
  } else {
#if SERIAL_DEBUG
    Serial.println("WiFi failed to connect");
#endif
  }
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// =========================================
// MQTT CALLBACK
// =========================================
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char* msg = (char*)malloc(length + 1);
  if (!msg) return;
  memcpy(msg, payload, length);
  msg[length] = '\0';

#if SERIAL_DEBUG
  Serial.print("MQTT Recv ["); Serial.print(topic); Serial.print("]: ");
  Serial.println(msg);
#endif

  markMqttOk();

  // Dispatch based on topic
  if (strcmp(topic, topicMissionAssign) == 0) {
    parseMissionPayload(msg);
  } else if (strcmp(topic, topicMissionCancel) == 0) {
    parseCancelPayload(msg);
  } else if (strcmp(topic, topicMissionReturnRoute) == 0) {
    parseReturnRoutePayload(msg);
  } else if (strcmp(topic, topicCommand) == 0) {
    parseCommandPayload(msg);
  }

  free(msg);
}

// =========================================
// BUILD TOPICS
// =========================================
void buildTopics() {
  snprintf(topicTelemetry, sizeof(topicTelemetry), TOPIC_TELEMETRY, ROBOT_ID);
  snprintf(topicMissionAssign, sizeof(topicMissionAssign), TOPIC_MISSION_ASSIGN, ROBOT_ID);
  snprintf(topicMissionProgress, sizeof(topicMissionProgress), TOPIC_MISSION_PROGRESS, ROBOT_ID);
  snprintf(topicMissionComplete, sizeof(topicMissionComplete), TOPIC_MISSION_COMPLETE, ROBOT_ID);
  snprintf(topicMissionReturned, sizeof(topicMissionReturned), TOPIC_MISSION_RETURNED, ROBOT_ID);
  snprintf(topicMissionCancel, sizeof(topicMissionCancel), TOPIC_MISSION_CANCEL, ROBOT_ID);
  snprintf(topicMissionReturnRoute, sizeof(topicMissionReturnRoute), TOPIC_MISSION_RETURN_ROUTE, ROBOT_ID);
  snprintf(topicPositionWaitingReturn, sizeof(topicPositionWaitingReturn), TOPIC_POSITION_WAITING_RETURN, ROBOT_ID);
  snprintf(topicCommand, sizeof(topicCommand), TOPIC_COMMAND, ROBOT_ID);
}

// =========================================
// MQTT FUNCTIONS
// =========================================
void mqttInit() {
  buildTopics();
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);  // Larger buffer for mission payloads
}

void mqttReconnect() {
  if (mqttClient.connected()) {
    mqttConnected = true;
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttReconnect < MQTT_RECONNECT_MS) return;
  lastMqttReconnect = now;

#if SERIAL_DEBUG
  Serial.print("MQTT connecting to "); Serial.print(mqttServer);
  Serial.print(":"); Serial.println(mqttPort);
#endif

  String clientId = String("CarryRobot-") + ROBOT_ID + "-" + String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)) {
    mqttConnected = true;
    markMqttOk();
    
    // Subscribe to relevant topics
    mqttClient.subscribe(topicMissionAssign);
    mqttClient.subscribe(topicMissionCancel);
    mqttClient.subscribe(topicMissionReturnRoute);
    mqttClient.subscribe(topicCommand);
    
#if SERIAL_DEBUG
    Serial.println("MQTT connected, subscribed to topics");
#endif
    beepOnce(60, 2400);
  } else {
    mqttConnected = false;
#if SERIAL_DEBUG
    Serial.print("MQTT failed, rc="); Serial.println(mqttClient.state());
#endif
  }
}

void mqttLoop() {
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();
}

bool isMqttConnected() {
  return mqttClient.connected();
}

// =========================================
// MQTT PUBLISH
// =========================================
void mqttPublish(const char* topic, const String& payload, bool retained) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(topic, payload.c_str(), retained);
#if SERIAL_DEBUG
  Serial.print("MQTT Pub ["); Serial.print(topic); Serial.print("]: ");
  Serial.println(payload);
#endif
}

void sendTelemetry() {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<256> doc;
  doc["robotId"] = ROBOT_ID;
  doc["name"] = String("Carry ") + ROBOT_ID;
  doc["type"] = "carry";
  doc["batteryLevel"] = 100;
  doc["firmwareVersion"] = "carry-mqtt-v1";
  bool busy = (state != IDLE_AT_MED) || (activeMissionId.length() > 0);
  doc["status"] = busy ? "busy" : "idle";
  doc["mqttConnected"] = mqttConnected;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicTelemetry, payload);
}

void sendProgress(const char* statusText, const String& nodeId, const char* note) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<256> doc;
  doc["missionId"] = activeMissionId;
  if (statusText && statusText[0]) doc["status"] = statusText;
  doc["currentNodeId"] = nodeId;
  doc["batteryLevel"] = 100;
  if (note) doc["note"] = note;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicMissionProgress, payload);
}

void sendComplete(const char* result) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc;
  doc["missionId"] = activeMissionId;
  doc["result"] = result;
  doc["note"] = "delivered; switch released; start return";
  String payload; serializeJson(doc, payload);
  mqttPublish(topicMissionComplete, payload);
}

void sendReturned(const char* note) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc;
  doc["missionId"] = activeMissionId;
  if (note) doc["note"] = note;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicMissionReturned, payload);
}

void sendPositionWaitingReturn(const String& currentNodeId) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  
  StaticJsonDocument<192> doc;
  doc["missionId"] = activeMissionId;
  doc["currentNodeId"] = currentNodeId;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicPositionWaitingReturn, payload);
  
#if SERIAL_DEBUG
  Serial.print("Sent position waiting for return: "); Serial.println(currentNodeId);
#endif
}

// =========================================
// MESSAGE PARSERS
// =========================================
void parseMissionPayload(const char* payload) {
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload)) {
#if SERIAL_DEBUG
    Serial.println("parseMissionPayload: JSON parse error");
#endif
    return;
  }

  if (state != IDLE_AT_MED || activeMissionId.length() > 0) {
#if SERIAL_DEBUG
    Serial.println("parseMissionPayload: Robot busy, ignoring");
#endif
    return;
  }

  String mid = doc["missionId"] | "";
  if (mid.length() == 0) return;

  activeMissionId = mid;
  activeMissionStatus = doc["status"] | "assigned";
  patientName = doc["patientName"] | "";
  bedId = doc["destBed"] | "";

  // Parse outbound route
  outbound.clear();
  JsonArray arr = doc["outboundRoute"].as<JsonArray>();
  for (JsonVariant v : arr) {
    RoutePoint rp;
    rp.nodeId = v["nodeId"] | "";
    rp.rfidUid = v["rfidUid"] | "";
    rp.x = v["x"] | 0.0f;
    rp.y = v["y"] | 0.0f;
    rp.action = (v["action"] | "F")[0];
    if (rp.nodeId.length() > 0) {
      outbound.push_back(rp);
    }
  }

  // Parse return route
  retRoute.clear();
  JsonArray retArr = doc["returnRoute"].as<JsonArray>();
  for (JsonVariant v : retArr) {
    RoutePoint rp;
    rp.nodeId = v["nodeId"] | "";
    rp.rfidUid = v["rfidUid"] | "";
    rp.x = v["x"] | 0.0f;
    rp.y = v["y"] | 0.0f;
    rp.action = (v["action"] | "F")[0];
    if (rp.nodeId.length() > 0) {
      retRoute.push_back(rp);
    }
  }

#if SERIAL_DEBUG
  Serial.print("Mission received: "); Serial.println(mid);
  Serial.print("Patient: "); Serial.println(patientName);
  Serial.print("Bed: "); Serial.println(bedId);
  Serial.print("Outbound nodes: "); Serial.println(outbound.size());
  Serial.print("Return nodes: "); Serial.println(retRoute.size());
#endif

  markMqttOk();
  beepOnce(120, 2200);
}

void parseCancelPayload(const char* payload) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return;

  String mid = doc["missionId"] | "";
  if (mid.length() == 0 || mid != activeMissionId) return;

  activeMissionStatus = "cancelled";
  
  if (state == RUN_OUTBOUND) {
    cancelPending = true;
    beepOnce(80, 1500);
#if SERIAL_DEBUG
    Serial.println("Cancel pending - will stop at next checkpoint");
#endif
  } else if (state == WAIT_AT_DEST) {
#if SERIAL_DEBUG
    Serial.println("Cancel received at destination");
#endif
  }
}

void parseReturnRoutePayload(const char* payload) {
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload)) {
#if SERIAL_DEBUG
    Serial.println("parseReturnRoutePayload: JSON parse error");
#endif
    return;
  }

  String mid = doc["missionId"] | "";
  if (mid.length() == 0 || mid != activeMissionId) return;
  
  String status = doc["status"] | "";
  if (status != "ok") {
#if SERIAL_DEBUG
    Serial.println("Return route status error - fallback to local");
#endif
    buildReturnFromVisited();
    waitingForReturnRoute = false;
    state = RUN_RETURN;
    routeIndex = 0;
    obstacleHold = false;
    beepOnce(120, 1800);
    return;
  }

  // Clear existing return route
  retRoute.clear();
  
  // Parse return route array
  JsonArray arr = doc["returnRoute"].as<JsonArray>();
  if (arr.isNull() || arr.size() < 2) {
#if SERIAL_DEBUG
    Serial.println("Return route too short - fallback to local");
#endif
    buildReturnFromVisited();
    waitingForReturnRoute = false;
    state = RUN_RETURN;
    routeIndex = 0;
    obstacleHold = false;
    beepOnce(120, 1800);
    return;
  }

  for (JsonVariant v : arr) {
    RoutePoint rp;
    rp.nodeId = v["nodeId"] | "";
    rp.rfidUid = v["rfidUid"] | "";
    rp.x = v["x"] | 0.0f;
    rp.y = v["y"] | 0.0f;
    rp.action = (v["action"] | "F")[0];
    if (rp.nodeId.length() > 0) {
      retRoute.push_back(rp);
    }
  }

#if SERIAL_DEBUG
  Serial.print("Received return route from Backend: ");
  Serial.print(retRoute.size()); Serial.println(" nodes");
#endif

  // Start return with Backend route
  waitingForReturnRoute = false;
  state = RUN_RETURN;
  routeIndex = 0;
  obstacleHold = false;
  beepOnce(120, 2400);
}

void parseCommandPayload(const char* payload) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return;

  String cmd = doc["command"] | "";
  if (cmd == "stop") {
    motorsStop();
    obstacleHold = true;
    beepOnce(200, 1200);
  } else if (cmd == "resume") {
    obstacleHold = false;
    beepOnce(60, 2400);
  }
}
