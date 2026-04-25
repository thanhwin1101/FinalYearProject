// ====================================================================
//  Carry Robot – STM32 SLAVE  –  main.cpp
//  ------------------------------------------------------------------
//  Implements the FSM described in the project spec.
//
//  UART contract with ESP32 (Master)  – framing: <CMD> or <CMD:DATA>
//  ----------------------------------------------------------------
//  RX (master → slave)
//    <ROUTE:id1,act1|id2,act2|...>   load route (compact, no malloc)
//    <START>                         begin executing the loaded route
//    <CANCEL_MISSION>                drop route, stop at next ANY tag
//    <MODE:FOLLOW>                   enter Follow Mode (HuskyLens TAG)
//    <MODE:FOLLOW_RECOVERY>          enter recovery sub-mode (TAG→LINE)
//    <MODE:AUTO>                     leave Follow / return to Auto idle
//    <SEARCH_LINE_45>                start 45°-rotational line search
//    <APPROACH_LINE>                 creep forward toward detected line
//    <TRACK_TO_CP>                   line-track until any RFID tag
//
//  TX (slave → master)
//    <CP_REACHED:id>                 mid-route checkpoint passed
//    <WRONG_CP:id>                   scanned id ≠ expected id
//    <ARRIVED:id>                    finished a non-MED route (after 180°)
//    <DONE:id>                       finished a route ending at MED
//    <OBSTACLE:1|0>                  SR05 brake / clear
//    <LINE_FOUND>                    HuskyLens LINE algorithm sees a line
//    <LINE_LOCKED>                   line under the 3-eye sensor
//
//  Action codes inside <ROUTE>
//    'F' = Forward / ignore the junction (just pass through the CP)
//    'L' = brake → 90° tank turn LEFT  → resume line follow
//    'R' = brake → 90° tank turn RIGHT → resume line follow
// ====================================================================
#include <Arduino.h>
#include "config.h"
#include "motor_tank_drive.h"
#include "line_and_rfid.h"
#include "huskylens_follow_pid.h"

// ------ UART handles ------------------------------------------------
#define UART_ESP        Serial1
#define UART_HUSKY      Serial3
#define MED_ID          START_CHECKPOINT

// ------ Globals -----------------------------------------------------
MotorTankDrive  g_drive;
LineFollower    g_line;
RfidCheckpoint  g_rfid;
HuskyFollowPID  g_follow;

// --------------------------------------------------------------------
//  Fixed-size route container  (no malloc, no String accumulation)
// --------------------------------------------------------------------
struct Checkpoint {
    char id[MAX_NODE_ID_LEN];   // nodeId or rfidUid – whichever the
                                // master chose as the match key
    char action;                // 'F' / 'L' / 'R'
};

static Checkpoint g_route[MAX_ROUTE_STEPS];
static uint8_t    g_routeN   = 0;
static uint8_t    g_routeIdx = 0;

// --------------------------------------------------------------------
//  System state machine
// --------------------------------------------------------------------
enum class Mode  { AUTO, FOLLOW, FOLLOW_RECOVERY };
enum class Phase {
    IDLE,                   // no route or just finished
    ROUTE_LOADED,           // got <ROUTE>, waiting <START>
    EXECUTING,              // line-follow + checkpoint match
    CANCEL_SEARCH_CP,       // <CANCEL_MISSION>: stop at next ANY tag

    // Follow→Auto recovery sub-states
    FREC_LINE_SEARCH,       // 45° rotational search, HuskyLens=LINE
    FREC_APPROACH,          // creep forward toward detected line
    FREC_TRACK_TO_CP        // line-follow until any RFID tag
};

static Mode  g_mode  = Mode::AUTO;
static Phase g_phase = Phase::IDLE;
static bool  g_obstacle = false;
static int16_t g_lastSr05Cm = 999;  // last raw SR05 reading (debug telemetry)
static String g_lastHandledCp;     // FSM-level dedup: prevents re-processing same CP
static int16_t g_lastTagId = -1;   // last HuskyLens tag ID seen (-1 if none)

// --------------------------------------------------------------------
//  Mode / phase short-name helpers (for <HB> heartbeat to master)
// --------------------------------------------------------------------
static const char* modeShort(Mode m) {
    switch (m) {
        case Mode::AUTO:            return "AUTO";
        case Mode::FOLLOW:          return "FOLLOW";
        case Mode::FOLLOW_RECOVERY: return "FREC";
    }
    return "?";
}
static const char* phaseShort(Phase p) {
    switch (p) {
        case Phase::IDLE:             return "IDLE";
        case Phase::ROUTE_LOADED:     return "ROUTE";
        case Phase::EXECUTING:        return "EXEC";
        case Phase::CANCEL_SEARCH_CP: return "CANCEL";
        case Phase::FREC_LINE_SEARCH: return "FSEARCH";
        case Phase::FREC_APPROACH:    return "FAPPR";
        case Phase::FREC_TRACK_TO_CP: return "FTRACK";
    }
    return "?";
}

// --------------------------------------------------------------------
//  UART framing
// --------------------------------------------------------------------
static char   g_rxBuf[UART_FRAME_MAX];
static size_t g_rxLen   = 0;
static bool   g_inFrame = false;

// --------------------------------------------------------------------
//  CRC8 (poly 0x07, init 0x00)  —  NFR-06: silent-drop corrupt frames
// --------------------------------------------------------------------
static uint8_t crc8_compute(const char* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

static inline void uartSend(const char* cmd) {
    uint8_t crc = crc8_compute(cmd, strlen(cmd));
    char hex[3]; snprintf(hex, sizeof(hex), "%02X", crc);
    UART_ESP.print('<'); UART_ESP.print(cmd);
    UART_ESP.print('|'); UART_ESP.print(hex); UART_ESP.print('>');
}
static inline void uartSend(const char* cmd, const char* data) {
    char content[UART_FRAME_MAX];
    snprintf(content, sizeof(content), "%s:%s", cmd, data);
    uint8_t crc = crc8_compute(content, strlen(content));
    char hex[3]; snprintf(hex, sizeof(hex), "%02X", crc);
    UART_ESP.print('<'); UART_ESP.print(content);
    UART_ESP.print('|'); UART_ESP.print(hex); UART_ESP.print('>');
}
static inline void uartSend(const char* cmd, const String& data) {
    uartSend(cmd, data.c_str());
}

static void onFrame(const char* cmd, const char* data);   // fwd
static void heartbeatService();                            // fwd

static void uartPoll() {
    while (UART_ESP.available()) {
        char c = (char)UART_ESP.read();
        if (c == '<') { g_rxLen = 0; g_inFrame = true; continue; }
        if (c == '>' && g_inFrame) {
            g_inFrame = false;
            g_rxBuf[g_rxLen] = '\0';
            // CRC8 validation (NFR-06): last '|XX' must match
            char* pipe = strrchr(g_rxBuf, '|');
            if (!pipe || strlen(pipe + 1) != 2) { g_rxLen = 0; continue; }
            uint8_t rxCrc   = (uint8_t)strtol(pipe + 1, nullptr, 16);
            uint8_t calcCrc = crc8_compute(g_rxBuf, (size_t)(pipe - g_rxBuf));
            if (rxCrc != calcCrc) { g_rxLen = 0; continue; } // silently drop
            *pipe = '\0';  // strip CRC suffix
            char* sep = strchr(g_rxBuf, ':');
            if (sep) { *sep = '\0'; onFrame(g_rxBuf, sep + 1); }
            else     { onFrame(g_rxBuf, "");                    }
            g_rxLen = 0;
            continue;
        }
        if (g_inFrame && g_rxLen < sizeof(g_rxBuf) - 1) g_rxBuf[g_rxLen++] = c;
    }
}

// --------------------------------------------------------------------
//  Route parser:  "id1,act1|id2,act2|..."
//  Robust: no malloc, no String accumulation, bounded copies.
// --------------------------------------------------------------------
static void parseRoute(const char* payload) {
    g_routeN = 0; g_routeIdx = 0;
    if (!payload || !*payload) return;

    const char* p = payload;
    while (*p && g_routeN < MAX_ROUTE_STEPS) {
        Checkpoint& cp = g_route[g_routeN];

        // Copy id up to ',' or '|' or end
        size_t i = 0;
        while (*p && *p != ',' && *p != '|' && i < MAX_NODE_ID_LEN - 1) {
            cp.id[i++] = *p++;
        }
        cp.id[i] = '\0';

        // Default action if none supplied
        cp.action = 'F';

        // Skip past id-terminator
        while (*p && *p != ',' && *p != '|') ++p;
        if (*p == ',') {
            ++p;
            if (*p && *p != '|') { cp.action = *p; ++p; }
        }
        // Skip until next '|' or end
        while (*p && *p != '|') ++p;
        if (*p == '|') ++p;

        if (i > 0) g_routeN++;
    }
}

// Reset dedup sentinel whenever a new route is loaded.
static void resetCheckpointDedup() { g_lastHandledCp = ""; }

static const Checkpoint* expectedCp() {
    if (g_routeIdx >= g_routeN) return nullptr;
    return &g_route[g_routeIdx];
}

// --------------------------------------------------------------------
//  Ultrasonic obstacle service (median-of-3 + 2-strike confirm)
//  Rationale: a single pulseIn() call can return 0 (no echo) or a
//  spurious large value, which previously left g_obstacle stuck at
//  false even when the robot was clearly inside SR05_STOP_CM. We now
//  read three pulses, take the median, and require two consecutive
//  in-range samples before braking — this filters out flicker but
//  still reacts within ~120 ms.
// --------------------------------------------------------------------
static long sr05ReadCmOnce() {
    digitalWrite(PIN_SR05_TRIG, LOW);  delayMicroseconds(3);
    digitalWrite(PIN_SR05_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_SR05_TRIG, LOW);
    long us = pulseIn(PIN_SR05_ECHO, HIGH, 30000);   // 30 ms ≈ 5 m
    if (us == 0) return 999;                          // timeout = far
    long cm = us / 58;
    if (cm < 2 || cm > 400) return 999;               // out-of-range
    return cm;
}
static long sr05ReadCm() {
    long a = sr05ReadCmOnce(); delayMicroseconds(800);
    long b = sr05ReadCmOnce(); delayMicroseconds(800);
    long c = sr05ReadCmOnce();
    // median of 3
    if (a > b) { long t = a; a = b; b = t; }
    if (b > c) { long t = b; b = c; c = t; }
    if (a > b) { long t = a; a = b; b = t; }
    return b;
}
static void obstacleService() {
    static uint32_t tNext = 0;
    static uint8_t  hits  = 0;          // consecutive in-range samples
    static uint8_t  miss  = 0;          // consecutive clear samples
    static uint32_t tDbg  = 0;
    if (millis() < tNext) return;
    tNext = millis() + 60;

    long cm = sr05ReadCm();
    g_lastSr05Cm = (int16_t)cm;

    // Throttled debug — every 500 ms send raw cm + state to master so the
    // serial monitor shows whether the SR05 is actually clearing.
    if (millis() >= tDbg) {
        tDbg = millis() + 500;
        char buf[40];
        snprintf(buf, sizeof(buf), "cm=%ld obs=%d hits=%u miss=%u",
                 cm, g_obstacle ? 1 : 0, (unsigned)hits, (unsigned)miss);
        uartSend("SR05", buf);
    }

    if (!g_obstacle) {
        if (cm > 0 && cm < SR05_STOP_CM) {
            if (++hits >= 2) {           // 2-strike → trip
                g_obstacle = true;
                g_drive.stop();
                uartSend("OBSTACLE", "1");
                hits = 0;
                miss = 0;
            }
        } else {
            hits = 0;
        }
    } else {
        // A full pulseIn timeout (cm == 999) means "no echo at all" →
        // strongly indicates the path is clear. Treat it as an immediate
        // resume so a brief sensor glitch can't keep the buzzer stuck.
        if (cm >= 999) {
            g_obstacle = false;
            uartSend("OBSTACLE", "0");
            miss = 0;
            hits = 0;
        } else if (cm >= SR05_RESUME_CM) {
            if (++miss >= 2) {           // 2-strike → resume
                g_obstacle = false;
                uartSend("OBSTACLE", "0");
                miss = 0;
                hits = 0;
            }
        } else {
            miss = 0;
        }
    }
}

// --------------------------------------------------------------------
//  Auto-mode helpers
// --------------------------------------------------------------------
static void executeAction(char action) {
    g_drive.brake();
    switch (action) {
        case 'L': g_drive.tankTurn90Left();  break;
        case 'R': g_drive.tankTurn90Right(); break;
        case 'B': g_drive.tankTurn180();     break;   // backend first-step return
        case 'F':
        default:  /* nothing – just pass the CP */ break;
    }
}

static void finishRoute(const char* cpId) {
    g_drive.brake();
    g_drive.tankTurn180();                                 // spec: 180° at end
    if (strcmp(cpId, MED_ID) == 0) uartSend("DONE",    cpId);
    else                            uartSend("ARRIVED", cpId);
    g_phase    = Phase::IDLE;
    g_routeN   = 0;
    g_routeIdx = 0;
}

// Called once per detected tag while in Auto / TRACK_TO_CP mode
static void handleCheckpoint(const String& scannedId) {
    // FSM-level dedup: prevents same CP re-firing during a 90/180 turn.
    // NFC_REPEAT_MS (700 ms) < TURN_90_MS (900 ms) / TURN_180_MS (1900 ms),
    // so without this guard the same tag fires WRONG_CP mid-turn.
    if (scannedId == g_lastHandledCp) return;
    g_lastHandledCp = scannedId;

    // Sentinel "?XXXX" means a tag was read but its UID is not in the map.
    const bool unknown = scannedId.length() && scannedId[0] == '?';

    if (g_phase == Phase::EXECUTING) {
        const Checkpoint* exp = expectedCp();
        if (!exp) return;

        if (unknown) {
            // Off-route tag: brake, 180°, escalate to master.
            g_drive.brake();
            g_drive.tankTurn180();
            uartSend("WRONG_CP", scannedId);
            g_phase = Phase::IDLE; g_routeN = 0; g_routeIdx = 0;
            return;
        }

        if (scannedId == exp->id) {
            char act  = exp->action;
            bool last = (g_routeIdx == g_routeN - 1);
            if (last) {
                finishRoute(exp->id);
            } else {
                uartSend("CP_REACHED", exp->id);
                executeAction(act);
                g_routeIdx++;
            }
        } else {
            // Wrong CP encountered: brake, 180° turn, ask master.
            g_drive.brake();
            g_drive.tankTurn180();
            uartSend("WRONG_CP", scannedId);
            g_phase    = Phase::IDLE;
            g_routeN   = 0;
            g_routeIdx = 0;
        }
        return;
    }

    if (g_phase == Phase::CANCEL_SEARCH_CP ||
        g_phase == Phase::FREC_TRACK_TO_CP) {
        // Stop at any tag — including unknown ones (the master will
        // resolve via the web recovery route).
        g_drive.brake();
        uartSend("CP_REACHED", scannedId);
        g_phase = Phase::IDLE;
    }
}

// --------------------------------------------------------------------
//  FOLLOW_RECOVERY support
// --------------------------------------------------------------------
static void enterFollowRecovery() {
    g_mode = Mode::FOLLOW_RECOVERY;
    g_follow.enable(false);                // disable PID + parks servo at 90°
    g_follow.useLineAlgorithm();           // switch HuskyLens to LINE
    g_follow.parkAtRecovery();             // tilt servo DOWN to 45° to look at the line
    g_drive.stop();
    g_phase = Phase::IDLE;
}

// One step of the rotational search (called from loop).
static void searchLineTick() {
    if (g_obstacle) return;
    g_drive.tankTurn45Right();             // discrete 45° hop
    delay(150);                            // settle for HuskyLens frame
    if (g_follow.seesLine()) {
        uartSend("LINE_FOUND");
        g_phase = Phase::FREC_APPROACH;
    }
}

// Creep forward until the 3-eye sensor confirms a line under the chassis.
static void approachLineTick() {
    if (g_obstacle) return;
    g_drive.drive(LF_BASE_PWM / 2, LF_BASE_PWM / 2);
    bool l = digitalRead(PIN_LINE_L) == LOW;
    bool c = digitalRead(PIN_LINE_C) == LOW;
    bool r = digitalRead(PIN_LINE_R) == LOW;
    if (l || c || r) {
        g_drive.brake();
        uartSend("LINE_LOCKED");
        g_phase = Phase::FREC_TRACK_TO_CP;
    }
}

// --------------------------------------------------------------------
//  Frame handler from ESP32
// --------------------------------------------------------------------
static void onFrame(const char* cmd, const char* data) {
    if (strcmp(cmd, "MODE") == 0) {
        if      (strcmp(data, "FOLLOW") == 0)          {
            // Master just turned R1 ON (HuskyLens powered back up) →
            // re-init the camera link before enabling follow.
            Serial.println("[MODE] -> FOLLOW: re-initialising HuskyLens...");
            bool ok = g_follow.reinit();
            Serial.print("[HUSKY] reinit: "); Serial.println(ok ? "OK" : "FAIL");
            g_mode = Mode::FOLLOW;
            g_drive.stop();
            g_follow.enable(true);
        }
        else if (strcmp(data, "FOLLOW_RECOVERY") == 0) {
            // R1+R2 are both ON in this submode — make sure PN532 is
            // ready because we will switch to line-follow + RFID soon.
            g_rfid.reinit();
            enterFollowRecovery();
        }
        else { /* AUTO */
            // R2 was just toggled (might have been off in Follow) →
            // PN532 needs SAMConfig again before we trust poll().
            g_rfid.reinit();
            g_mode = Mode::AUTO;
            g_follow.enable(false);
            g_drive.stop();
            g_phase = Phase::IDLE;
        }
        return;
    }

    // Recovery-flow movement primitives can arrive only in FOLLOW_RECOVERY
    if (strcmp(cmd, "SEARCH_LINE_45") == 0) {
        if (g_mode != Mode::FOLLOW_RECOVERY) enterFollowRecovery();
        g_phase = Phase::FREC_LINE_SEARCH;
        return;
    }
    if (strcmp(cmd, "APPROACH_LINE") == 0) { g_phase = Phase::FREC_APPROACH;    return; }
    if (strcmp(cmd, "TRACK_TO_CP")   == 0) { g_phase = Phase::FREC_TRACK_TO_CP; return; }

    // ---------- Auto-mode-only commands ---------------------------
    if (g_mode == Mode::FOLLOW) return;

    if (strcmp(cmd, "ROUTE") == 0) {
        parseRoute(data);
        resetCheckpointDedup();        // clear dedup on new route load
        g_phase = (g_routeN > 0) ? Phase::ROUTE_LOADED : Phase::IDLE;
    } else if (strcmp(cmd, "START") == 0) {
        if (g_phase == Phase::ROUTE_LOADED && g_routeN > 0)
            g_phase = Phase::EXECUTING;
    } else if (strcmp(cmd, "CANCEL_MISSION") == 0) {
        if (g_phase == Phase::EXECUTING)
            g_phase = Phase::CANCEL_SEARCH_CP;
    }
}

// --------------------------------------------------------------------
//  Auto-mode driving tick (uses 3-eye line PD when actively tracking)
// --------------------------------------------------------------------
static void autoDriveTick() {
    if (g_obstacle) return;
    bool tracking = (g_phase == Phase::EXECUTING        ||
                     g_phase == Phase::CANCEL_SEARCH_CP ||
                     g_phase == Phase::FREC_TRACK_TO_CP);
    if (!tracking) { g_drive.stop(); return; }
    int l, r; g_line.step(l, r);
    g_drive.drive(l, r);
}

// --------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);   // let USB CDC enumerate before first print
    Serial.println("[BOOT] STM32 slave starting...");

    UART_ESP.begin(UART_ESP_BAUD);
    Serial.println("[UART] Serial1 (ESP32) ready @ 115200");

    pinMode(PIN_SR05_TRIG, OUTPUT);
    pinMode(PIN_SR05_ECHO, INPUT);

    g_drive.begin();
    g_line.begin();
    g_rfid.begin();

    Serial.println("[HUSKY] Initialising HuskyLens on Serial3 (USART3)...");
    Serial.print(  "[HUSKY]   UART baud : "); Serial.println(HUSKY_BAUD);
    Serial.println("[HUSKY]   Pins      : PB10=TX, PB11=RX");
    g_follow.begin(UART_HUSKY, &g_drive);

    if (g_follow.ready()) {
        Serial.println("[HUSKY] OK — connected, algorithm=TAG_RECOGNITION");
    } else {
        Serial.println("[HUSKY] FAIL — no ACK from HuskyLens (check wiring/power)");
        Serial.println("[HUSKY]   -> R1 relay must be ON for HuskyLens to have power.");
        Serial.println("[HUSKY]   -> Will retry in reinit() when MODE:FOLLOW arrives.");
    }
    Serial.println("[BOOT] Setup done.");
}

void loop() {
    uartPoll();
    heartbeatService();
    obstacleService();

    // ---- Pure Follow Mode ----------------------------------------
    if (g_mode == Mode::FOLLOW) {
        g_follow.loop();
        // Forward HuskyLens tag telemetry to master so OLED can display
        // the learned ID and the 30-s lost-tag alarm can be driven.
        int16_t tagId;
        if (g_follow.consumeIdChange(tagId)) {
            g_lastTagId = tagId;
            char buf[8]; snprintf(buf, sizeof(buf), "%d", tagId);
            uartSend("TAG_ID", buf);
            Serial.print("[HUSKY] Tag ID seen: "); Serial.println(tagId);
        }
        bool lost;
        if (g_follow.consumeLostChange(lost)) {
            uartSend("TAG_LOST", lost ? "1" : "0");
            Serial.print("[HUSKY] Tag "); Serial.println(lost ? "LOST" : "FOUND (reacquired)");
        }
        // Periodic HuskyLens ready-status log (every 5 s) for diagnosis.
        static uint32_t tHuskyLog = 0;
        if (millis() - tHuskyLog > 5000) {
            tHuskyLog = millis();
            Serial.print("[HUSKY] ready="); Serial.print(g_follow.ready() ? "Y" : "N");
            Serial.print("  mode=FOLLOW");
            Serial.println();
        }
        return;
    }

    // ---- Follow→Auto Recovery sub-modes --------------------------
    if (g_mode == Mode::FOLLOW_RECOVERY) {
        switch (g_phase) {
            case Phase::FREC_LINE_SEARCH: searchLineTick();   break;
            case Phase::FREC_APPROACH:    approachLineTick(); break;
            case Phase::FREC_TRACK_TO_CP: {
                String id = g_rfid.poll();
                if (id.length()) handleCheckpoint(id);
                else             autoDriveTick();
                break;
            }
            default:                       g_drive.stop();    break;
        }
        return;
    }

    // ---- Standard Auto Mode --------------------------------------
    String id = g_rfid.poll();
    if (id.length()) handleCheckpoint(id);
    autoDriveTick();
}

// --------------------------------------------------------------------
//  Heartbeat → master (every 2 s).  Lets ESP32 watchdog the link and
//  prints STM32/HuskyLens status to its serial monitor.
//    <HB:m=AUTO,p=IDLE,husky=Y,tag=-1,obs=0>
// --------------------------------------------------------------------
static void heartbeatService() {
    static uint32_t tNext = 0;
    if (millis() < tNext) return;
    tNext = millis() + 2000;
    char buf[110];
    snprintf(buf, sizeof(buf),
             "m=%s,p=%s,husky=%c,tag=%d,obs=%d,L=%d,R=%d,sr=%d",
             modeShort(g_mode), phaseShort(g_phase),
             g_follow.ready() ? 'Y' : 'N',
             (int)g_lastTagId,
             g_obstacle ? 1 : 0,
             (int)g_drive.lastL(), (int)g_drive.lastR(),
             (int)g_lastSr05Cm);
    uartSend("HB", buf);
}

