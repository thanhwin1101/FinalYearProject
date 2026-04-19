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
#include "servo_control.h"
#include "mqtt_client.h"
#include "auto_mode.h"
#include "follow_mode.h"
#include "recovery_mode.h"
#include "ota_manager.h"

// ── Hardware serial ports ───────────────────────────────────────────
// STM32: dùng Serial2 toàn project (auto/follow/find/recovery) — tránh hai đối tượng UART2.
// HuskyLens: đã chuyển xuống STM32 (USART3) — ESP32 không còn kết nối trực tiếp.

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

        case CMD_HUSKY_STATUS:
            if (len >= 11) {
                g_huskyDetected = buf[0] != 0;
                g_huskyXCenter  = (int16_t)(((uint16_t)buf[1] << 8) | buf[2]);
                g_huskyYCenter  = (int16_t)(((uint16_t)buf[3] << 8) | buf[4]);
                g_huskyWidth    = (int16_t)(((uint16_t)buf[5] << 8) | buf[6]);
                g_huskyHeight   = (int16_t)(((uint16_t)buf[7] << 8) | buf[8]);
                g_huskyId       = (int16_t)(((uint16_t)buf[9] << 8) | buf[10]);
                g_huskyNew = true;
            }
            break;

        case CMD_TAG_LOST:
            g_stm32TagLost = true;
            Serial.println("[UART] <<< TAG_LOST");
            break;

        case CMD_TAG_FOUND:
            g_stm32TagFound = true;
            Serial.println("[UART] <<< TAG_FOUND");
            break;

        case CMD_LINE_STATUS:
            if (len >= 1) g_stm32LineBits = buf[0];
            break;
            break;

        default:
            Serial.printf("[UART] unknown cmd 0x%02X\n", cmd);
        }
    }
}

// ── Mode switching (long press: toggle AUTO ↔ FOLLOW) ─────────────
static void checkModeSwitch() {
    if (!g_btnLongPress) return;

    if (g_mode == MODE_AUTO && g_autoState == AUTO_IDLE) {
        g_btnLongPress = false;
        g_mode = MODE_FOLLOW;
        followModeInit();
        buzzerBeep(150);
        Serial.printf("[MODE] AUTO → FOLLOW (last CP=0x%04X)\n", g_lastCheckpointId);
    } else if (g_mode == MODE_FOLLOW) {
        g_btnLongPress = false;
        g_mode = MODE_AUTO;
        autoModeInit();
        buzzerBeep(150);
        Serial.println("[MODE] FOLLOW → AUTO");
    } else {
        // not switchable (mission running, recovery…) → just consume + beep
        g_btnLongPress = false;
        buzzerBeep(60);
    }
}

// ── Periodic tasks ──────────────────────────────────────────────────
// static uint32_t lastBatt = 0;   // tạm tắt battery
static uint32_t lastTelem = 0;
static uint32_t lastBatt  = 0;
static uint32_t lastDebug = 0;

static void periodicTasks() {
    uint32_t now = millis();

    if (now - lastBatt >= 5000UL) {   // đọc pin mỗi 5 giây
        lastBatt = now;
        batteryRead();
    }

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
    delay(5000);        // chờ phần cứng (PN532, line sensor) ổn định trước khi STM32 khởi tạo
    buzzerInit();
    batteryInit();
    buttonInit();

    // UARTs
    Serial2.begin(STM32_BAUD, SERIAL_8N1, PIN_STM32_RX, PIN_STM32_TX);

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

    // OTA — must be called after WiFi is connected
    otaInit();

    // Sensors & default mode
    servoInit();

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
    otaLoop();
    handleSTM32();
    periodicTasks();

    // long press → toggle mode (AUTO ↔ FOLLOW)
    checkModeSwitch();

    // ── MQTT requested mode change → call proper init here (safe stack) ──
    if (g_modeChangeReq) {
        g_modeChangeReq = false;
        switch (g_mode) {
        case MODE_AUTO:     autoModeInit();     break;
        case MODE_FOLLOW:   followModeInit();   break;
        case MODE_RECOVERY: recoveryModeInit(); break;
        }
        Serial.printf("[MODE] init after mode change → %u\n", g_mode);
    }

    // mode-specific loop
    switch (g_mode) {
    case MODE_AUTO:     autoModeLoop();     break;
    case MODE_FOLLOW:   followModeLoop();   break;
    case MODE_RECOVERY:
        // recoveryModeInit() is called once via g_modeChangeReq above; never call it again here.
        recoveryModeLoop();
        break;
    }
}
