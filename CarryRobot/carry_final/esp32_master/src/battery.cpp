#include "battery.h"
#include "globals.h"

// ── Battery tạm tắt ─────────────────────────────────────────────────
// static float s_adcVoltage = 3.25f;

void batteryInit() {
    // tạm comment – không đọc ADC pin battery
    // analogSetAttenuation(ADC_11db);
    // pinMode(PIN_BATTERY, INPUT);
    g_batteryPercent = 100;
}

void batteryRead() {
    // tạm comment – luôn giữ 100%
    // const int    samples = 16;
    // uint32_t sum = 0;
    // for (int i = 0; i < samples; i++) sum += analogRead(PIN_BATTERY);
    // float adcAvg = (float)sum / samples;
    // s_adcVoltage = adcAvg / 4095.0f * 3.6f;
    // float pct = (s_adcVoltage - BATT_ADC_EMPTY_V) / (BATT_ADC_FULL_V - BATT_ADC_EMPTY_V) * 100.0f;
    // if (pct > 100.0f) pct = 100.0f;
    // if (pct < 0.0f)   pct = 0.0f;
    // g_batteryPercent = (uint8_t)pct;
}

uint8_t batteryGetPercent() { return g_batteryPercent; }
float   batteryGetVoltage() { return 3.25f; /*tạm*/ }
bool    batteryOk()         { return true; /*tạm luôn OK*/ }
