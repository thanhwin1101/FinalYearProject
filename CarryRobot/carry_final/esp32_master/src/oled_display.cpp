#include "oled_display.h"
#include "globals.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ── helpers ─────────────────────────────────────────────────────────
static void header(const char *title) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, title);
    u8g2.drawHLine(0, 12, 128);
}

static void statusBar() {
    char buf[32];
    snprintf(buf, sizeof(buf), "Bat:%3u%%  %s",
             (unsigned)g_batteryPercent,
             g_mqttConnected ? "MQTT" : "----");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 63, buf);
}

// ── public functions ────────────────────────────────────────────────
void oledInit() {
    u8g2.begin();
    u8g2.setContrast(200);
}

void oledSplash() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(10, 30, "CarryFinal");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(30, 50, "ESP32 Master");
    u8g2.sendBuffer();
}

void oledBoot(bool wifi, bool mqtt) {
    u8g2.clearBuffer();
    header("BOOT");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 28, wifi  ? "WiFi:  OK" : "WiFi:  ...");
    u8g2.drawStr(0, 42, mqtt  ? "MQTT:  OK" : "MQTT:  ...");
    u8g2.sendBuffer();
}

void oledIdle() {
    u8g2.clearBuffer();
    header("AUTO  -  IDLE");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 30, "Waiting for route...");
    statusBar();
    u8g2.sendBuffer();
}

void oledAutoWaitStart(const char *patient, const char *dest, uint8_t totalCp) {
    u8g2.clearBuffer();
    header("AUTO  -  Route Ready");
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[40];
    snprintf(buf, sizeof(buf), "Patient: %s", patient);
    u8g2.drawStr(0, 26, buf);
    snprintf(buf, sizeof(buf), "Dest: %s  CP:%u", dest, totalCp);
    u8g2.drawStr(0, 38, buf);
    u8g2.drawStr(0, 52, ">> Press BTN to START");
    statusBar();
    u8g2.sendBuffer();
}

void oledAutoRunning(uint8_t cpIdx, uint8_t totalCp, const char *dest) {
    u8g2.clearBuffer();
    header("AUTO  -  Running");
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[40];
    snprintf(buf, sizeof(buf), "CP: %u / %u", cpIdx, totalCp);
    u8g2.drawStr(0, 30, buf);
    snprintf(buf, sizeof(buf), "-> %s", dest);
    u8g2.drawStr(0, 44, buf);
    statusBar();
    u8g2.sendBuffer();
}

void oledAutoWaitReturn() {
    u8g2.clearBuffer();
    header("AUTO  -  Arrived");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 30, "Delivery done.");
    u8g2.drawStr(0, 44, ">> Press BTN to return");
    statusBar();
    u8g2.sendBuffer();
}

void oledAutoReturning(uint8_t cpIdx, uint8_t totalCp) {
    u8g2.clearBuffer();
    header("AUTO  -  Returning");
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[32];
    snprintf(buf, sizeof(buf), "CP: %u / %u", cpIdx, totalCp);
    u8g2.drawStr(0, 30, buf);
    u8g2.drawStr(0, 44, "-> MED (home)");
    statusBar();
    u8g2.sendBuffer();
}

void oledFollowMode(int tagX, int tagY, int area, float wallL, float wallR) {
    u8g2.clearBuffer();
    header("FOLLOW");
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[40];
    snprintf(buf, sizeof(buf), "Tag x:%d y:%d a:%d", tagX, tagY, area);
    u8g2.drawStr(0, 26, buf);
    snprintf(buf, sizeof(buf), "WallL:%.0f  WallR:%.0f", wallL, wallR);
    u8g2.drawStr(0, 38, buf);
    statusBar();
    u8g2.sendBuffer();
}

void oledFindMode(uint8_t attempts) {
    u8g2.clearBuffer();
    header("FIND  -  Searching");
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[32];
    snprintf(buf, sizeof(buf), "Attempts: %u / 3", attempts);
    u8g2.drawStr(0, 30, buf);
    statusBar();
    u8g2.sendBuffer();
}

void oledRecovery(const char *phase) {
    u8g2.clearBuffer();
    header("RECOVERY");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 30, phase);
    statusBar();
    u8g2.sendBuffer();
}

void oledObstacle() {
    u8g2.clearBuffer();
    header("!! OBSTACLE !!");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 35, "Waiting for clear...");
    statusBar();
    u8g2.sendBuffer();
}

void oledPortal(const char *apName, const char *ip) {
    u8g2.clearBuffer();
    header("WiFiManager Portal");
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[40];
    snprintf(buf, sizeof(buf), "AP: %s", apName);
    u8g2.drawStr(0, 28, buf);
    snprintf(buf, sizeof(buf), "IP: %s", ip);
    u8g2.drawStr(0, 40, buf);
    u8g2.drawStr(0, 54, "Connect & configure");
    u8g2.sendBuffer();
}

void oledBatteryLow(uint8_t pct) {
    u8g2.clearBuffer();
    header("LOW BATTERY");
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[32];
    snprintf(buf, sizeof(buf), "Battery: %u%%", pct);
    u8g2.drawStr(0, 35, buf);
    u8g2.drawStr(0, 50, "Commands blocked!");
    u8g2.sendBuffer();
}

void oledError(const char *msg) {
    u8g2.clearBuffer();
    header("ERROR");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 35, msg);
    u8g2.sendBuffer();
}
