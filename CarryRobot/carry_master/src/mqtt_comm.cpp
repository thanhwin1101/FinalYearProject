/*  mqtt_comm.cpp  –  MQTT over WiFi for backend communication
 */
#include "mqtt_comm.h"
#include "config.h"
#include "globals.h"
#include "route_manager.h"
#include "state_machine.h"
#include "mission_delegate.h"
#include "espnow_comm.h"
#include "display.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static unsigned long lastReconnectMs = 0;
static uint8_t brokerIdx = 0;
static bool brokerCandidatesReady = false;
static char brokerCandidates[3][32] = {};
static char mqttServerHost[32] = MQTT_DEFAULT_SERVER;

void mqttSetServerHost(const char* host) {
    if (!host) return;
    String v(host);
    v.trim();
    if (v.isEmpty()) return;

    snprintf(mqttServerHost, sizeof(mqttServerHost), "%s", v.c_str());
    brokerCandidatesReady = false;
    brokerIdx = 0;
}

const char* mqttGetServerHost() {
    return mqttServerHost;
}

static void setupBrokerCandidates() {
    if (WiFi.status() != WL_CONNECTED) return;

    const IPAddress ip = WiFi.localIP();
    snprintf(brokerCandidates[0], sizeof(brokerCandidates[0]), "%s", mqttServerHost);
    snprintf(brokerCandidates[1], sizeof(brokerCandidates[1]), "%u.%u.%u.100", ip[0], ip[1], ip[2]);
    snprintf(brokerCandidates[2], sizeof(brokerCandidates[2]), "%u.%u.%u.102", ip[0], ip[1], ip[2]);
    brokerCandidatesReady = true;
}

static const char* currentBrokerHost() {
    if (!brokerCandidatesReady) return mqttServerHost;
    return brokerCandidates[brokerIdx % 3];
}

/* ─── Topic helpers ─── */
static String topicOf(const char* suffix) {
    return String("hospital/robots/") + ROBOT_ID + "/" + suffix;
}

/* ─── Incoming message callback ─── */
static void mqttCallback(char* topic, byte* payload, unsigned int len) {
    StaticJsonDocument<MQTT_BUFFER_SIZE> doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[MQTT] JSON parse error on %s: %s (len=%u)\n",
                      topic, err.c_str(), len);
        return;
    }

    String t(topic);

    // ── mission/assign ──
    if (t.endsWith("/mission/assign")) {
        if (robotState != ST_IDLE) return;
        routeParseAssign(doc);
        missionDelegateSendRoutesOnly();  // send routes only; start on SW+MED
        return;
    }

    // ── mission/cancel ──
    if (t.endsWith("/mission/cancel")) {
        const char* mid = doc["missionId"];
        if (!mid && doc.containsKey("mission") && doc["mission"].is<JsonObjectConst>()) {
            mid = doc["mission"]["missionId"];
        }
        if (mid && missionId == mid) {
            if (robotState == ST_IDLE) {
                routeClear();
                displayCentered("MISSION CANCELED", "Back to standby");
            } else if (robotState == ST_MISSION_DELEGATED) {
                masterMsg.missionCancel = 1;
                espnowSendToSlave(masterMsg);
                routeClear();
                displayCentered("MISSION CANCELED", "Slave stopping...");
                smEnterIdle();
            } else {
                routeHandleCancel();
            }
        }
        return;
    }

    // ── mission/return_route ──
    if (t.endsWith("/mission/return_route")) {
        routeParseReturn(doc);
        return;
    }

    // ── command ──
    if (t.endsWith("/command")) {
        const char* cmd = doc["command"];
        if (!cmd) return;

        // Remote stop / resume (obstacle hold)
        if (strcmp(cmd, "stop") == 0) {
            obstacleHold = true;
            return;
        }
        if (strcmp(cmd, "resume") == 0) {
            obstacleHold = false;
            return;
        }

        // Remote mode switching:
        //   {"command":"set_mode","mode":"follow"}
        //   {"command":"set_mode","mode":"idle"}
        //   {"command":"follow"}  (shorthand)
        //   {"command":"idle"}    (shorthand)
        if (strcmp(cmd, "set_mode") == 0) {
            const char* mode = doc["mode"];
            smOnMqttCommand(cmd, mode);
            return;
        }
        if (strcmp(cmd, "follow") == 0 || strcmp(cmd, "idle") == 0) {
            smOnMqttCommand(cmd, nullptr);
            return;
        }

        Serial.printf("[MQTT] Unknown command: %s\n", cmd);
    }
}

/* ─── Init ─── */
void mqttInit() {
    mqtt.setServer(mqttServerHost, MQTT_DEFAULT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(MQTT_BUFFER_SIZE);
    Serial.println(F("[MQTT] Initialised"));
}

/* ─── Reconnect + subscribe ─── */
static void mqttReconnect() {
    if (mqtt.connected()) return;
    if (millis() - lastReconnectMs < MQTT_RECONNECT_MS) return;
    lastReconnectMs = millis();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[MQTT] Skip reconnect (WiFi not connected)"));
        return;
    }

    if (!brokerCandidatesReady) {
        setupBrokerCandidates();
    }

    const char* brokerHost = currentBrokerHost();
    mqtt.setServer(brokerHost, MQTT_DEFAULT_PORT);

    char clientId[40];
    snprintf(clientId, sizeof(clientId), "CarryMaster-%s-%04X",
             ROBOT_ID, (uint16_t)random(0xFFFF));

    Serial.printf("[MQTT] Connecting to %s:%u as %s …\n", brokerHost, (unsigned)MQTT_DEFAULT_PORT, clientId);
    if (mqtt.connect(clientId, MQTT_DEFAULT_USER, MQTT_DEFAULT_PASS)) {
        Serial.println(F("[MQTT] Connected"));
        mqtt.subscribe(topicOf("mission/assign").c_str(),   1);
        mqtt.subscribe(topicOf("mission/cancel").c_str(),   1);
        mqtt.subscribe(topicOf("mission/return_route").c_str(), 1);
        mqtt.subscribe(topicOf("command").c_str(),          1);
    } else {
        Serial.printf("[MQTT] Fail rc=%d\n", mqtt.state());
        brokerIdx = (brokerIdx + 1) % 3;
    }
}

/* ─── Loop ─── */
void mqttLoop() {
    if (!mqtt.connected()) mqttReconnect();
    mqtt.loop();
}

bool mqttConnected() { return mqtt.connected(); }

/* ─── Publish helpers ─── */

void mqttSendTelemetry(const char* state, const char* nodeId, const char* dest) {
    StaticJsonDocument<512> doc;
    doc["payloadVersion"]   = MQTT_PAYLOAD_VERSION;
    doc["robotId"]          = ROBOT_ID;
    doc["name"]             = ROBOT_NAME;
    doc["type"]             = "carry";
    doc["batteryLevel"]     = 100;
    doc["firmwareVersion"]  = FW_VERSION;
    doc["status"]           = (robotState == ST_IDLE) ? "idle" : "busy";
    doc["mqttConnected"]    = mqtt.connected();
    doc["currentNodeId"]    = nodeId ? nodeId : "";
    doc["destBed"]          = dest   ? dest   : "";
    char buf[512];
    serializeJson(doc, buf);
    mqtt.publish(topicOf("telemetry").c_str(), buf);
}

void mqttSendProgress(const char* mid, const char* nodeId, uint8_t idx, uint8_t total) {
    StaticJsonDocument<256> doc;
    doc["payloadVersion"] = MQTT_PAYLOAD_VERSION;
    doc["missionId"]  = mid;
    doc["robotId"]    = ROBOT_ID;
    doc["currentNodeId"] = nodeId;
#if MQTT_SEND_LEGACY_FIELDS
    doc["nodeId"]        = nodeId;
#endif
    doc["routeIndex"] = idx;
    doc["routeTotal"] = total;
    char buf[256];
    serializeJson(doc, buf);
    mqtt.publish(topicOf("mission/progress").c_str(), buf);
}

void mqttSendComplete(const char* mid) {
    StaticJsonDocument<128> doc;
    doc["payloadVersion"] = MQTT_PAYLOAD_VERSION;
    doc["missionId"] = mid;
    doc["robotId"]   = ROBOT_ID;
    char buf[128];
    serializeJson(doc, buf);
    mqtt.publish(topicOf("mission/complete").c_str(), buf);
}

void mqttSendReturned(const char* mid) {
    StaticJsonDocument<128> doc;
    doc["payloadVersion"] = MQTT_PAYLOAD_VERSION;
    doc["missionId"] = mid;
    doc["robotId"]   = ROBOT_ID;
    char buf[128];
    serializeJson(doc, buf);
    mqtt.publish(topicOf("mission/returned").c_str(), buf);
}

void mqttSendWaitingReturn(const char* nodeId, const char* previousNodeId) {
    StaticJsonDocument<192> doc;
    doc["payloadVersion"] = MQTT_PAYLOAD_VERSION;
    doc["robotId"]  = ROBOT_ID;
    doc["currentNodeId"] = nodeId;
#if MQTT_SEND_LEGACY_FIELDS
    doc["nodeId"]        = nodeId;
#endif
    if (previousNodeId && previousNodeId[0] != '\0') {
        doc["previousNodeId"] = previousNodeId;
    }
    char buf[192];
    serializeJson(doc, buf);
    mqtt.publish(topicOf("position/waiting_return").c_str(), buf);
}
