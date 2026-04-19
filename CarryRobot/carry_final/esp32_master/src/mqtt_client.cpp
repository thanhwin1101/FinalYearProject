#include "mqtt_client.h"
#include "ota_manager.h"
#include "globals.h"
#include "relay_control.h"
#include "auto_mode.h"
#include "follow_mode.h"
#include "recovery_mode.h"
#include "huskylens_uart.h"
#include "servo_control.h"
#include "uart_protocol.h"
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
                // ─ reject if battery too low (≤30%) ────────────────────────────────────
                if (g_batteryPercent <= BATT_MIN_PERCENT) {
                    char rej[80];
                    snprintf(rej, sizeof(rej),
                             "{\"evt\":\"mission_rejected\",\"reason\":\"low_battery\",\"pct\":%u}",
                             (unsigned)g_batteryPercent);
                    mqtt.publish(T_EVT, rej);
                    Serial.printf("[MQTT] route rejected – battery %u%%\n", g_batteryPercent);
                    return;
                }
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
            // Switch to AUTO+RETURNING from any mode (recovery or auto-wait)
            if (g_mode == MODE_RECOVERY) g_mode = MODE_AUTO;
            autoModeActivateReturn();
            Serial.printf("[MQTT] return route: %d pts → AUTO_RETURNING\n", g_routeLen);
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

        // ─── tune_speed: set run/turn speed on STM32 ───────────────
        if (strcmp(action, "tune_speed") == 0) {
            if (doc.containsKey("runSpeed"))  g_tuneRunSpeed  = doc["runSpeed"].as<uint8_t>();
            if (doc.containsKey("turnSpeed")) g_tuneTurnSpeed = doc["turnSpeed"].as<uint8_t>();
            uint8_t payload[2] = { g_tuneRunSpeed, g_tuneTurnSpeed };
            uartSendFrame(Serial2, CMD_TUNE_SPEED, payload, 2);
            Serial.printf("[MQTT] tune_speed run=%u turn=%u\n", g_tuneRunSpeed, g_tuneTurnSpeed);
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

        // ─── direct_vel: send Vx/Vy/Vr to STM32 via UART ──────────
        if (strcmp(action, "direct_vel") == 0) {
            int16_t vx = doc["vx"] | (int16_t)0;
            int16_t vy = doc["vy"] | (int16_t)0;
            int16_t vr = doc["vr"] | (int16_t)0;
            uint8_t data[6];
            data[0] = (vx >> 8) & 0xFF; data[1] = vx & 0xFF;
            data[2] = (vy >> 8) & 0xFF; data[3] = vy & 0xFF;
            data[4] = (vr >> 8) & 0xFF; data[5] = vr & 0xFF;
            uartSendFrame(Serial2, CMD_DIRECT_VEL, data, 6);
            Serial.printf("[MQTT] direct_vel vx=%d vy=%d vr=%d\n", vx, vy, vr);
            return;
        }

        // ─── wheel_set: raw per-wheel PWM -255..255 for individual testing ─
        if (strcmp(action, "wheel_set") == 0) {
            int16_t fl = doc["fl"] | (int16_t)0;
            int16_t fr = doc["fr"] | (int16_t)0;
            int16_t bl = doc["bl"] | (int16_t)0;
            int16_t br = doc["br"] | (int16_t)0;
            uint8_t data[8];
            data[0] = (fl >> 8) & 0xFF; data[1] = fl & 0xFF;
            data[2] = (fr >> 8) & 0xFF; data[3] = fr & 0xFF;
            data[4] = (bl >> 8) & 0xFF; data[5] = bl & 0xFF;
            data[6] = (br >> 8) & 0xFF; data[7] = br & 0xFF;
            uartSendFrame(Serial2, CMD_WHEEL_SET, data, 8);
            Serial.printf("[MQTT] wheel_set fl=%d fr=%d bl=%d br=%d\n", fl, fr, bl, br);
            return;
        }

        // ─── servo_set: set servo Y angle ──────────────────────────
        if (strcmp(action, "servo_set") == 0) {
            if (doc.containsKey("y")) servoSetY(doc["y"].as<int>());
            Serial.printf("[MQTT] servo_set y=%d\n", servoGetY());
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"evt\":\"servo_ack\",\"y\":%d}", servoGetY());
            mqtt.publish(T_EVT, buf);
            return;
        }

        // ─── servo_center: reset servo Y to level ───────────────────
        if (strcmp(action, "servo_center") == 0) {
            servoSetY(SERVO_Y_LEVEL);
            Serial.println("[MQTT] servo_center");
            return;
        }

        // ─── ota_esp32: put ESP32 into OTA listen mode ─────────────
        // Payload: {"action":"ota_esp32"}
        // After this, upload new firmware via:  pio run -e ota -t upload
        if (strcmp(action, "ota_esp32") == 0) {
            Serial.println("[MQTT] OTA ESP32: ready — upload now via PlatformIO/IDE");
            mqtt.publish(T_EVT, "{\"evt\":\"ota_esp32_ready\"}");
            // otaInit() already called in setup; ArduinoOTA.handle() runs in otaLoop()
            // Nothing else to do — the next ota upload will be caught automatically.
            return;
        }

        // ─── ota_stm32: download .bin and flash STM32 via UART bootloader ──
        // Payload: {"action":"ota_stm32","url":"http://192.168.x.x:8080/slave.bin"}
        if (strcmp(action, "ota_stm32") == 0) {
            const char *url = doc["url"] | "";
            if (url[0] == '\0') {
                Serial.println("[MQTT] ota_stm32: missing url");
                mqtt.publish(T_EVT, "{\"evt\":\"ota_stm32_error\",\"msg\":\"missing url\"}");
                return;
            }
            Serial.printf("[MQTT] ota_stm32: flashing from %s\n", url);
            mqtt.publish(T_EVT, "{\"evt\":\"ota_stm32_start\"}");
            bool ok = otaDownloadAndFlashSTM32(url);
            if (ok) {
                mqtt.publish(T_EVT, "{\"evt\":\"ota_stm32_done\"}");
            } else {
                mqtt.publish(T_EVT, "{\"evt\":\"ota_stm32_error\",\"msg\":\"flash failed\"}");
            }
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
            // ─ reject if battery too low (≤30%) ─────────────────────────────────────
            if (g_batteryPercent <= BATT_MIN_PERCENT) {
                char rej[80];
                snprintf(rej, sizeof(rej),
                         "{\"evt\":\"mission_rejected\",\"reason\":\"low_battery\",\"pct\":%u}",
                         (unsigned)g_batteryPercent);
                mqtt.publish(T_EVT, rej);
                Serial.printf("[MQTT] mission rejected – battery %u%%\n", g_batteryPercent);
                return;
            }
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

    static const char *modeNames[] = { "auto", "follow", "recovery" };
    const char *modeName = (g_mode < 4) ? modeNames[g_mode] : "?";

    // Determine if robot is "running" (actively executing something)
    bool running = false;
    if (g_mode == MODE_AUTO) {
        running = (g_autoState == AUTO_RUNNING || g_autoState == AUTO_RETURNING);
    } else if (g_mode == MODE_FOLLOW) {
        running = true;
    }
    g_running = running;

    char buf[340];
    snprintf(buf, sizeof(buf),
        "{\"evt\":\"telemetry\",\"debug\":{"
        "\"battEsp\":%u,"
        "\"tofMm\":0,"
        "\"line\":%u,"
        "\"spinMs\":%u,"
        "\"brakeMs\":%u,"
        "\"wallCm\":%u,"
        "\"runSpeed\":%u,"
        "\"turnSpeed\":%u,"
        "\"mode\":\"%s\","
        "\"run\":%s,"
        "\"testDash\":%s,"
        "\"r1\":%d,\"r2\":%d,\"r3\":%d,"
        "\"servoY\":%d"
        "}}",
        g_batteryPercent,
        (unsigned)g_stm32LineBits,
        g_tuneSpinMs, g_tuneBrakeMs, g_tuneWallCm,
        (unsigned)g_tuneRunSpeed, (unsigned)g_tuneTurnSpeed,
        modeName,
        running ? "true" : "false",
        g_testDashboard ? "true" : "false",
        relayGetVision() ? 1 : 0,
        relayGetLine()   ? 1 : 0,
        relayGetNfc()    ? 1 : 0,
        servoGetY());
    mqtt.publish(T_EVT, buf);
}
