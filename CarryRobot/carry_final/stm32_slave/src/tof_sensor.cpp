#include "tof_sensor.h"

#if USE_TOF

#include <Wire.h>
#include <VL53L0X.h>

static VL53L0X sensor;
static bool    s_ready    = false;
static int     s_lastDist = 9999;
static uint32_t lastRead  = 0;

void tofInit() {
    Wire.setSDA(TOF_SDA);
    Wire.setSCL(TOF_SCL);
    Wire.begin();
    sensor.setTimeout(500);
    if (!sensor.init()) {
        Serial.println("[TOF] VL53L0X init FAILED");
        s_ready = false;
        return;
    }
    sensor.setMeasurementTimingBudget(20000);
    sensor.startContinuous();
    s_ready = true;
    Serial.println("[TOF] VL53L0X OK");
}

bool tofAvailable() { return s_ready; }

int tofReadMm() {
    if (!s_ready) return 9999;
    uint32_t now = millis();
    if (now - lastRead < TOF_READ_MS) return s_lastDist;
    lastRead = now;

    int d = sensor.readRangeContinuousMillimeters();
    if (sensor.timeoutOccurred()) return s_lastDist;
    s_lastDist = d;
    return d;
}

bool tofObstacle() { return tofReadMm() <= TOF_STOP_MM; }
bool tofClear()    { return tofReadMm() >= TOF_RESUME_MM; }

#else
// ── ToF disabled (USE_TOF = 0) ────────────────────────────────────
void tofInit()      { Serial.println("[TOF] disabled"); }
bool tofAvailable() { return false; }
int  tofReadMm()    { return 9999; }
bool tofObstacle()  { return false; }
bool tofClear()     { return true;  }

#endif
