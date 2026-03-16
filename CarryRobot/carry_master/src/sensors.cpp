/*  sensors.cpp  –  VL53L0X ToF + dual ultrasonic implementation
 */
#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <VL53L0X.h>

static VL53L0X tof;
static bool    tofOk = false;

/* ─── Initialisation ─── */
void sensorsInit() {
    // VL53L0X
    tof.setTimeout(500);
    if (tof.init()) {
        tof.setMeasurementTimingBudget(TOF_MEAS_BUDGET_US);
        tofOk = true;
        Serial.println(F("[SENSOR] VL53L0X OK"));
    } else {
        Serial.println(F("[SENSOR] VL53L0X FAIL – continuing without ToF"));
    }

    // Ultrasonics
    pinMode(US_LEFT_TRIG,  OUTPUT);
    pinMode(US_LEFT_ECHO,  INPUT);
    pinMode(US_RIGHT_TRIG, OUTPUT);
    pinMode(US_RIGHT_ECHO, INPUT);
    Serial.println(F("[SENSOR] Ultrasonics configured"));
}

/* ─── VL53L0X ─── */
bool tofRead(uint16_t &distMm) {
    if (!tofOk) return false;
    distMm = tof.readRangeSingleMillimeters();
    if (tof.timeoutOccurred()) return false;
    return true;
}

/* ─── Ultrasonic generic ─── */
long usReadMm(uint8_t trigPin, uint8_t echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long dur = pulseIn(echoPin, HIGH, US_TIMEOUT_US);
    if (dur == 0) return -1;          // timeout
    return dur * 10 / 58;             // mm  (dur/58 = cm, ×10 = mm)
}

long usLeftMm()  { return usReadMm(US_LEFT_TRIG,  US_LEFT_ECHO);  }
long usRightMm() { return usReadMm(US_RIGHT_TRIG, US_RIGHT_ECHO); }
