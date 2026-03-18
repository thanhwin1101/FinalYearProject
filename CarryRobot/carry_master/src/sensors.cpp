#include "sensors.h"
#include "config.h"
#include "globals.h"
#include <Wire.h>
#include <VL53L0X.h>

static VL53L0X tof;
static bool    tofOk = false;

void sensorsInit() {

    tof.setTimeout(500);
    if (tof.init()) {
        tof.setMeasurementTimingBudget(TOF_MEAS_BUDGET_US);
        tofOk = true;
        Serial.println(F("[SENSOR] VL53L0X OK"));
    } else {
        Serial.println(F("[SENSOR] VL53L0X FAIL – continuing without ToF"));
    }

    pinMode(US_LEFT_TRIG,  OUTPUT);
    pinMode(US_LEFT_ECHO,  INPUT);
    pinMode(US_RIGHT_TRIG, OUTPUT);
    pinMode(US_RIGHT_ECHO, INPUT);
    Serial.println(F("[SENSOR] Ultrasonics configured"));
}

bool tofRead(uint16_t &distMm) {
    if (!tofOk) return false;
    if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
    distMm = tof.readRangeSingleMillimeters();
    if (g_i2cMutex) xSemaphoreGive(g_i2cMutex);
    if (tof.timeoutOccurred()) return false;
    return true;
}

long usReadMm(uint8_t trigPin, uint8_t echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long dur = pulseIn(echoPin, HIGH, US_TIMEOUT_US);
    if (dur == 0) return -1;
    return dur * 10 / 58;
}

long usLeftMm()  { return usReadMm(US_LEFT_TRIG,  US_LEFT_ECHO);  }
long usRightMm() { return usReadMm(US_RIGHT_TRIG, US_RIGHT_ECHO); }

void sensorsUpdateCache50ms() {
    uint16_t d = 0;
    const bool ok = tofRead(d);
    g_tofValid = ok;
    if (ok) g_tofMm = d;

    // Ultrasonics (can be slow/blocking due to pulseIn timeout)
    g_usLeftMm  = usLeftMm();
    g_usRightMm = usRightMm();
}
