#include "huskylens_uart.h"
#include "mqtt_client.h"

static HUSKYLENS huskylens;
static bool      s_connected = false;
static HardwareSerial *s_port = nullptr;

// ── health monitoring (PN532-like pattern) ──────────────────────────
static uint8_t  s_consecFails    = 0;
static const uint8_t MAX_HUSKY_FAILS = 40;   // ~40 × 50ms = 2s of failures → reconnect
static uint32_t s_lastRetryMs    = 0;
static const uint32_t HUSKY_RETRY_MS = 3000; // retry reconnect every 3s

void huskyInit(HardwareSerial &port) {
    s_port = &port;
    s_connected = huskylens.begin(port);
    if (s_connected) {
        Serial.println("[HUSKY] connected");
    } else {
        Serial.println("[HUSKY] init FAILED");
    }
}

void huskyReconnect() {
    if (!s_port) return;
    Serial.println("[HUSKY] reconnecting...");
    s_connected = huskylens.begin(*s_port);
    s_consecFails = 0;
    Serial.printf("[HUSKY] reconnect %s\n", s_connected ? "OK" : "FAILED");
}

// ── periodic retry if not connected ─────────────────────────────────
static void huskyRetryInit() {
    uint32_t now = millis();
    if (now - s_lastRetryMs < HUSKY_RETRY_MS) return;
    s_lastRetryMs = now;
    Serial.println("[HUSKY] retrying init...");
    huskyReconnect();
    if (s_connected) {
        mqttPublishEvent("husky_recovered");
    }
}

bool huskyConnected() { return s_connected; }

void huskySetTagMode() {
    if (s_connected)
        huskylens.writeAlgorithm(ALGORITHM_TAG_RECOGNITION);
}

void huskySetLineMode() {
    if (s_connected)
        huskylens.writeAlgorithm(ALGORITHM_LINE_TRACKING);
}

HuskyResult huskyRead() {
    HuskyResult r = {};
    if (!s_connected) {
        huskyRetryInit();
        return r;
    }

    if (!huskylens.request()) {
        if (++s_consecFails >= MAX_HUSKY_FAILS) {
            s_consecFails = 0;
            s_connected = false;
            Serial.println("[HUSKY] too many fails – will reconnect");
            mqttPublishEvent("husky_timeout");
        }
        return r;
    }
    if (!huskylens.isLearned()) return r;
    if (!huskylens.available()) return r;

    s_consecFails = 0;   // successful communication resets counter

    HUSKYLENSResult res = huskylens.read();
    r.detected = true;
    r.xCenter  = res.xCenter;
    r.yCenter  = res.yCenter;
    r.width    = res.width;
    r.height   = res.height;
    r.id       = res.ID;
    return r;
}
