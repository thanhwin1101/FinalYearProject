#include "sr05.h"
#include "mqtt_client.h"

// ── health monitoring (PN532-like pattern) ──────────────────────────
static uint8_t  s_failsL = 0, s_failsR = 0;
static const uint8_t MAX_SR05_FAILS = 20;   // ~20 × 100ms = 2s → report
static bool     s_reportedL = false, s_reportedR = false; // one-shot MQTT

static float readSR05(uint8_t trigPin, uint8_t echoPin,
                      uint8_t &fails, bool &reported, const char *side) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    unsigned long dur = pulseIn(echoPin, HIGH, SR05_TIMEOUT_US);
    if (dur == 0) {
        if (++fails >= MAX_SR05_FAILS && !reported) {
            reported = true;
            Serial.printf("[SR05] %s: %u consecutive timeouts\n", side, fails);
            char evt[32];
            snprintf(evt, sizeof(evt), "sr05_%s_timeout", side);
            mqttPublishEvent(evt);
        }
        return 0.0f;
    }
    fails = 0;
    reported = false;
    return (float)dur * 0.0343f / 2.0f;    // cm
}

void sr05Init() {
    pinMode(PIN_SR05_L_TRIG, OUTPUT);
    pinMode(PIN_SR05_L_ECHO, INPUT);
    pinMode(PIN_SR05_R_TRIG, OUTPUT);
    pinMode(PIN_SR05_R_ECHO, INPUT);
}

float sr05ReadLeft()  { return readSR05(PIN_SR05_L_TRIG, PIN_SR05_L_ECHO,
                                        s_failsL, s_reportedL, "left"); }
float sr05ReadRight() { return readSR05(PIN_SR05_R_TRIG, PIN_SR05_R_ECHO,
                                        s_failsR, s_reportedR, "right"); }

bool sr05WallLeft() {
    float d = sr05ReadLeft();
    return (d > 0.0f && d < SR05_WALL_WARN_CM);
}

bool sr05WallRight() {
    float d = sr05ReadRight();
    return (d > 0.0f && d < SR05_WALL_WARN_CM);
}
