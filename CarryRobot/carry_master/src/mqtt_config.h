#pragma once
// ====================================================================
//  Carry Robot – Master – MQTT / WiFi glue
//  Aligned with Hospital Dashboard backend (mqttService.js):
//    RX  carry/robot/cmd                JSON  { mission: {...} }
//                                       JSON  { action: 'start' | 'cancel' }
//                                       JSON  { action: 'return_route', route|ids }
//                                       JSON  { action: 'route', ids: [...] }
//    TX  carry/robot/evt                JSON  { evt, id?, mission?, ... }
//    TX  robot/return_request           JSON  { checkpoint_id }
// ====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "checkpoint_map.h"

namespace MqttCfg {

static WiFiClient   s_wifi;
static PubSubClient s_mqtt(s_wifi);

// outboundCompact / returnCompact: "nodeId,action|nodeId,action|..."
typedef void (*MissionCb)     (const String& outboundCompact,
                               const String& returnCompact,
                               const String& missionId,
                               const String& patientName,
                               const String& destLabel);
typedef void (*StartCb)       ();
typedef void (*CancelCb)      ();
typedef void (*ReturnRouteCb) (const String& routeCompact);

static MissionCb     s_missionCb     = nullptr;
static StartCb       s_startCb       = nullptr;
static CancelCb      s_cancelCb      = nullptr;
static ReturnRouteCb s_returnRouteCb = nullptr;

inline void setCallbacks(MissionCb m, StartCb s, CancelCb c, ReturnRouteCb r) {
    s_missionCb = m; s_startCb = s; s_cancelCb = c; s_returnRouteCb = r;
}

// --------------------------------------------------------------------
//  Pack a JSON array of route steps into the slave-friendly compact
//  string  "nodeId,action|nodeId,action|..."  Action defaults to 'F'.
//  Validates: 'F','L','R','B' only.
// --------------------------------------------------------------------
inline String packRouteFromArray(JsonArray arr) {
    String out;
    for (JsonObject step : arr) {
        // Prefer nodeId; fall back to numeric id → name lookup so test
        // commands (action:'route', ids:[...]) and return_route ids
        // arrays still work.
        String nodeId = step["nodeId"].as<String>();
        if (nodeId.length() == 0) {
            uint16_t cpId = step["id"].as<uint16_t>();
            const char* nm = cpIdToName(cpId);
            if (nm) nodeId = nm;
        }
        if (nodeId.length() == 0) continue;

        const char* a = step["action"] | "F";
        char act = (a && a[0]) ? a[0] : 'F';
        if (act != 'F' && act != 'L' && act != 'R' && act != 'B') act = 'F';

        if (out.length()) out += '|';
        out += nodeId;
        out += ',';
        out += act;
    }
    return out;
}

// Find the label of the last step in a route array (for OLED "Dest:" line).
inline String lastLabelOfArray(JsonArray arr) {
    String lbl;
    for (JsonObject step : arr) {
        const char* l = step["label"] | "";
        if (l && *l) lbl = l;
    }
    return lbl;
}

inline String packRouteFromIds(JsonArray ids) {
    String out;
    for (JsonVariant v : ids) {
        uint16_t cpId = v.as<uint16_t>();
        const char* nm = cpIdToName(cpId);
        if (!nm) continue;
        if (out.length()) out += '|';
        out += nm;
        out += ",F";
    }
    return out;
}

// --------------------------------------------------------------------
//  Inbound dispatch
// --------------------------------------------------------------------
inline void onMessage(char* topic, byte* payload, unsigned int len) {
    String t(topic);
    Serial.printf("[MQTT<-] %s len=%u\n", topic, len);
    if (t != TOPIC_CMD_RX) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[MQTT] cmd parse err: %s\n", err.c_str());
        return;
    }

    // ----- mission assignment --------------------------------------
    JsonVariant m = doc["mission"];
    if (m.is<JsonObject>()) {
        String missionId   = m["missionId"].as<String>();
        String patientName = m["patientName"].as<String>();
        if (!patientName.length()) patientName = m["patient"].as<String>();
        JsonArray obArr  = m["outboundRoute"].as<JsonArray>();
        JsonArray retArr = m["returnRoute"].as<JsonArray>();
        String ob  = packRouteFromArray(obArr);
        String ret = packRouteFromArray(retArr);
        String destLabel = lastLabelOfArray(obArr);
        Serial.printf("[MQTT] mission id=%s ob='%s' ret='%s' dest='%s'\n",
                      missionId.c_str(), ob.c_str(), ret.c_str(), destLabel.c_str());
        if (s_missionCb) s_missionCb(ob, ret, missionId, patientName, destLabel);
        return;
    }

    // ----- imperative actions --------------------------------------
    const char* action = doc["action"] | "";
    if (!*action) return;

    if (!strcmp(action, "start")) {
        if (s_startCb) s_startCb();
    } else if (!strcmp(action, "cancel")) {
        if (s_cancelCb) s_cancelCb();
    } else if (!strcmp(action, "return_route")) {
        String comp;
        if (doc["route"].is<JsonArray>())     comp = packRouteFromArray(doc["route"].as<JsonArray>());
        else if (doc["ids"].is<JsonArray>())  comp = packRouteFromIds  (doc["ids"].as<JsonArray>());
        if (comp.length() && s_returnRouteCb) s_returnRouteCb(comp);
    } else if (!strcmp(action, "route")) {
        // Test command: { action:'route', ids:[cpId,...] } — treat as
        // a one-shot outbound route with no return leg.
        String comp;
        if (doc["ids"].is<JsonArray>())       comp = packRouteFromIds(doc["ids"].as<JsonArray>());
        else if (doc["route"].is<JsonArray>())comp = packRouteFromArray(doc["route"].as<JsonArray>());
        if (comp.length() && s_missionCb) s_missionCb(comp, String(), String(), String(), String());
    }
}

inline void begin() {
    s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    s_mqtt.setBufferSize(4096);
    s_mqtt.setCallback(onMessage);
}

// Allow runtime override of the broker IP (from WiFiManager / NVS).
inline void setHost(const char* host) {
    if (host && *host) s_mqtt.setServer(host, MQTT_PORT);
    else               s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
}

inline bool reconnect() {
    if (s_mqtt.connected()) return true;
    if (WiFi.status() != WL_CONNECTED) return false;
    if (s_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
        s_mqtt.subscribe(TOPIC_CMD_RX, 1);
        Serial.printf("[MQTT] subscribed %s\n", TOPIC_CMD_RX);
        // Announce ourselves so the backend creates the Robot row and
        // the dashboard shows us immediately.
        String hello = String("{\"evt\":\"hello\",\"id\":\"") + ROBOT_ID + "\"}";
        s_mqtt.publish(TOPIC_EVT_TX, (const uint8_t*)hello.c_str(), hello.length(), false);
        Serial.printf("[MQTT->] %s %s\n", TOPIC_EVT_TX, hello.c_str());
        return true;
    }
    return false;
}

inline void loop() {
    if (!s_mqtt.connected()) {
        // NFR-07: retry every 3 s on disconnection
        static uint32_t tRetry = 0;
        if (millis() - tRetry >= 3000) {
            tRetry = millis();
            reconnect();
        }
    }
    s_mqtt.loop();
}

inline bool isConnected() { return s_mqtt.connected(); }

inline bool publishRaw(const char* topic, const String& payload) {
    if (!s_mqtt.connected()) return false;
    return s_mqtt.publish(topic, (const uint8_t*)payload.c_str(), payload.length(), false);
}

// --------------------------------------------------------------------
//  Event publishers (carry/robot/evt) — backend-compatible schema.
// --------------------------------------------------------------------
inline void publishEvent(JsonDocument& d) {
    String s; serializeJson(d, s);
    publishRaw(TOPIC_EVT_TX, s);
    Serial.printf("[MQTT->] %s %s\n", TOPIC_EVT_TX, s.c_str());
}

inline void publishCheckpoint(uint16_t cpId, const String& nodeId) {
    JsonDocument d;
    d["evt"] = "checkpoint";
    d["id"]  = cpId;
    if (nodeId.length()) d["node"] = nodeId;     // bonus; ignored by backend
    publishEvent(d);
}

inline void publishArrivedDestination(const String& missionId,
                                      uint16_t cpId, const String& nodeId) {
    JsonDocument d;
    d["evt"]     = "arrived_destination";
    d["mission"] = missionId;
    if (cpId)            d["id"]   = cpId;
    if (nodeId.length()) d["node"] = nodeId;
    publishEvent(d);
}

inline void publishMissionDone(const String& missionId) {
    JsonDocument d;
    d["evt"]     = "mission_done";
    d["mission"] = missionId;
    publishEvent(d);
}

// Operator pressed the button at the destination → robot is leaving for
// MED. Backend uses this to raise an alert "BN <name> ở giường <bed>
// đã nhận đồ, robot đang quay về".
inline void publishReturnStarted(const String& missionId,
                                 const String& nodeId = "") {
    JsonDocument d;
    d["evt"]     = "return_started";
    d["mission"] = missionId;
    if (nodeId.length()) d["node"] = nodeId;
    publishEvent(d);
}

inline void publishCpMismatch(uint16_t recvId, const String& recvName,
                              uint16_t expId,  const String& expName) {
    JsonDocument d;
    d["evt"] = "cp_mismatch";
    if (recvId) d["recv"] = recvId; else d["recv"] = recvName;
    if (expId)  d["exp"]  = expId;  else d["exp"]  = expName;
    publishEvent(d);
}

inline void publishObstacle(bool blocked) {
    JsonDocument d;
    d["evt"] = "obstacle";
    d["blocked"] = blocked;
    publishEvent(d);
}

inline void publishCancelAck() {
    // Backend listens for "cancelled" to flip status→idle. Also emit
    // the user-requested "cancel_ack" so it shows in logs.
    {
        JsonDocument d;
        d["evt"] = "cancelled";
        publishEvent(d);
    }
    {
        JsonDocument d;
        d["evt"] = "cancel_ack";
        publishEvent(d);
    }
}

inline void publishRouteAccept(const String& missionId, int nSteps) {
    JsonDocument d;
    d["evt"] = "route_accept";
    d["n"]   = nSteps;
    if (missionId.length()) d["mission"] = missionId;
    publishEvent(d);
}

inline void publishLineLost() {
    JsonDocument d; d["evt"] = "line_lost"; publishEvent(d);
}

inline void publishRecoveryNfc(uint16_t cpId) {
    JsonDocument d;
    d["evt"] = "recovery_nfc";
    d["id"]  = cpId;
    publishEvent(d);
}

// --------------------------------------------------------------------
//  Return-request: ask the backend for a route from current cp → MED.
// --------------------------------------------------------------------
inline void publishReturnRequest(uint16_t cpId) {
    JsonDocument d;
    d["checkpoint_id"] = cpId;
    String s; serializeJson(d, s);
    publishRaw(TOPIC_RETURN_REQ_TX, s);
    Serial.printf("[MQTT->] %s %s\n", TOPIC_RETURN_REQ_TX, s.c_str());
}

// Announce ourselves to the backend so the dashboard creates the
// Robot row even before any mission starts.
//   - location:   last scanned CP id (empty = unknown, backend keeps prev)
//   - mode:       "auto" | "follow" | "follow_recovery" | "idle" (omit if empty)
//   - batteryPct: 0..100 percent, or <0 to omit (no battery sensor wired)
inline void publishHello(const String& location = "",
                         const String& mode = "",
                         int batteryPct = -1) {
    JsonDocument d;
    d["evt"] = "hello";
    d["id"]  = ROBOT_ID;
    d["fw"]  = __DATE__ " " __TIME__;
    if (location.length()) d["location"] = location;
    if (mode.length())     d["mode"]     = mode;
    if (batteryPct >= 0)   d["pct"]      = batteryPct;
    publishEvent(d);
}

} // namespace MqttCfg