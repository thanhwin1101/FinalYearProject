#include "huskylens_wrapper.h"
#include "config.h"
#include "HUSKYLENS.h"

static HUSKYLENS huskylens;
static bool s_huskyReady = false;
static unsigned long s_lastReconnectTryMs = 0;
static protocolAlgorithm s_requestedAlgo = ALGORITHM_OBJECT_TRACKING;

static void applyRequestedAlgorithm() {
    if (!s_huskyReady) return;
    huskylens.writeAlgorithm(s_requestedAlgo);
}

static bool tryConnectHusky(unsigned long timeoutMs) {
    const unsigned long t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (huskylens.begin(Serial2)) {
            s_huskyReady = true;
            applyRequestedAlgorithm();
            Serial.println(F("[HUSKY] HuskyLens connected"));
            return true;
        }
        delay(80);
    }
    return false;
}

void huskyInit() {
    Serial2.begin(HUSKY_BAUD, SERIAL_8N1, HUSKY_RX_PIN, HUSKY_TX_PIN);
    delay(120);
    if (!tryConnectHusky(HUSKY_INIT_TIMEOUT_MS)) {
        s_huskyReady = false;
        Serial.println(F("[HUSKY] Not detected at boot - will retry in loop"));
    }
}

void huskyMaintain() {
    if (s_huskyReady) return;
    const unsigned long now = millis();
    if (now - s_lastReconnectTryMs < HUSKY_RECONNECT_INTERVAL_MS) return;
    s_lastReconnectTryMs = now;

    if (tryConnectHusky(300)) {
        Serial.println(F("[HUSKY] Reconnected"));
    }
}

bool huskyIsReady() {
    return s_huskyReady;
}

bool huskyRequest() {
    huskyMaintain();
    if (!s_huskyReady) return false;

    if (!huskylens.request()) {
        s_huskyReady = false;
        return false;
    }
    return true;
}

HuskyTarget huskyGetTarget() {
    HuskyTarget t = {};
    if (!s_huskyReady) return t;
    if (!huskylens.available())  return t;

    HUSKYLENSResult r = huskylens.read();
    if (r.command == COMMAND_RETURN_BLOCK) {
        t.detected = true;
        t.xCenter  = r.xCenter;
        t.yCenter  = r.yCenter;
        t.width    = r.width;
        t.height   = r.height;
        t.id       = r.ID;
    }
    return t;
}

HuskyLine huskyGetLine() {
    HuskyLine l = {};
    if (!s_huskyReady) return l;
    if (!huskylens.available()) return l;

    HUSKYLENSResult r = huskylens.read();
    if (r.command == COMMAND_RETURN_ARROW) {
        l.detected = true;
        l.xOrigin  = r.xOrigin;
        l.yOrigin  = r.yOrigin;
        l.xTarget  = r.xTarget;
        l.yTarget  = r.yTarget;
    }
    return l;
}

void huskySwitchToObjectTracking() {
    s_requestedAlgo = ALGORITHM_OBJECT_TRACKING;
    applyRequestedAlgorithm();
    Serial.println(F("[HUSKY] -> Object Tracking"));
}

void huskySwitchToFaceRecognition() {
    s_requestedAlgo = ALGORITHM_FACE_RECOGNITION;
    applyRequestedAlgorithm();
    Serial.println(F("[HUSKY] -> Face Recognition"));
}

void huskySwitchToLineTracking() {
    s_requestedAlgo = ALGORITHM_LINE_TRACKING;
    applyRequestedAlgorithm();
    Serial.println(F("[HUSKY] -> Line Tracking"));
}

void huskySwitchToTagRecognition() {
    s_requestedAlgo = ALGORITHM_TAG_RECOGNITION;
    applyRequestedAlgorithm();
    Serial.println(F("[HUSKY] -> Tag Recognition"));
}
