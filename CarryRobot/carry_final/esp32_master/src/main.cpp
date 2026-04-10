// ====================================================================
//  carry_final  –  ESP32 Master  –  main.cpp
//  WiFiManager portal, MQTT, UART↔STM32, mode state machine
// ====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>

#include "config.h"
#include "globals.h"
#include "uart_protocol.h"
#include "relay_control.h"
#include "buzzer.h"
#include "battery.h"
#include "button_handler.h"
#include "oled_display.h"
#include "huskylens_uart.h"
#include "servo_control.h"
#include "sr05.h"
#include "mqtt_client.h"
#include "auto_mode.h"
#include "follow_mode.h"
#include "find_mode.h"
#include "recovery_mode.h"

// ── Hardware serial ports ───────────────────────────────────────────
// STM32: dùng Serial2 toàn project (auto/follow/find/recovery) — tránh hai đối tượng UART2.
HardwareSerial SerialHusky(1);     // UART1  → HuskyLens

// ── WiFiManager: MQTT-only portal (keep WiFi, re-enter MQTT IP) ─────
static void startMqttPortal() {
    Serial.println("[BOOT] MQTT failed – opening portal to update MQTT IP");
    oledPortal(WM_AP_NAME, "192.168.4.1");
    buzzerBeepN(2, 150, 80);

    char savedSrv[64] = MQTT_DEFAULT_SERVER;
    {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        strlcpy(savedSrv, prefs.getString("mqtt_srv", MQTT_DEFAULT_SERVER).c_str(), sizeof(savedSrv));
        prefs.end();
    }

    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setConfigPortalTimeout(WM_PORTAL_TIMEOUT);   // 0 = infinite
    WiFiManagerParameter paramSrv("mqtt_srv", "MQTT Server IP", savedSrv, 63);
    wm.addParameter(&paramSrv);

    // startConfigPortal WITHOUT resetSettings → WiFi stays connected
    wm.startConfigPortal(WM_AP_NAME, WM_AP_PASS);

    // Save whatever was entered (even if unchanged)
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("mqtt_srv", paramSrv.getValue());
    prefs.end();
    Serial.printf("[WM] MQTT IP saved: %s – restarting\n", paramSrv.getValue());
    ESP.restart();
}

// ── WiFiManager: autoConnect or force portal ────────────────────────
// forcePortal=false → autoConnect (uses saved WiFi creds or shows portal)
// forcePortal=true  → always open portal (long press);
static void startPortal(bool forcePortal) {
    oledPortal(WM_AP_NAME, "192.168.4.1");

    char savedSrv[64] = MQTT_DEFAULT_SERVER;
    {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        strlcpy(savedSrv, prefs.getString("mqtt_srv", MQTT_DEFAULT_SERVER).c_str(), sizeof(savedSrv));
        prefs.end();
    }

    WiFiManager wm;
    wm.setDebugOutput(true);
    wm.setConfigPortalTimeout(WM_PORTAL_TIMEOUT);   // 0 = infinite

    // Chỉ 1 field: IP của MQTT broker
    WiFiManagerParameter paramSrv("mqtt_srv", "MQTT Server IP", savedSrv, 63);
    wm.addParameter(&paramSrv);

    bool connected;
    if (forcePortal) {
        // Xoá WiFi cũ → bắt buộc hiện portal
        wm.resetSettings();
        connected = wm.startConfigPortal(WM_AP_NAME, WM_AP_PASS);
    } else {
        // Tự kết nối nếu đã có creds; nếu chưa → hiện portal
        connected = wm.autoConnect(WM_AP_NAME, WM_AP_PASS);
    }

    if (connected) {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putString("mqtt_srv", paramSrv.getValue());
        prefs.end();
        Serial.printf("[WM] WiFi OK – MQTT srv saved: %s\n", paramSrv.getValue());
        if (forcePortal) ESP.restart();   // restart after forced re-config
        // autoConnect: tiếp tục setup bình thường
    } else {
        Serial.println("[WM] portal timeout – restarting");
        ESP.restart();
    }
}

// ── Process frames from STM32 ───────────────────────────────────────
static void handleSTM32() {
    uint8_t cmd, buf[UART_MAX_FRAME], len;
    while (uartReceiveFrame(Serial2, cmd, buf, len)) {
        switch (cmd) {
        case CMD_BATTERY:
            // tạm tắt – luôn giữ 100%
            // if (len >= 1) g_batteryPercent = buf[0];
            break;

        case CMD_CHECKPOINT:
            if (len >= 2) {
                g_lastCheckpointId = ((uint16_t)buf[0] << 8) | buf[1];
                g_newCheckpoint = true;
                Serial.printf("[UART] <<< CHECKPOINT 0x%04X (%u)\n",
                              g_lastCheckpointId, g_lastCheckpointId);
            }
            break;

        case CMD_OBSTACLE:
            g_stm32Obstacle = true;
            buzzerBeep(600);  // obstacle warning
            break;

        case CMD_ACK:
            // acknowledged – no action needed
            break;

        case CMD_MISSION_DONE:
            g_stm32MissionDone = true;
            g_stm32Obstacle    = false;
            break;

        case CMD_MISMATCH:
            if (len >= 4) {
                g_stm32MismatchGot  = ((uint16_t)buf[0] << 8) | buf[1];
                g_stm32MismatchExp  = ((uint16_t)buf[2] << 8) | buf[3];
                g_stm32MismatchFlag = true;
            }
            break;

        case CMD_DEBUG_MSG:
            if (len > 0) {
                buf[len] = '\0';   // null-terminate
                Serial.printf("[STM32] %s\n", (char*)buf);
            }
            break;

        case CMD_LINE_LOST:
            Serial.println("[STM32] line lost");
            mqttPublishEvent("line_lost");
            break;

        default:
            Serial.printf("[UART] unknown cmd 0x%02X\n", cmd);
        }
    }
}

// ── Mode switching (double click at MED + IDLE) ─────────────────────
static void checkModeSwitch() {
    if (!g_btnDoubleClick) return;

    if (g_mode == MODE_AUTO && g_autoState == AUTO_IDLE
        && g_lastCheckpointId == MED_CHECKPOINT_ID) {
        // Auto → Follow (only when at MED and IDLE)
        g_btnDoubleClick = false;
        g_mode = MODE_FOLLOW;
        followModeInit();
        Serial.println("[MODE] AUTO → FOLLOW (at MED)");
    } else if (g_mode == MODE_AUTO && g_autoState == AUTO_IDLE) {
        g_btnDoubleClick = false;
        buzzerBeepN(2);  // reject: not at MED
        Serial.println("[MODE] switch rejected – not at MED");
    }
    // Follow/Find → Recovery is handled inside their own loops
}

// ── Periodic tasks ──────────────────────────────────────────────────
// static uint32_t lastBatt = 0;   // tạm tắt battery
static uint32_t lastTelem = 0;
static uint32_t lastDebug = 0;

static void periodicTasks() {
    uint32_t now = millis();

    // tạm tắt battery read
    // if (now - lastBatt >= BATTERY_READ_MS) {
    //     lastBatt = now;
    //     batteryRead();
    // }

    if (now - lastTelem >= TELEMETRY_MS) {
        lastTelem = now;
        mqttPublishBattery(g_batteryPercent);
    }

    // debug telemetry for Robot test lab — every 1s
    if (now - lastDebug >= 1000) {
        lastDebug = now;
        mqttPublishTelemetry();
    }

    // detect MQTT disconnect → show on OLED
    static bool prevMqtt = false;
    bool curMqtt = mqttIsConnected();
    if (prevMqtt && !curMqtt) {
        oledError("MQTT disconnected!");
        buzzerBeep(200);
        Serial.println("[MQTT] lost connection");
    }
    prevMqtt = curMqtt;
}

// ====================================================================
//  SETUP
// ====================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CarryFinal ESP32 Master ===");

    // peripherals
    oledInit();
    oledSplash();
    relayInit();
    relayLineNfcOn();   // power PN532 + line sensors early so STM32 nfcInit() succeeds
    buzzerInit();
    // batteryInit();      // tạm tắt battery
    g_batteryPercent = 100;
    buttonInit();

    // UARTs
    Serial2.begin(STM32_BAUD, SERIAL_8N1, PIN_STM32_RX, PIN_STM32_TX);
    SerialHusky.begin(HUSKY_BAUD, SERIAL_8N1, PIN_HUSKY_RX, PIN_HUSKY_TX);

    // WiFi — autoConnect (dùng creds đã lưu, hoặc mở portal nếu chưa có)
    oledBoot(false, false);
    startPortal(false);   // chỉ trả về khi đã kết nối WiFi thành công
    g_wifiConnected = true;
    Serial.printf("[BOOT] WiFi OK  IP=%s\n", WiFi.localIP().toString().c_str());
    oledBoot(true, false);

    // MQTT — wait for connection before entering idle
    mqttInit();
    {
        uint32_t mqttStart = millis();
        while (!mqttIsConnected() && (millis() - mqttStart) < 10000) {
            mqttLoop();
            oledBoot(true, false);
            delay(200);
        }
    }
    if (mqttIsConnected()) {
        g_mqttConnected = true;
        Serial.println("[BOOT] MQTT OK");
    } else {
        Serial.println("[BOOT] MQTT timeout – opening MQTT config portal");
        startMqttPortal();   // asks user to re-enter IP then restarts
    }
    oledBoot(true, mqttIsConnected());

    // Sensors & default mode
    servoInit();
    sr05Init();
    huskyInit(SerialHusky);

    // default: Auto mode (only enter idle when both WiFi and MQTT are connected)
    autoModeInit();
    buzzerBeep(60);
    Serial.println("[BOOT] ready");
}

// ====================================================================
//  LOOP
// ====================================================================
void loop() {
    // always run
    buttonLoop();
    mqttLoop();
    handleSTM32();
    periodicTasks();

    // long press → force WiFi + MQTT portal
    if (g_btnLongPress) {
        g_btnLongPress = false;
        Serial.println("[BTN] long press → force WiFi portal");
        buzzerBeep(300);
        startPortal(true);   // restarts after save
    }

    // mode switch check
    checkModeSwitch();

    // ── MQTT requested mode change → call proper init here (safe stack) ──
    if (g_modeChangeReq) {
        g_modeChangeReq = false;
        switch (g_mode) {
        case MODE_AUTO:     autoModeInit();     break;
        case MODE_FOLLOW:   followModeInit();   break;
        case MODE_FIND:     relaySetFollow();   break;   // find dùng relay giống follow
        case MODE_RECOVERY: recoveryModeInit(); break;
        }
        Serial.printf("[MODE] init after MQTT set_mode → %u\n", g_mode);
    }

    // mode-specific loop
    switch (g_mode) {
    case MODE_AUTO:     autoModeLoop();     break;
    case MODE_FOLLOW:   followModeLoop();   break;
    case MODE_FIND:     findModeLoop();     break;
    case MODE_RECOVERY:
        {
            static bool recInited = false;
            if (!recInited) { recoveryModeInit(); recInited = true; }
            recoveryModeLoop();
            if (g_mode != MODE_RECOVERY) recInited = false;
        }
        break;
    }
}
