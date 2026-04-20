# Architecture

**Analysis Date:** 2026-04-20

## Pattern Overview

**Overall:** Distributed IoT control system with three physical tiers:

1. **Embedded AGV tier** — Dual-MCU master/slave (`AGV/carry_final/esp32_master` ↔ `AGV/carry_final/stm32_slave`) connected over a framed UART protocol.
2. **Backend broker/API tier** — Node/Express + MongoDB + MQTT bridge (`Hospital Dashboard/Backend`).
3. **Operator UI tier** — Vite + React + TypeScript SPA (`Hospital Dashboard/Frontend`).

**Key Characteristics:**
- Master/slave split: ESP32 owns connectivity (WiFi, MQTT, OTA, UI/OLED, mode FSM); STM32 owns real‑time actuation (mecanum drive, line follow PID, NFC read, ToF guard).
- All robot↔cloud communication is MQTT JSON; all robot↔robot-internal communication is a custom 4‑byte‑header binary UART frame with CRC‑8.
- Sensor power domains are gated by two relays on ESP32 (`relay_control.cpp`) — only the sensors the active mode needs are powered, to reduce EMI and current draw.
- Arduino-style cooperative `loop()` scheduling on both MCUs (no FreeRTOS tasks are spawned — the ESP32 uses the Arduino default task and collaborative polling; docs in `AGV/docs/agent_skill.txt` refer to this conceptually as "FreeRTOS tasks", but the implemented model is a single `loop()` dispatching mode handlers and periodic timers via `millis()`).
- Backend is a stateless HTTP API plus an MQTT bridge that translates carry‑stack topics (`carry/robot/cmd|evt`, `robot/return_request`) into Mongo state and hospital‑domain topics (`hospital/robots/+/…`).

## Layers

### Embedded — ESP32 Master (`AGV/carry_final/esp32_master/src/`)

**Purpose:** Connectivity + mode state machine + operator I/O (OLED, button, buzzer).

**Entry point:** `main.cpp` — `setup()` and `loop()`.

**Responsibilities:**
- WiFi provisioning via WiFiManager portal (`main.cpp:startPortal`, `startMqttPortal`), persisted in NVS (`Preferences`, namespace `robotcfg`).
- MQTT broker connection (`mqtt_client.cpp`, `PubSubClient`), OTA (`ota_manager.cpp` + ArduinoOTA on port `OTA_HOSTNAME=agv-esp32`), TCP netmonitor log stream (`net_monitor.cpp`, port `OTA_MONITOR_PORT=2323`).
- Mode FSM dispatch in `loop()`: `autoModeLoop()` / `followModeLoop()` / `recoveryModeLoop()`.
- Relay power‑domain control (`relay_control.cpp`): R1 = vision (HuskyLens/servo), R2 = line + NFC combined. Note: docs describe three logical domains (R1 vision, R2 line, R3 NFC) but the current `config.h` collapses R2+R3 into a single `PIN_RELAY_LINE_NFC` (GPIO 23) because line sensor and PN532 share a power rail.
- UART master: owns `Serial2` (GPIO16 RX / GPIO17 TX @ 115200) and drains STM32 frames in `handleSTM32()`.
- UI: OLED SH1106 128×64 via U8g2 on I²C `SDA=21/SCL=22` (`oled_display.cpp`), buzzer on GPIO25, single button on GPIO15 (debounce + single/double/long click).
- Battery monitor: ADC on GPIO35 (`battery.cpp`), voltage‑divider scaling 3.036V…3.6V → 10%…100%.

### Embedded — STM32 Slave (`AGV/carry_final/stm32_slave/src/`)

**Purpose:** Real-time motion + local sensors.

**Entry point:** `main.cpp` — `setup()` and `loop()`.

**Responsibilities:**
- Motor driver: 4 mecanum wheels via 2× L298N (`motor_control.cpp`). Hardware PWM timers (TIM1/TIM2/TIM3) chosen so that SWD debug and JTAG release (PB3/PB4) are preserved.
- Line sensor: 3 digital eyes L/C/R (`line_sensor.cpp`, pins PB8/PB9/PA4, active LOW, INPUT_PULLUP).
- PN532 NFC over SPI1 (`pn532_nfc.cpp`, SCK=PA5/MISO=PA6/MOSI=PA7/SS=PB1) — reads 2‑byte checkpoint ID (last 2 bytes of UID).
- VL53L0X ToF over I²C1 (`tof_sensor.cpp`, SDA=PB7/SCL=PB6) — obstacle stop @ ≤200 mm, resume ≥300 mm.
- Servo Y (tilt) on PB4 / TIM3_CH1 (`servo_y.cpp`).
- UART slave: `Serial2` on PA2/PA3 @ 115200 to ESP32 (`uart_protocol.cpp`).
- Mode‑specific runners: `auto_runner.cpp` (line‑follow + NFC + PID + route/action execution) and `follow_runner.cpp` (applies Vx/Vy/Vr from ESP32 or raw wheel PWM).

### Backend — Node/Express API + MQTT bridge (`Hospital Dashboard/Backend/src/`)

**Purpose:** Persist state (Mongo), expose REST API for SPA, translate MQTT between robot carry‑stack topics and hospital domain topics, emit SSE robot position events.

**Entry point:** `src/index.js` — boots Mongo (`db.js`), mounts routers, starts MQTT bridge.

**Layers:**
- **Routes layer** (`src/routes/`): `users`, `events`, `patients`, `robots`, `maps`, `missions`, `alerts`. All mounted under `/api/*`.
- **Model layer** (`src/models/`): Mongoose schemas — `User`, `Patient`, `Robot`, `TransportMission`, `Event`, `Alert`, `MapGraph`.
- **Service layer** (`src/services/mqttService.js`): the MQTT bridge (see "Cross-Cutting Concerns").
- **Utils** (`src/utils/`): `checkpointIds.js` (UID → name mapping, returned‑route generation), `bedUtils.js`, `agg.js`, `constants.js`.
- **Static** (`app.use(express.static(frontendDist))`): serves the Vite build at `Frontend/dist`; SPA fallback sends `index.html` for any non‑`/api` GET.

### Frontend — Vite React SPA (`Hospital Dashboard/Frontend/src/`)

**Purpose:** Operator dashboard for patients, robots, missions, test lab.

**Entry point:** `src/main.tsx` → mounts `src/app/App.tsx`.

**Layers:**
- **App shell** (`src/app/App.tsx`): top‑level tabs (`patients` / `robot` / `lab`), alert panel, global refresh.
- **Components** (`src/app/components/`): feature screens (`PatientDashboard`, `RobotCenter`, `RobotLiveTracking`, `RobotManagement`, `RobotTestLab`, `RobotHistory`, `MissionHistoryTab`, `BedMap`, `CameraCapture`, `ConnectionStatus`) plus a full shadcn‑style `ui/` primitive library.
- **API clients** (`src/app/api/`): one module per resource (`alerts`, `events`, `maps`, `missions`, `patients`, `robots`, `users`) talking to `/api/*` (Vite dev proxy → `http://localhost:3000`, production same origin).
- **Hooks** (`src/app/hooks/`): `usePatients`, `useRobots`, `useAlerts`, `useMissions`, `useSerialRFID` — data fetching with polling intervals.
- **Contexts** (`src/app/contexts/RFIDContext.tsx`): shared RFID reader state from WebSerial.
- **Types/Utils** (`src/app/types/`, `src/app/utils/`): `patient.ts`, `robot.ts`, helpers.
- **Styles** (`src/styles/`): `tailwind.css`, `theme.css`, `fonts.css`.

## Data Flow

### Flow 1 — Outbound mission: dashboard → robot

1. Operator creates a delivery mission in `Frontend/src/app/hooks/useMissions.ts` → POST to `/api/missions`.
2. `Backend/src/routes/missions.js` persists a `TransportMission`, resolves the outbound route (list of checkpoint IDs) via `MapGraph` and `utils/checkpointIds.js`.
3. `Backend/src/services/mqttService.js` publishes to `carry/robot/cmd` with `{"action":"route","missionId":"…","patient":"…","destination":"…","ids":[0x8083, …]}`.
4. ESP32 `mqtt_client.cpp::parseCmdMsg` copies into `g_route[]` + `g_patientName` / `g_destination` and transitions `g_autoState → AUTO_WAIT_START`.
5. Operator presses the physical button (or dashboard sends `{"action":"start"}` which simulates `g_btnSingleClick`); `auto_mode.cpp::autoModeLoop` sends `CMD_SET_MODE=AUTO` then `CMD_SEND_ROUTE (0x02)` over UART to STM32.
6. STM32 `main.cpp::dispatchFrame` → `parseRouteFrame` → `auto_onRouteReceived()`; `auto_runner.cpp` drives mecanum wheels under line‑follow PID, watches ToF for obstacles, polls PN532 for checkpoint UIDs.
7. On each checkpoint scan, STM32 emits `CMD_CHECKPOINT (0x82)` with 2‑byte ID; ESP32 replies with `CMD_CONFIRM_ARRIVAL (0x06)` and publishes `carry/robot/evt` → backend updates mission progress → SSE/poll to frontend.
8. Final checkpoint → STM32 emits `CMD_MISSION_DONE (0x85)` → ESP32 enters `AUTO_WAIT_RETURN_BTN`.

### Flow 2 — Return / cancel

1. Cancel from dashboard publishes `{"action":"cancel"}` on `carry/robot/cmd` → ESP32 sets `g_mqttCancel=true`.
2. `auto_mode.cpp::autoModeLoop` sends `CMD_CANCEL_MISSION (0x05)` → STM32 halts motors.
3. STM32 reads current CP via PN532, sends `CMD_CHECKPOINT`. ESP32 publishes that CP ID on `robot/return_request`.
4. Backend `mqttService.js` computes return route (`utils/checkpointIds.js::routeReturnMedFrom`) and publishes `{"action":"return_route","route":[{"rfidUid":"…","action":"R"}, …]}` on `carry/robot/cmd`.
5. ESP32 `mqtt_client.cpp` parses return route, calls `autoModeActivateReturn()` → state `AUTO_RETURNING`; same UART sequence as outbound but with `g_routeType=1`.

### Flow 3 — Follow mode (person following)

1. Long‑press button in AUTO+IDLE → `main.cpp::checkModeSwitch` flips `g_mode=MODE_FOLLOW`, calls `followModeInit()`.
2. `followModeInit` (`follow_mode.cpp`): toggles relays (R1 vision ON, R2 line+NFC OFF via `relaySetFollow`), sends `CMD_SET_MODE=FOLLOW`, sets servo Y down.
3. HuskyLens data is produced on STM32 (USART3 PB10/PB11) and forwarded to ESP32 as `CMD_HUSKY_STATUS (0x89)` with detected flag + x/y/w/h/id.
4. `followModeLoop` tracks tag, adjusts servo Y, and publishes Vx/Vy/Vr as `CMD_DIRECT_VEL (0x03)` (computation also partly on STM32 `follow_runner.cpp::follow_onDirectVel`).
5. Tag lost >30 s or battery ≤30 % for 10 s → auto‑promote to `MODE_RECOVERY`.

### Flow 4 — Recovery / Find

1. `recoveryModeInit` switches relays to R1 off / R2 on (line+NFC), forces STM32 into `MODE_AUTO` via `CMD_SET_MODE`, then sends `CMD_CANCEL_MISSION` so STM32's `auto_runner` enters blind‑line‑follow until the next NFC checkpoint is read.
2. STM32 reports the CP; ESP32 publishes `robot/return_request`.
3. Backend replies with return route → robot resumes Auto back to MED.

### Flow 5 — Boot / provisioning (see `AGV/docs/setup_wifi_MQTT.txt`)

1. ESP32 `setup()` initialises OLED, relays (line+NFC ON early so STM32's `nfc_init()` succeeds), UART, then `startPortal(false)` (WiFiManager autoConnect).
2. If no WiFi creds → open AP `AGV_hospital` at 192.168.4.1 with a single field `mqtt_srv`.
3. After WiFi OK, try MQTT for 10 s; on failure → `startMqttPortal()` (portal limited to re‑entering MQTT IP), then `ESP.restart()`.
4. On success → OTA init, servo init, `autoModeInit()`, ready.

**State Management:**
- ESP32: globals in `globals.cpp/globals.h` (single shared memory, `volatile` flags for cross‑function signalling — no RTOS locks needed since everything runs on `loop()`).
- STM32: mirrors the ESP32 pattern (`globals.cpp/globals.h`).
- Backend: MongoDB via Mongoose; no in‑process cache beyond `mqttService.js` small maps (`lastStackCpByRobot`, `lastMissionRepublishAt`).
- Frontend: React component state + polling hooks; no Redux/Zustand. RFID state is the only context.

## Key Abstractions

### Route

**Purpose:** An ordered list of checkpoints plus per‑CP action (`F`/`L`/`R`/`B` → 0/1/2/3 on the wire).

**Examples:**
- Type: `struct RoutePoint` — ESP32 `AGV/carry_final/esp32_master/src/globals.h`, STM32 `AGV/carry_final/stm32_slave/src/globals.h`.
- Serialization: `auto_mode.cpp::sendRouteToSTM32` (payload `[type][count][id_hi id_lo action]×N`).
- Parsing: `AGV/carry_final/stm32_slave/src/main.cpp::parseRouteFrame`.

**Pattern:** Shared binary layout on both sides of UART, capped at `MAX_ROUTE_LEN = 30`.

### UART Frame

**Purpose:** Reliable command/event channel between ESP32 and STM32.

**Format:** `[STX=0x7E][LEN][CMD][DATA…][CRC8]` (CRC over LEN+CMD+DATA, polynomial 0x07).

**Examples:** `AGV/carry_final/esp32_master/src/uart_protocol.h`, `AGV/carry_final/stm32_slave/src/uart_protocol.h`.

**Command table (ESP32 → STM32):**
- `0x01 CMD_SET_MODE` — 1 byte mode (0=AUTO / 1=FOLLOW / 2=RECOVERY).
- `0x02 CMD_SEND_ROUTE` — `[type][count][id_hi id_lo action]×N`.
- `0x03 CMD_DIRECT_VEL` — int16 Vx, Vy, Vr (6 bytes).
- `0x04 CMD_REQUEST_STATUS` — no data.
- `0x05 CMD_CANCEL_MISSION` — no data.
- `0x06 CMD_CONFIRM_ARRIVAL` — uint16 checkpointId.
- `0x07 CMD_SERVO_SET` — uint8 x, uint8 y (x ignored — only servo Y on STM32).
- `0x08 CMD_WHEEL_SET` — int16 FL, FR, BL, BR raw PWM for per‑wheel test lab.
- `0x09 CMD_SERVO_SWEEP` — uint8 active, center, amplitude, freqX10.
- `0x0A CMD_TUNE_SPEED` — uint8 runSpeed, uint8 turnSpeed.

**Command table (STM32 → ESP32):**
- `0x81 CMD_BATTERY` — uint8 percent.
- `0x82 CMD_CHECKPOINT` — uint16 id.
- `0x83 CMD_OBSTACLE` — no data.
- `0x84 CMD_ACK` — uint8 cmd_ref.
- `0x85 CMD_MISSION_DONE` — no data.
- `0x86 CMD_MISMATCH` — uint16 got, uint16 expected.
- `0x87 CMD_DEBUG_MSG` — ASCII string (bridged to MQTT `carry/robot/evt` via `mqttPublishDebug`).
- `0x88 CMD_LINE_LOST`, `0x89 CMD_HUSKY_STATUS`, `0x8A CMD_TAG_LOST`, `0x8B CMD_TAG_FOUND`, `0x8C CMD_LINE_STATUS`, `0x8D CMD_TOF_DIST`.

### Mode State Machines

**ESP32 `RobotMode`** (`globals.h`): `MODE_AUTO=0`, `MODE_FOLLOW=1`, `MODE_RECOVERY=2`.

**ESP32 `AutoState`** (`globals.h`): `AUTO_IDLE → AUTO_WAIT_START → AUTO_RUNNING → AUTO_WAIT_RETURN_BTN → AUTO_WAIT_RETURN_ROUTE → AUTO_RETURNING → AUTO_COMPLETE`. Handler: `auto_mode.cpp::autoModeLoop`.

**STM32 `AutoState`** (`globals.h`): `AUTO_IDLE → AUTO_LINE_FOLLOW → AUTO_AT_CHECKPOINT → AUTO_DO_TURN`, with side branches `AUTO_OBSTACLE`, `AUTO_BLIND_FOLLOW` (used after cancel in recovery), `AUTO_DONE`. Handler: `auto_runner.cpp::auto_loop`.

**Follow FSM** (`follow_mode.cpp`): linear — track tag → lose tag (30 s beeping countdown) → RECOVERY, or low battery (10 s countdown) → RECOVERY, or double‑click → RECOVERY.

**Recovery FSM** (`recovery_mode.cpp`): two phases — (1) blind line‑follow until any PN532 CP read, (2) publish `robot/return_request`, re‑request every 5 s until backend returns a route; MQTT callback then flips `g_mode = MODE_AUTO` and calls `autoModeActivateReturn()`.

**Find mode** (docs `AGV/docs/Follow_mode.txt`): a sub‑state of follow invoked when tag is lost; in this codebase the behaviour is implemented inside the 30‑second timer in `followModeLoop` rather than a separate file.

### Relay Power Domains

**Purpose:** Keep only the sensors the active mode needs energised — reduces EMI into HuskyLens/PN532 and cuts idle current.

**Files:** `AGV/carry_final/esp32_master/src/relay_control.cpp` + `config.h`.

**Domains:**
- **R1 `PIN_RELAY_VISION` (GPIO 18)** — HuskyLens, servo power (and SR04/SR05 per docs). Used in Follow/Recovery.
- **R2 `PIN_RELAY_LINE_NFC` (GPIO 23)** — combined line‑sensor + PN532 rail. Used in Auto/Recovery.
- *Docs reference a third logical relay* (`PIN_RELAY_NFC`) but the current build folds line + NFC into one rail; any future split would add a third GPIO here.

**Helpers:** `relaySetAuto` (vision off, line+nfc on, 5 s settle), `relaySetFollow` (line+nfc off, vision on, 5 s settle), `relaySetRecovery` (both on).

## Entry Points

| Binary | Entry file | Role |
| --- | --- | --- |
| ESP32 master firmware | `AGV/carry_final/esp32_master/src/main.cpp` (`setup`, `loop`) | WiFi+MQTT+FSM |
| STM32 slave firmware | `AGV/carry_final/stm32_slave/src/main.cpp` (`setup`, `loop`) | Motion+sensors |
| Backend API | `Hospital Dashboard/Backend/src/index.js` (`app.listen` on `PORT`, default 3000) | Express + MQTT bridge |
| Frontend SPA | `Hospital Dashboard/Frontend/src/main.tsx` → `src/app/App.tsx` | React UI |
| PlatformIO envs | `AGV/carry_final/esp32_master/platformio.ini` (`env:esp32dev`, `env:ota`) and `AGV/carry_final/stm32_slave/platformio.ini` (`env:bluepill_f103c8`) | Build targets |
| Boot scripts | `start-hospital-stack.bat`, `stop-hospital-stack.bat` (repo root) | Launch backend + Mongo locally |

## FreeRTOS / Task Model

Despite docs describing five FreeRTOS tasks (MQTT / UART / Control / OLED / Button), the actual implementation is a single cooperative `loop()` on each MCU:

**ESP32 `loop()` order** (`main.cpp`):
1. `buttonLoop()` — debounce + click detection.
2. `mqttLoop()` — `PubSubClient::loop` + reconnect.
3. `otaLoop()` — ArduinoOTA poll.
4. `netMonLoop()` — TCP log streaming.
5. `handleSTM32()` — drain all pending UART frames.
6. `periodicTasks()` — battery (5 s), telemetry (5 s), debug MQTT (1 s), MQTT‑drop detection.
7. `checkModeSwitch()` — long‑press mode toggle.
8. Consume `g_modeChangeReq` — call the right `*ModeInit` on the main stack.
9. Dispatch mode loop: `autoModeLoop` / `followModeLoop` / `recoveryModeLoop`.

**STM32 `loop()` order** (`main.cpp`):
1. Drain UART frames → `dispatchFrame`.
2. Mode‑specific runner: `auto_loop()` (also used by `MODE_RECOVERY`) or `follow_loop()` (plus `tof_poll`).
3. 5‑second dummy battery heartbeat (`uart_send_battery(100)`).

Arduino core on ESP32 does run its own Wi‑Fi/TCP tasks internally; the application code does not spawn additional RTOS tasks.

## Error Handling

**Strategy:** Embedded uses return codes + global `volatile` flags and OLED/buzzer for user feedback. Backend uses standard Express try/catch returning JSON errors.

**Patterns:**
- STM32 reports hardware errors as structured UART commands: `CMD_OBSTACLE`, `CMD_LINE_LOST`, `CMD_MISMATCH`, `CMD_TAG_LOST`. ESP32 aggregates them on OLED and relays to MQTT (`mqttPublishEvent`, `mqttPublishDebug`).
- Checkpoint mismatch (`CMD_MISMATCH`) pushes ESP32 into `AUTO_WAIT_RETURN_ROUTE` and publishes `robot/return_request` with the *received* ID so the backend re‑plans.
- Battery ≤ 30 % blocks mission start in Auto (`auto_mode.cpp::autoModeLoop` `AUTO_WAIT_START`) and triggers a 10‑second beeping warning then auto‑Recovery in Follow (`follow_mode.cpp`).
- MQTT disconnect detection: `periodicTasks()` edge‑detects `mqttIsConnected()` and calls `oledError("MQTT disconnected!")` + buzzer.
- Backend: `connectDB()` in `db.js` exits process on Mongo unavailable; `mqttService.js` has legacy‑field warning/rejection behind `MQTT_STRICT_SCHEMA` env flag.

## Cross-Cutting Concerns

**Logging:**
- ESP32: `Serial.print*` (USB) + `netMonPrintf()` over TCP port 2323 for remote log follow (`net_monitor.cpp`).
- STM32: no direct UART logger (USB CDC disabled because PA11/PA12 are used by L298N #2); debug strings are shipped to ESP32 via `CMD_DEBUG_MSG (0x87)` and forwarded to MQTT `carry/robot/evt` as `{"evt":"debug",...}`.
- Backend: `console.*` + Express request logger middleware (`index.js`).
- Frontend: browser devtools only.

**Validation:**
- Embedded: length checks in `uart_protocol::uartReceiveFrame` (CRC‑8) and defensive `len >=` guards in every `handleSTM32` / `dispatchFrame` branch.
- Backend: Mongoose schema validation; `mqttService.js::normalizeNodeFields` with strict/lenient modes.
- Frontend: React hook level — no global schema validator (no zod/yup imported).

**Authentication:**
- Robot↔broker: MQTT username/password (`MQTT_DEFAULT_USER=hospital_robot`, `MQTT_DEFAULT_PASS=123456` — defaults in `AGV/carry_final/esp32_master/src/config.h`, override via NVS/portal).
- Backend↔broker: `MQTT_USER=hospital_backend` from `.env`.
- SPA↔backend: none — open REST API.
- OTA: `OTA_PASSWORD=agv_ota_123`.

**Configuration:**
- ESP32: compile‑time defines in `config.h`; runtime overrides in NVS namespace `robotcfg` (key `mqtt_srv`).
- STM32: compile‑time only (`config.h`); a couple of live tunables (`g_runSpeed`, `g_turnSpeed`) pushed via `CMD_TUNE_SPEED`.
- Backend: `.env` (`MONGO_URI`, `MQTT_BROKER`, `MQTT_USER`, `MQTT_PASS`, `MQTT_STACK_*_TOPIC`, `MQTT_STACK_ROBOT_ID`, `PORT`, `MQTT_STRICT_SCHEMA`).
- Frontend: `.env.example` only; Vite dev‑proxies `/api` and `/uploads` to `http://localhost:3000` (`vite.config.ts`).

**MQTT topic map (from `mqttService.js`):**
- Hospital domain subscribe: `hospital/robots/+/telemetry`, `hospital/robots/+/mission/progress|complete|returned`, `hospital/robots/+/position/waiting_return`, `hospital/robots/+/alert`.
- Carry‑stack bridge: publish commands on `carry/robot/cmd`, subscribe events on `carry/robot/evt`, subscribe return‑requests on `robot/return_request`.

---

*Architecture analysis: 2026-04-20*
