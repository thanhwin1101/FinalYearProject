#include "battery.h"
#include "globals.h"

static float s_adcVoltage = 3.25f;

void batteryInit() {
    analogSetAttenuation(ADC_11db);
    pinMode(PIN_BATTERY, INPUT);
    batteryRead();
}

void batteryRead() {
    const int    samples = 16;
    uint32_t sum = 0;
    for (int i = 0; i < samples; i++) sum += analogRead(PIN_BATTERY);
    float adcAvg = (float)sum / samples;
    s_adcVoltage = adcAvg / 4095.0f * 3.6f;

    // Linear map: [BATT_ADC_MIN_V → BATT_PCT_AT_MIN%] to [BATT_ADC_MAX_V → 100%]
    // e.g. 2.6V → 10%, 3.2V → 100%
    float pct = BATT_PCT_AT_MIN
                + (s_adcVoltage - BATT_ADC_MIN_V)
                  / (BATT_ADC_MAX_V - BATT_ADC_MIN_V)
                  * (100.0f - BATT_PCT_AT_MIN);
    if (pct > 100.0f) pct = 100.0f;
    if (pct < (float)BATT_PCT_AT_MIN) pct = (float)BATT_PCT_AT_MIN;
    g_batteryPercent = (uint8_t)pct;
}

uint8_t batteryGetPercent() { return g_batteryPercent; }
float   batteryGetVoltage() { return s_adcVoltage; }
bool    batteryOk()         { return g_batteryPercent >= BATT_MIN_PERCENT; }
