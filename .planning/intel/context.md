# Context Notes

Running notes keyed by topic, with source attribution. These are DOC-class
observations: commentary, implementation hints, and open items that are
neither locked decisions, requirements, nor hard constraints.

> **Update 2026-04 — các open question phía dưới ĐÃ RESOLVED trong
> [.planning/intel/decisions.md](./decisions.md) sau đợt code alignment.
> Các resolved questions: OLED mockups (ADR-013 trong checklist), MQTT topic
> names (ADR-004), pin assignments (config.h), battery ownership (ADR-005),
> Find fallback (ADR-006), reconnect backoff (3 s constant), 3-relay → 2-relay
> (ADR-003), FreeRTOS disabled (ADR-007). Giữ file này nguyên làm audit
> trail để hiểu quyết định trước đây.**

---

## Topic: System architecture (overview)

Two-MCU split:

- **ESP32 (master)** — WiFi/MQTT, WiFiManager portal, OLED, button UX,
  Huskylens (UART), SR05, servos, battery ADC, orchestration of mode state
  machines. Arduino framework.
- **STM32 (slave)** — real-time motion control (2× L298N, 4× mecanum motors,
  3 line sensors, PN532 over SPI, VL53L0X over I2C). STM32CubeIDE or
  Arduino_STM32 toolchain.

The two MCUs communicate over a single UART link with a 0x7E-framed +
CRC8-verified command protocol.

- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/Follow_mode.txt

---

## Topic: Operating modes (state overview)

Five modes plus a Return-to-Base sub-flow:

1. **Auto** — checkpoint route execution, STM32-driven line-follow + NFC +
   ToF. Default after boot.
2. **Follow** — tag tracking with Huskylens, SR05 wall clearance, servo-Y
   height adjust. ESP32-driven velocity commands to STM32.
3. **Find** — sub-mode of Follow. Rotation search when tag is lost > 1 s.
4. **Recovery** — safe transition back to Auto. Finds the guide line via
   Huskylens line-tracking + servo X sweep, then reads NFC to request a
   route back to MED.
5. **Return-to-Base** — Auto-mode sub-flow triggered on backend mission
   cancel. Reads current NFC checkpoint and publishes `robot/return_request`
   for a route back to MED; handles checkpoint mismatches with brake + 180°
   turn + re-request.

- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/Follow_mode.txt

---

## Topic: Hardware integrations (sensor + actuator inventory)

- **Huskylens** — UART, two modes (tag recognition, line tracking).
- **PN532 NFC reader** — SPI (SS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7 on STM32).
  Used to identify "checkpoints" on the floor; each checkpoint has a
  `uint16` ID.
- **VL53L0X ToF** — I2C. < 20 cm triggers auto-stop in STM32.
- **3 × line sensor eyes** — GPIO digital, routed to STM32 (powered via R2).
- **2 × SR05** — GPIO trigger + echo, mounted L/R for wall clearance during
  Follow.
- **Servo X** — PWM with analog feedback (ADC). Used during Recovery for the
  line-search sweep.
- **Servo Y** — PWM. Used during Follow to track tag height.
- **4 × mecanum motors** — 2× L298N driver boards, 8 direction pins + PWM via
  STM32 timers TIM1/TIM2/TIM3. `mecanum(Vx, Vy, Vr)` is the exposed primitive.
- **Buzzer** — short/long/patterned beeps.
- **OLED SH1106** — U8g2 driver on ESP32, per-mode screens.
- **Battery** — 12 V, measured via voltage divider on ESP32 GPIO35 ADC.

- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt
- source: AGV/docs/setup_wifi_MQTT.txt
- source: AGV/docs/Follow_mode.txt

---

## Topic: Connectivity (WiFi + MQTT)

Boot sequence (from DOC):

1. Initialize Serial, OLED, I2C, relay pins, GPIO36 button.
2. OLED: "Connecting... WiFi: waiting / MQTT: waiting".
3. Read `wifi_ssid, wifi_pass, mqtt_server, mqtt_port, mqtt_user, mqtt_pass`
   from Preferences.
4. If any missing/empty → open WiFiManager portal.
5. Else try WiFi with 10 s timeout → on fail → portal.
6. On WiFi OK, try MQTT with 5 s timeout → on fail → portal.
7. On MQTT OK: `R1=LOW, R2=HIGH, R3=HIGH`, settle 200 ms, init line + PN532,
   send UART `0x01=AUTO`, OLED → Auto IDLE, subscribe
   `robot/command` + `robot/return_request`.

Portal:
- AP `Robot_Setup` at `192.168.4.1`.
- Timeout 0 (wait forever).
- Extra fields for MQTT; `saveConfigCallback()` persists to Preferences;
  restart on save.

Reconnect policy:
- On MQTT disconnect during runtime, OLED shows a banner and the client
  auto-reconnects in the background (exact backoff TBD).

- source: AGV/docs/setup_wifi_MQTT.txt
- source: AGV/docs/agent_skill.txt

---

## Topic: MQTT topic map (known + TBD)

Known named topics:
- `robot/command` (subscribed) — route / cancel / status-request.
- `robot/return_request` (published) — sent with current checkpoint ID when a
  return route is needed; backend must respond with a route.

Topics implied but **not named** in the source docs (planner must finalize):
- Checkpoint-arrival events from ESP32 → backend.
- Battery % from ESP32 → backend.
- Error/warning events from ESP32 → backend.

The PRD wording is "gửi checkpoint ID lên backend qua MQTT", which
establishes the behavior but not the topic name. Planner/roadmapper decision.

- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Auto_mode.txt
- source: AGV/docs/Follow_mode.txt

---

## Topic: OLED screen design

Per-mode screens designed as ASCII mockups in a prior conversation that is
not ingested here. agent_skill.txt references them ("xem các hình vẽ ASCII"
+ "Đã thiết kế sẵn trong phần trả lời trước"). Implementation target is
U8g2 on SH1106.

**Gap to close in planner:** the actual OLED mockups are not in the ingest
set — they will need to be recovered or re-designed before `oled_display.cpp`
can be specified.

- source: AGV/docs/agent_skill.txt

---

## Topic: FreeRTOS task layout (ESP32)

From the PRD workflow section + Checklist 1.18:

- `MQTT task` — subscribe / publish / route ingest / cancel.
- `UART task` — frame send/receive, dispatch to control task.
- `Control task` — Auto/Follow/Find/Recovery state machine.
- `OLED task` — 200 ms redraw.
- `Button task` — debounce, single/double/hold detection, mode gating.
- `Battery task` — periodic ADC + MQTT publish + enforcement of ≥30% gate.

Task priorities and stack sizes are not specified in the docs — planner
will need to define.

- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt

---

## Topic: Implementation file layout (proposed modules)

**ESP32:**

- `main.ino` — setup + FreeRTOS task spawning.
- `wifi_config.cpp/h` — Preferences read/write, portal orchestration.
- `mqtt_client.cpp/h` — subscribe/publish, route parser.
- `uart_protocol.cpp/h` — 0x7E framing, CRC8, CMD dispatch.
- `relay_control.cpp/h` — R1/R2/R3 setters + settle delays.
- `oled_display.cpp/h` — U8g2 per-mode screens.
- `button_handler.cpp/h` — debounce, click classifier, gate enforcement.
- `auto_mode.cpp/h`, `follow_mode.cpp/h`, `find_mode.cpp/h`,
  `recovery_mode.cpp/h`, `return_to_base.cpp/h` — state machines.
- `battery.cpp/h` — ADC + % + threshold gate.
- `huskylens_uart.cpp/h`, `sr05.cpp/h`, `servo_control.cpp/h`,
  `buzzer.cpp/h` — drivers.
- `debug.h` — logging macros.
- `pinout.h`, `config.h` — pin assignments + constants.

**STM32:**

- `main.c` — Clock/GPIO/PWM/UART/I2C/SPI init.
- `uart_protocol.c/h` — 0x7E frame RX, CRC8, TX responses.
- `motor_control.c/h` — 2× L298N setup, `setMotorPWM()`, `mecanum(Vx,Vy,Vr)`.
- `line_sensor.c/h` — 3-eye digital read.
- `pn532_spi.c/h` — SPI init + `readPassiveTarget` + UID → checkpoint ID.
- `tof_sensor.c/h` — VL53L0X init + < 20 cm detection.
- `auto_state_machine.c/h` — route follow + checkpoint + obstacle.
- `follow_receiver.c/h` — 0x03 velocity ingest + control loop.
- `return_check.c/h` — mismatch handling (brake + 180° + re-request).
- `system_timer.c` — watchdog + non-blocking timers.
- `battery_stm32.c` — optional; battery monitoring primarily lives on ESP32.
- `pinout_stm32.h`, `config_stm32.h` — pin assignments + constants.

**Shared:**

- `README.md` — build + flash instructions (both targets).

- source: AGV/docs/Checklist.txt

---

## Topic: External libraries

**ESP32:** WiFiManager, PubSubClient, U8g2, Adafruit_VL53L0X (if routed via
ESP32; in the canonical design ToF is on STM32), a PN532_SPI library (if
routed via ESP32; canonical is STM32), ServoESP32, Preferences.

**STM32:** STM32duino SPI + Wire, VL53L0X library, a PN532_SPI library.

- source: AGV/docs/Checklist.txt (3.1, 3.2)

---

## Topic: Integration test plan (from DOC)

1. UART ESP32↔STM32 — send/receive frames, CRC, basic commands.
2. Auto mode with synthetic route — STM32 line-follow + PN532 + checkpoint
   publish over MQTT.
3. Follow mode — Huskylens tag, servo X/Y, SR05, Vx/Vy/Vr flow.
4. Find mode — lose tag, SR05-driven search, re-acquire.
5. Recovery mode — double-click from Follow → sweep line → NFC → route →
   return to Auto.
6. Return-to-Base — MQTT cancel, read checkpoint, request route, run,
   handle mismatches.
7. Battery < 30% — gate enforcement + OLED + MQTT warning.
8. WiFiManager portal — long-press → portal → wrong creds no-save → correct
   creds restart + reconnect.

- source: AGV/docs/Checklist.txt (§4)

---

## Topic: Known gaps / open questions for the planner

1. **OLED mockups not ingested** — PRD references prior-conversation ASCII
   screens that were not checked into `AGV/docs/`. Planner must re-derive.
2. **MQTT publish topic names** — arrival / battery / error topic names are
   not specified; planner must choose (e.g. `robot/arrival`, `robot/battery`,
   `robot/error`).
3. **Exact pin assignments** — most GPIO/UART/I2C/PWM pins are TBD in
   `pinout.h` and `pinout_stm32.h`.
4. **Battery telemetry ownership** — PRD implies ESP32-owned (GPIO35 ADC);
   UART CMD 0x81 from STM32 is defined but flagged optional in Checklist
   2.11. Planner should lock to ESP32-owned and either keep 0x81 reserved
   or drop it.
5. **Find mode fallback** — after 3 search attempts with no tag, Checklist
   1.10 implies an error state; specific behavior TBD (stop? back to
   Recovery? MQTT error?).
6. **Reconnect backoff policy** — MQTT reconnect after disconnect is
   mentioned but not parameterized.
7. **Recovery entry condition ambiguity** — resolved in conflict report:
   SPEC allows anywhere, PRD's universal "MED IDLE only" is overridden for
   Recovery specifically.

- source: AGV/docs/agent_skill.txt
- source: AGV/docs/Checklist.txt
- source: AGV/docs/Follow_mode.txt
