#include "buzzer.h"

#define BUZZER_CHANNEL 0
#define BUZZER_FREQ    2000

void buzzerInit() {
    ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
    ledcWrite(BUZZER_CHANNEL, 0);
}

void buzzerBeep(uint16_t ms) {
    ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQ);
    delay(ms);
    ledcWrite(BUZZER_CHANNEL, 0);
}

void buzzerBeepN(uint8_t n, uint16_t onMs, uint16_t offMs) {
    for (uint8_t i = 0; i < n; i++) {
        buzzerBeep(onMs);
        if (i < n - 1) delay(offMs);
    }
}

void buzzerTone(uint16_t freq, uint16_t ms) {
    ledcWriteTone(BUZZER_CHANNEL, freq);
    delay(ms);
    ledcWrite(BUZZER_CHANNEL, 0);
}

void buzzerOff() {
    ledcWrite(BUZZER_CHANNEL, 0);
}
