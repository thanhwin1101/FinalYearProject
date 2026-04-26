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
static bool  g_turning  = false;   // TRUE while a tankTurn*() is in progress —
                                   // obstacleService must NOT stop() the motors,
                                   // because a turn command MUST complete
                                   // (even if SR05 sees an obstacle mid-spin).
static int16_t g_lastSr05Cm = 999;  // last raw SR05 reading (debug telemetry)
static String g_lastHandledCp;     // FSM-level dedup: prevents re-processing same CP
static String g_lastTurnTag;       // tag where the LAST 90/180° turn was performed.
                                   // Same-tag scans without an intervening different
                                   // tag will NOT spin again — fixes "robot keeps
                                   // turning 180° forever when parked on MED".
static int16_t g_lastTagId = -1;   // last HuskyLens tag ID seen (-1 if none)

// While TRUE, autoDriveTick() drives both wheels straight at LF_BASE_PWM
// instead of using the line-PD output. Set on every START transition and
// cleared the first time the center line sensor sees the line. Prevents
// pivot-in-place when the robot is parked on MED (where a side sensor
// may flicker on the marker edge while the center is off the line).
static bool g_creepStraightUntilLine = false;

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
    // Mirror to USB serial monitor for live debugging.
    Serial.print("[UART->] "); Serial.println(cmd);
}
static inline void uartSend(const char* cmd, const char* data) {
    char content[UART_FRAME_MAX];
    snprintf(content, sizeof(content), "%s:%s", cmd, data);
    uint8_t crc = crc8_compute(content, strlen(content));
    char hex[3]; snprintf(hex, sizeof(hex), "%02X", crc);
    UART_ESP.print('<'); UART_ESP.print(content);
    UART_ESP.print('|'); UART_ESP.print(hex); UART_ESP.print('>');
    // Mirror to USB serial monitor.
    Serial.print("[UART->] "); Serial.println(content);
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
    // Timeout giảm từ 30 ms xuống 3 ms (~50 cm). Robot chỉ cần phản
    // ứng vật cản gần (SR05_STOP_CM = 20 cm, SR05_RESUME_CM = 40 cm).
    // 30 ms × 3 lần = 90 ms blocking khi trống → bỏ tag PN532.
    long us = pulseIn(PIN_SR05_ECHO, HIGH, 3000);    // ~50 cm max
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
                // Do NOT cut motor PWM if a turn is in progress —
                // a turn command must run to completion regardless
                // of obstacle state.
                if (!g_turning) g_drive.stop();
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
// Run the route-step action at this CP. We dedup turn-actions by tag so
// the robot will not spin 90/180° more than once at the same physical
// checkpoint — even if the master re-issues a route that starts on the
// same tag we are currently parked on.
static void executeAction(char action, const String& tag) {
    g_drive.brake();
    const bool sameTagAsLastTurn = (tag.length() && tag == g_lastTurnTag);
    switch (action) {
        case 'L':
            if (!sameTagAsLastTurn) { g_drive.tankTurn90Left();  g_lastTurnTag = tag; }
            else Serial.println("[AUTO] skip 90L (already turned at this tag)");
            break;
        case 'R':
            if (!sameTagAsLastTurn) { g_drive.tankTurn90Right(); g_lastTurnTag = tag; }
            else Serial.println("[AUTO] skip 90R (already turned at this tag)");
            break;
        case 'B':
            if (!sameTagAsLastTurn) { g_drive.tankTurn180();     g_lastTurnTag = tag; }
            else Serial.println("[AUTO] skip 180 (already turned at this tag)");
            break;
        case 'F':
        default:  /* nothing – just pass the CP */ break;
    }
}

static void finishRoute(const char* cpId, char action) {
    g_drive.brake();
    const String tag(cpId);
    // Only the final-step action 'B' produces a 180° turn at the
    // destination. 'F' (or any other) means stop only — the turn is
    // an explicit instruction from the route, not a default behaviour.
    if (action == 'B') {
        if (tag.length() && tag == g_lastTurnTag) {
            Serial.println("[AUTO] finishRoute: skip 180 (already turned at this tag)");
        } else {
            g_drive.tankTurn180();
            g_lastTurnTag = tag;
        }
    }
    if (strcmp(cpId, MED_ID) == 0) uartSend("DONE",    cpId);
    else                            uartSend("ARRIVED", cpId);
    g_phase    = Phase::IDLE;
    g_routeN   = 0;
    g_routeIdx = 0;
}

// Called once per detected tag while in Auto / TRACK_TO_CP mode
static void handleCheckpoint(const String& scannedId) {
    // Sentinel "?XXXX" means a tag was read but its UID is not in the map.
    const bool unknown = scannedId.length() && scannedId[0] == '?';

    // BRAKE FIRST whenever PN532 catches any tag while a route is
    // executing. This prevents the robot from rolling past the CP pad
    // before the FSM has a chance to dispatch the action (turn / stop).
    // It also gives PN532 a stable read window so subsequent polls can
    // confirm the UID. IDLE/ROUTE_LOADED phases must NOT brake here
    // (the robot is already stopped — emitting a brake would be noise).
    if (g_phase == Phase::EXECUTING       ||
        g_phase == Phase::CANCEL_SEARCH_CP||
        g_phase == Phase::FREC_TRACK_TO_CP) {
        g_drive.brake();
    }

    // Always echo the raw poll() hit upstream so the master serial
    // monitor / telnet can show every tag the PN532 sees — even ones
    // dropped by the EXECUTING dedup guard. Format:
    //   SCAN:<id>,phase=<n>,exp=<expectedId>
    {
        const Checkpoint* exp = expectedCp();
        char buf[80];
        snprintf(buf, sizeof(buf), "%s,phase=%d,exp=%s",
                 scannedId.c_str(),
                 (int)g_phase,
                 exp ? exp->id : "-");
        uartSend("SCAN", buf);
    }

    // FSM-level dedup is ONLY needed for EXECUTING / CANCEL_SEARCH_CP /
    // FREC_TRACK_TO_CP — those run a 90/180 turn after handling a CP and
    // NFC_REPEAT_MS (700 ms) is shorter than TURN_*_MS, so without this
    // guard the same tag would fire WRONG_CP mid-turn.
    //
    // For IDLE / ROUTE_LOADED we WANT every fresh poll() return to update
    // the master's "current location" — even if it's the same tag we saw
    // last time (e.g. user picked the robot up and put it back down on
    // the same CP, or a previous IDLE_SCAN frame was lost in transit).
    const bool needDedup =
        (g_phase == Phase::EXECUTING       ||
         g_phase == Phase::CANCEL_SEARCH_CP||
         g_phase == Phase::FREC_TRACK_TO_CP);
    if (needDedup && scannedId == g_lastHandledCp) return;
    g_lastHandledCp = scannedId;
    // Robot has physically moved to a new tag → forget the "already
    // turned here" guard so a future visit back to this tag will turn.
    if (scannedId != g_lastTurnTag) {
        // (do not clear g_lastTurnTag here — only update it inside
        //  executeAction()/finishRoute() when an actual turn fires.)
    }

    if (g_phase == Phase::EXECUTING) {
        const Checkpoint* exp = expectedCp();
        if (!exp) return;

        if (unknown) {
            // Off-route tag: brake, 180°, escalate to master.
            g_drive.brake();
            if (scannedId != g_lastTurnTag) {
                g_drive.tankTurn180();
                g_lastTurnTag = scannedId;
            }
            uartSend("WRONG_CP", scannedId);
            g_phase = Phase::IDLE; g_routeN = 0; g_routeIdx = 0;
            return;
        }

        if (scannedId == exp->id) {
            char act  = exp->action;
            bool last = (g_routeIdx == g_routeN - 1);
            if (last) {
                finishRoute(exp->id, act);
            } else {
                uartSend("CP_REACHED", exp->id);
                executeAction(act, scannedId);
                g_routeIdx++;
            }
        } else {
            // Wrong CP encountered: brake, 180° turn, ask master.
            g_drive.brake();
            if (scannedId != g_lastTurnTag) {
                g_drive.tankTurn180();
                g_lastTurnTag = scannedId;
            }
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
        // Cancel always happens mid-outbound, so the robot is facing
        // AWAY from MED. The recovery route from the backend assumes
        // the robot is already pointing back along the corridor, so
        // perform a 180° turn here before reporting the CP. Skip if we
        // already turned at this exact tag (prevents double-spin if
        // the tag is re-scanned by accident).
        if (g_phase == Phase::CANCEL_SEARCH_CP && scannedId != g_lastTurnTag) {
            g_drive.tankTurn180();
            g_lastTurnTag = scannedId;
        }
        uartSend("CP_REACHED", scannedId);
        g_phase = Phase::IDLE;
        return;
    }

    // IDLE / ROUTE_LOADED: passive scan — let master know the robot's
    // current physical location (used by web dashboard + OLED), but do
    // not start any route execution.
    //
    // Throttle: emit every time the tag *changes*, otherwise at most
    // once every IDLE_SCAN_RESEND_MS so a same-tag reading keeps the
    // web in sync (in case an earlier frame was dropped) without
    // flooding MQTT.
    static String   sLastEmitId;
    static uint32_t sLastEmitMs = 0;
    constexpr uint32_t IDLE_SCAN_RESEND_MS = 5000;
    if (!unknown) {
        const uint32_t now = millis();
        const bool changed  = (scannedId != sLastEmitId);
        const bool overdue  = (now - sLastEmitMs >= IDLE_SCAN_RESEND_MS);
        if (changed || overdue) {
            uartSend("IDLE_SCAN", scannedId);
            Serial.print("[RFID] IDLE_SCAN -> "); Serial.println(scannedId);
            sLastEmitId = scannedId;
            sLastEmitMs = now;
        }
    } else {
        Serial.print("[RFID] unknown UID seen (idle): "); Serial.println(scannedId);
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
        if (g_phase == Phase::ROUTE_LOADED && g_routeN > 0) {
            // Clear the dedup sentinel right before we begin executing.
            // While IDLE/ROUTE_LOADED, every IDLE_SCAN we sent set
            // g_lastHandledCp = <current tag>. If we don't reset it now,
            // the very first scan in EXECUTING (which is the SAME tag
            // we are physically parked on, == expected[0].id) would be
            // dropped by the EXECUTING dedup guard. The route then
            // never advances past idx 0, and autoDriveTick() spins the
            // wheels forever via the line PD because we are sitting
            // partially on the MED marker.
            resetCheckpointDedup();
            // Also forget the last-turn tag — a new mission must be
            // free to perform the route's first explicit turn even if
            // it happens at the same physical CP we previously turned
            // at (e.g. MED with 'B' at the end of the previous return).
            g_lastTurnTag = "";
            // Re-initialise PN532 SPI — some boards lose state after
            // motor PWM (TIM1 on PA8) activates and SPI reads start
            // failing silently. A fresh SAMConfig fixes that.
            Serial.println("[START] PN532 reinit before EXEC...");
            bool pnOk = g_rfid.reinit();
            Serial.print("[START] PN532 reinit: "); Serial.println(pnOk ? "OK" : "FAIL");
            // Force an initial "creep straight" segment until the
            // center line sensor catches the painted line. Stops the
            // robot from pivoting in place at MED if a side sensor
            // sees the marker edge while the center sees nothing.
            g_creepStraightUntilLine = true;
            g_phase = Phase::EXECUTING;

            // Auto-confirm the start CP. The robot is physically parked
            // on route[0] when a mission begins, so we don't need the
            // PN532 to re-read that tag — it often misses because the
            // robot starts driving before another 700ms repeat-window
            // elapses, leaving expectedCp=route[0] forever and making
            // the next physical tag (route[1]) look like a WRONG_CP.
            // Instead, fire CP_REACHED:route[0] now and advance idx so
            // EXECUTING begins with expected = route[1].
            const Checkpoint& first = g_route[0];
            // Treat scanning of route[0]'s id as already "handled" so
            // a stray PN532 hit on the start tag doesn't double-fire.
            g_lastHandledCp = String(first.id);
            if (g_routeN == 1) {
                // Single-CP route: starting CP is also the destination.
                finishRoute(first.id, first.action);
            } else {
                uartSend("CP_REACHED", first.id);
                executeAction(first.action, String(first.id));
                g_routeIdx = 1;
            }
        }
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

    // Creep straight at LF_BASE_PWM until the center line sensor first
    // sees the line. Without this, autoDriveTick() at MED would feed
    // the PD's stale ±2 error and one wheel would stall while the other
    // ran at full speed → pivot in place forever.
    if (g_creepStraightUntilLine) {
        const bool centerOnLine =
            (digitalRead(PIN_LINE_C) == LOW);
        if (centerOnLine) {
            g_creepStraightUntilLine = false;
        } else {
            g_drive.drive(LF_BASE_PWM, LF_BASE_PWM);
            return;
        }
    }

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
    const bool fast = (g_phase == Phase::EXECUTING ||
                       g_phase == Phase::CANCEL_SEARCH_CP ||
                       g_phase == Phase::FREC_TRACK_TO_CP);
    if (millis() < tNext) return;
    tNext = millis() + (fast ? 500 : 2000);

    // Snapshot 3-eye line sensor (LOW = sees black line) so the ESP32
    // serial monitor can spot "sensor stuck on/off" issues that cause
    // the line PD to pivot the robot in place.
    const int lL = (digitalRead(PIN_LINE_L) == LOW) ? 1 : 0;
    const int lC = (digitalRead(PIN_LINE_C) == LOW) ? 1 : 0;
    const int lR = (digitalRead(PIN_LINE_R) == LOW) ? 1 : 0;

    // Truncate dedup/turn tags to a few chars to keep the frame short.
    char dedup[8] = {0};
    char turn[8]  = {0};
    strncpy(dedup, g_lastHandledCp.c_str(), sizeof(dedup) - 1);
    strncpy(turn,  g_lastTurnTag.c_str(),  sizeof(turn)  - 1);

    char buf[200];
    snprintf(buf, sizeof(buf),
             "m=%s,p=%s,husky=%c,tag=%d,obs=%d,L=%d,R=%d,sr=%d,"
             "line=%d%d%d,idx=%u/%u,dedup=%s,turn=%s,creep=%d,"
             "pn=%lu/%lu/%lu",
             modeShort(g_mode), phaseShort(g_phase),
             g_follow.ready() ? 'Y' : 'N',
             (int)g_lastTagId,
             g_obstacle ? 1 : 0,
             (int)g_drive.lastL(), (int)g_drive.lastR(),
             (int)g_lastSr05Cm,
             lL, lC, lR,
             (unsigned)g_routeIdx, (unsigned)g_routeN,
             dedup[0] ? dedup : "-",
             turn[0]  ? turn  : "-",
             g_creepStraightUntilLine ? 1 : 0,
             (unsigned long)g_rfid.pollOk(),
             (unsigned long)g_rfid.pollEmpty(),
             (unsigned long)g_rfid.pollTotal());
    uartSend("HB", buf);
}

