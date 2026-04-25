#include "huskylens_uart.h"

static HUSKYLENS huskylens;
static bool      s_connected = false;
static Stream   *s_stream = nullptr;

// ── health monitoring ───────────────────────────────────────────────
static uint8_t  s_consecFails    = 0;
static const uint8_t MAX_HUSKY_FAILS = 40;   // ~40 × 50ms = 2s → reconnect
static uint32_t s_lastRetryMs    = 0;
static const uint32_t HUSKY_RETRY_MS = 3000;
static uint32_t s_lastDiagMs     = 0;
static const uint32_t HUSKY_DIAG_MS = 1000;

static void huskyDiag(const char *msg, int pending = -1) {
    uint32_t now = millis();
    if (now - s_lastDiagMs < HUSKY_DIAG_MS) return;
    s_lastDiagMs = now;
    if (pending < 0 && s_stream) pending = s_stream->available();
    Serial.printf("[HUSKY] %s conn=%d pending=%d fails=%u\n",
                  msg,
                  s_connected ? 1 : 0,
                  pending,
                  s_consecFails);
}

void huskyInit(Stream &stream) {
    s_stream = &stream;
    s_connected = huskylens.begin(stream);
    Serial.printf("[HUSKY] %s\n", s_connected ? "connected" : "init FAILED");
}

void huskyReconnect() {
    if (!s_stream) return;
    Serial.println("[HUSKY] reconnecting...");
    s_connected = huskylens.begin(*s_stream);
    s_consecFails = 0;
    Serial.printf("[HUSKY] reconnect %s\n", s_connected ? "OK" : "FAILED");
}

static void huskyRetryInit() {
    uint32_t now = millis();
    if (now - s_lastRetryMs < HUSKY_RETRY_MS) return;
    s_lastRetryMs = now;
    huskyReconnect();
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

    int pendingBefore = s_stream ? s_stream->available() : -1;
    if (!huskylens.request()) {
        bool hadBytes = pendingBefore > 0;
        if (++s_consecFails >= MAX_HUSKY_FAILS) {
            s_consecFails = 0;
            s_connected = false;
            Serial.println("[HUSKY] too many fails – will reconnect");
        } else {
            huskyDiag(hadBytes ? "request failed after RX activity"
                               : "request timeout/no RX bytes",
                      pendingBefore);
        }
        return r;
    }

    if (!huskylens.isLearned()) {
        huskyDiag("request OK but no learned object");
        return r;
    }
    if (!huskylens.available()) {
        huskyDiag("request OK but no visible object");
        return r;
    }

    s_consecFails = 0;

    HUSKYLENSResult res = huskylens.read();
    r.detected = true;
    r.xCenter  = res.xCenter;
    r.yCenter  = res.yCenter;
    r.width    = res.width;
    r.height   = res.height;
    r.id       = res.ID;
    return r;
}
