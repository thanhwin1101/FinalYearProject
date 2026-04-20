# Codebase Structure

**Analysis Date:** 2026-04-20

## Directory Layout

```
Hospital/                                   # repo root
├── AGV/                                    # embedded AGV subsystem
│   ├── carry_final/
│   │   ├── esp32_master/                   # ESP32 master firmware (PlatformIO)
│   │   │   ├── platformio.ini              # env:esp32dev + env:ota (ArduinoOTA @ 192.168.1.77)
│   │   │   └── src/                        # Arduino-framework .cpp/.h modules
│   │   │       ├── main.cpp                # setup / loop, mode dispatcher
│   │   │       ├── config.h                # pins, timings, MQTT/Wi-Fi defaults, OTA
│   │   │       ├── globals.{cpp,h}         # shared state, enums (RobotMode, AutoState)
│   │   │       ├── auto_mode.{cpp,h}       # AUTO FSM (IDLE→WAIT_START→…→COMPLETE)
│   │   │       ├── follow_mode.{cpp,h}     # FOLLOW FSM, tag-lost 30s timer, batt warn
│   │   │       ├── recovery_mode.{cpp,h}   # RECOVERY two-phase (blind-follow + return_req)
│   │   │       ├── mqtt_client.{cpp,h}     # PubSubClient bridge, carry/robot/* topics
│   │   │       ├── uart_protocol.{cpp,h}   # STX/LEN/CMD/CRC8 frame codec
│   │   │       ├── relay_control.{cpp,h}   # R1 vision / R2 line+NFC power gating
│   │   │       ├── oled_display.{cpp,h}    # U8g2 SH1106 screens per mode/state
│   │   │       ├── button_handler.{cpp,h}  # single/double/long click (GPIO15)
│   │   │       ├── buzzer.{cpp,h}          # PWM tones on GPIO25
│   │   │       ├── battery.{cpp,h}         # ADC GPIO35 + voltage-divider scaling
│   │   │       ├── servo_control.{cpp,h}   # ESP32Servo wrapper for servo Y (GPIO14)
│   │   │       ├── huskylens_uart.{cpp,h}  # legacy direct HuskyLens path (now on STM32)
│   │   │       ├── ota_manager.{cpp,h}     # ArduinoOTA (agv-esp32 / agv_ota_123)
│   │   │       ├── net_monitor.{cpp,h}     # TCP log stream on :2323
│   │   │       ├── checkpoint_names.{cpp,h}# CP ID → human name table
│   │   │       └── .pio/                   # build output (ignored)
│   │   └── stm32_slave/                    # STM32F103C8 firmware (PlatformIO)
│   │       ├── platformio.ini              # env:bluepill_f103c8, stlink upload
│   │       └── src/
│   │           ├── main.cpp                # setup / loop, UART dispatch, mode select
│   │           ├── config.h                # pins (L298N, PN532, ToF, servo), PID, speeds
│   │           ├── globals.{cpp,h}         # shared state (route, velocity, sensors)
│   │           ├── auto_runner.{cpp,h}     # line-follow PID + NFC checkpoint FSM
│   │           ├── follow_runner.{cpp,h}   # applies Vx/Vy/Vr or raw wheel PWM
│   │           ├── motor_control.{cpp,h}   # 2×L298N mecanum, PWM, turn-in-place
│   │           ├── line_sensor.{cpp,h}     # 3 digital eyes, active-LOW bits
│   │           ├── pn532_nfc.{cpp,h}       # SPI1 PN532, reads 2-byte CP ID
│   │           ├── tof_sensor.{cpp,h}      # VL53L0X I2C1, stop/resume thresholds
│   │           ├── servo_y.{cpp,h}         # PB4 / TIM3_CH1 servo Y
│   │           ├── uart_protocol.{cpp,h}   # matching STX/LEN/CMD/CRC8 codec
│   │           └── .pio/                   # build output (ignored)
│   └── docs/                               # flow references (Vietnamese ASCII diagrams)
│       ├── agent_skill.txt                 # full system overview (inputs/outputs/relays)
│       ├── Auto_mode.txt                   # Auto FSM + Return-to-Base
│       ├── Follow_mode.txt                 # Follow + Find + Recovery FSMs
│       ├── setup_wifi_MQTT.txt             # boot/provisioning flow
│       └── Checklist.txt                   # manual bring-up checklist
│
├── Hospital Dashboard/                     # cloud side (broker + UI)
│   ├── Backend/                            # Node/Express + Mongoose + MQTT bridge
│   │   ├── package.json                    # type: module, scripts: dev/start = node src/index.js
│   │   ├── .env                            # MONGO_URI, MQTT_BROKER, … (not committed ideally)
│   │   ├── .env.example                    # template
│   │   ├── seed/                           # DB seed scripts
│   │   ├── uploads/                        # multer destination (served at /uploads)
│   │   └── src/
│   │       ├── index.js                    # app bootstrap, router mount, SPA fallback
│   │       ├── db.js                       # Mongoose connect (default mongodb://127.0.0.1:27017/hospital)
│   │       ├── services/
│   │       │   └── mqttService.js          # broker client, topic bridge, mission progress
│   │       ├── routes/                     # REST handlers mounted under /api/*
│   │       │   ├── users.js
│   │       │   ├── events.js
│   │       │   ├── patients.js
│   │       │   ├── robots.js               # also exports emitRobotPosition (SSE source)
│   │       │   ├── maps.js
│   │       │   ├── missions.js
│   │       │   └── alerts.js
│   │       ├── models/                     # Mongoose schemas
│   │       │   ├── User.js
│   │       │   ├── Patient.js
│   │       │   ├── Robot.js
│   │       │   ├── TransportMission.js
│   │       │   ├── Event.js
│   │       │   ├── Alert.js
│   │       │   └── MapGraph.js
│   │       └── utils/
│   │           ├── constants.js            # LOW_BATTERY_PCT, DEFAULT_MAP_ID, …
│   │           ├── checkpointIds.js        # UID→name table + return-route builder
│   │           ├── bedUtils.js
│   │           └── agg.js
│   │
│   └── Frontend/                           # Vite + React + TS SPA
│       ├── package.json                    # scripts: dev, build (vite)
│       ├── vite.config.ts                  # @ alias → ./src, /api proxy to :3000
│       ├── tsconfig.json
│       ├── postcss.config.mjs
│       ├── index.html                      # Vite entry
│       ├── .env.example
│       ├── dist/                           # vite build output (served by backend)
│       └── src/
│           ├── main.tsx                    # createRoot → App
│           ├── vite-env.d.ts
│           ├── app/
│           │   ├── App.tsx                 # top-level shell, tabs: patients/robot/lab
│           │   ├── api/                    # REST clients (one file per resource)
│           │   │   ├── http.ts             # fetch wrapper
│           │   │   ├── config.ts
│           │   │   ├── index.ts            # barrel export
│           │   │   ├── alerts.ts
│           │   │   ├── events.ts
│           │   │   ├── maps.ts
│           │   │   ├── missions.ts
│           │   │   ├── patients.ts
│           │   │   ├── robots.ts
│           │   │   └── users.ts
│           │   ├── components/
│           │   │   ├── PatientDashboard.tsx
│           │   │   ├── PatientDetails.tsx
│           │   │   ├── PatientForm.tsx
│           │   │   ├── BedMap.tsx
│           │   │   ├── CameraCapture.tsx
│           │   │   ├── ConnectionStatus.tsx
│           │   │   ├── RobotCenter.tsx
│           │   │   ├── RobotLiveTracking.tsx
│           │   │   ├── RobotManagement.tsx
│           │   │   ├── RobotHistory.tsx
│           │   │   ├── RobotTestLab.tsx
│           │   │   ├── MissionHistoryTab.tsx
│           │   │   ├── figma/
│           │   │   │   └── ImageWithFallback.tsx
│           │   │   └── ui/                 # shadcn-style primitives (accordion…tooltip)
│           │   ├── contexts/
│           │   │   └── RFIDContext.tsx     # WebSerial RFID shared state
│           │   ├── hooks/
│           │   │   ├── index.ts
│           │   │   ├── useAlerts.ts
│           │   │   ├── useMissions.ts
│           │   │   ├── usePatients.ts
│           │   │   ├── useRobots.ts
│           │   │   └── useSerialRFID.ts
│           │   ├── types/
│           │   │   ├── patient.ts
│           │   │   └── robot.ts
│           │   └── utils/
│           │       └── patient-helpers.ts
│           └── styles/
│               ├── index.css               # imported by main.tsx
│               ├── tailwind.css
│               ├── theme.css
│               └── fonts.css
│
├── .cursor/                                # Cursor IDE assets (agents, skills, rules)
├── .planning/                              # GSD workflow state + codebase docs
│   └── codebase/                           # ← this folder (ARCHITECTURE.md, STRUCTURE.md, …)
├── .venv/                                  # (misc python venv, not used by runtime)
├── .vscode/
├── README.md                               # top-level project readme
├── start-hospital-stack.bat                # launch Mongo + backend together (Windows)
├── stop-hospital-stack.bat
├── flash_stm32.cfg                         # openocd config for manual STM32 flash
├── stm32_slave.bin                         # pre-built slave firmware binary
└── test_cmd.txt                            # scratch test command
```

## Directory Purposes

**`AGV/`**
- Purpose: everything that runs on the robot itself.
- Contains: two PlatformIO projects + design docs.
- Key files: `AGV/carry_final/esp32_master/src/main.cpp`, `AGV/carry_final/stm32_slave/src/main.cpp`, `AGV/docs/agent_skill.txt`.

**`AGV/carry_final/esp32_master/`**
- Purpose: ESP32 master firmware project.
- Contains: one `src/` directory (flat module layout) plus `platformio.ini`.
- Key files: `src/main.cpp`, `src/config.h`, `src/uart_protocol.h`, `src/mqtt_client.cpp`, `src/auto_mode.cpp`, `src/follow_mode.cpp`, `src/recovery_mode.cpp`.

**`AGV/carry_final/stm32_slave/`**
- Purpose: STM32F103C8 slave firmware project.
- Contains: flat `src/` + `platformio.ini` targeting `bluepill_f103c8`.
- Key files: `src/main.cpp`, `src/config.h`, `src/auto_runner.cpp`, `src/motor_control.cpp`, `src/pn532_nfc.cpp`, `src/uart_protocol.h`.

**`AGV/docs/`**
- Purpose: authoritative flow diagrams for each mode. Any change to FSMs or relay sequencing should update the matching file here.
- Key files: `AGV/docs/agent_skill.txt` (overview), `AGV/docs/Auto_mode.txt`, `AGV/docs/Follow_mode.txt`, `AGV/docs/setup_wifi_MQTT.txt`, `AGV/docs/Checklist.txt`.

**`Hospital Dashboard/Backend/`**
- Purpose: REST API, Mongo persistence, MQTT bridge. Serves the built SPA as static files.
- Key files: `src/index.js`, `src/db.js`, `src/services/mqttService.js`, `src/utils/checkpointIds.js`.

**`Hospital Dashboard/Backend/src/routes/`**
- Purpose: one Express router file per resource; mounted under `/api/*` in `index.js`.

**`Hospital Dashboard/Backend/src/models/`**
- Purpose: Mongoose schemas. One file per collection; module name matches collection.

**`Hospital Dashboard/Backend/src/services/`**
- Purpose: long‑running integrations. Currently only `mqttService.js` (the broker client).

**`Hospital Dashboard/Backend/src/utils/`**
- Purpose: stateless helpers shared by routes and services (aggregation, RFID↔checkpoint translation, constants).

**`Hospital Dashboard/Frontend/src/app/`**
- Purpose: all application code under a single `app/` namespace (matches the `@/app/...` import alias).

**`Hospital Dashboard/Frontend/src/app/components/ui/`**
- Purpose: shadcn‑style primitives (Radix + Tailwind). Treat these as third‑party / generated — do not add app logic here.

**`Hospital Dashboard/Frontend/src/app/components/`** (top level)
- Purpose: feature screens composed from `ui/` primitives. Each major screen = one file.

**`Hospital Dashboard/Frontend/src/app/hooks/`**
- Purpose: data‑fetching / polling hooks bound to a single API module in `api/`.

**`Hospital Dashboard/Frontend/src/styles/`**
- Purpose: global CSS. `index.css` is the entry imported by `main.tsx`.

**`.planning/`**
- Purpose: GSD workflow state, codebase analysis docs, roadmap artifacts. Not runtime.

## Key File Locations

**Entry Points:**
- `AGV/carry_final/esp32_master/src/main.cpp`: ESP32 firmware `setup()` / `loop()`.
- `AGV/carry_final/stm32_slave/src/main.cpp`: STM32 firmware `setup()` / `loop()`.
- `Hospital Dashboard/Backend/src/index.js`: HTTP server + MQTT bridge.
- `Hospital Dashboard/Frontend/src/main.tsx`: React root.
- `start-hospital-stack.bat`: local dev launcher (Mongo + backend).

**Configuration:**
- `AGV/carry_final/esp32_master/src/config.h`: ESP32 pins, timings, MQTT defaults, OTA, STM32 bootloader pins.
- `AGV/carry_final/stm32_slave/src/config.h`: STM32 pins, PWM/PID/motor constants, route size, CP timeout.
- `AGV/carry_final/esp32_master/platformio.ini`: ESP32 build env + lib_deps (WiFiManager, PubSubClient, ArduinoJson, U8g2, ESP32Servo, HuskyLens).
- `AGV/carry_final/stm32_slave/platformio.ini`: STM32 build env + lib_deps (Adafruit PN532, Pololu VL53L0X).
- `Hospital Dashboard/Backend/.env`: runtime secrets & broker/DB URIs.
- `Hospital Dashboard/Frontend/vite.config.ts`: dev server proxy `/api` → `:3000`.

**Core Logic:**
- `AGV/carry_final/esp32_master/src/auto_mode.cpp`: outbound/return mission FSM.
- `AGV/carry_final/esp32_master/src/follow_mode.cpp`: person follow + tag-lost / low-batt escalations.
- `AGV/carry_final/esp32_master/src/recovery_mode.cpp`: blind-follow-to-CP + backend return request.
- `AGV/carry_final/esp32_master/src/mqtt_client.cpp`: command parser + event publisher.
- `AGV/carry_final/esp32_master/src/uart_protocol.cpp` / `uart_protocol.h`: CRC8 frame codec + command ids.
- `AGV/carry_final/esp32_master/src/relay_control.cpp`: power-domain sequencer.
- `AGV/carry_final/stm32_slave/src/auto_runner.cpp`: line-follow PID + NFC checkpoint FSM.
- `AGV/carry_final/stm32_slave/src/motor_control.cpp`: mecanum wheel driver.
- `Hospital Dashboard/Backend/src/services/mqttService.js`: broker client and topic bridge.
- `Hospital Dashboard/Backend/src/utils/checkpointIds.js`: RFID UID ↔ checkpoint‑name + return‑route generator.
- `Hospital Dashboard/Frontend/src/app/App.tsx`: top-level shell and tab routing.

**Testing:**
- No automated test suite present in either embedded or web projects. Manual bring-up is driven from `AGV/docs/Checklist.txt` and the dashboard's `RobotTestLab` component (`Hospital Dashboard/Frontend/src/app/components/RobotTestLab.tsx`) — the lab UI exercises MQTT actions like `wheel_set`, `direct_vel`, `tune_turn`, `relay`.

## Naming Conventions

### Embedded (`AGV/carry_final/**/src/`)
- **Files:** lowercase snake_case, one header + one source per module (e.g. `auto_mode.cpp` + `auto_mode.h`, `motor_control.cpp` + `motor_control.h`). Keep this 1:1 pairing when adding new modules.
- **Types:** `PascalCase` for enums/structs (`RobotMode`, `AutoState`, `RoutePoint`). `enum class` is not used; plain `enum : uint8_t` with `MODE_*` / `AUTO_*` prefixes.
- **Constants / macros:** `UPPER_SNAKE_CASE` in `config.h` (`PIN_*`, `CMD_*`, `MOTOR_SPEED`, `MED_CHECKPOINT_ID`).
- **Global state:** `g_` prefix, `volatile` when crossed between interrupts/modes (`g_mode`, `g_route`, `g_routeLen`, `g_stm32Obstacle`).
- **Functions:** `camelCase` on ESP32 (`autoModeInit`, `mqttPublishCheckpoint`), `snake_case` on STM32 (`auto_init`, `motor_stop`, `uart_send_ack`) — this split is consistent within each subsystem; keep the style of the file you're editing.
- **UART command ids:** `CMD_*` macros, ESP32→STM32 uses `0x0x`, STM32→ESP32 uses `0x8x`.

### Backend (`Hospital Dashboard/Backend/src/`)
- **Files:** `camelCase.js` for services/utils (`mqttService.js`, `bedUtils.js`, `checkpointIds.js`); routes are lowercase plural matching the resource (`patients.js`, `robots.js`); models are `PascalCase.js` matching the class (`Patient.js`, `TransportMission.js`).
- **Exports:** ES modules (`"type": "module"`); default‑export Express routers and Mongoose models, named‑export helpers.
- **Constants:** `UPPER_SNAKE_CASE` in `utils/constants.js`.

### Frontend (`Hospital Dashboard/Frontend/src/`)
- **Files:** `PascalCase.tsx` for components (`PatientDashboard.tsx`), `camelCase.ts` for hooks (`usePatients.ts`), `lowercase.ts` for API clients (`patients.ts`), `lowercase.ts` for types (`patient.ts`).
- **Imports:** use the `@/app/...` alias (configured in `vite.config.ts` and `tsconfig.json`) instead of relative `../..` paths.
- **Hooks:** `useXxx` camelCase, colocated in `src/app/hooks/`.

## Where to Add New Code

**New robot mode / state:**
- Add enum value to both `AGV/carry_final/esp32_master/src/globals.h` and `AGV/carry_final/stm32_slave/src/globals.h` (keep values aligned — they travel over UART as a single byte).
- Create a new `<mode>_mode.{cpp,h}` pair in `esp32_master/src/` and register it in `main.cpp` (`#include`, `setup()`, `loop()` switch, and `checkModeSwitch()` if button-switchable).
- If STM32 needs a new behaviour, add `<mode>_runner.{cpp,h}` in `stm32_slave/src/` and wire it into `main.cpp::loop`'s mode switch.

**New UART command:**
- Define `CMD_*` in both `esp32_master/src/uart_protocol.h` and `stm32_slave/src/uart_protocol.h` (keep the byte value identical).
- Handle it in `esp32_master/src/main.cpp::handleSTM32` (for STM32→ESP32) or `stm32_slave/src/main.cpp::dispatchFrame` (for ESP32→STM32).
- Document in `AGV/docs/agent_skill.txt` command table.

**New MQTT action (backend → robot):**
- Extend `parseCmdMsg` in `AGV/carry_final/esp32_master/src/mqtt_client.cpp` with a new `action` branch.
- Publisher side: add a helper in `Hospital Dashboard/Backend/src/services/mqttService.js` (follow the pattern of existing `publish*` functions) and call it from the relevant route in `Backend/src/routes/*.js`.
- If new topic rather than new action: add constant near `TOPICS`/`STACK_*_TOPIC` at the top of `mqttService.js` and the matching `static const char *T_*` in `mqtt_client.cpp`.

**New REST endpoint (backend):**
- Create handler in the appropriate `Backend/src/routes/<resource>.js` (or a new router file, then mount it in `src/index.js` under `/api/<name>`).
- If it needs a new collection, add a model in `Backend/src/models/<Name>.js`.
- Stateless helpers go in `Backend/src/utils/`.

**New screen / feature (frontend):**
- New feature component: `Hospital Dashboard/Frontend/src/app/components/<FeatureName>.tsx`.
- Corresponding API client: add/extend `Frontend/src/app/api/<resource>.ts` using `http.ts`.
- Data‑fetching hook: `Frontend/src/app/hooks/use<Feature>.ts`.
- Add tab entry / routing in `Frontend/src/app/App.tsx` (`Module` union + `currentModule` state).
- Reuse primitives from `src/app/components/ui/`; do not edit `ui/` files for app logic.

**New checkpoint / map node:**
- Extend UID→name mapping in `Hospital Dashboard/Backend/src/utils/checkpointIds.js`.
- Update `MapGraph` seed if used; ensure `MED_CHECKPOINT_ID` in both `esp32_master/src/config.h` and `stm32_slave/src/config.h` stays in sync with backend.

**Utilities:**
- Embedded shared helpers: add to `globals.{cpp,h}` of the matching MCU, or create a new `<name>.{cpp,h}` pair.
- Backend: `Hospital Dashboard/Backend/src/utils/<name>.js`.
- Frontend: `Hospital Dashboard/Frontend/src/app/utils/<name>.ts` (e.g. existing `patient-helpers.ts`).

## Special Directories

**`AGV/carry_final/*/.pio/`**
- Purpose: PlatformIO build artifacts.
- Generated: Yes.
- Committed: No.

**`Hospital Dashboard/Frontend/dist/`**
- Purpose: `vite build` output. Served by the backend in production via `express.static` (see `Backend/src/index.js::frontendDist`).
- Generated: Yes (run `npm run build` inside `Frontend/`).
- Committed: Yes in this repo (present on disk) — but treat as generated when reviewing diffs.

**`Hospital Dashboard/Backend/uploads/`**
- Purpose: multer upload destination (patient photos, etc.). Served at `/uploads`.
- Generated: Yes (runtime).
- Committed: Directory kept, contents ignored.

**`Hospital Dashboard/Backend/seed/`**
- Purpose: one-shot DB seed scripts. Run manually against a fresh Mongo.
- Generated: No.
- Committed: Yes.

**`Hospital Dashboard/*/node_modules/`**
- Purpose: installed npm dependencies (two separate lockfiles — backend and frontend are independent npm roots).
- Generated: Yes (`npm install`).
- Committed: No.

**`.planning/`**
- Purpose: GSD workflow artifacts (this file lives here).
- Generated: Partially (some files are written by GSD skills; human edits are allowed).
- Committed: Yes.

**`.venv/` (repo root and `AGV/.venv/`)**
- Purpose: leftover Python virtual environments from tooling experiments. Not used by firmware build or backend runtime.
- Generated: Yes.
- Committed: No.

**`stm32_slave.bin` (repo root)**
- Purpose: pre‑built STM32 firmware image for flashing without rebuilding. Paired with `flash_stm32.cfg` for openocd.
- Generated: Yes (from `AGV/carry_final/stm32_slave/` build).
- Committed: Yes — replace after any STM32 firmware change.

---

*Structure analysis: 2026-04-20*
