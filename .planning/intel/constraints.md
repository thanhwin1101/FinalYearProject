# Constraints

Technical constraints extracted from SPECs (`Auto_mode.txt`, `Follow_mode.txt`)
and the PRD's machine-facing tables. These are the contracts planners and
implementers must honor.

---

## CON-uart-frame-format (type: protocol)

**Statement:** All ESP32↔STM32 UART traffic uses a fixed framing:

```
[STX=0x7E][LEN][CMD][DATA...][CRC8]
```

- `STX` is the literal byte `0x7E`. Receiver must resync on any non-0x7E byte
  after a framing or CRC error.
- `LEN` is the byte count of `DATA` (not including CMD or CRC).
- `CRC8` covers `LEN`, `CMD`, and all `DATA` bytes.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.4, 2.2)

---

## CON-uart-command-table (type: protocol)

**Statement:** The UART command set is closed. Any new command must be added
here first before code can reference it.

**ESP32 → STM32:**

| CMD  | Name               | Data                                 |
|------|--------------------|--------------------------------------|
| 0x01 | Set Mode           | `uint8 mode` (0=AUTO, 1=FOLLOW, 2=RECOVERY) |
| 0x02 | Send Route         | `uint8 count` + `uint16[count]` checkpoint IDs |
| 0x03 | Direct Vel         | `int16 Vx, int16 Vy, int16 Vr`       |
| 0x04 | Request Status     | (none)                               |
| 0x05 | Cancel Mission     | (none)                               |
| 0x06 | Confirm Arrival    | `uint16 checkpointID`                |

**STM32 → ESP32:**

| CMD  | Name                  | Data                                  |
|------|-----------------------|---------------------------------------|
| 0x81 | Battery               | `uint8 percent` (0–100)               |
| 0x82 | Checkpoint Reached    | `uint16 id`                           |
| 0x83 | Obstacle              | (none)                                |
| 0x84 | Ack                   | `uint8 cmd_ref`                       |
| 0x85 | Mission Complete      | (none)                                |
| 0x86 | Checkpoint Mismatch   | `uint16 received, uint16 expected`    |

Note: Checklist 2.11 flags 0x81 as optional — the production source of battery
telemetry is the ESP32 ADC on GPIO35, not STM32.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.4, 2.2, 2.11)

---

## CON-relay-power-domains (type: nfr)

**Statement:** The three relays partition sensor power. Software must adhere
to the per-mode relay state table to prevent cross-domain brown-outs.

| Relay | ESP32 GPIO | Powered domain                                           |
|-------|------------|----------------------------------------------------------|
| R1    | GPIO18     | Huskylens + 2× servos + 2× SR04/SR05 ("Vision" domain)   |
| R2    | GPIO19     | Line-sensor array (routed through STM32)                 |
| R3    | GPIO23     | PN532 NFC reader                                         |

Per-mode relay state (authoritative — SPECs win over the PRD's informal text):

| Mode             | R1  | R2  | R3  | Settle delay |
|------------------|-----|-----|-----|--------------|
| Boot default     | OFF | ON  | ON  | 200 ms       |
| Auto             | OFF | ON  | ON  | 200 ms       |
| Follow / Find    | ON  | OFF | OFF | 300 ms       |
| Recovery         | ON  | ON  | ON  | 300 ms       |
| Portal/idle-no-mode | OFF | OFF | OFF | —         |

**Sources:**
- source: AGV/docs/agent_skill.txt (pin assignments)
- source: AGV/docs/Auto_mode.txt (Auto relay state)
- source: AGV/docs/Follow_mode.txt (Follow + Recovery relay state)
- source: AGV/docs/setup_wifi_MQTT.txt (boot relay state)

---

## CON-battery-threshold (type: nfr)

**Statement:** Motion commands are rejected when battery < 30%. This is a
hard gate in firmware; backends may not override it. Warning published on
MQTT and OLED when the gate is engaged.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (1.13, 4.7)

---

## CON-mode-switch-gate (type: nfr)

**Statement:** Mode transitions are permitted only by the following table.
This overrides the PRD's universal "only at MED IDLE" phrasing — see
INGEST-CONFLICTS.md.

| From → To              | Allowed when                                |
|------------------------|---------------------------------------------|
| Auto → Follow          | Robot is at MED checkpoint **and** IDLE     |
| Follow → Find          | Tag lost > 1 s (automatic)                  |
| Find → Follow          | Tag re-acquired (automatic)                 |
| Follow/Find → Recovery | Double-click, **anywhere** (SPEC override)  |
| Recovery → Auto        | After checkpoint read + route received      |
| Auto → Return-to-Base  | MQTT cancel only (backend-initiated)        |

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/Checklist.txt (1.7)

---

## CON-mqtt-topics (type: api-contract)

**Statement:** The backend API surface is restricted to these topics:

**Subscribed by ESP32:**
- `robot/command` — JSON with `{type: "route" | "cancel" | "status", ...}`;
  route payload carries checkpoint list, patient name, destination.

**Published by ESP32:**
- Checkpoint-arrival events (topic name TBD in planner — implied by
  "gửi checkpoint ID lên backend qua MQTT"; see open question in
  `context.md`).
- Battery % (topic TBD — implied by "gửi MQTT định kỳ").
- Error/warning events (ToF obstacle timeout, lost line, lost tag, low
  battery — topic TBD).
- `robot/return_request` — payload: `{checkpoint_id: uint16}`; backend must
  respond with a route (timeout 10 s client-side).

**Open question for downstream planner:** PRD lists topic *names*
(`robot/command`, `robot/return_request`) but does not name the publish
topics for checkpoint-arrival, battery, or errors. Planner must finalize.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/setup_wifi_MQTT.txt

---

## CON-wifi-portal (type: api-contract)

**Statement:** Provisioning portal contract:

- AP SSID: `Robot_Setup`
- Gateway IP: `192.168.4.1`
- Portal timeout: `0` (wait indefinitely)
- Extra fields beyond default WiFiManager: `mqtt_server`, `mqtt_port`,
  `mqtt_user`, `mqtt_pass`.
- `setSaveConfigCallback()` persists MQTT fields to Preferences; WiFiManager
  auto-persists WiFi creds. `ESP.restart()` is called after save.

**Sources:**
- source: AGV/docs/setup_wifi_MQTT.txt
- source: AGV/docs/Checklist.txt (1.2)

---

## CON-esp32-pinout (type: schema)

**Statement (from PRD + Checklist — exact assignments pending pinout.h):**

- `GPIO18` — R1 relay (vision)
- `GPIO19` — R2 relay (line)
- `GPIO23` — R3 relay (NFC)
- `GPIO35` — battery ADC (voltage divider from 12 V)
- `GPIO36` — user button (input-only pin, hold/single/double)
- UART to STM32 — pins TBD (defined in `pinout.h`)
- UART to Huskylens — pins TBD
- I2C for OLED (+ optional ToF if routed to ESP32) — pins TBD
- PWM for Servo X (with analog feedback on ADC) — pins TBD
- PWM for Servo Y — pins TBD
- 2× SR05 (trigger + echo per sensor) — pins TBD

**Planner must:** finalize TBD pin assignments in `pinout.h` and cross-check
with STM32 pinout_stm32.h to avoid electrical conflicts.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt (3.3)

---

## CON-stm32-pinout (type: schema)

**Statement (from Checklist — exact assignments pending pinout_stm32.h):**

- PN532 SPI: `SS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7` (per setup_wifi_MQTT.txt).
- 4 × motor direction pins (8 total) + PWM on TIM1 / TIM2 / TIM3 — pins TBD.
- 3 × line sensor inputs (GPIO) — pins TBD.
- ToF VL53L0X on I2C — pins TBD.
- UART to ESP32 — pins TBD.
- Independent watchdog + non-blocking task timers.

**Sources:**
- source: AGV/docs/Checklist.txt (2.1, 2.3, 2.4, 2.6, 3.4)
- source: AGV/docs/setup_wifi_MQTT.txt

---

## CON-obstacle-threshold (type: nfr)

**Statement:** ToF VL53L0X detects obstacles and autonomously stops the car
when distance < 20 cm. STM32 self-resumes when the obstacle clears; ESP32
receives `0x83` notification but does not intervene.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/Checklist.txt (2.6)

---

## CON-sr05-wall-threshold (type: nfr)

**Statement:** In Follow mode, SR05 L/R distance < 30 cm triggers lateral
Vx bias to preserve wall clearance.

**Sources:**
- source: AGV/docs/Follow_mode.txt

---

## CON-follow-control-rate (type: nfr)

**Statement:** Follow control loop runs at 20–30 Hz. OLED task runs at 5 Hz
(200 ms). FreeRTOS priorities should reflect: Button > UART > Control >
OLED > Battery.

**Sources:**
- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Follow_mode.txt
- source: AGV/docs/Checklist.txt (1.18)

---

## CON-recovery-timeouts (type: nfr)

**Statement:** Recovery mode uses fixed timeouts:

- Servo X sweep: 1° per 50 ms, full 0–180° sweep, up to 3 retries before
  error escalation.
- NFC tag wait after sweep: 30 s.
- Backend route response wait: 10 s.

**Sources:**
- source: AGV/docs/Follow_mode.txt
