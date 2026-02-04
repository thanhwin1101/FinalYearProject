#include "helpers.h"
#include "config.h"
#include "globals.h"

// =========================================
// URL HELPERS
// =========================================
String joinUrl(const char* base, const String& path) {
  String u(base);
  if (u.endsWith("/")) u.remove(u.length() - 1);
  if (!path.startsWith("/")) u += "/";
  u += path;
  return u;
}

// =========================================
// WEB STATUS
// =========================================
void markWebOk() {
  lastWebOkAt = millis();
  webOkUntil = lastWebOkAt + WEB_OK_SHOW_MS;
}

bool webConnected() {
  return (lastWebOkAt > 0) && (millis() - lastWebOkAt <= WEB_OK_ALIVE_MS);
}

// =========================================
// STRING HELPERS
// =========================================
String truncStr(const String& s, size_t maxLen) {
  if (s.length() <= (int)maxLen) return s;
  return s.substring(0, maxLen);
}

// =========================================
// CARGO SWITCH
// =========================================
void updateCargoSwitch() {
  bool r = digitalRead(CARGO_SWITCH_PIN);
  if (r != cargoRaw) {
    cargoRaw = r;
    cargoLastChange = millis();
  }
  if ((millis() - cargoLastChange) >= SWITCH_DEBOUNCE_MS) {
    cargoStable = cargoRaw;
  }
}

bool cargoHeld() { 
  return cargoStable == LOW; 
}

// =========================================
// NFC TIMING
// =========================================
void ignoreNfcFor(unsigned long ms) { 
  nfcIgnoreUntil = millis() + ms; 
}

bool nfcAllowed() { 
  return millis() >= nfcIgnoreUntil; 
}

// =========================================
// TURN OVERLAY
// =========================================
void showTurnOverlay(char a, unsigned long ms) {
  lastTurnChar = a;
  turnOverlayUntil = millis() + ms;
}

// =========================================
// BUZZER
// =========================================
void buzzerInit() {
  ledcAttach((uint8_t)BUZZER_PIN, 2000, 8);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

void toneOff() { 
  ledcWriteTone((uint8_t)BUZZER_PIN, 0); 
}

void beepOnce(int ms, int freq) {
  ledcWriteTone((uint8_t)BUZZER_PIN, (uint32_t)freq);
  delay(ms);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

void beepArrivedPattern() {
  for (int i = 0; i < 3; i++) {
    beepOnce(140, 1800);
    delay(90);
  }
}
