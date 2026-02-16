#include "display.h"
#include "globals.h"

// =========================================
// DISPLAY FUNCTIONS
// =========================================
void displayInit() {
  oled.begin();
  oled.setFont(u8g2_font_6x12_tr);
  oled.clearBuffer();
  oled.drawStr(0, 12, "Carry Robot");
  oled.drawStr(0, 28, "Initializing...");
  oled.sendBuffer();
}

void drawCentered(const char* line1, const char* line2, const char* line3, const char* line4) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  
  int y = 12;
  if (line1) { oled.drawStr(0, y, line1); y += 14; }
  if (line2) { oled.drawStr(0, y, line2); y += 14; }
  if (line3) { oled.drawStr(0, y, line3); y += 14; }
  if (line4) { oled.drawStr(0, y, line4); }
  
  oled.sendBuffer();
}

void drawState(const char* stateName, const char* info) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, "State:");
  oled.drawStr(0, 26, stateName);
  if (info) {
    oled.drawStr(0, 44, info);
  }
  oled.sendBuffer();
}

void drawRouteProgress(const char* phase, int idx, int total, const char* node) {
  char buf1[24], buf2[24];
  snprintf(buf1, sizeof(buf1), "%s %d/%d", phase, idx + 1, total);
  snprintf(buf2, sizeof(buf2), "-> %s", node);
  
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, buf1);
  oled.drawStr(0, 28, buf2);
  oled.sendBuffer();
}

void drawMissionInfo(const char* missionId, const char* dest, const char* status) {
  char buf1[24], buf2[24], buf3[24];
  snprintf(buf1, sizeof(buf1), "M: %.8s", missionId);
  snprintf(buf2, sizeof(buf2), "To: %s", dest);
  snprintf(buf3, sizeof(buf3), "St: %s", status);
  
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, buf1);
  oled.drawStr(0, 28, buf2);
  oled.drawStr(0, 44, buf3);
  oled.sendBuffer();
}

void drawWaitingCargo(const char* missionId, const char* dest) {
  char buf[24];
  snprintf(buf, sizeof(buf), "-> %s", dest);
  
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, "WAITING CARGO");
  oled.drawStr(0, 28, buf);
  oled.drawStr(0, 44, "Press button when");
  oled.drawStr(0, 58, "cargo loaded");
  oled.sendBuffer();
}

void drawError(const char* errTitle, const char* errDetail) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, "ERROR");
  oled.drawStr(0, 28, errTitle);
  if (errDetail) {
    oled.drawStr(0, 44, errDetail);
  }
  oled.sendBuffer();
}

void drawWaitingForReturnRoute() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, "CANCELLED");
  oled.drawStr(0, 28, "Waiting for");
  oled.drawStr(0, 44, "return route...");
  oled.sendBuffer();
}

void showTurnOverlay(char direction, unsigned long durationMs) {
  const char* turnText = "TURN";
  const char* dirText = (direction == 'L') ? "LEFT" : 
                        (direction == 'R') ? "RIGHT" : "UTURN";
  
  oled.clearBuffer();
  oled.setFont(u8g2_font_10x20_tr);
  oled.drawStr(30, 30, turnText);
  oled.drawStr(25, 55, dirText);
  oled.sendBuffer();
  
  // Brief display, non-blocking for short durations
  if (durationMs > 0 && durationMs <= 100) {
    delay(durationMs);
  }
}
