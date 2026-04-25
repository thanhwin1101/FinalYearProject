// ====================================================================
//  Carry Robot – ESP32 MASTER  –  main.cpp
//  ------------------------------------------------------------------
//  Implements the complete FSM described in the project spec:
//
//    BOOTING → CONNECTING → (fail) WIFI_AP_PORTAL  // WiFiManager captive
//                         → (ok)   AUTO_IDLE       // R1=OFF R2=ON
//
//    AUTO branch:
//      AUTO_IDLE → WAIT_START_OUTBOUND → EXECUTING_OUTBOUND
//        → AT_LAST_OUTBOUND → WAIT_START_RETURN → EXECUTING_RETURN
//        → DONE → AUTO_IDLE
//      Cancel: CANCEL_SEARCH_CP → WAIT_RECOVERY_ROUTE → EXECUTING_RECOVERY
//
//    Follow branch:
//      AUTO_IDLE -longPress-> FOLLOW_ACTIVE
//      FOLLOW_ACTIVE -longPress-> FOLLOW_TO_AUTO_RECOVERY (R1=ON,R2=ON)
//        → SEARCHING_LINE_45DEG → APPROACHING_LINE
//        → TRACKING_TO_CP → WEB_RECOVERY (waits route)
//        → EXECUTING_RECOVERY → RECOVERY_DONE → AUTO_IDLE (R1=OFF,R2=ON)
// ====================================================================
//  Wiring (ESP32 DevKit)
//    OLED SH1106 1.3" I2C  : SDA=GPIO21  SCL=GPIO22
//    Push button           : GPIO15 (INPUT_PULLUP, to GND)
//    Buzzer (active)       : GPIO25
//    Relay 1 (Vision)      : GPIO18  (HIGH = ON)
//    Relay 2 (Line/NFC)    : GPIO23  (HIGH = ON)
//    UART to STM32         : RX=GPIO16  TX=GPIO17  @ 115200
// ====================================================================
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "checkpoint_map.h"
#include "mqtt_config.h"
#include "button_logic.h"
#include "uart_master.h"

// --------------------------------------------------------------------
//  System-wide FSM
// --------------------------------------------------------------------
enum class Sys {
    BOOTING,
    CONNECTING,
    WIFI_AP_PORTAL,            // captive portal active – everything blocked
    AUTO_IDLE,                 // at MED, waiting for route or long-press

    WAIT_START_OUTBOUND,       // route loaded, waiting short press
    EXECUTING_OUTBOUND,        // outbound running
    AT_LAST_OUTBOUND,          // arrived, waiting short press for return
    WAIT_START_RETURN,         // (alias kept for clarity)
    EXECUTING_RETURN,          // return running
    DONE_AT_MED,               // briefly DONE, will fall back to AUTO_IDLE

    CANCEL_SEARCH_CP,          // cancel: keep tracking until next CP
    WAIT_RECOVERY_ROUTE,       // CP reached, waiting recovery from web
    EXECUTING_RECOVERY,        // recovery route running

    FOLLOW_ACTIVE,             // Follow Mode (HuskyLens tag track)

    FOLLOW_REC_START,          // R1=ON,R2=ON, tell slave: HUSKY=LINE
    FOLLOW_REC_LINE_SEARCH,    // 45° rotational search
    FOLLOW_REC_APPROACH,       // approach line
    FOLLOW_REC_TRACK_TO_CP,    // line-track until any CP
    FOLLOW_REC_WEB_WAIT,       // waiting recovery route from web
    FOLLOW_REC_RUN_ROUTE,      // running recovery route to MED
    FOLLOW_REC_DONE            // back at MED, restore relays
};

static Sys       g_state = Sys::BOOTING;
static uint32_t  g_arriveBeepUntil = 0;

// Pending route legs (compact format ready for slave: "id,act|...")
static String    g_pendingOutbound;
static String    g_pendingReturn;

// Mission tracking (assigned by backend on carry/robot/cmd → mission)
static String    g_missionId;
static String    g_lastNodeId;       // last CP reported by slave ("next CP" display)
static uint16_t  g_lastCpId = 0;     // numeric form for backend events

// OLED display context (populated from MQTT mission + UART frames)
static String    g_patientName;      // "David King"
static String    g_destLabel;        // "Room 4 Bed M2"
static bool      g_obstacleActive = false;
static uint32_t  g_wrongCpUntilMs = 0;
static String    g_wrongCpExp;       // expected CP name
static String    g_wrongCpRecv;      // received CP name

// Follow-mode tag telemetry (driven by slave <TAG_ID> / <TAG_LOST> frames).
static int       g_followTagId      = -1;     // last learned ID seen (-1 = none)
static bool      g_followLost       = false;  // currently in lost state
static uint32_t  g_followLostUntil  = 0;      // millis() deadline for 30 s alarm
static uint32_t  g_followLostBeepNext = 0;    // next periodic beep tick
// Defer <MODE:FOLLOW> until HuskyLens finishes booting (~3 s after R1 on).
static uint32_t  g_pendingFollowSendAt = 0;
// STM32 link watchdog (driven by <HB:...> heartbeats from slave every 2 s).
static uint32_t  g_lastSlaveHbMs       = 0;
static bool      g_slaveLinkOk         = false;

// --------------------------------------------------------------------
//  Globals
// --------------------------------------------------------------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C g_oled(U8G2_R0);
Button                             g_btn;
UartMaster                         g_uart;
WiFiManager                        g_wm;
Preferences                        g_prefs;

// Persistent MQTT broker IP (editable via WiFiManager portal).
static char g_mqttHost[32] = MQTT_HOST;

static void loadMqttHost() {
    g_prefs.begin("netcfg", true);
    String saved = g_prefs.getString("mqtt_host", MQTT_HOST);
    g_prefs.end();
    saved.toCharArray(g_mqttHost, sizeof(g_mqttHost));
}

static void saveMqttHost(const char* host) {
    if (!host || !*host) return;
    g_prefs.begin("netcfg", false);
    g_prefs.putString("mqtt_host", host);
    g_prefs.end();
    strncpy(g_mqttHost, host, sizeof(g_mqttHost) - 1);
    g_mqttHost[sizeof(g_mqttHost) - 1] = 0;
    Serial.printf("[NET] saved MQTT host: %s\n", g_mqttHost);
}

// Single WiFiManagerParameter shared across autoConnect() and
// startConfigPortal() so the portal form shows exactly one "MQTT
// Broker IP" field. It is added to g_wm only once (lazily).
static WiFiManagerParameter& mqttHostParam() {
    static WiFiManagerParameter p("mqtt_host", "MQTT Broker IP",
                                  g_mqttHost, sizeof(g_mqttHost) - 1);
    return p;
}
static void ensureMqttHostParamAdded() {
    static bool added = false;
    if (added) return;
    g_wm.addParameter(&mqttHostParam());
    added = true;
}
static void persistMqttHostFromParam() {
    const char* v = mqttHostParam().getValue();
    if (v && *v && strcmp(v, g_mqttHost) != 0) saveMqttHost(v);
}

// --------------------------------------------------------------------
//  Relays (per spec)
//    Auto / Auto-Cancel / Auto-Recovery : R1=OFF, R2=ON
//    Follow Mode                        : R1=ON,  R2=OFF
//    Follow→Auto Recovery               : R1=ON,  R2=ON
//
//  After toggling a relay the peripheral behind it (PN532, HuskyLens)
//  has just been power-cycled. The slave re-inits them when it sees
//  <MODE:...>, so we add a short settle delay before sending the mode
//  frame to give the modules time to finish booting.
// --------------------------------------------------------------------
static constexpr uint16_t RELAY_SETTLE_MS = 400;   // PN532 ~200, HuskyLens ~500
static const char* stateName(Sys s);   // fwd-decl so [RELAY] log can print state name
static const char* relayPolicyForState(Sys s);  // fwd-decl: "AUTO"/"FOLLOW"/"REC"

// Wrap raw relay writes so every transition is logged with caller hint.
static void relaysAuto()           { digitalWrite(PIN_RELAY_1, RELAY_OFF_LEVEL); digitalWrite(PIN_RELAY_2, RELAY_ON_LEVEL);
    Serial.printf("[RELAY] policy=AUTO    R1=%d R2=%d\n", digitalRead(PIN_RELAY_1), digitalRead(PIN_RELAY_2)); }
static void relaysFollow()         { digitalWrite(PIN_RELAY_1, RELAY_ON_LEVEL);  digitalWrite(PIN_RELAY_2, RELAY_OFF_LEVEL);
    Serial.printf("[RELAY] policy=FOLLOW  R1=%d R2=%d\n", digitalRead(PIN_RELAY_1), digitalRead(PIN_RELAY_2)); }
static void relaysFollowRecovery() { digitalWrite(PIN_RELAY_1, RELAY_ON_LEVEL);  digitalWrite(PIN_RELAY_2, RELAY_ON_LEVEL);
    Serial.printf("[RELAY] policy=REC     R1=%d R2=%d\n", digitalRead(PIN_RELAY_1), digitalRead(PIN_RELAY_2)); }

// State-driven relay enforcement: called from enter() so the relays
// always match the current FSM state regardless of which path took us
// there. Spec:
//   AUTO_*           -> R1 OFF, R2 ON   (line + RFID powered)
//   FOLLOW_ACTIVE    -> R1 ON,  R2 OFF  (HuskyLens + servo powered)
//   FOLLOW_REC_*     -> R1 ON,  R2 ON   (need both vision + line/RFID)
static void applyRelaysForState(Sys s) {
    switch (s) {
        case Sys::FOLLOW_ACTIVE:
            relaysFollow();
            break;
        case Sys::FOLLOW_REC_START:
        case Sys::FOLLOW_REC_LINE_SEARCH:
        case Sys::FOLLOW_REC_APPROACH:
        case Sys::FOLLOW_REC_TRACK_TO_CP:
        case Sys::FOLLOW_REC_WEB_WAIT:
        case Sys::FOLLOW_REC_RUN_ROUTE:
        case Sys::FOLLOW_REC_DONE:
            relaysFollowRecovery();
            break;
        default:
            // BOOT / CONNECT / WIFI portals + every AUTO/mission state
            relaysAuto();
            break;
    }
    Serial.printf("[RELAY] state=%-12s policy=%-6s R1=%d R2=%d\n",
                  stateName(s), relayPolicyForState(s),
                  digitalRead(PIN_RELAY_1),
                  digitalRead(PIN_RELAY_2));
}

// Returns the relay policy label this state must enforce.
static const char* relayPolicyForState(Sys s) {
    switch (s) {
        case Sys::FOLLOW_ACTIVE: return "FOLLOW";
        case Sys::FOLLOW_REC_START:
        case Sys::FOLLOW_REC_LINE_SEARCH:
        case Sys::FOLLOW_REC_APPROACH:
        case Sys::FOLLOW_REC_TRACK_TO_CP:
        case Sys::FOLLOW_REC_WEB_WAIT:
        case Sys::FOLLOW_REC_RUN_ROUTE:
        case Sys::FOLLOW_REC_DONE:
            return "REC";
        default: return "AUTO";
    }
}

// Periodic relay heartbeat (every 2s). Logs current state + relay pin
// readback so we can see live whether the relays are actually held high
// during recovery, follow, etc., across every state in every mode.
static void relayHeartbeat() {
    static uint32_t tNext = 0;
    if (millis() < tNext) return;
    tNext = millis() + 2000;
    Serial.printf("[RELAY HB] state=%-12s policy=%-6s R1=%d R2=%d\n",
                  stateName(g_state), relayPolicyForState(g_state),
                  digitalRead(PIN_RELAY_1),
                  digitalRead(PIN_RELAY_2));
}

// --------------------------------------------------------------------
//  Buzzer
// --------------------------------------------------------------------
static void beep(uint16_t ms) {
    digitalWrite(PIN_BUZZER, HIGH); delay(ms); digitalWrite(PIN_BUZZER, LOW);
}
static void beepStart(uint32_t durMs) { g_arriveBeepUntil = millis() + durMs; digitalWrite(PIN_BUZZER, HIGH); }
static void beepService() {
    if (g_arriveBeepUntil && millis() >= g_arriveBeepUntil) {
        digitalWrite(PIN_BUZZER, LOW);
        g_arriveBeepUntil = 0;
    }
    // ---- Continuous obstacle alarm (non-blocking 200/200 ms pattern) ----
    // Beeps only while the obstacle flag is set. Forced OFF the instant
    // the obstacle clears so the buzzer never "sticks" after the path is
    // free again.
    static bool     obstHigh = false;
    static uint32_t obstNext = 0;
    static bool     obstWas  = false;
    const bool obstNow = g_obstacleActive && (g_state != Sys::FOLLOW_ACTIVE);
    if (obstNow) {
        if (!obstWas) { obstHigh = false; obstNext = 0; }   // fresh trip
        if (millis() >= obstNext) {
            obstHigh = !obstHigh;
            digitalWrite(PIN_BUZZER, obstHigh ? HIGH : LOW);
            obstNext = millis() + 200;
        }
    } else if (obstWas) {
        // Obstacle just cleared → force buzzer OFF immediately.
        obstHigh = false;
        digitalWrite(PIN_BUZZER, LOW);
    }
    obstWas = obstNow;
}

// --------------------------------------------------------------------
//  OLED  (SH1106 128x64 via U8g2)
//  Font cheat-sheet:
//    u8g2_font_6x10_tr       : 6x10  (21 chars/line, ~6 lines)
//    u8g2_font_7x14B_tr      : 7x14 bold
//    u8g2_font_ncenB14_tr    : ~14px bold (BIG headers)
//    u8g2_font_logisoso24_tr : ~24px (OBSTACLE overlay)
// --------------------------------------------------------------------
static const char* stateName(Sys s) {
    switch (s) {
        case Sys::BOOTING:               return "BOOT";
        case Sys::CONNECTING:            return "CONNECT";
        case Sys::WIFI_AP_PORTAL:        return "AP MODE";
        case Sys::AUTO_IDLE:             return "AUTO IDLE";
        case Sys::WAIT_START_OUTBOUND:   return "WAIT START";
        case Sys::EXECUTING_OUTBOUND:    return "OUTBOUND";
        case Sys::AT_LAST_OUTBOUND:      return "ARRIVED";
        case Sys::WAIT_START_RETURN:     return "WAIT RETURN";
        case Sys::EXECUTING_RETURN:      return "RETURN";
        case Sys::DONE_AT_MED:           return "DONE";
        case Sys::CANCEL_SEARCH_CP:      return "CANCELLING";
        case Sys::WAIT_RECOVERY_ROUTE:   return "WAIT RECOV";
        case Sys::EXECUTING_RECOVERY:    return "RECOVERY";
        case Sys::FOLLOW_ACTIVE:         return "FOLLOW";
        case Sys::FOLLOW_REC_START:      return "F->A INIT";
        case Sys::FOLLOW_REC_LINE_SEARCH:return "SEARCH LINE";
        case Sys::FOLLOW_REC_APPROACH:   return "APPR LINE";
        case Sys::FOLLOW_REC_TRACK_TO_CP:return "TRACK CP";
        case Sys::FOLLOW_REC_WEB_WAIT:   return "WEB WAIT";
        case Sys::FOLLOW_REC_RUN_ROUTE:  return "RUN RECOV";
        case Sys::FOLLOW_REC_DONE:       return "REC DONE";
    }
    return "?";
}

// ---- small drawing primitives -------------------------------------
static inline bool blink500() { return (millis() / 500) & 1; }

static void drawHeaderBar(const char* text, bool inverted) {
    // Header occupies y=0..14 (15px). Bold 14px centered.
    g_oled.setFont(u8g2_font_7x14B_tr);
    const int w = g_oled.getUTF8Width(text);
    const int x = (128 - w) / 2;
    if (inverted) {
        g_oled.setDrawColor(1);
        g_oled.drawBox(0, 0, 128, 15);
        g_oled.setDrawColor(0);
        g_oled.drawStr(x, 12, text);
        g_oled.setDrawColor(1);
    } else {
        g_oled.drawStr(x, 12, text);
    }
    g_oled.drawHLine(0, 16, 128);
}

static void drawLine6x10(int y, const char* text) {
    g_oled.setFont(u8g2_font_6x10_tr);
    g_oled.drawStr(0, y, text);
}

static void drawCentered6x10(int y, const char* text) {
    g_oled.setFont(u8g2_font_6x10_tr);
    int w = g_oled.getUTF8Width(text);
    g_oled.drawStr((128 - w) / 2, y, text);
}

static void drawCenteredBig(int y, const char* text) {
    g_oled.setFont(u8g2_font_ncenB14_tr);
    int w = g_oled.getUTF8Width(text);
    g_oled.drawStr((128 - w) / 2, y, text);
}

// Truncate a String to a max number of chars (for 6x10 font on 128px).
static String clip(const String& s, size_t n) {
    if (s.length() <= n) return s;
    return s.substring(0, n);
}

// ---- per-state screen renderers -----------------------------------
static void drawBootOrConnecting() {
    // +-------------------------+
    // |      CARRY ROBOT        |  big
    // |-------------------------|
    // | WiFi: <status>          |
    // | MQTT: <status>          |
    // +-------------------------+
    drawCenteredBig(18, "AGV_01");
    g_oled.drawHLine(0, 24, 128);

    String wifi = "WiFi: ";
    if (WiFi.isConnected()) { wifi += WiFi.localIP().toString(); }
    else if (g_state == Sys::BOOTING) wifi += "Init...";
    else wifi += "Connecting...";
    drawLine6x10(40, clip(wifi, 21).c_str());

    String mqtt = "MQTT: ";
    mqtt += MqttCfg::isConnected() ? "Ready" : "Wait...";
    drawLine6x10(54, clip(mqtt, 21).c_str());
}

static void drawApPortal() {
    drawHeaderBar(blink500() ? "[!] NO CONNECTION" : "  NO CONNECTION  ", false);
    drawLine6x10(30, "Connect WiFi to:");
    String ap = "AP: ";    ap += WM_AP_SSID;
    drawLine6x10(42, clip(ap, 21).c_str());
    drawLine6x10(54, "IP: 192.168.4.1");
}

static void drawAutoIdle() {
    drawHeaderBar("AUTO MODE", true);
    String loc = "Loc: ";
    loc += g_lastNodeId.length() ? g_lastNodeId : String(START_CHECKPOINT);
    drawLine6x10(30, clip(loc, 21).c_str());
    drawLine6x10(42, "Status:   READY");
    drawLine6x10(62, "Waiting for Web...");
}

static void drawWaitStartOutbound() {
    drawHeaderBar("NEW MISSION", false);
    String pt = "Pt: "; pt += g_patientName.length() ? g_patientName : "—";
    drawLine6x10(30, clip(pt, 21).c_str());
    String to = "To: "; to += g_destLabel.length() ? g_destLabel : "—";
    drawLine6x10(42, clip(to, 21).c_str());
    if (blink500()) drawLine6x10(62, ">> [SHORT PRESS] GO");
}

static void drawExecutingMoving() {
    drawHeaderBar("MOVING... >>", false);
    String dst = "Dest: "; dst += g_destLabel.length() ? g_destLabel : "—";
    drawLine6x10(30, clip(dst, 21).c_str());
    String nx  = "Next CP: "; nx += g_lastNodeId.length() ? g_lastNodeId : "—";
    drawLine6x10(42, clip(nx, 21).c_str());
    drawLine6x10(62, "[Line Tracking ON]");
}

static void drawArrivedWaitReturn() {
    // Header blinks together with buzzer window (g_arriveBeepUntil).
    const bool beeping = (g_arriveBeepUntil != 0);
    const bool on = !beeping || blink500();
    drawHeaderBar(on ? "[ ARRIVED ]" : "           ", true);
    String pt = "Pt: "; pt += g_patientName.length() ? g_patientName : "—";
    drawLine6x10(30, clip(pt, 21).c_str());
    drawLine6x10(42, "Please collect items.");
    drawLine6x10(62, ">> [PRESS] TO RETURN");
}

static void drawDone() {
    drawHeaderBar("MISSION DONE", false);
    drawLine6x10(30, "All tasks completed.");
    drawLine6x10(42, "Returning to Idle...");
}

static void drawFollowActive() {
    drawHeaderBar("FOLLOW MODE", true);
    if (g_followLost && g_followLostUntil && millis() < g_followLostUntil) {
        // Alarm window: blink "TAG LOST" + show countdown.
        if (blink500()) drawLine6x10(30, "!! TAG LOST !!");
        else            drawLine6x10(30, "               ");
        uint32_t leftMs = g_followLostUntil - millis();
        char buf[24];
        snprintf(buf, sizeof(buf), "Search... %lus", (unsigned long)(leftMs / 1000UL + 1UL));
        drawLine6x10(42, buf);
    } else if (g_followTagId >= 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Tag ID: %d", g_followTagId);
        drawLine6x10(30, buf);
        drawLine6x10(42, "Tracking Target...");
    } else {
        drawLine6x10(30, "Tracking Target...");
        drawLine6x10(42, "HuskyLens TAG lock");
    }
    drawLine6x10(62, ">> [HOLD] TO EXIT");
}

static void drawRecovery(const char* status) {
    drawHeaderBar("RECOVERY MODE", false);
    String s = "Status: "; s += status;
    drawLine6x10(30, clip(s, 21).c_str());
    drawLine6x10(42, "Auto returning to MED");
    String cp = "CP: "; cp += g_lastNodeId.length() ? g_lastNodeId : "—";
    drawLine6x10(62, clip(cp, 21).c_str());
}

// ---- overlays (drawn on top) --------------------------------------
static void drawObstacleOverlay() {
    // Obstacle screen replaces whatever is beneath it.
    g_oled.clearBuffer();
    g_oled.setFont(u8g2_font_logisoso16_tr);
    const char* l1 = "!! OBSTACLE !!";
    int w = g_oled.getUTF8Width(l1);
    g_oled.drawStr((128 - w) / 2, 28, l1);
    const char* l2 = "PLEASE CLEAR";
    w = g_oled.getUTF8Width(l2);
    g_oled.drawStr((128 - w) / 2, 56, l2);
}

static void drawWrongCpOverlay() {
    g_oled.clearBuffer();
    drawHeaderBar("[!] SYSTEM ERROR", false);
    drawLine6x10(30, "WRONG CHECKPOINT");
    String exp = "Expected: "; exp += g_wrongCpExp.length() ? g_wrongCpExp : "?";
    drawLine6x10(42, clip(exp, 21).c_str());
    String got = "Read:     "; got += g_wrongCpRecv.length() ? g_wrongCpRecv : "?";
    drawLine6x10(54, clip(got, 21).c_str());
    drawLine6x10(64, "Requesting route...");
}

// ---- main dispatch ------------------------------------------------
static void oledRender() {
    // Overlays (highest priority). Obstacle is suppressed in FOLLOW_ACTIVE.
    if (g_wrongCpUntilMs && millis() < g_wrongCpUntilMs) {
        drawWrongCpOverlay();
        g_oled.sendBuffer();
        return;
    }
    if (g_obstacleActive && g_state != Sys::FOLLOW_ACTIVE) {
        drawObstacleOverlay();
        g_oled.sendBuffer();
        return;
    }

    g_oled.clearBuffer();
    switch (g_state) {
        case Sys::BOOTING:
        case Sys::CONNECTING:
            drawBootOrConnecting();            break;
        case Sys::WIFI_AP_PORTAL:
            drawApPortal();                    break;
        case Sys::AUTO_IDLE:
        case Sys::DONE_AT_MED:
            if (g_state == Sys::DONE_AT_MED) drawDone();
            else                             drawAutoIdle();
            break;
        case Sys::WAIT_START_OUTBOUND:
            drawWaitStartOutbound();           break;
        case Sys::EXECUTING_OUTBOUND:
        case Sys::EXECUTING_RETURN:
            drawExecutingMoving();             break;
        case Sys::AT_LAST_OUTBOUND:
        case Sys::WAIT_START_RETURN:
            drawArrivedWaitReturn();           break;
        case Sys::CANCEL_SEARCH_CP:
            drawRecovery("Stop at next CP");   break;
        case Sys::WAIT_RECOVERY_ROUTE:
            drawRecovery("Waiting Web Route"); break;
        case Sys::EXECUTING_RECOVERY:
            drawRecovery("Returning to MED");  break;
        case Sys::FOLLOW_ACTIVE:
            drawFollowActive();                break;
        case Sys::FOLLOW_REC_START:
            drawRecovery("Init Recovery");     break;
        case Sys::FOLLOW_REC_LINE_SEARCH:
            drawRecovery("Searching Line");    break;
        case Sys::FOLLOW_REC_APPROACH:
            drawRecovery("Approach Line");     break;
        case Sys::FOLLOW_REC_TRACK_TO_CP:
            drawRecovery("Track to CP");       break;
        case Sys::FOLLOW_REC_WEB_WAIT:
            drawRecovery("Waiting Web Route"); break;
        case Sys::FOLLOW_REC_RUN_ROUTE:
            drawRecovery("Returning to MED");  break;
        case Sys::FOLLOW_REC_DONE:
            drawDone();                        break;
    }
    g_oled.sendBuffer();
}

// Back-compat wrapper so `enter()` still compiles; arg is no longer used.
static void oledShow(const String& = "") { oledRender(); }

// --------------------------------------------------------------------
//  Helpers
// --------------------------------------------------------------------
static inline void enter(Sys s, const String& detail = "") {
    g_state = s;
    applyRelaysForState(s);   // enforce relay configuration for this state
    oledShow(detail);
    Serial.printf("[FSM] -> %s  (%s)\n", stateName(s), detail.c_str());
}

// Count steps in a compact route string ("a,F|b,L|c,F" → 3).
static int routeStepCount(const String& compact) {
    if (!compact.length()) return 0;
    int n = 1;
    for (size_t i = 0; i < compact.length(); ++i) if (compact[i] == '|') ++n;
    return n;
}

// --------------------------------------------------------------------
//  MQTT inbound callbacks (new backend contract)
// --------------------------------------------------------------------
static void onMission(const String& outboundCompact,
                      const String& returnCompact,
                      const String& missionId,
                      const String& patientName,
                      const String& destLabel) {
    g_pendingOutbound = outboundCompact;
    g_pendingReturn   = returnCompact;
    g_missionId       = missionId;
    if (patientName.length()) g_patientName = patientName;
    if (destLabel.length())   g_destLabel   = destLabel;
    if (!g_pendingOutbound.length()) return;

    g_uart.send("ROUTE", g_pendingOutbound);
    MqttCfg::publishRouteAccept(g_missionId, routeStepCount(g_pendingOutbound));
    beep(120);
    Serial.printf("[MISSION] received id=%s state=%s\n",
                  g_missionId.c_str(), stateName(g_state));
    // Accept new mission whenever we are not actively running another
    // route/recovery — go to WAIT_START so OLED shows "NEW MISSION".
    if (g_state == Sys::AUTO_IDLE          ||
        g_state == Sys::DONE_AT_MED        ||
        g_state == Sys::WAIT_START_OUTBOUND) {
        enter(Sys::WAIT_START_OUTBOUND, "press to start");
    }
}

static void handleShortPress();   // fwd

static void onStart() {
    // Backend may auto-trigger START (rarely used).
    if (g_state == Sys::WAIT_START_OUTBOUND || g_state == Sys::AT_LAST_OUTBOUND) {
        handleShortPress();
    }
}

static void onCancel() {
    bool valid = (g_state == Sys::EXECUTING_OUTBOUND ||
                  g_state == Sys::EXECUTING_RETURN   ||
                  g_state == Sys::EXECUTING_RECOVERY ||
                  g_state == Sys::FOLLOW_REC_RUN_ROUTE);
    if (!valid) return;
    g_uart.send("CANCEL_MISSION");
    MqttCfg::publishCancelAck();
    enter(Sys::CANCEL_SEARCH_CP, "stop at next CP");
}

static void onReturnRoute(const String& routeCompact) {
    // Backend response to robot/return_request — overrides any cached
    // return leg and is also used as the recovery route.
    if (g_state == Sys::AT_LAST_OUTBOUND) {
        g_pendingReturn = routeCompact;
        return;     // wait for short press to actually move
    }
    if (g_state == Sys::WAIT_RECOVERY_ROUTE) {
        g_uart.send("ROUTE", routeCompact);
        g_uart.send("START");
        MqttCfg::publishRouteAccept(g_missionId, routeStepCount(routeCompact));
        enter(Sys::EXECUTING_RECOVERY, "to MED");
    } else if (g_state == Sys::FOLLOW_REC_WEB_WAIT) {
        g_uart.send("ROUTE", routeCompact);
        g_uart.send("START");
        MqttCfg::publishRouteAccept(g_missionId, routeStepCount(routeCompact));
        enter(Sys::FOLLOW_REC_RUN_ROUTE, "to MED");
    }
}

// --------------------------------------------------------------------
//  UART inbound (from STM32)
//  Frames: <ARRIVED:id>, <DONE:id>, <CP_REACHED:id>, <WRONG_CP:id>,
//          <OBSTACLE:1|0>, <LINE_FOUND>, <LINE_LOCKED>
// --------------------------------------------------------------------
static void onUartFrame(const String& cmd, const String& data) {
    Serial.printf("[UART<-] %s : %s\n", cmd.c_str(), data.c_str());

    if (cmd == "OBSTACLE") {
        const bool blocked = (data == "1");
        // User spec: ignore SR05 completely while in FOLLOW_ACTIVE.
        if (g_state == Sys::FOLLOW_ACTIVE) {
            g_obstacleActive = false;
            return;
        }
        g_obstacleActive = blocked;
        MqttCfg::publishObstacle(blocked);
        // Buzzer is driven continuously by beepService() while
        // g_obstacleActive is true; no blocking beep here.
        if (!blocked) digitalWrite(PIN_BUZZER, LOW);
        return;
    }
    if (cmd == "CP_REACHED") {
        const bool unknown = (data.length() && data[0] == '?');
        const String nodeId = unknown ? String() : data;
        const uint16_t cpId = unknown ? 0 : nameToCpId(nodeId);
        if (!unknown) { g_lastNodeId = nodeId; g_lastCpId = cpId; }

        if (g_state == Sys::EXECUTING_OUTBOUND ||
            g_state == Sys::EXECUTING_RETURN  ||
            g_state == Sys::EXECUTING_RECOVERY||
            g_state == Sys::FOLLOW_REC_RUN_ROUTE) {
            MqttCfg::publishCheckpoint(cpId, nodeId);
        } else if (g_state == Sys::CANCEL_SEARCH_CP) {
            MqttCfg::publishRecoveryNfc(cpId);
            enter(Sys::WAIT_RECOVERY_ROUTE, nodeId);
            MqttCfg::publishReturnRequest(cpId);     // ask web for new route
        } else if (g_state == Sys::FOLLOW_REC_TRACK_TO_CP) {
            MqttCfg::publishRecoveryNfc(cpId);
            enter(Sys::FOLLOW_REC_WEB_WAIT, nodeId);
            MqttCfg::publishReturnRequest(cpId);
        }
        return;
    }
    if (cmd == "WRONG_CP") {
        const bool unknown = (data.length() && data[0] == '?');
        const String recvName = unknown ? String() : data;
        const uint16_t recvId = unknown ? 0 : nameToCpId(recvName);
        MqttCfg::publishCpMismatch(recvId, recvName, /*expId*/0, String("?"));

        // Populate overlay (shown on top of current state for ~3s).
        g_wrongCpRecv   = recvName.length() ? recvName : String("?");
        g_wrongCpExp    = g_lastNodeId.length() ? g_lastNodeId : String("?");
        g_wrongCpUntilMs = millis() + 3000;

        // FSM bug 1 fix: any EXECUTING_* must transition to a WAIT_*
        // state instead of staying mid-route while the slave has
        // already braked + 180°.
        switch (g_state) {
            case Sys::EXECUTING_OUTBOUND:
            case Sys::EXECUTING_RETURN:
            case Sys::EXECUTING_RECOVERY:
                enter(Sys::WAIT_RECOVERY_ROUTE, "wrong cp");
                if (recvId) MqttCfg::publishReturnRequest(recvId);
                break;
            case Sys::FOLLOW_REC_RUN_ROUTE:
                enter(Sys::FOLLOW_REC_WEB_WAIT, "wrong cp");
                if (recvId) MqttCfg::publishReturnRequest(recvId);
                break;
            default: break;
        }
        return;
    }
    if (cmd == "ARRIVED") {
        const String nodeId = data;
        const uint16_t cpId = nameToCpId(nodeId);
        g_lastNodeId = nodeId; g_lastCpId = cpId;
        MqttCfg::publishArrivedDestination(g_missionId, cpId, nodeId);
        // Ask backend for the (possibly fresh) return route from here.
        if (cpId) MqttCfg::publishReturnRequest(cpId);
        beepStart(ARRIVED_BEEP_MS);
        enter(Sys::AT_LAST_OUTBOUND, nodeId);
        return;
    }
    if (cmd == "DONE") {
        const String nodeId = data;
        MqttCfg::publishMissionDone(g_missionId);
        // FSM bug 3 fix: explicitly drop the slave back to AUTO mode
        // so a previous Follow / Follow-Recovery context cannot leak.
        relaysAuto();
        delay(RELAY_SETTLE_MS);
        g_uart.send("MODE", "AUTO");
        g_missionId = String();
        if (g_state == Sys::FOLLOW_REC_RUN_ROUTE) enter(Sys::FOLLOW_REC_DONE, "MED");
        else                                       enter(Sys::DONE_AT_MED,    "MED");
        enter(Sys::AUTO_IDLE);
        return;
    }
    if (cmd == "LINE_FOUND") {
        if (g_state == Sys::FOLLOW_REC_LINE_SEARCH) {
            enter(Sys::FOLLOW_REC_APPROACH, "approach");
            g_uart.send("APPROACH_LINE");
        }
        return;
    }
    if (cmd == "LINE_LOCKED") {
        if (g_state == Sys::FOLLOW_REC_APPROACH) {
            enter(Sys::FOLLOW_REC_TRACK_TO_CP, "track CP");
            g_uart.send("TRACK_TO_CP");
        }
        return;
    }
    if (cmd == "TAG_ID") {
        // HuskyLens learned-tag ID for the OLED follow screen.
        g_followTagId = data.toInt();
        return;
    }
    if (cmd == "TAG_LOST") {
        const bool lost = (data == "1");
        if (lost) {
            // Spec: 30 s alarm when tag is lost AFTER first sighting.
            if (!g_followLost) {
                g_followLost         = true;
                g_followLostUntil    = millis() + 30000UL;
                g_followLostBeepNext = millis();   // beep immediately
                {
                    JsonDocument d; d["evt"] = "tag_lost";
                    MqttCfg::publishEvent(d);
                }
            }
        } else {
            // Tag re-acquired \u2014 silence the alarm.
            if (g_followLost) {
                g_followLost      = false;
                g_followLostUntil = 0;
                digitalWrite(PIN_BUZZER, LOW);
            }
        }
        return;
    }
    if (cmd == "HB") {
        // Slave heartbeat — mark link alive and forward to serial monitor.
        g_lastSlaveHbMs = millis();
        if (!g_slaveLinkOk) {
            g_slaveLinkOk = true;
            Serial.println("[LINK] STM32 link UP");
        }
        Serial.printf("[STM32] %s\n", data.c_str());
        return;
    }
}

// --------------------------------------------------------------------
//  Button transitions
// --------------------------------------------------------------------
static void handleShortPress() {
    switch (g_state) {
        case Sys::WAIT_START_OUTBOUND:
            g_uart.send("START");
            enter(Sys::EXECUTING_OUTBOUND);
            break;
        case Sys::AT_LAST_OUTBOUND:
            // Push the return-leg route, then START.
            if (g_pendingReturn.length()) {
                g_uart.send("ROUTE", g_pendingReturn);
                g_pendingReturn = "";
            }
            g_uart.send("START");
            enter(Sys::EXECUTING_RETURN);
            break;
        default:
            break;
    }
}

static void handleLongPress() {
    if (g_state == Sys::AUTO_IDLE) {
        // Spec: long press only valid from AUTO_IDLE → enter Follow.
        relaysFollow();
        // HuskyLens needs ~2-3 s to boot fully + finish its splash before
        // it answers UART. Give it 3 s before telling the slave to switch
        // to FOLLOW (the slave will then reinit() over Serial3).
        // We DON'T block here; instead enter FOLLOW_ACTIVE immediately
        // for OLED feedback and arm a deferred-send timer below.
        // Mode-change feedback: 2 short beeps.
        beep(120); delay(80); beep(120);
        // Tell backend we're in Follow → frontend Mode column updates.
        {
            JsonDocument d; d["evt"] = "mode"; d["mode"] = "follow";
            MqttCfg::publishEvent(d);
        }
        // Reset any prior follow telemetry.
        g_followTagId      = -1;
        g_followLost       = false;
        g_followLostUntil  = 0;
        // Defer <MODE:FOLLOW> by 3 s so HuskyLens finishes booting first.
        g_pendingFollowSendAt = millis() + 3000;
        enter(Sys::FOLLOW_ACTIVE, "tag track (warmup 3s)");
    } else if (g_state == Sys::FOLLOW_ACTIVE) {
        // Long press in Follow → start Follow→Auto recovery.
        relaysFollowRecovery();
        delay(RELAY_SETTLE_MS);              // R2 just turned ON → PN532 re-init
        g_uart.send("MODE", "FOLLOW_RECOVERY");
        // If a deferred MODE:FOLLOW was still pending, cancel it.
        g_pendingFollowSendAt = 0;
        // Mode-change feedback: 2 short beeps.
        beep(120); delay(80); beep(120);
        // Leaving Follow → backend goes back to auto-derived mode.
        {
            JsonDocument d; d["evt"] = "mode"; d["mode"] = "auto";
            MqttCfg::publishEvent(d);
        }
        // Cancel any pending lost-tag alarm.
        g_followLost      = false;
        g_followLostUntil = 0;
        digitalWrite(PIN_BUZZER, LOW);
        enter(Sys::FOLLOW_REC_START, "init");
        // Tell slave to start the 45° line search routine.
        g_uart.send("SEARCH_LINE_45");
        enter(Sys::FOLLOW_REC_LINE_SEARCH, "rotating");
    }
    // long press ignored in any other state
}

// --------------------------------------------------------------------
//  Boot / Connect / Captive Portal
// --------------------------------------------------------------------
static void runWifiManager() {
    enter(Sys::CONNECTING, "WiFi/MQTT");
    WiFi.mode(WIFI_STA);
    g_wm.setConfigPortalTimeout(WM_PORTAL_TIMEOUT_S);   // 0 = block forever
    g_wm.setDebugOutput(false);

    // Show the AP-portal screen the moment WiFiManager starts its AP.
    g_wm.setAPCallback([](WiFiManager*){
        g_state = Sys::WIFI_AP_PORTAL;
        oledRender();
    });

    // Custom param: MQTT broker IP (editable in captive portal).
    ensureMqttHostParamAdded();
    g_wm.setSaveConfigCallback([](){
        // portal closed with a saved config
    });

    bool ok = g_wm.autoConnect(WM_AP_SSID, WM_AP_PASS);
    persistMqttHostFromParam();
    if (!ok) {
        enter(Sys::WIFI_AP_PORTAL, "configure");
        delay(500);
        ESP.restart();
    }
    Serial.print("[WIFI] OK IP="); Serial.println(WiFi.localIP());
}

// --------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] Carry Robot Master");

    pinMode(PIN_BUZZER,  OUTPUT); digitalWrite(PIN_BUZZER,  LOW);
    pinMode(PIN_RELAY_1, OUTPUT);
    pinMode(PIN_RELAY_2, OUTPUT);
    relaysAuto();                                          // safe default

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    g_oled.begin();
    enter(Sys::BOOTING, "init");

    g_btn.begin(PIN_BUTTON);
    g_uart.begin(onUartFrame);

    // Reset slave state on master boot: drop any cached route from a
    // previous run and force AUTO mode. Without this, an ESP32-only
    // reset would leave STM32 holding the last route in g_route[].
    delay(50);                                  // let STM32 UART settle
    g_uart.send("CANCEL_MISSION");
    g_uart.send("ROUTE", "");
    g_uart.send("MODE", "AUTO");

    // Load persisted MQTT broker IP (fallback = compiled default).
    loadMqttHost();
    Serial.printf("[NET] MQTT host: %s\n", g_mqttHost);

    // Bring up Wi-Fi (captive portal blocks here if needed)
    runWifiManager();

    // ---- ArduinoOTA (over-the-air firmware update) ----------------
    ArduinoOTA.setHostname("AGV-01");
    ArduinoOTA.setPassword("ota123456");
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Start");
        g_oled.clearBuffer();
        g_oled.setFont(u8g2_font_7x14B_tr);
        g_oled.drawStr(10, 30, "OTA UPDATE...");
        g_oled.sendBuffer();
        digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        int pct = (int)((done * 100UL) / total);
        g_oled.clearBuffer();
        g_oled.setFont(u8g2_font_7x14B_tr);
        g_oled.drawStr(10, 20, "OTA UPDATE");
        char buf[16]; snprintf(buf, sizeof(buf), "%d%%", pct);
        g_oled.setFont(u8g2_font_ncenB14_tr);
        int w = g_oled.getUTF8Width(buf);
        g_oled.drawStr((128 - w) / 2, 50, buf);
        g_oled.drawFrame(4, 54, 120, 10);
        g_oled.drawBox(4, 54, (unsigned int)(120 * done / total), 10);
        g_oled.sendBuffer();
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] Done – rebooting");
        digitalWrite(PIN_BUZZER, HIGH); delay(300); digitalWrite(PIN_BUZZER, LOW);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Error %u\n", e);
        g_oled.clearBuffer();
        g_oled.setFont(u8g2_font_7x14B_tr);
        g_oled.drawStr(10, 30, "OTA ERROR");
        g_oled.sendBuffer();
        digitalWrite(PIN_BUZZER, HIGH); delay(800); digitalWrite(PIN_BUZZER, LOW);
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] Ready — hostname AGV-01, port 3232");
    MqttCfg::begin();
    MqttCfg::setHost(g_mqttHost);
    MqttCfg::setCallbacks(onMission, onStart, onCancel, onReturnRoute);

    relaysAuto();                                          // confirm Auto state
    // Stay in CONNECTING until MQTT is actually connected (handled in loop()).
    enter(Sys::CONNECTING, "MQTT...");
}

// --------------------------------------------------------------------
//  Open the WiFiManager captive portal on-demand (e.g. when MQTT times
//  out). Blocks here until the user reconfigures, then reboots.
// --------------------------------------------------------------------
static void openConfigPortalAndRestart(const char* reason) {
    Serial.printf("[NET] opening config portal: %s\n", reason);
    g_state = Sys::WIFI_AP_PORTAL;
    oledRender();
    g_wm.setConfigPortalTimeout(0);
    // Reuse the single shared MQTT-host parameter (already added during
    // runWifiManager; ensureMqttHostParamAdded() is a no-op the 2nd time).
    ensureMqttHostParamAdded();
    g_wm.startConfigPortal(WM_AP_SSID, WM_AP_PASS);
    persistMqttHostFromParam();
    delay(500);
    ESP.restart();
}

// --------------------------------------------------------------------
void loop() {
    ArduinoOTA.handle();
    MqttCfg::loop();
    g_uart.loop();
    beepService();
    relayHeartbeat();

    // ---- Boot-time MQTT gate ------------------------------------------
    // Do not enter AUTO_IDLE until MQTT is actually connected. If it
    // fails to connect within 10s after boot, open the captive portal
    // so the user can fix WiFi / broker settings.
    static uint32_t sMqttWaitStart = millis();
    if (g_state == Sys::CONNECTING) {
        if (MqttCfg::isConnected()) {
            enter(Sys::AUTO_IDLE, "ready");
        } else if (millis() - sMqttWaitStart > 10000) {
            openConfigPortalAndRestart("MQTT timeout 10s");
        }
    } else {
        // Keep the timer aligned for any future re-entry into CONNECTING.
        sMqttWaitStart = millis();
    }

    BtnEvent ev = g_btn.poll();
    if      (ev == BtnEvent::SHORT_PRESS) handleShortPress();
    else if (ev == BtnEvent::LONG_PRESS)  handleLongPress();

    // ---- Deferred <MODE:FOLLOW> after HuskyLens warm-up (3 s) -------
    if (g_pendingFollowSendAt && millis() >= g_pendingFollowSendAt) {
        g_pendingFollowSendAt = 0;
        g_uart.send("MODE", "FOLLOW");
        Serial.println("[FOLLOW] HuskyLens warm-up done → MODE:FOLLOW sent");
    }

    // ---- STM32 link watchdog (no <HB> for >5 s ⇒ link DOWN) ---------
    {
        const uint32_t now = millis();
        if (g_slaveLinkOk && (now - g_lastSlaveHbMs > 5000)) {
            g_slaveLinkOk = false;
            Serial.println("[LINK] STM32 link DOWN — no HB for >5s");
        }
    }

    // ---- Periodic heartbeat to backend (every 3s while MQTT up) ------
    static uint32_t tHb = 0;
    if (MqttCfg::isConnected() && millis() - tHb > 3000) {
        tHb = millis();
        String curLoc = g_lastNodeId.length() ? g_lastNodeId : String(START_CHECKPOINT);
        MqttCfg::publishHello(curLoc);
    }

    // ---- Obstacle alarm: beep every 400 ms while blocked -----------
    static uint32_t tObBeep = 0;
    if (g_obstacleActive && g_state != Sys::FOLLOW_ACTIVE &&
        millis() - tObBeep > 400) {
        tObBeep = millis();
        digitalWrite(PIN_BUZZER, HIGH); delay(60); digitalWrite(PIN_BUZZER, LOW);
    }

    // ---- Lost-tag alarm: 30 s buzzer pulse train while in Follow ---
    if (g_state == Sys::FOLLOW_ACTIVE && g_followLost && g_followLostUntil) {
        if (millis() >= g_followLostUntil) {
            // Window expired — silence the buzzer but keep the lost
            // flag so OLED still reflects the situation.
            g_followLostUntil = 0;
            digitalWrite(PIN_BUZZER, LOW);
        } else if ((int32_t)(millis() - g_followLostBeepNext) >= 0) {
            g_followLostBeepNext = millis() + 500;     // pulse every 500 ms
            digitalWrite(PIN_BUZZER, HIGH); delay(120); digitalWrite(PIN_BUZZER, LOW);
        }
    }

    static uint32_t tOled = 0;
    if (millis() - tOled > 120) { tOled = millis(); oledRender(); }
}
