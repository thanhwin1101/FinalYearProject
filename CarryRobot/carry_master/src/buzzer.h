#pragma once
#include <Arduino.h>
#include "config.h"

#define BUZZER_CH  2

inline void buzzerInit() {
    ledcSetup(BUZZER_CH, 2200, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWrite(BUZZER_CH, 0);
}

inline void buzzerTone(uint16_t freqHz, uint16_t durMs) {
    ledcWriteTone(BUZZER_CH, freqHz);
    delay(durMs);
    ledcWriteTone(BUZZER_CH, 0);
}

inline void buzzerBeep(uint16_t durMs = 80) {
    buzzerTone(2200, durMs);
}

inline void buzzerStartup() {
    buzzerTone(2200, 120);
}

inline void buzzerMqttConnect() {
    buzzerTone(2400, 60);
}

inline void buzzerCheckpoint() {
    buzzerTone(2200, 60);
}

inline void buzzerArrived() {
    for (int i = 0; i < 3; i++) {
        buzzerTone(1800, 140);
        delay(90);
    }
}

inline void buzzerObstacle() {
    buzzerTone(1500, 100);
}

inline void buzzerError() {
    buzzerTone(800, 300);
}
