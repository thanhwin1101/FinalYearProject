/*
 * ============================================================
 * BIPED USER MANAGER — MQTT SERVICE (Implementation)
 * ============================================================
 * WiFi (WiFiManager) + MQTT (PubSubClient) giao tiếp Dashboard
 * 
 * Publish topics:
 *   hospital/robots/BIPED-001/telemetry       — heartbeat mỗi 5s
 *   hospital/robots/BIPED-001/session/start    — bắt đầu session
 *   hospital/robots/BIPED-001/session/update   — cập nhật step count
 *   hospital/robots/BIPED-001/session/end      — kết thúc session
 * 
 * Subscribe topics:
 *   hospital/robots/BIPED-001/command          — lệnh remote (stop/resume)
 *   hospital/robots/BIPED-001/session/ack      — backend ACK session
 * ============================================================
 */

#include "mqtt_service.h"
#include "config.h"
#include "globals.h"
#include "hardware.h"

#include <WiFiManager.h>
#include <ArduinoJson.h>

// =========================================
// WIFI
// =========================================

void wifiInit(bool forcePortal) {
  WiFiManager wm;
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);

  // Custom parameter cho MQTT server
  WiFiManagerParameter mqttParam("mqtt", "MQTT Server", mqttServer, sizeof(mqttServer));
  wm.addParameter(&mqttParam);

  wm.setSaveParamsCallback([&]() {
    strncpy(mqttServer, mqttParam.getValue(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
    shouldSaveConfig = true;
  });

  wm.setClass("invert"); // Dark theme

  bool ok = false;
  if (forcePortal) {
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    displayWiFiSetup();
    ok = wm.startConfigPortal(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);
  } else {
    wm.setConfigPortalTimeout(0); // Không mở portal tự động
    ok = wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);
  }

  if (ok && WiFi.status() == WL_CONNECTED) {
    wifiOk = true;
    Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Lưu MQTT server nếu thay đổi
    if (shouldSaveConfig) {
      prefs.begin("biped", false);
      prefs.putString("mqtt_server", mqttServer);
      prefs.end();
      shouldSaveConfig = false;
      Serial.printf("[NVS] Saved mqtt_server: %s\n", mqttServer);
    }
  } else {
    wifiOk = false;
    Serial.println("[WIFI] Connection failed!");
  }
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void startWiFiPortal() {
  currentState = STATE_PORTAL;
  wifiInit(true);

  if (isWiFiConnected()) {
    displayWiFiOk();
    delay(2000);
    // Reinit MQTT với server mới
    mqttBuildTopics();
    mqttInit();
    mqttReconnect();
    currentState = STATE_IDLE;
  } else {
    displayWiFiFail();
    delay(2000);
    currentState = STATE_IDLE;
  }
}

// =========================================
// MQTT
// =========================================

void mqttBuildTopics() {
  snprintf(topicTelemetry,     sizeof(topicTelemetry),     TOPIC_TELEMETRY,      ROBOT_ID);
  snprintf(topicSessionStart,  sizeof(topicSessionStart),  TOPIC_SESSION_START,   ROBOT_ID);
  snprintf(topicSessionUpdate, sizeof(topicSessionUpdate), TOPIC_SESSION_UPDATE,  ROBOT_ID);
  snprintf(topicSessionEnd,    sizeof(topicSessionEnd),    TOPIC_SESSION_END,     ROBOT_ID);
  snprintf(topicCommand,       sizeof(topicCommand),       TOPIC_COMMAND,         ROBOT_ID);
  snprintf(topicSessionAck,    sizeof(topicSessionAck),    TOPIC_SESSION_ACK,     ROBOT_ID);

  Serial.printf("[MQTT] Topics built for %s\n", ROBOT_ID);
}

void mqttInit() {
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setBufferSize(1024);
  mqttClient.setCallback(mqttCallback);
  Serial.printf("[MQTT] Server: %s:%d\n", mqttServer, mqttPort);
}

void mqttLoop() {
  if (!isWiFiConnected()) {
    mqttConnected = false;
    return;
  }

  if (!mqttClient.connected()) {
    mqttConnected = false;
    unsigned long now = millis();
    if (now - lastMqttReconnect >= MQTT_RECONNECT_MS) {
      lastMqttReconnect = now;
      mqttReconnect();
    }
  } else {
    mqttClient.loop();
  }
}

void mqttReconnect() {
  if (mqttClient.connected()) {
    mqttConnected = true;
    return;
  }

  // Client ID uniquereconnect
  char clientId[48];
  snprintf(clientId, sizeof(clientId), "BipedUser-%s-%04X", ROBOT_ID, (uint16_t)random(0xFFFF));

  Serial.printf("[MQTT] Connecting as %s...\n", clientId);

  if (mqttClient.connect(clientId, mqttUser, mqttPass)) {
    mqttConnected = true;
    Serial.println("[MQTT] Connected!");

    // Subscribe to command + session ACK
    mqttClient.subscribe(topicCommand);
    mqttClient.subscribe(topicSessionAck);

    Serial.printf("[MQTT] Subscribed: %s\n", topicCommand);
    Serial.printf("[MQTT] Subscribed: %s\n", topicSessionAck);

    buzzerBeep(60, 2400); // MQTT connect beep
  } else {
    Serial.printf("[MQTT] Failed, rc=%d\n", mqttClient.state());
  }
}

bool isMqttConnected() {
  return mqttClient.connected();
}

// =========================================
// MQTT CALLBACK (Incoming)
// =========================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[512];
  int len = min((unsigned int)(sizeof(msg) - 1), length);
  memcpy(msg, payload, len);
  msg[len] = '\0';

  Serial.printf("[MQTT] RX [%s]: %s\n", topic, msg);

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) {
    Serial.println("[MQTT] JSON parse error");
    return;
  }

  // Command topic: stop / resume
  if (strstr(topic, "/command")) {
    const char* cmd = doc["command"];
    if (!cmd) return;

    if (strcmp(cmd, "stop") == 0) {
      sendUartCommand(CMD_STOP_STR);
      isMoving = false;
      Serial.println("[MQTT] Remote STOP");
    } else if (strcmp(cmd, "resume") == 0) {
      Serial.println("[MQTT] Remote RESUME");
    }
  }

  // Session ACK: backend xác nhận session và gửi sessionId
  if (strstr(topic, "/session/ack")) {
    const char* sid = doc["sessionId"];
    if (sid && session.isActive) {
      strncpy(session.sessionId, sid, sizeof(session.sessionId) - 1);
      Serial.printf("[MQTT] Session ACK: %s\n", sid);
    }

    // Nếu backend cần gửi thêm thông tin user
    const char* userName = doc["userName"];
    if (userName && strlen(userName) > 0) {
      strncpy(currentUser.userName, userName, sizeof(currentUser.userName) - 1);
      strncpy(session.userName, userName, sizeof(session.userName) - 1);
    }

    bool valid = doc["valid"] | true;
    if (!valid) {
      // Backend reject — thẻ không hợp lệ
      displayCardInvalid();
      delay(3000);
      extern void endSessionLocal();
      endSessionLocal();
    }
  }
}

// =========================================
// MQTT PUBLISH FUNCTIONS
// =========================================

static void mqttPub(const char* topic, const String& payload, bool retained = false) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(topic, payload.c_str(), retained);
  Serial.printf("[MQTT] PUB [%s] %d bytes\n", topic, payload.length());
}

/**
 * Telemetry / Heartbeat — publish mỗi 5s
 * Backend dùng để biết robot online, hiển thị trên dashboard
 */
void mqttSendTelemetry() {
  StaticJsonDocument<384> doc;
  doc["robotId"]         = ROBOT_ID;
  doc["name"]            = ROBOT_NAME;
  doc["type"]            = ROBOT_TYPE;
  doc["batteryLevel"]    = 100; // TODO: đọc pin thực tế
  doc["firmwareVersion"] = "biped-mqtt-v1";
  doc["status"]          = session.isActive ? "busy" : "idle";
  doc["mqttConnected"]   = true;

  if (session.isActive) {
    doc["currentUser"]      = session.userName;
    doc["stepCount"]        = session.stepCount;
    doc["currentSessionId"] = session.sessionId;
  }

  if (strlen(currentCheckpoint) > 0) {
    doc["currentNodeId"] = currentCheckpoint;
  }

  String payload;
  serializeJson(doc, payload);
  mqttPub(topicTelemetry, payload);
}

/**
 * Session Start — gửi khi quét thẻ RFID thành công
 * Backend tạo BipedSession document + trả sessionId qua ACK topic
 */
void mqttSendSessionStart() {
  StaticJsonDocument<384> doc;
  doc["robotId"]     = ROBOT_ID;
  doc["robotName"]   = ROBOT_NAME;
  doc["userId"]      = session.cardUid;     // RFID UID
  doc["userName"]    = session.userName;     // Tên bệnh nhân (có thể để trống nếu chưa biết)
  doc["patientId"]   = session.patientId;
  doc["patientName"] = session.userName;
  doc["cardNumber"]  = session.cardUid;

  if (strlen(currentUser.roomBed) > 0) {
    doc["roomBed"] = currentUser.roomBed;
  }

  String payload;
  serializeJson(doc, payload);
  mqttPub(topicSessionStart, payload);
}

/**
 * Session Update — gửi số bước mỗi 2s khi đang có session
 */
void mqttSendSessionUpdate() {
  if (!session.isActive) return;

  StaticJsonDocument<256> doc;
  doc["robotId"]    = ROBOT_ID;
  doc["sessionId"]  = session.sessionId;
  doc["steps"]      = session.stepCount;
  doc["speed"]      = currentSpeed;

  // Thời gian session tính bằng giây
  unsigned long durationSec = (millis() - session.startTime) / 1000;
  doc["durationSec"] = durationSec;

  if (strlen(balanceStatus) > 0) {
    doc["balanceStatus"] = balanceStatus;
  }

  String payload;
  serializeJson(doc, payload);
  mqttPub(topicSessionUpdate, payload);
}

/**
 * Session End — gửi khi quét thẻ lần 2 hoặc nhấn giữ Stop
 */
void mqttSendSessionEnd(const char* status) {
  StaticJsonDocument<256> doc;
  doc["robotId"]    = ROBOT_ID;
  doc["sessionId"]  = session.sessionId;
  doc["status"]     = status;          // "completed" / "interrupted"
  doc["totalSteps"] = session.stepCount;

  unsigned long durationSec = (millis() - session.startTime) / 1000;
  doc["durationSec"]   = durationSec;
  doc["durationMin"]   = durationSec / 60;

  String payload;
  serializeJson(doc, payload);
  mqttPub(topicSessionEnd, payload);
}

/**
 * Checkpoint report — gửi khi quét thẻ checkpoint
 */
void mqttSendCheckpoint(const char* checkpointId) {
  StaticJsonDocument<192> doc;
  doc["robotId"]      = ROBOT_ID;
  doc["checkpoint"]   = checkpointId;
  doc["timestamp"]    = millis();

  if (session.isActive) {
    doc["currentUser"] = session.userName;
    doc["sessionId"]   = session.sessionId;
  }

  String payload;
  serializeJson(doc, payload);
  // Gửi qua telemetry topic với thông tin checkpoint
  mqttPub(topicTelemetry, payload);
}
