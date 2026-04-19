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

void oledFollowMode(int tagX, int tagY, int area) {
    // X direction: 0=far-left, 160=center, 320=far-right
    // area vs target 15360: bigger = closer
    u8g2.clearBuffer();
    header("FOLLOW");
    u8g2.setFont(u8g2_font_6x10_tr);

    // Horizontal indicator bar (width 100px, range 0-320)
    int barX = 10;
    int barW = 108;
    int markerX = barX + (int)((long)tagX * barW / 320);
    markerX = constrain(markerX, barX, barX + barW);
    u8g2.drawFrame(barX, 18, barW, 8);
    u8g2.drawBox(markerX - 1, 18, 3, 8);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 26, "L");
    u8g2.drawStr(120, 26, "R");

    // Distance row
    const char *distStr;
    if      (area > 22000) distStr = "v BACK OFF v";
    else if (area >  8000) distStr = "  -- OK --  ";
    else                   distStr = "^ ADVANCE ^";
    u8g2.drawStr(14, 40, distStr);

    statusBar();
    u8g2.sendBuffer();
}

void oledFollowTagLost(uint8_t secsLeft) {
    u8g2.clearBuffer();
    header("FOLLOW  -  TAG LOST");
    u8g2.setFont(u8g2_font_10x20_tr);
    char buf[8];
    snprintf(buf, sizeof(buf), "%2us", secsLeft);
    u8g2.drawStr(44, 44, buf);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(4, 28, "Recovery in:");
    statusBar();
    u8g2.sendBuffer();
}

void oledFollowEnter(bool ok) {
    u8g2.clearBuffer();
    header("FOLLOW MODE");
    u8g2.setFont(u8g2_font_6x10_tr);
    if (ok) {
        u8g2.drawStr(20, 32, ">>> ACTIVE <<<");
    } else {
        u8g2.drawStr(10, 28, "! CANNOT ENTER !");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(0, 44, "Need: MED + IDLE");
    }
    statusBar();
    u8g2.sendBuffer();
}

void oledAutoCancel() {
    u8g2.clearBuffer();
    header("AUTO  -  Cancelled");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 30, "Finding checkpoint...");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 44, "Will request return route");
    statusBar();
    u8g2.sendBuffer();
}

void oledAutoMismatch() {
    u8g2.clearBuffer();
    header("AUTO  -  Rerouting");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 30, "Wrong checkpoint!");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 44, "Requesting new route...");
    statusBar();
    u8g2.sendBuffer();
}

void oledMissionDone() {
    u8g2.clearBuffer();
    header("AUTO  -  Complete");
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(12, 44, "DONE!");
    statusBar();
    u8g2.sendBuffer();
}

void oledRecovery(uint8_t step, uint16_t cpId) {
    static const char *stepStr[] = {
        "",
        "1/5 Relay switch...",
        "2/5 Finding CP...",
        "3/5 CP found!",
        "4/5 Calling MED...",
        "5/5 Waiting route...",
    };
    u8g2.clearBuffer();
    header("RECOVERY");
    u8g2.setFont(u8g2_font_6x10_tr);
    if (step >= 1 && step <= 5) {
        u8g2.drawStr(0, 28, stepStr[step]);
    }
    if (cpId != 0) {
        char buf[20];
        snprintf(buf, sizeof(buf), "CP: 0x%04X", cpId);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(0, 44, buf);
    }
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

void oledOtaProgress(uint8_t pct, const char *label) {
    u8g2.clearBuffer();
    header("OTA UPDATE");
    // Progress bar (x=0, y=20, w=128, h=12)
    u8g2.drawFrame(0, 20, 128, 12);
    uint8_t fill = (uint8_t)((uint16_t)pct * 126 / 100);
    if (fill > 0) u8g2.drawBox(1, 21, fill, 10);
    // Percentage text
    char buf[8];
    snprintf(buf, sizeof(buf), "%3u%%", pct);
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(52, 40, buf);
    // Label
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 55, label);
    u8g2.sendBuffer();
}

