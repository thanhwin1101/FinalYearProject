# Requirements

Extracted from the PRD (`AGV/docs/agent_skill.txt`) and refined by behavioral
SPECs (`Auto_mode.txt`, `Follow_mode.txt`). Every requirement has an ID of the
form `REQ-{slug}`, a description, acceptance criteria, and provenance.

---

## REQ-wifi-mqtt-provisioning

**Description:** The ESP32 must expose a WiFiManager captive portal for
initial WiFi + MQTT credential entry when stored credentials are missing or
unusable.

**Acceptance:**
- On boot, saved credentials are read from Preferences (NVS).
- If any field is missing/empty, the portal opens immediately.
- If credentials exist, ESP32 attempts WiFi connect with a 10 s timeout; on
  failure, the portal opens.
- On WiFi success, ESP32 attempts MQTT connect with a 5 s timeout; on failure,
  the portal opens.
- Portal AP SSID is `Robot_Setup`, gateway IP `192.168.4.1`.
- Portal exposes MQTT parameters (server, port, user, pass) in addition to WiFi.
- On "Save", credentials are persisted and the ESP32 reboots into the normal
  boot flow.
- OLED reflects `Connecting...` / `WiFi: OK` / `MQTT: OK` / portal banner states.

**Scope:** ESP32 firmware only.

**Sources:**
- source: AGV/docs/setup_wifi_MQTT.txt
- source: AGV/docs/agent_skill.txt

---

## REQ-mqtt-command-channel

**Description:** The robot must subscribe to backend MQTT topics to receive
route, cancel, and status-request commands, and must publish checkpoint,
battery, error, and return-request events.

**Acceptance:**
- Subscribes to `robot/command` (route new, cancel mission, request status).
- Publishes checkpoint arrivals with checkpoint ID and route progress metadata.
- Publishes battery percentage on a periodic cadence.
- Publishes error states (ToF obstacle timeout, lost line, lost tag, etc.).
- Publishes to `robot/return_request` with the current checkpoint ID when a
  return-to-base route must be issued.
- On MQTT disconnect, the OLED shows a banner and the client auto-reconnects.

**Scope:** ESP32 firmware.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/setup_wifi_MQTT.txt (topic list)
- source: AGV/docs/Checklist.txt (1.3)

---

## REQ-uart-protocol

**Description:** ESP32 and STM32 must communicate over a framed UART protocol:
`[STX=0x7E][LEN][CMD][DATA...][CRC8]`, with the command set defined below.

**Acceptance (ESP → STM32 commands):**
- `0x01 Set Mode` — payload 1 byte: `0=AUTO, 1=FOLLOW, 2=RECOVERY`
- `0x02 Send Route` — payload `[count][id1_hi][id1_lo]...` (2 bytes per ID)
- `0x03 Direct Vel` — payload `int16 Vx, int16 Vy, int16 Vr`
- `0x04 Request Status` — no data
- `0x05 Cancel Mission` — no data
- `0x06 Confirm Arrival` — `uint16 checkpointID`

**Acceptance (STM32 → ESP commands):**
- `0x81 Battery` — `uint8 percent` (0–100) [see note in Checklist 2.11: ESP32 may own battery instead]
- `0x82 Checkpoint Reached` — `uint16 id`
- `0x83 Obstacle` — no data
- `0x84 Ack` — `uint8 cmd_ref`
- `0x85 Mission Complete` — no data
- `0x86 Checkpoint Mismatch` — `uint16 received, uint16 expected`

**Acceptance (framing):**
- CRC8 covers LEN, CMD, DATA bytes.
- Receiver resyncs on any byte that is not 0x7E after a framing error.

**Scope:** Both ESP32 and STM32 firmware.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/Checklist.txt (1.4, 2.2)

---

## REQ-auto-mode

**Description:** Auto mode autonomously executes a route of NFC checkpoints
using the STM32's line-follow + PN532 + ToF hot-path, with ESP32 brokering
MQTT updates and user input.

**Acceptance:**
- On entering Auto: relays configured `R1=OFF, R2=ON (line), R3=ON (PN532)`,
  wait 200 ms for power stabilization, then init line sensor (3 pins) and
  PN532 (SPI), then send `0x01` with payload AUTO.
- IDLE state: OLED shows "Auto IDLE, waiting for route"; wait for MQTT route
  OR physical button single-click.
- On new route via MQTT: store route (checkpoint list, patient name,
  destination), display route summary on OLED, wait for single-click.
- On single-click (route queued): send `0x02` with route payload to STM32.
- While running: relay every STM32 `0x82` checkpoint arrival to MQTT, update
  OLED (`CP current / total`).
- On MQTT cancel during run: send `0x05`, execute Return-to-Base flow.
- On new MQTT route during run: send `0x05`, discard old route, store new
  route, return to "wait for single-click".
- On STM32 `0x83` obstacle: STM32 self-stops and self-resumes; ESP32 only logs
  a warning — no state change.
- On STM32 `0x85` mission complete: display "Route completed", send `0x06`
  confirm, STM32 rotates 180°, wait for single-click, publish
  return-request for MED, accept return route, run it.

**Scope:** ESP32 + STM32 cooperative.

**Sources:**
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.8, 2.7)

---

## REQ-follow-mode

**Description:** Follow mode tracks a Huskylens tag and issues Vx/Vy/Vr
velocity commands to the STM32, using SR05 left/right to maintain wall
clearance.

**Acceptance:**
- Entry: relays `R2=OFF, R3=OFF, R1=ON (Huskylens + SR05 + servos)`, wait 300 ms.
- Init: Huskylens UART in tag-recognition mode; SR05 L/R trigger+echo; servo Y
  attached at 45°, servo X at 95°.
- Send `0x01` with payload FOLLOW to STM32.
- Control loop at 20–30 Hz:
  - Button double-click has top priority → send emergency stop
    (Vx=Vy=Vr=0) and transition to Recovery mode (no return).
  - If tag visible within last 1 s: compute Vx/Vy/Vr from (x, y, area) error
    (x offset → Vr, area offset → Vy, y offset → adjust servo Y), read SR05
    L/R; if either < 30 cm threshold, bias Vx for side-slip. Send `0x03` with
    Vx/Vy/Vr. Update OLED.
  - If tag missing > 1 s → transition to Find mode.

**Scope:** ESP32-driven; STM32 executes 0x03 velocity commands.

**Sources:**
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.9)

---

## REQ-find-mode

**Description:** Find mode is a sub-mode of Follow that searches for a lost
tag by rotating and using SR05 deltas.

**Acceptance:**
- Entry: tag lost > 1 s in Follow mode.
- Rotate left/right driven by SR05 change; up to 3 search attempts.
- If tag re-acquired → return to Follow loop.
- Double-click during Find has priority → transition directly to Recovery
  mode.
- If 3 attempts exhausted without tag → error state (design TBD; Checklist
  implies 3-attempt cap then fallback).

**Scope:** ESP32 (state machine); STM32 continues to accept 0x03 velocity.

**Sources:**
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/Checklist.txt (1.10, 4.4)

---

## REQ-recovery-mode

**Description:** Recovery mode safely transitions from Follow/Find back into
Auto by using Huskylens in line-tracking mode + servo X sweep to re-acquire
the guide line, then reading an NFC checkpoint to request a return route.

**Acceptance:**
- Entry: user double-click from Follow or Find (see note on trigger location
  in conflict report — SPEC allows anywhere, PRD originally said MED-only).
- Send emergency stop (Vx=Vy=Vr=0) before any reconfiguration.
- Enable **all three relays** R1 + R2 + R3 (power all sensors), wait 300 ms.
- Re-init line sensor (3 pins), PN532 (SPI), Huskylens (line-tracking mode),
  SR05, servo X, servo Y.
- Set servo Y = 100°, servo X = 95° (center).
- OLED: "RECOVERY - Finding line...".
- **Phase 1 (find line):** sweep servo X 0°→180° at 1°/50 ms; if Huskylens
  reports a line within view, stop servo at that angle, command mecanum
  (Vx/Vy/Vr) to center the line per Huskylens, concurrently read the 3 line
  sensor eyes; as soon as the center eye (or any eye) goes active, send
  Vx=Vy=Vr=0, OLED "Line found, aligning OK", advance to Phase 2. If a full
  sweep returns no line, buzzer×3 and retry up to 3 times, then escalate to
  backend error.
- **Phase 2 (read checkpoint):** OLED prompts for NFC tag presentation; wait
  up to 30 s for PN532 read. On success, publish `robot/return_request` with
  the checkpoint ID and wait up to 10 s for a route. On route received:
  disable R1, keep R2 and R3 on, re-init Auto sensors, send `0x02` to STM32,
  transition to Auto RUN. On route timeout: OLED "Route error! Stay in
  Recovery" — retry or error escalate. On NFC read timeout: OLED "No NFC tag!
  Exiting Recovery" — disable all relays, return to Follow or idle (TBD).

**Scope:** ESP32 owns; STM32 executes motion and PN532 reads on command.

**Sources:**
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.11, 4.5)

---

## REQ-return-to-base

**Description:** On mission cancel, the robot must immediately stop, read
the current NFC checkpoint, and request a route back to the MED checkpoint.

**Acceptance:**
- Trigger: MQTT cancel command (only backend may cancel).
- ESP32 sends `0x05` → STM32 halts instantly.
- STM32 reads current checkpoint via PN532; sends `0x82` with checkpoint ID
  (or ESP32 sends `0x04 Request Status`).
- ESP32 publishes `robot/return_request` with that checkpoint ID.
- Backend responds with a route; ESP32 sends `0x02` to STM32; STM32 executes.
- During return: on every `0x82` arrival, if received ID != expected next ID,
  STM32 brakes, rotates 180°, and re-requests the route from the current
  checkpoint (i.e. sends `0x86` mismatch; ESP32 requests a fresh route).
- On reaching MED (final checkpoint): OLED notifies, transition to Auto IDLE.

**Scope:** ESP32 + STM32 cooperative.

**Sources:**
- source: AGV/docs/Auto_mode.txt (Return-to-Base sub-flow)
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.12, 2.9, 4.6)

---

## REQ-button-ux

**Description:** The GPIO36 physical button must distinguish hold, single
click, and double click, and must gate mode transitions.

**Acceptance:**
- Long hold (> 3 s) on boot → open WiFiManager portal.
- Single click in Auto IDLE with a queued route → start route execution.
- Single click after mission complete → request return route.
- Double click in Auto at MED+IDLE → transition to Follow.
- Double click in Follow or Find (anywhere) → transition to Recovery.
- Debounce required; spurious bounces must not register as clicks.

**Scope:** ESP32 firmware.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/Checklist.txt (1.7)

---

## REQ-battery-guard

**Description:** Battery monitoring must gate motion commands and publish
state to the backend.

**Acceptance:**
- ADC read on GPIO35 via voltage divider from 12 V battery.
- Convert to %; publish on MQTT at a defined cadence.
- If battery < 30%: reject mode-execution commands (cannot start a route,
  cannot enter Follow/Recovery), display warning on OLED, publish warning
  event on MQTT.

**Scope:** ESP32 firmware.

**Sources:**
- source: AGV/docs/agent_skill.txt ("Pin phải ≥30% mới cho phép thực thi lệnh")
- source: AGV/docs/Checklist.txt (1.13, 4.7)

---

## REQ-oled-ux

**Description:** OLED SH1106 must display per-mode screens with live data
using U8g2.

**Acceptance:**
- Screens exist for: boot/portal, Auto IDLE, Auto RUN (CP current/total,
  patient, destination), Follow (tag id/x/y/area, wall L/R), Find, Recovery
  (phase banner), Return-to-Base, and error/warning states.
- Refresh at ≥ 5 Hz (update task every 200 ms per agent_skill.txt).
- Shows connection banners (WiFi / MQTT) on disconnect.

**Scope:** ESP32 firmware.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.6)

---

## REQ-mode-switch-constraint

**Description:** Mode transitions must be constrained by robot state.

**Acceptance (as locked by PRD, refined by SPECs):**
- **Auto → Follow:** only allowed when robot is at MED checkpoint **and**
  state is IDLE. (PRD + agent_skill.)
- **Follow/Find → Recovery:** allowed **anywhere** via double-click
  (SPEC refinement — Follow_mode.txt explicitly states "không cần ở MED").
- **Recovery → Auto:** automatic after checkpoint read + route received.
- **Auto → Return-to-Base:** triggered by backend cancel only.

Note: The PRD statement "Chuyển mode chỉ được phép khi robot ở checkpoint MED
và đang IDLE" is a universal constraint that the Follow SPEC explicitly
overrides for the Recovery transition. See `INGEST-CONFLICTS.md` §INFO.

**Scope:** ESP32 state machine.

**Sources:**
- source: AGV/docs/agent_skill.txt (original universal constraint)
- source: AGV/docs/Follow_mode.txt (Recovery refinement)
- source: AGV/docs/Checklist.txt (1.7)

---

## REQ-freertos-tasking

**Description:** ESP32 firmware must be structured as FreeRTOS tasks.

**Acceptance:**
- `MQTT task` — subscribe/publish, route ingest, cancel handling.
- `UART task` — UART to/from STM32, frame parser, dispatch.
- `Control task` — state machine for Auto/Follow/Find/Recovery.
- `OLED task` — 200 ms redraw cadence.
- `Button task` — debounce, click type detection, mode-switch gating.
- `Battery task` — periodic ADC read + MQTT publish + gate.

**Scope:** ESP32 firmware.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.18)
