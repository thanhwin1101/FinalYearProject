/*  main.cpp  –  Master ESP32 entry point
 *
 *  Carry Robot – Dual-ESP32 architecture
 *  Master handles: HuskyLens, OLED, ToF, Ultrasonics, Servos,
 *                  Buzzer, Switch, WiFi/MQTT, ESP-NOW (→ Slave)
 */
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "globals.h"
#include "config.h"
#include "buzzer.h"
#include "display.h"
#include "sensors.h"
#include "servo_gimbal.h"
#include "huskylens_wrapper.h"
#include "espnow_comm.h"
#include "mqtt_comm.h"
#include "route_manager.h"
#include "state_machine.h"

// ─── Button debounce ───
static unsigned long     btnDownMs      = 0;
static unsigned long     btnUpMs        = 0;
static unsigned long     lastClickReleaseMs = 0;  // for double-click detection
static bool              btnRawState    = false;  // true = pressed
static bool              btnStableState = false;
static unsigned long     btnDebounceMs  = 0;
static bool              longPressFired = false;
static unsigned long     lastSwRawLogMs = 0;

// Logical "pressed": active-low => LOW is pressed, active-high => HIGH is pressed.
static inline bool swPinPressed(int rawLevel) {
    return (rawLevel == (SW_ACTIVE_LOW ? LOW : HIGH));
}

static bool shouldEnterWiFiPortalOnBoot() {
    if (!swPinPressed(digitalRead(SW_PIN))) return false;
    const unsigned long start = millis();
    while (millis() - start < LONG_PRESS_MS) {
        if (!swPinPressed(digitalRead(SW_PIN))) return false;
        delay(10);
    }
    return true;
}

static const char* PREF_NS = "carrycfg";
static const char* PREF_KEY_MQTT_SERVER = "mqtt_server";

static void loadSavedMqttServer(char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    snprintf(out, outSize, "%s", MQTT_DEFAULT_SERVER);

    Preferences prefs;
    if (!prefs.begin(PREF_NS, true)) return;

    String saved = prefs.getString(PREF_KEY_MQTT_SERVER, MQTT_DEFAULT_SERVER);
    prefs.end();

    saved.trim();
    if (saved.isEmpty()) return;
    snprintf(out, outSize, "%s", saved.c_str());
}

static void saveMqttServer(const char* host) {
    if (!host) return;
    String v(host);
    v.trim();
    if (v.isEmpty()) return;

    Preferences prefs;
    if (!prefs.begin(PREF_NS, false)) return;
    prefs.putString(PREF_KEY_MQTT_SERVER, v);
    prefs.end();
}

// WiFi reconnect watchdog + fast ESP-NOW beacon window for rapid slave relock.
static bool          wifiWasConnected = false;
static unsigned long lastWifiCheckMs = 0;
static unsigned long fastBeaconUntilMs = 0;
static unsigned long lastFastBeaconMs = 0;

static void startFastRelockBeaconWindow();

static void processButton() {
    const unsigned long now = millis();
    const int rawRead = digitalRead(SW_PIN);
    const bool rawState = swPinPressed(rawRead);

#if MONITOR_SERIAL
    if (now - lastSwRawLogMs >= 500) {
        lastSwRawLogMs = now;
        Serial.printf("[MON] t=%lu SW_RAW=%d pressed=%d\n", now, rawRead, rawState ? 1 : 0);
    }
#endif

    if (rawState != btnRawState) {
        btnRawState = rawState;
        btnDebounceMs = now;
    }

    // Wait until input is stable for debounce window.
    if ((now - btnDebounceMs) < DEBOUNCE_MS) {
        return;
    }

    // No stable edge.
    if (btnStableState == btnRawState) {
        // Fire long-press while still holding the button for better UX.
        if (btnStableState && !longPressFired && (now - btnDownMs) >= LONG_PRESS_MS) {
            longPressFired = true;
            Serial.println(F("[BTN] Long press"));
#if MONITOR_SERIAL
            Serial.printf("[MON] t=%lu SW LONG -> smOnLongPress\n", now);
#endif
            smOnLongPress();
        }
        return;
    }

    // Stable edge detected.
    btnStableState = btnRawState;

    if (btnStableState) {  // pressed
        btnDownMs = now;
        longPressFired = false;
        Serial.println(F("[BTN] Pressed"));
#if MONITOR_SERIAL
        Serial.printf("[MON] t=%lu SW DOWN\n", now);
#endif
        return;
    }

    // Released edge.
    btnUpMs = now;
    const unsigned long held = btnUpMs - btnDownMs;
    Serial.printf("[BTN] Released after %lu ms\n", held);
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu SW UP held=%lu ms\n", now, held);
#endif

    if (longPressFired || held >= LONG_PRESS_MS) {
        // Already handled (or safety net if hold threshold hit exactly on release).
        longPressFired = false;
        return;
    }

    if (held < DEBOUNCE_MS) {
        return;
    }

    // Double-click: second release within DOUBLE_CLICK_MS of previous release
    const bool isDoubleClick = (now - lastClickReleaseMs) <= DOUBLE_CLICK_MS;
    lastClickReleaseMs = now;

    if (isDoubleClick) {
        Serial.println(F("[BTN] Double click"));
#if MONITOR_SERIAL
        Serial.printf("[MON] t=%lu -> DOUBLE -> smOnDoubleClick\n", now);
#endif
        smOnDoubleClick();
    } else {
        Serial.println(F("[BTN] Single click"));
#if MONITOR_SERIAL
        Serial.printf("[MON] t=%lu -> SINGLE -> smOnSingleClick\n", now);
#endif
        smOnSingleClick();
    }
}

// ─── Process incoming Slave message ───
static uint8_t lastProgressIndex = 0xFF;
static uint8_t lastProgressSegment = 0xFF;

static void processSlaveMsg() {
    if (!slaveMsgNew) return;
    slaveMsgNew = false;

    // Relay Slave autonomous mission status to MQTT
    if (robotState == ST_MISSION_DELEGATED) {
        const uint8_t st = slaveMsg.missionStatus;
        if (st == 1) {  // ongoing – throttle: only send progress when index or segment changes
            if (slaveMsg.routeIndex != lastProgressIndex || slaveMsg.routeSegment != lastProgressSegment) {
                lastProgressIndex = slaveMsg.routeIndex;
                lastProgressSegment = slaveMsg.routeSegment;
                const char* nodeId = "???";
                if (slaveMsg.routeSegment == 0 && slaveMsg.routeIndex < outRouteLen) {
                    nodeId = outRoute[slaveMsg.routeIndex].nodeId.c_str();
                } else if (slaveMsg.routeSegment == 1 && slaveMsg.routeIndex < retRouteLen) {
                    nodeId = retRoute[slaveMsg.routeIndex].nodeId.c_str();
                }
                mqttSendProgress(missionId.c_str(), nodeId, slaveMsg.routeIndex, slaveMsg.routeTotal);
            }
        } else if (st == 2) {  // complete at dest
            mqttSendComplete(missionId.c_str());
            lastProgressIndex = 0xFF;
            lastProgressSegment = 0xFF;
            smSetWaitingAtDest(true);  // wait for SW to start return
        } else if (st == 3) {  // back at MED
            mqttSendReturned(missionId.c_str());
            lastProgressIndex = 0xFF;
            lastProgressSegment = 0xFF;
            smEnterIdle();
        }
    }

    // RFID / sync (for non-delegated states)
    if (robotState != ST_MISSION_DELEGATED) {
        if (slaveMsg.rfid_new && slaveMsg.rfid_uid[0] != '\0') {
            smOnSlaveRfid((const char*)slaveMsg.rfid_uid);
        }
        if (slaveMsg.sync_docking) {
            smOnSlaveSyncDocking();
        }
    }
}

static void startFastRelockBeaconWindow() {
    fastBeaconUntilMs = millis() + ESPNOW_RELOCK_BEACON_MS;
    Serial.printf("[ESPNOW] Fast beacon enabled for %u ms\n", (unsigned)ESPNOW_RELOCK_BEACON_MS);
}

static void monitorWiFiReconnect() {
    const unsigned long now = millis();
    if (now - lastWifiCheckMs < WIFI_LINK_CHECK_INTERVAL_MS) return;
    lastWifiCheckMs = now;

    const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected && !wifiWasConnected) {
        Serial.printf("[WIFI] Reconnected - IP %s  CH %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
        startFastRelockBeaconWindow();
    }
    wifiWasConnected = wifiConnected;
}

static void sendFastRelockBeaconIfNeeded() {
    const unsigned long now = millis();
    if (now >= fastBeaconUntilMs) return;
    if (now - lastFastBeaconMs < ESPNOW_RELOCK_BEACON_INTERVAL_MS) return;

    lastFastBeaconMs = now;
    masterMsg.state = (uint8_t)robotState;
    espnowSendToSlave(masterMsg);
}

static void waitForSlaveBeforeWiFi() {
    displayCentered("SLAVE LINK", "Connecting...", "Waiting packet");
    const unsigned long startMs = millis();
    unsigned long lastProbeMs = 0;

    while (millis() - startMs < ESPNOW_BOOT_WAIT_MS) {
        if (millis() - lastProbeMs >= ESPNOW_TX_INTERVAL) {
            lastProbeMs = millis();
            masterMsg.state = (uint8_t)robotState;
            espnowSendToSlave(masterMsg);
        }

        if (espnowSlaveConnected()) {
            displayCentered("SLAVE LINK", "Connected", "Starting WiFi...");
            Serial.println(F("[ESPNOW] Slave linked before WiFi"));
            delay(350);
            return;
        }
        delay(10);
    }

    displayCentered("SLAVE LINK", "Timeout", "Starting WiFi...");
    Serial.println(F("[ESPNOW] Slave wait timeout - continue to WiFi"));
    delay(350);
}

static void waitForSystemReady() {
    unsigned long stableSinceMs = 0;
    unsigned long lastOledUpdateMs = 0;
    unsigned long lastProbeMs = 0;
    const unsigned long startMs = millis();

    while (true) {
        const unsigned long now = millis();

        mqttLoop();
        monitorWiFiReconnect();
        espnowMasterMaintainLink();
        huskyMaintain();

        if (now - lastProbeMs >= ESPNOW_TX_INTERVAL) {
            lastProbeMs = now;
            masterMsg.state = (uint8_t)robotState;
            espnowSendToSlave(masterMsg);
        }

        const bool wifiOk = (WiFi.status() == WL_CONNECTED);
        const bool mqttOk = mqttConnected();
        const bool slaveOk = espnowSlaveConnected();
        const bool allOk = wifiOk && mqttOk && slaveOk;

        // Chỉ thoát khi đủ cả 3: WiFi + MQTT + Slave ổn định SYSTEM_READY_STABLE_MS.
        // Không thoát sớm khi timeout với "partial links" (chỉ Slave) -> không vào IDLE thiếu link.
        if ((now - startMs) >= SYSTEM_READY_MAX_WAIT_MS && !allOk) {
            if (!slaveOk)
                Serial.println(F("[BOOT] Ready timeout but slave missing -> keep waiting"));
            // Nếu thiếu WiFi/MQTT: vẫn chờ, không return -> không vào màn hình IDLE.
        }

        if (allOk) {
            if (stableSinceMs == 0) stableSinceMs = now;
            if ((now - stableSinceMs) >= SYSTEM_READY_STABLE_MS) {
                displayCentered("SYSTEM READY", "All links stable", "Entering IDLE...");
                Serial.println(F("[BOOT] WiFi+MQTT+Slave stable -> entering IDLE"));
                delay(350);
                return;
            }
        } else {
            if (stableSinceMs != 0) {
                Serial.println(F("[BOOT] Link unstable, reset stability timer"));
            }
            stableSinceMs = 0;
        }

        if (now - lastOledUpdateMs >= OLED_INTERVAL) {
            lastOledUpdateMs = now;
            uint16_t stableLeftMs = SYSTEM_READY_STABLE_MS;
            if (allOk) {
                const unsigned long stableFor = now - stableSinceMs;
                stableLeftMs = (stableFor >= SYSTEM_READY_STABLE_MS)
                                   ? 0
                                   : (uint16_t)(SYSTEM_READY_STABLE_MS - stableFor);
            }
            displayBootChecklist(wifiOk, mqttOk, slaveOk, stableLeftMs);
        }

        delay(5);
    }
}

// ================================================================
//  setup()
// ================================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== Carry Robot – Master ESP32 ==="));

    // ESP-NOW requires WiFi radio to be initialized first.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    // Use fixed channel 7 before connecting to AP so Slave (on channel 7) can link.
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // ESP-NOW first: initialize radio/link as early as possible.
    espnowMasterInit();

    // GPIO
    pinMode(SW_PIN, INPUT_PULLUP);
    pinMode(SERVO_X_FB_PIN, INPUT);

    // I2C
    Wire.begin(I2C_SDA, I2C_SCL);

    // Hardware init
    buzzerInit();
    displayInit();
    sensorsInit();
    gimbalInit();
    huskyInit();

    // Wait for slave packet before WiFi setup.
    waitForSlaveBeforeWiFi();

    const bool requestPortal = shouldEnterWiFiPortalOnBoot();

    char mqttServerHost[32] = {0};
    loadSavedMqttServer(mqttServerHost, sizeof(mqttServerHost));
    mqttSetServerHost(mqttServerHost);
    Serial.printf("[MQTT] Preferred broker: %s\n", mqttGetServerHost());

    // WiFi (for MQTT)
    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
    wm.setConnectTimeout(25);
    WiFiManagerParameter pMqttServer(
        "mqtt_server",
        "MQTT Server/IP",
        mqttServerHost,
        sizeof(mqttServerHost) - 1
    );
    wm.addParameter(&pMqttServer);
    bool wifiOk = false;

    if (requestPortal) {
        buzzerBeep(100);
        delay(80);
        buzzerBeep(100);
        displayWiFiSetup();
        wifiOk = wm.startConfigPortal(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);

        if (wifiOk) {
            String newHost = pMqttServer.getValue();
            newHost.trim();
            if (!newHost.isEmpty()) {
                saveMqttServer(newHost.c_str());
                mqttSetServerHost(newHost.c_str());
                Serial.printf("[MQTT] Saved broker: %s\n", mqttGetServerHost());
            }
        }
    } else {
        // Normal boot: connect using default WiFi credentials.
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_DEFAULT_SSID, WIFI_DEFAULT_PASS);
        const unsigned long startMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 15000) {
            delay(100);
        }
        wifiOk = (WiFi.status() == WL_CONNECTED);
    }

    if (wifiOk) {
        displayConnected(WiFi.localIP().toString().c_str());
        buzzerBeep(70);
        Serial.printf("[WIFI] Connected – IP %s  CH %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
        wifiWasConnected = true;
    } else {
        Serial.println(F("[WIFI] Failed – continuing offline"));
        wifiWasConnected = false;
    }

    // Rebind ESP-NOW after WiFi init so peer follows current channel.
    espnowMasterInit();
    if (wifiWasConnected) {
        // Speed up initial slave relock at boot.
        startFastRelockBeaconWindow();
    }

    // MQTT
    mqttInit();

    // Block here until WiFi/MQTT/Slave are all stable.
    waitForSystemReady();

    // State machine
    smInit();

    buzzerStartup();
    Serial.println(F("=== Master setup complete ===\n"));
}

// ================================================================
//  loop()
// ================================================================
void loop() {
    // 1. MQTT
    mqttLoop();

    // 1.1 WiFi reconnect detection
    monitorWiFiReconnect();
    espnowMasterMaintainLink();

    // 2. Button
    processButton();

    // 3. Process Slave data
    processSlaveMsg();

    // 4. State machine update (includes obstacle, HuskyLens, drive logic)
    smUpdate();

    // 4.1 Temporary high-rate ESP-NOW beacon after WiFi reconnect.
    sendFastRelockBeaconIfNeeded();

    // 5. Periodic telemetry
    if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL) {
        lastTelemetryMs = millis();
        mqttSendTelemetry(
            (robotState == ST_IDLE) ? "idle" : "busy",
            currentNodeIdLive.c_str(),
            destBed.c_str()
        );
    }

    delay(2);   // yield
}
