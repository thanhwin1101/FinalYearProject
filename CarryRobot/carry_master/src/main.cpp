#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

static unsigned long     btnDownMs      = 0;
static unsigned long     btnUpMs        = 0;
static unsigned long     lastClickReleaseMs = 0;
static bool              btnRawState    = false;
static bool              btnStableState = false;
static unsigned long     btnDebounceMs  = 0;
static bool              longPressFired = false;
static unsigned long     lastSwRawLogMs = 0;

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

static bool          wifiWasConnected = false;
static unsigned long lastWifiCheckMs = 0;
static unsigned long fastBeaconUntilMs = 0;
static unsigned long lastFastBeaconMs = 0;

static void startFastRelockBeaconWindow();

// ======================= FreeRTOS Tasks =======================
static TaskHandle_t hTaskComm    = nullptr;
static TaskHandle_t hTaskSensors = nullptr;
static TaskHandle_t hTaskLogic   = nullptr;

static void Task_Communication(void* arg);
static void Task_Sensors(void* arg);
static void Task_Logic(void* arg);

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

    if ((now - btnDebounceMs) < DEBOUNCE_MS) {
        return;
    }

    if (btnStableState == btnRawState) {

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

    btnStableState = btnRawState;

    if (btnStableState) {
        btnDownMs = now;
        longPressFired = false;
        Serial.println(F("[BTN] Pressed"));
#if MONITOR_SERIAL
        Serial.printf("[MON] t=%lu SW DOWN\n", now);
#endif
        return;
    }

    btnUpMs = now;
    const unsigned long held = btnUpMs - btnDownMs;
    Serial.printf("[BTN] Released after %lu ms\n", held);
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu SW UP held=%lu ms\n", now, held);
#endif

    if (longPressFired || held >= LONG_PRESS_MS) {

        longPressFired = false;
        return;
    }

    if (held < DEBOUNCE_MS) {
        return;
    }

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

static uint8_t lastProgressIndex = 0xFF;
static uint8_t lastProgressSegment = 0xFF;

static void processSlaveMsg() {
    if (!slaveMsgNew) return;
    slaveMsgNew = false;

    if (robotState == ST_MISSION_DELEGATED) {
        const uint8_t st = slaveMsg.missionStatus;
        if (st == 1) {
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
        } else if (st == 2) {
            mqttSendComplete(missionId.c_str());
            lastProgressIndex = 0xFF;
            lastProgressSegment = 0xFF;
            smSetWaitingAtDest(true);
        } else if (st == 3) {
            mqttSendReturned(missionId.c_str());
            lastProgressIndex = 0xFF;
            lastProgressSegment = 0xFF;
            smEnterIdle();
        }
    }

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

        if ((now - startMs) >= SYSTEM_READY_MAX_WAIT_MS && !allOk) {
            if (!slaveOk)
                Serial.println(F("[BOOT] Ready timeout but slave missing -> keep waiting"));

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

void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== Carry Robot – Master ESP32 ==="));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);

    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    espnowMasterInit();

    pinMode(SW_PIN, INPUT_PULLUP);
    pinMode(SERVO_X_FB_PIN, INPUT);

    Wire.begin(I2C_SDA, I2C_SCL);

    // Create I2C mutex before any OLED/ToF access
    g_i2cMutex = xSemaphoreCreateMutex();

    buzzerInit();
    displayInit();
    sensorsInit();
    gimbalInit();
    huskyInit();

    waitForSlaveBeforeWiFi();

    const bool requestPortal = shouldEnterWiFiPortalOnBoot();

    char mqttServerHost[32] = {0};
    loadSavedMqttServer(mqttServerHost, sizeof(mqttServerHost));
    mqttSetServerHost(mqttServerHost);
    Serial.printf("[MQTT] Preferred broker: %s\n", mqttGetServerHost());

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

    espnowMasterInit();
    if (wifiWasConnected) {

        startFastRelockBeaconWindow();
    }

    mqttInit();

    waitForSystemReady();

    smInit();

    buzzerStartup();
    Serial.println(F("=== Master setup complete ===\n"));

    // Start FreeRTOS tasks
    // Core allocation:
    //   Core 0: Communication (WiFi/MQTT/ESP-NOW)
    //   Core 1: Sensors + Logic (real-time)
    xTaskCreatePinnedToCore(Task_Communication, "Task_Comm", 8192, nullptr, 2, &hTaskComm, 0);
    xTaskCreatePinnedToCore(Task_Sensors,       "Task_Sensors", 4096, nullptr, 4, &hTaskSensors, 1);
    xTaskCreatePinnedToCore(Task_Logic,         "Task_Logic",   8192, nullptr, 3, &hTaskLogic,   1);
}

void loop() {
    // All work is done in FreeRTOS tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ================================================================
//  Task implementations
// ================================================================

// Core 0: WiFi + MQTT + ESP-NOW + telemetry
static void Task_Communication(void* arg) {
    (void)arg;

    TickType_t lastWake = xTaskGetTickCount();
    TickType_t lastEspNowTick = lastWake;
    TickType_t lastTelemetryTick = lastWake;

    for (;;) {
        // Keep MQTT alive and handle inbound commands
        mqttLoop();
        monitorWiFiReconnect();
        espnowMasterMaintainLink();
        sendFastRelockBeaconIfNeeded();

        // 50ms: sync state/vel to Slave (ESP-NOW)
        if ((xTaskGetTickCount() - lastEspNowTick) >= pdMS_TO_TICKS(50)) {
            lastEspNowTick = xTaskGetTickCount();
            masterMsg.state = (uint8_t)robotState;
            espnowSendToSlave(masterMsg);
        }

        // 1000ms: telemetry to MQTT
        if ((xTaskGetTickCount() - lastTelemetryTick) >= pdMS_TO_TICKS(1000)) {
            lastTelemetryTick = xTaskGetTickCount();
            mqttSendTelemetry(
                (robotState == ST_IDLE) ? "idle" : "busy",
                currentNodeIdLive.c_str(),
                destBed.c_str()
            );
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5));
    }
}

// Core 1: Read sensors every 50ms (ToF + 2x ultrasonic) into global cache
static void Task_Sensors(void* arg) {
    (void)arg;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        sensorsUpdateCache50ms();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(50));
    }
}

// Core 1: Real-time logic (button + slave msg + state machine + Husky maintain)
static void Task_Logic(void* arg) {
    (void)arg;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        processButton();
        processSlaveMsg();
        huskyMaintain();
        smUpdate();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
    }
}
