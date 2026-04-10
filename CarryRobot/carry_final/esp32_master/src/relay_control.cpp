#include "relay_control.h"

void relayInit() {
    pinMode(PIN_RELAY_VISION,   OUTPUT);
    pinMode(PIN_RELAY_LINE_NFC, OUTPUT);
    relayAllOff();
}

void relayVisionOn()   { digitalWrite(PIN_RELAY_VISION,   HIGH); }
void relayVisionOff()  { digitalWrite(PIN_RELAY_VISION,   LOW);  }
void relayLineNfcOn()  { digitalWrite(PIN_RELAY_LINE_NFC, HIGH); }
void relayLineNfcOff() { digitalWrite(PIN_RELAY_LINE_NFC, LOW);  }

void relayAllOff() {
    relayVisionOff();
    relayLineNfcOff();
}

void relaySetAuto() {
    relayVisionOff();
    relayLineNfcOn();
    delay(200);
}

void relaySetFollow() {
    relayLineNfcOff();
    relayVisionOn();
    delay(300);
}

void relaySetRecovery() {
    relayVisionOn();
    relayLineNfcOn();
    delay(300);
}

bool relayGetVision()  { return digitalRead(PIN_RELAY_VISION)   == HIGH; }
bool relayGetLineNfc() { return digitalRead(PIN_RELAY_LINE_NFC) == HIGH; }
