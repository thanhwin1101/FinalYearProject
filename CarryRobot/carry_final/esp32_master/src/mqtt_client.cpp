#include "mqtt_client.h"
#include "globals.h"
#include "relay_control.h"
#include "sr05.h"
#include "auto_mode.h"
#include "follow_mode.h"
#include "recovery_mode.h"
#include "huskylens_uart.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ── internal state ──────────────────────────────────────────────────
static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static char         s_server[64]  = MQTT_DEFAULT_SERVER;
static uint16_t     s_port        = MQTT_DEFAULT_PORT;
static char         s_user[32]    = MQTT_DEFAULT_USER;
static char         s_pass[32]    = MQTT_DEFAULT_PASS;
static uint32_t     s_lastTry     = 0;

// ── topics — khớp backend mqttService.js carry stack bridge ─────────
// ESP32 publishes events → backend subscribes
static const char *T_EVT         = "carry/robot/evt";
// ESP32 publishes return request → backend subscribes
static const char *T_RETURN_REQ  = "robot/return_request";
// Backend publishes commands → ESP32 subscribes
static const char *T_CMD         = "carry/robot/cmd";

// ── helper: convert "XX:XX:XX:XX" rfidUid → uint16 (last 2 bytes) ──
static uint16_t uidStringToId(const char *uid) {
    // Parse colon-separated hex, take last 2 bytes
    uint8_t bytes[7];
    uint8_t count = 0;
    const char *p = uid;
    while (*p && count < 7) {
        bytes[count++] = (uint8_t)strtol(p, nullptr, 16);
        p = strchr(p, ':');
        if (!p) break;
        p++;
    }
    if (count < 2) return 0;
    return ((uint16_t)bytes[count - 2] << 8) | bytes[count - 1];
}

// ── parse CMD from backend ──────────────────────────────────────────
// Backend sends on "carry/robot/cmd":
//   Route assign:  {"action":"route","missionId":"...","patient":"...","destination":"...","ids":[0x8083,...]}
//   Return route:  {"action":"return_route","ids":[0x8083,...]}
//   Cancel:        {"action":"cancel"}
//   Mission assign (full): {"mission":{"missionId":"...","patientName":"...","bedId":"...","outboundRoute":[{"rfidUid":"XX:XX:XX:XX","action":"F",...}],...}}
static void parseCmdMsg(const uint8_t *payload, unsigned int len) {
    static StaticJsonDocument<MQTT_BUFFER_SIZE> doc;   // static: avoid 4KB stack alloc
    doc.clear();
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) { Serial.printf("[MQTT] JSON err: %s\n", err.c_str()); return; }

    // ── Format 1: simple carry stack {"action":"route","ids":[...]} ──
    const char *action = doc["action"] | (const char*)nullptr;
    if (action) {
        if (strcmp(action, "route") == 0 || strcmp(action, "assign") == 0) {
            strlcpy(g_patientName, doc["patient"]    | "", sizeof(g_patientName));
            strlcpy(g_destination, doc["destination"] | "", sizeof(g_destination));
            strlcpy(g_missionId,   doc["missionId"]   | "", sizeof(g_missionId));
            JsonArray ids = doc["ids"];
            g_routeLen = 0;
            for (JsonVariant v : ids) {
                if (g_routeLen >= MAX_ROUTE_LEN) break;
                g_route[g_routeLen].checkpointId = v.as<uint16_t>();
                g_route[g_routeLen].action = 'F';
                g_routeLen++;
            }
            g_routeIdx = 0;
            if (g_mode == MODE_AUTO) {
                if (g_autoState == AUTO_RUNNING) {
                    g_mqttCancel = true;
                    Serial.println("[MQTT] new route while running → cancel old");
                }
                g_autoState = AUTO_WAIT_START;
            }
            Serial.printf("[MQTT] route (ids): %d pts\n", g_routeLen);
            // publish accept
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"evt\":\"route_accept\",\"n\":%u}", g_routeLen);
            mqtt.publish(T_EVT, buf);
            return;
        }
        if (strcmp(action, "return_route") == 0) {
            JsonArray ids = doc["ids"];
            g_routeLen = 0;
            for (JsonVariant v : ids) {
                if (g_routeLen >= MAX_ROUTE_LEN) break;
                g_route[g_routeLen].checkpointId = v.as<uint16_t>();
                g_route[g_routeLen].action = 'F';
                g_routeLen++;
            }
            g_routeIdx = 0;
            if (g_mode == MODE_AUTO) g_autoState = AUTO_RETURNING;
            Serial.printf("[MQTT] return route (ids): %d pts\n", g_routeLen);
            return;
        }
        if (strcmp(action, "cancel") == 0) {
            if (g_mode == MODE_AUTO &&
                (g_autoState == AUTO_RUNNING || g_autoState == AUTO_WAIT_START)) {
                g_mqttCancel = true;
            }
            Serial.println("[MQTT] cancel requested");
            return;
        }

        // ─── start: simulate button press to begin mission ──────────
        if (strcmp(action, "start") == 0) {
            g_btnSingleClick = true;
            Serial.println("[MQTT] start → simulating button");
            return;
        }

        // ─── stop: emergency stop ───────────────────────────────────
        if (strcmp(action, "stop") == 0) {
            g_stopped = true;
            g_running = false;
            Serial.println("[MQTT] stop");
            return;
        }

        // ─── resume: clear stop flag ────────────────────────────────
        if (strcmp(action, "resume") == 0) {
            g_stopped = false;
            Serial.println("[MQTT] resume");
            return;
        }

        // ─── set_mode: set mode + flag, main loop handles init ─────
        if (strcmp(action, "set_mode") == 0) {
            const char *m = doc["mode"] | "";
            if (strcmp(m, "auto") == 0)          g_mode = MODE_AUTO;
            else if (strcmp(m, "follow") == 0)   g_mode = MODE_FOLLOW;
            else if (strcmp(m, "find") == 0)     g_mode = MODE_FIND;
            else if (strcmp(m, "recovery") == 0) g_mode = MODE_RECOVERY;
            g_modeChangeReq = true;   // main loop sẽ gọi init + relay
            Serial.printf("[MQTT] set_mode → %s\n", m);
            return;
        }

        // ─── tune_turn: adjust turn parameters ─────────────────────
        if (strcmp(action, "tune_turn") == 0) {
            if (doc.containsKey("spinMs"))  g_tuneSpinMs  = doc["spinMs"].as<uint16_t>();
            if (doc.containsKey("brakeMs")) g_tuneBrakeMs = doc["brakeMs"].as<uint16_t>();
            if (doc.containsKey("wallCm"))  g_tuneWallCm  = doc["wallCm"].as<uint16_t>();
            Serial.printf("[MQTT] tune_turn spin=%u brake=%u wall=%u\n",
                          g_tuneSpinMs, g_tuneBrakeMs, g_tuneWallCm);
            return;
        }

        // ─── test_dashboard: toggle OLED test view ──────────────────
        if (strcmp(action, "test_dashboard") == 0) {
            g_testDashboard = doc["enabled"] | false;
            Serial.printf("[MQTT] test_dashboard %s\n", g_testDashboard ? "ON" : "OFF");
            return;
        }

        // ─── relay: manual relay control ────────────────────────────
        if (strcmp(action, "relay") == 0) {
            const char *which = doc["which"] | "";
            bool on = doc["on"] | false;
            if (strcmp(which, "vision") == 0) { on ? relayVisionOn() : relayVisionOff(); }
            else if (strcmp(which, "line") == 0) { on ? relayLineOn() : relayLineOff(); }
            else if (strcmp(which, "nfc") == 0)  { on ? relayNfcOn()  : relayNfcOff();  }
            Serial.printf("[MQTT] relay %s → %s\n", which, on ? "ON" : "OFF");
            // ack back
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"evt\":\"relay_ack\",\"which\":\"%s\",\"on\":%s}",
                     which, on ? "true" : "false");
            mqtt.publish(T_EVT, buf);
            return;
        }

        // ─── relay_resume: restore relays per current mode ──────────
        if (strcmp(action, "relay_resume") == 0) {
            if (g_mode == MODE_AUTO)        relaySetAuto();
            else if (g_mode == MODE_FOLLOW) { relaySetFollow(); huskyReconnect(); }
            else                            { relaySetRecovery(); huskyReconnect(); }
            Serial.println("[MQTT] relay_resume + module reinit");
            mqtt.publish(T_EVT, "{\"evt\":\"relay_resume\"}");
            return;
        }

        // ─── status: publish current state ──────────────────────────
        if (strcmp(action, "status") == 0) {
            mqttPublishTelemetry();
            Serial.println("[MQTT] status requested");
            return;
        }
    }

    // ── Format 2: full mission assign from dashboard ────────────────
    // {"mission":{"missionId":"...","patientName":"...","bedId":"R1M1",
    //   "outboundRoute":[{"nodeId":"MED","rfidUid":"45:54:80:83","action":"F",...},...]}}
    JsonObject mission = doc["mission"];
    if (!mission.isNull()) {
        strlcpy(g_missionId,   mission["missionId"]   | "", sizeof(g_missionId));
        strlcpy(g_patientName, mission["patientName"]  | "", sizeof(g_patientName));
        strlcpy(g_destination, mission["bedId"]        | "", sizeof(g_destination));

        JsonArray outRoute = mission["outboundRoute"];
        g_routeLen = 0;
        for (JsonObject p : outRoute) {
            if (g_routeLen >= MAX_ROUTE_LEN) break;
            const char *uid = p["rfidUid"] | (const char*)nullptr;
            if (uid) {
                g_route[g_routeLen].checkpointId = uidStringToId(uid);
            } else {
                g_route[g_routeLen].checkpointId = p["id"] | 0;
            }
            const char *act = p["action"] | "F";
            g_route[g_routeLen].action = act[0];
            g_routeLen++;
        }
        g_routeIdx = 0;

        if (g_mode == MODE_AUTO) {
            if (g_autoState == AUTO_RUNNING) {
                g_mqttCancel = true;
                Serial.println("[MQTT] new mission while running → cancel old");
            }
            g_autoState = AUTO_WAIT_START;
        }
        Serial.printf("[MQTT] mission: %d pts  patient=%s  bed=%s\n",
                      g_routeLen, g_patientName, g_destination);

        char buf[48];
        snprintf(buf, sizeof(buf), "{\"evt\":\"route_accept\",\"n\":%u}", g_routeLen);
        mqtt.publish(T_EVT, buf);
        return;
    }

    Serial.printf("[MQTT] unhandled cmd: %.*s\n", min(len, 120u), payload);
}

// ── MQTT callback ───────────────────────────────────────────────────
static void callback(char *topic, byte *payload, unsigned int len) {
    Serial.printf("[MQTT] << %s (%u B)\n", topic, len);
    if (strcmp(topic, T_CMD) == 0) {
        parseCmdMsg(payload, len);
    }
}

// ── connect / reconnect ─────────────────────────────────────────────
static bool reconnect() {
    if (mqtt.connected()) return true;
    uint32_t now = millis();
    if (now - s_lastTry < MQTT_RECONNECT_MS) return false;
    s_lastTry = now;

    char clientId[32];
    snprintf(clientId, sizeof(clientId), "robot-%lu", (unsigned long)now);
    Serial.printf("[MQTT] connecting %s:%d …\n", s_server, s_port);

    if (mqtt.connect(clientId, s_user, s_pass)) {
        Serial.println("[MQTT] connected");
        mqtt.subscribe(T_CMD, 1);
        g_mqttConnected = true;
        return true;
    }
    Serial.printf("[MQTT] failed rc=%d\n", mqtt.state());
    g_mqttConnected = false;
    return false;
}

// ── public API ──────────────────────────────────────────────────────
void mqttInit() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    strlcpy(s_server, prefs.getString("mqtt_srv",  MQTT_DEFAULT_SERVER).c_str(), sizeof(s_server));
    s_port = prefs.getUShort("mqtt_port", MQTT_DEFAULT_PORT);
    strlcpy(s_user,   prefs.getString("mqtt_user", MQTT_DEFAULT_USER).c_str(),   sizeof(s_user));
    strlcpy(s_pass,   prefs.getString("mqtt_pass", MQTT_DEFAULT_PASS).c_str(),   sizeof(s_pass));
    prefs.end();

    mqtt.setServer(s_server, s_port);
    mqtt.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt.setCallback(callback);
}

void mqttLoop() {
    if (!mqtt.connected()) reconnect();
    mqtt.loop();
}

bool mqttIsConnected() { return mqtt.connected(); }

// ── publish events to backend (carry/robot/evt) ─────────────────────
// Format: {"evt":"checkpoint","id":32899}
void mqttPublishCheckpoint(uint16_t cpId) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"evt\":\"checkpoint\",\"id\":%u}", cpId);
    mqtt.publish(T_EVT, buf);
}

// Format: {"evt":"idle_scan","id":32899}  — idle NFC scan (no status change)
void mqttPublishIdleScan(uint16_t cpId) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"evt\":\"idle_scan\",\"id\":%u}", cpId);
    mqtt.publish(T_EVT, buf);
}

// Format: {"evt":"battery","pct":85}
void mqttPublishBattery(uint8_t pct) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"evt\":\"battery\",\"pct\":%u}", pct);
    mqtt.publish(T_EVT, buf);
}

// Format: {"checkpoint_id":32899}  on topic "robot/return_request"
void mqttPublishReturnRequest(uint16_t cpId) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"checkpoint_id\":%u}", cpId);
    mqtt.publish(T_RETURN_REQ, buf);
}

// Format: {"evt":"..."}
void mqttPublishStatus(const char *status) {
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"evt\":\"%s\"}", status);
    mqtt.publish(T_EVT, buf);
}

// Format: {"evt":"mission_done"}
void mqttPublishMissionDone(const char *missionId, bool success) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"evt\":\"mission_done\",\"mission\":\"%s\",\"success\":%s}",
             missionId, success ? "true" : "false");
    mqtt.publish(T_EVT, buf);
}

// ── generic sensor/system event ─────────────────────────────────────
void mqttPublishEvent(const char *evt) {
    if (!mqtt.connected()) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"evt\":\"%s\"}", evt);
    mqtt.publish(T_EVT, buf);
}

// Format: {"evt":"telemetry","debug":{...}} — periodic sensor snapshot for test lab
void mqttPublishTelemetry() {
    if (!mqtt.connected()) return;

    static const char *modeNames[] = { "auto", "follow", "find", "recovery" };
    const char *modeName = (g_mode < 4) ? modeNames[g_mode] : "?";

    // Determine if robot is "running" (actively executing something)
    bool running = false;
    if (g_mode == MODE_AUTO) {
        running = (g_autoState == AUTO_RUNNING || g_autoState == AUTO_RETURNING);
    } else if (g_mode == MODE_FOLLOW || g_mode == MODE_FIND) {
        running = true;
    }
    g_running = running;

    // Read SR05 distances (cached or live - they're cheap on ESP32 side)
    float sr05L = sr05ReadLeft();
    float sr05R = sr05ReadRight();

    char buf[300];
    snprintf(buf, sizeof(buf),
        "{\"evt\":\"telemetry\",\"debug\":{"
        "\"battEsp\":%u,"
        "\"tofMm\":0,"
        "\"sr05L\":%.0f,"
        "\"sr05R\":%.0f,"
        "\"line\":0,"
        "\"spinMs\":%u,"
        "\"brakeMs\":%u,"
        "\"wallCm\":%u,"
        "\"mode\":\"%s\","
        "\"run\":%s,"
        "\"testDash\":%s,"
        "\"r1\":%d,\"r2\":%d,\"r3\":%d"
        "}}",
        g_batteryPercent,
        sr05L, sr05R,
        g_tuneSpinMs, g_tuneBrakeMs, g_tuneWallCm,
        modeName,
        running ? "true" : "false",
        g_testDashboard ? "true" : "false",
        relayGetVision() ? 1 : 0,
        relayGetLine()   ? 1 : 0,
        relayGetNfc()    ? 1 : 0);
    mqtt.publish(T_EVT, buf);
}
