#include "display.h"
#include "config.h"
#include "globals.h"
#include "route_manager.h"
#include "mqtt_comm.h"
#include <WiFi.h>
#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static inline void i2cLock()   { if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY); }
static inline void i2cUnlock() { if (g_i2cMutex) xSemaphoreGive(g_i2cMutex); }

static void setMainFont()  { u8g2.setFont(u8g2_font_6x12_tr); }
static void setSmallFont() { u8g2.setFont(u8g2_font_5x7_tr);  }

static void drawHeader(const char* title) {
    setMainFont();
    u8g2.drawStr((128 - u8g2.getStrWidth(title)) / 2, 14, title);
    u8g2.drawHLine(0, 18, 128);
}

static void drawHint(const char* hintStr) {
    u8g2.drawHLine(0, 54, 128);
    setSmallFont();
    const uint16_t w = u8g2.getStrWidth(hintStr);
    u8g2.drawStr((128 - w) / 2, 63, hintStr);
    setMainFont();
}

static void drawStatusBadge() {
    const bool wOk = (WiFi.status() == WL_CONNECTED);
    const bool mOk = mqttConnected();
    setSmallFont();
    char badge[10];
    snprintf(badge, sizeof(badge), "%s %s",
             wOk ? "W+" : "W-",
             mOk ? "M+" : "M-");
    u8g2.drawStr(128 - u8g2.getStrWidth(badge) - 1, 7, badge);
    setMainFont();
}

void displayInit() {
    // Init without mutex (called once before tasks start). All subsequent
    // drawing goes through display*() which already lock the I2C bus.
    u8g2.begin();
    u8g2.clearBuffer();
    setMainFont();
    displayCentered("Carry Robot", "Initializing...");
    Serial.println(F("[OLED] Display init OK"));
}

void displayCentered(const char* l1, const char* l2,
                     const char* l3, const char* l4)
{
    i2cLock();
    u8g2.clearBuffer();
    setMainFont();
    const char* lines[] = {l1, l2, l3, l4};
    uint8_t count = 0;
    for (int i = 0; i < 4; i++) if (lines[i]) count++;
    uint8_t startY = (64 - count * 14) / 2 + 12;
    for (int i = 0; i < 4; i++) {
        if (!lines[i]) continue;
        uint16_t w = u8g2.getStrWidth(lines[i]);
        u8g2.drawStr((128 - w) / 2, startY + i * 14, lines[i]);
    }
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayIdle() {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=IDLE\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    setMainFont();

    const bool hasMission = (outRouteLen > 0);
    const char* hdr = hasMission ? "MISSION READY" : "--- STANDBY ---";
    u8g2.drawStr((128 - u8g2.getStrWidth(hdr)) / 2, 14, hdr);
    u8g2.drawHLine(0, 18, 128);
    drawStatusBadge();

    if (hasMission) {
        char line1[22], line2[22];
        snprintf(line1, sizeof(line1), "Patient: %.10s", patientName.c_str());
        snprintf(line2, sizeof(line2), "Bed: %.14s",     destBed.c_str());
        u8g2.drawStr(2, 34, line1);
        u8g2.drawStr(2, 50, line2);
        drawHint("[1x]Start [2x]Follow [L]WiFi");
    } else {
        const char* b1 = "Waiting for";
        const char* b2 = "Order...";
        u8g2.drawStr((128 - u8g2.getStrWidth(b1)) / 2, 34, b1);
        u8g2.drawStr((128 - u8g2.getStrWidth(b2)) / 2, 48, b2);
        drawHint("[2x]Follow  [L]WiFi Setup");
    }

    u8g2.sendBuffer();
    i2cUnlock();
}

void displayOutbound(const char* patient, const char* nextNode) {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=OUTBOUND\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("DELIVERING...");
    char buf[22];
    snprintf(buf, sizeof(buf), "To: %.17s", patient  ? patient  : "???");
    u8g2.drawStr(2, 34, buf);
    snprintf(buf, sizeof(buf), "->%.18s", nextNode ? nextNode : "---");
    u8g2.drawStr(2, 50, buf);
    drawHint("[1x]Cancel  [2x]Abort");
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayObstacle() {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=OBSTACLE\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("!! WARNING !!");
    const char* b1 = "Object Detected!";
    const char* b2 = "Braking...";
    u8g2.drawStr((128 - u8g2.getStrWidth(b1)) / 2, 36, b1);
    u8g2.drawStr((128 - u8g2.getStrWidth(b2)) / 2, 50, b2);
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayWaitAtDest() {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=WAIT_AT_DEST\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("ARRIVED!");
    const char* b1 = "Please take items";
    const char* b2 = "Press SW to Return";
    u8g2.drawStr((128 - u8g2.getStrWidth(b1)) / 2, 34, b1);
    u8g2.drawStr((128 - u8g2.getStrWidth(b2)) / 2, 50, b2);
    drawHint("[1x]Return  [2x]Abort");
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayFollow(const char* targetLabel, uint16_t distCm) {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=FOLLOW\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("FOLLOWING...");
    char buf[22];
    snprintf(buf, sizeof(buf), "Target: %.10s", targetLabel ? targetLabel : "---");
    u8g2.drawStr(2, 34, buf);
    snprintf(buf, sizeof(buf), "Dist:   %u cm", distCm);
    u8g2.drawStr(2, 50, buf);
    drawHint("[1x]Stop  [2x]Recover");
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayFaceAuth(const char* phase, uint8_t streak, uint8_t needed) {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=FaceAuth\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("FACE VERIFY");
    char line1[24], line2[24];
    snprintf(line1, sizeof(line1), "Step: %.14s", phase ? phase : "Wait");
    snprintf(line2, sizeof(line2), "Match: %u / %u", (unsigned)streak, (unsigned)needed);
    u8g2.drawStr(2, 34, line1);
    u8g2.drawStr(2, 50, line2);
    drawHint("[1x]Cancel");
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayRecovery(uint8_t step) {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=RECOVERY step=%u\n", millis(), (unsigned)step);
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("RECOVERING...");
    const char* stepStr = "???";
    if      (step == 1) stepStr = "Searching Line...";
    else if (step == 2) stepStr = "Blind Run...";
    else if (step == 3) stepStr = "Calling Home...";
    u8g2.drawStr((128 - u8g2.getStrWidth(stepStr)) / 2, 36, stepStr);
    char buf[16];
    snprintf(buf, sizeof(buf), "Step: %u / 3", step);
    u8g2.drawStr((128 - u8g2.getStrWidth(buf)) / 2, 50, buf);
    drawHint("[1x]Cancel  [2x]Abort");
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayBack(const char* nextNode) {
#if MONITOR_SERIAL
    Serial.printf("[MON] t=%lu OLED=BACK\n", millis());
#endif
    i2cLock();
    u8g2.clearBuffer();
    drawHeader("RETURNING...");
    char buf[22];
    snprintf(buf, sizeof(buf), "Next: %.15s", nextNode ? nextNode : "MED");
    u8g2.drawStr(2, 36, buf);
    drawHint("[1x]Cancel  [2x]Abort");
    u8g2.sendBuffer();
    i2cUnlock();
}

void displayWiFiSetup() {
    displayCentered("WIFI SETUP",
                    "SSID: " WIFI_PORTAL_SSID,
                    "Pass: " WIFI_PORTAL_PASS,
                    "IP:  192.168.4.1");
}

void displayPortalLaunching() {
    u8g2.clearBuffer();
    drawHeader("WIFI PORTAL");
    const char* b1 = "Opening...";
    const char* b2 = "SSID: " WIFI_PORTAL_SSID;
    const char* b3 = "192.168.4.1";
    u8g2.drawStr((128 - u8g2.getStrWidth(b1)) / 2, 30, b1);
    setSmallFont();
    u8g2.drawStr((128 - u8g2.getStrWidth(b2)) / 2, 45, b2);
    u8g2.drawStr((128 - u8g2.getStrWidth(b3)) / 2, 55, b3);
    setMainFont();
    u8g2.sendBuffer();
}

void displayConnected(const char* ip) {
    char buf[22];
    snprintf(buf, sizeof(buf), "IP: %.15s", ip);
    const char* mqttHost = mqttGetServerHost();
    char mqttBuf[22];
    snprintf(mqttBuf, sizeof(mqttBuf), "%.21s", mqttHost ? mqttHost : "---");
    displayCentered("WiFi OK", buf, mqttBuf);
}

void displayBootChecklist(bool wifiOk, bool mqttOk, bool slaveOk, uint16_t stableLeftMs) {
    i2cLock();
    u8g2.clearBuffer();
    setMainFont();

    const char* hdr = "SYSTEM CHECK";
    u8g2.drawStr((128 - u8g2.getStrWidth(hdr)) / 2, 12, hdr);
    u8g2.drawHLine(0, 16, 128);

    u8g2.drawStr(2, 30, wifiOk  ? "WiFi : OK"   : "WiFi : WAIT");
    u8g2.drawStr(2, 42, mqttOk  ? "MQTT : OK"   : "MQTT : WAIT");
    u8g2.drawStr(2, 54, slaveOk ? "Slave: OK"   : "Slave: WAIT");

    if (wifiOk && mqttOk && slaveOk) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Stable: %.1fs", stableLeftMs / 1000.0f);
        const uint16_t w = u8g2.getStrWidth(buf);
        u8g2.drawBox(128 - w - 6, 0, w + 6, 12);
        u8g2.setDrawColor(0);
        u8g2.drawStr(128 - w - 3, 10, buf);
        u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
    i2cUnlock();
}
