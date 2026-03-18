#include "line_follower.h"
#include "config.h"

static const uint8_t pins[3] = {LINE_S1, LINE_S2, LINE_S3};
static const int16_t weights[3] = {-2000, 0, 2000};

static bool     sensorVal[3];
static uint8_t  bits = 0;
static int16_t  error = 0;

static float integral = 0;
static float prevErr  = 0;

void lineInit() {
    for (int i = 0; i < 3; i++) {
        pinMode(pins[i], INPUT);
    }
    Serial.println(F("[LINE] 3-sensor init OK"));
}

static void readSensors() {
    bits = 0;
    for (int i = 0; i < 3; i++) {
        bool raw = digitalRead(pins[i]);

        sensorVal[i] = LINE_INVERT ? !raw : raw;
        if (sensorVal[i]) bits |= (1 << (2 - i));
    }
}

static void computeError() {
    int32_t num = 0;
    int32_t den = 0;
    for (int i = 0; i < 3; i++) {
        if (sensorVal[i]) {
            num += weights[i];
            den++;
        }
    }
    if (den == 0) {

        return;
    }
    error = (int16_t)(num / den);
}

float lineUpdate(float dt) {
    readSensors();
    computeError();

    if (dt <= 0) dt = 0.02f;
    float e = (float)error;

    float P = LINE_KP * e;
    integral += e * dt;
    integral = constrain(integral, -5000.0f, 5000.0f);
    float I = LINE_KI * integral;
    float D = LINE_KD * (e - prevErr) / dt;
    prevErr = e;

    float correction = P + I + D;
    return constrain(correction, -LINE_MAX_CORRECTION, LINE_MAX_CORRECTION);
}

uint8_t lineGetBits()   { return bits; }
int16_t lineGetError()  { return error; }
bool    lineDetected()  { return bits != 0; }
bool    lineCentred()   { return sensorVal[2]; }

void lineResetPID() {
    integral = 0;
    prevErr  = 0;
}
