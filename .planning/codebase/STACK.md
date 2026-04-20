# Technology Stack

**Analysis Date:** 2026-04-20

This repository is a **dual-subsystem** project combining embedded firmware (AGV robot) with a full-stack web dashboard. The stack is described per-subsystem because they have different toolchains and dependency systems that do not share code.

---

## Subsystem A — AGV Firmware (`AGV/carry_final/`)

Two PlatformIO projects compiled to separate MCU binaries that talk to each other over UART.

### A.1 ESP32 Master (`AGV/carry_final/esp32_master/`)

#### Languages

**Primary:**
- C++ (Arduino-flavored, C++11) — all files in `AGV/carry_final/esp32_master/src/`

#### Runtime

**Environment:**
- MCU: ESP32 (board `esp32dev`, Xtensa LX6 dual-core)
- Framework: Arduino (ESP32 Arduino core, via `platform = espressif32` in `AGV/carry_final/esp32_master/platformio.ini`)
- Serial monitor baud: 115200
- Upload baud: 921600

**Package Manager:**
- PlatformIO (`pio`)
- Lockfile: none — versions pinned via `^` semver in `AGV/carry_final/esp32_master/platformio.ini`

#### Frameworks / Libraries

**Core networking & configuration (from `platformio.ini` `lib_deps`):**
- `tzapu/WiFiManager @ ^2.0.17` — captive-portal WiFi + MQTT-IP provisioning (`AGV/carry_final/esp32_master/src/main.cpp::startPortal`)
- `knolleary/PubSubClient @ ^2.8` — MQTT client (`AGV/carry_final/esp32_master/src/mqtt_client.cpp`)
- `bblanchon/ArduinoJson @ ^6.21.3` — JSON serialization for MQTT payloads (`AGV/carry_final/esp32_master/src/mqtt_client.cpp`)
- `olikraus/U8g2 @ ^2.35.7` — SH1106 128×64 OLED driver over I²C (`AGV/carry_final/esp32_master/src/oled_display.cpp`)
- `madhephaestus/ESP32Servo @ ^3.0.5` — servo-Y PWM control (`AGV/carry_final/esp32_master/src/servo_control.cpp`)
- `HuskyLens/HUSKYLENSArduino` (git HEAD) — vision sensor interface (`AGV/carry_final/esp32_master/src/huskylens_uart.cpp`)

**Platform built-ins used:**
- `WiFi.h` — STA mode, reconnection
- `Preferences.h` — NVS key/value persistence (namespace `robotcfg`, see `NVS_NAMESPACE` in `AGV/carry_final/esp32_master/src/config.h`)
- `ArduinoOTA` — WiFi firmware upload (hostname `agv-esp32`, password `agv_ota_123`, see `AGV/carry_final/esp32_master/src/ota_manager.cpp`)
- `HardwareSerial` — `Serial` (USB log), `Serial1` (HuskyLens, legacy), `Serial2` (STM32 link at 115200 8N1 on pins 16/17)

**Testing:** Not applicable — no unit-test framework on firmware side.

**Build/Dev:**
- PlatformIO CLI for compile/upload/monitor (`pio run`, `pio run -t upload`, `pio device monitor`)
- Second environment `[env:ota]` in `platformio.ini` — uses `upload_protocol = espota`, `upload_port = 192.168.1.77`, `upload_flags = --auth=agv_ota_123` for over-the-air upload
- Build flag: `-DCORE_DEBUG_LEVEL=5` (verbose ESP-IDF logs)

#### Key Dependencies

**Critical:**
- `PubSubClient ^2.8` — backbone of backend ↔ robot communication. MQTT buffer enlarged to 4096 bytes via `mqtt.setBufferSize(MQTT_BUFFER_SIZE)` to fit full mission JSON (`AGV/carry_final/esp32_master/src/mqtt_client.cpp::mqttInit`).
- `WiFiManager ^2.0.17` — only WiFi/broker provisioning path. AP name `AGV_hospital`, open (no password), infinite portal timeout.
- `ArduinoJson ^6.21.3` — both parsing backend commands and emitting event JSON.

**Infrastructure:**
- `Preferences` (ESP32 NVS) — persists `mqtt_srv`, `mqtt_port`, `mqtt_user`, `mqtt_pass` in namespace `robotcfg`
- `ArduinoOTA` — WiFi-triggered OTA flashing; exposes TCP log stream on port 2323 (`OTA_MONITOR_PORT` in `AGV/carry_final/esp32_master/src/config.h`)
- Custom STM32 UART bootloader (AN3155) flasher — `AGV/carry_final/esp32_master/src/ota_manager.cpp` (`otaDownloadAndFlashSTM32`), uses GPIO26 (BOOT0), GPIO27 (NRST), GPIO32/33 (USART1 bridge)

#### Configuration

**Environment:**
- No `.env` file — all firmware config in `AGV/carry_final/esp32_master/src/config.h`
- Runtime-overridable via WiFiManager portal (MQTT IP) and NVS
- Default broker: `192.168.137.1:1883`, user `hospital_robot`, password `123456` (see `MQTT_DEFAULT_*` macros)

**Build:**
- `AGV/carry_final/esp32_master/platformio.ini`
- `AGV/carry_final/esp32_master/src/config.h` — all pin assignments, tuning constants, timeouts, UART framing constants

#### Platform Requirements

**Development:**
- PlatformIO Core or PlatformIO IDE (VS Code extension)
- USB-to-UART driver (CP210x / CH340) for flashing
- ESP32 dev board (any `esp32dev`-compatible)

**Production:**
- Deployed onboard the AGV chassis; communicates over the hospital's 2.4 GHz WiFi network to the backend.

---

### A.2 STM32 Slave (`AGV/carry_final/stm32_slave/`)

#### Languages

**Primary:**
- C++ (Arduino-flavored) — all files in `AGV/carry_final/stm32_slave/src/`

#### Runtime

**Environment:**
- MCU: STM32F103C8T6 "Blue Pill" (board `bluepill_f103c8`, ARM Cortex-M3 @72 MHz, 64 KB flash, 20 KB SRAM)
- Framework: Arduino (STM32duino core, via `platform = ststm32` in `AGV/carry_final/stm32_slave/platformio.ini`)
- Upload protocol: `stlink` (ST-Link V2 required)
- Serial monitor baud: 115200
- **USB CDC disabled** by design — PA11/PA12 repurposed as L298N #2 BR-direction GPIO (see comment block in `AGV/carry_final/stm32_slave/platformio.ini`). All debug text is tunneled to ESP32 via `CMD_DEBUG_MSG = 0x87`.

**Package Manager:**
- PlatformIO (`pio`)
- Lockfile: none

#### Frameworks / Libraries

**From `AGV/carry_final/stm32_slave/platformio.ini` `lib_deps`:**
- `Wire` — I²C1 (bundled with STM32 Arduino core)
- `SPI` — SPI1 for PN532 (bundled with STM32 Arduino core)
- `adafruit/Adafruit BusIO @ ^1.14.5` — transitive dependency of Adafruit PN532
- `adafruit/Adafruit PN532 @ ^1.3.3` — NFC reader driver (`AGV/carry_final/stm32_slave/src/pn532_nfc.cpp`)
- `pololu/VL53L0X @ ^1.3.1` — ToF distance sensor driver (`AGV/carry_final/stm32_slave/src/tof_sensor.cpp`)

**STM32 HAL features enabled by Arduino core (no explicit lib needed):**
- Hardware timers: `TIM1_CH1` (L1_ENA / PA8), `TIM3_CH3` (L1_ENB / PB0), `TIM2_CH2` (L2_ENA / PB3), `TIM3_CH2` (L2_ENB / PB5), `TIM3_CH1` (PB4 servo-Y, requires JTAG disable)
- USART2 (PA2/PA3) — UART link to ESP32
- USART3 (PB10/PB11) — reserved for HuskyLens (currently unused by ESP32 master, moved back to ESP32 per comment)
- SPI1 (PA5 SCK / PA6 MISO / PA7 MOSI, PB1 SS) — PN532
- I²C1 (PB6 SCL / PB7 SDA) — VL53L0X ToF

**Testing:** Not applicable.

**Build/Dev:**
- PlatformIO CLI
- ST-Link V2 USB dongle + OpenOCD (for direct `pio run -t upload` and also for `flash_stm32.cfg` at repo root)
- Pre-built `stm32_slave.bin` (46,516 bytes) kept at repo root for ESP32-driven bootloader flashing without a ST-Link

#### Key Dependencies

**Critical:**
- `Adafruit PN532` — SPI-mode reads of MIFARE card UIDs (last 2 bytes of UID → 16-bit checkpoint ID, see `AGV/carry_final/stm32_slave/src/pn532_nfc.cpp`)
- `pololu/VL53L0X` — obstacle stop at ≤200 mm, resume at ≥300 mm (`TOF_STOP_MM`, `TOF_RESUME_MM` in `AGV/carry_final/stm32_slave/src/config.h`)

**Infrastructure:**
- STM32 Arduino core — hardware timers, GPIO, PWM, SPI, I²C
- Custom UART frame protocol — see `INTEGRATIONS.md` § UART

#### Configuration

**Environment:**
- No `.env` — all config in `AGV/carry_final/stm32_slave/src/config.h`
- Motor tuning: `MOTOR_SPEED=165`, `MOTOR_TURN_90_MS=950`, `MOTOR_TURN_180_MS=1900`, PWM at 20 kHz 8-bit (`PWM_FREQ`, `PWM_RES`)
- Line-follow PID: `LF_KP=100`, `LF_KI=0`, `LF_KD=30`, `LF_MAX_CORR=120`
- Checkpoint timing: `NFC_READ_MS=100`, `NFC_REPEAT_GUARD_MS=700`, `CONFIRM_TIMEOUT_MS=800`

**Build:**
- `AGV/carry_final/stm32_slave/platformio.ini`
- `AGV/carry_final/stm32_slave/src/config.h`
- `flash_stm32.cfg` (repo root) — OpenOCD script invoking `stm32f1x unlock 0` then `program firmware.bin 0x08000000 verify reset` (note: path inside file points to legacy `AGV/carry_final/carry_final_stm32` directory; see `CONCERNS.md` if regenerated)

#### Platform Requirements

**Development:**
- PlatformIO Core
- ST-Link V2 + OpenOCD (for initial flash; subsequent flashes can use ESP32 UART-bootloader bridge)
- Physical BOOT0/NRST pull-up wiring if using UART bootloader path

**Production:**
- Mounted on the AGV chassis; cannot be accessed remotely directly — all remote flashing goes through ESP32 (`otaDownloadAndFlashSTM32` serves a `.bin` via MQTT command `ota_stm32`).

---

## Subsystem B — Hospital Dashboard (`Hospital Dashboard/`)

Two independent Node.js packages (no monorepo tool, no workspaces).

### B.1 Backend (`Hospital Dashboard/Backend/`)

#### Languages

**Primary:**
- JavaScript (ES modules, `"type": "module"` in `Hospital Dashboard/Backend/package.json`) — all files in `Hospital Dashboard/Backend/src/`

#### Runtime

**Environment:**
- Node.js (version not pinned — no `.nvmrc`, no `engines` field)
- Listens on port `3000` by default (`PORT` env, `Hospital Dashboard/Backend/src/index.js`)
- Binds `0.0.0.0` (accessible on LAN)

**Package Manager:**
- npm
- Lockfile: `Hospital Dashboard/Backend/package-lock.json` present (63,978 bytes, committed)

#### Frameworks / Libraries

**Core (from `Hospital Dashboard/Backend/package.json`):**
- `express ^4.19.2` — HTTP server & router (`Hospital Dashboard/Backend/src/index.js`)
- `cors ^2.8.5` — permissive CORS (`app.use(cors())`, no origin whitelist)
- `dotenv ^16.4.0` — loads `Hospital Dashboard/Backend/.env`
- `mongoose ^8.4.0` — MongoDB ODM (`Hospital Dashboard/Backend/src/db.js`, schemas in `Hospital Dashboard/Backend/src/models/`)
- `mqtt ^5.15.0` — MQTT.js client for broker bridge (`Hospital Dashboard/Backend/src/services/mqttService.js`)
- `multer ^2.0.2` — multipart uploads for patient photos (`Hospital Dashboard/Backend/src/routes/patients.js`, saves into `Hospital Dashboard/Backend/uploads/`)

**Testing:** Not detected — no `jest`, `vitest`, `mocha`, nor test scripts in `package.json`.

**Build/Dev:**
- `npm run dev` → `node src/index.js` (no watcher — restart manual)
- `npm start` → same
- No transpile/build step (pure ESM Node)

#### Key Dependencies

**Critical:**
- `mongoose` — all persistence; schemas at `Hospital Dashboard/Backend/src/models/{Alert,Event,MapGraph,Patient,Robot,TransportMission,User}.js`
- `mqtt` — broker bridge; both subscribes (robot events) and publishes (robot commands); see `INTEGRATIONS.md`
- `express` — REST surface under `/api/{users,events,patients,robots,maps,missions,alerts}`; also serves the frontend `dist/` as a SPA (`Hospital Dashboard/Backend/src/index.js` lines 27, 62–81)

**Infrastructure:**
- Server-Sent Events for live robot telemetry at `GET /api/robots/live` (`Hospital Dashboard/Backend/src/routes/robots.js` lines 13–41)
- SPA fallback middleware (Express 4 style, no `app.get('*')`) — any GET outside `/api` / `/uploads` returns `Frontend/dist/index.html`

#### Configuration

**Environment:**
- `Hospital Dashboard/Backend/.env` (committed — see `CONCERNS.md`) and `Hospital Dashboard/Backend/.env.example`
- Required/used keys (from `Hospital Dashboard/Backend/src/db.js` and `Hospital Dashboard/Backend/src/services/mqttService.js`):
  - `MONGO_URI` (default `mongodb://127.0.0.1:27017/hospital`)
  - `PORT` (default `3000`)
  - `NODE_ENV`
  - `MQTT_BROKER` (default `mqtt://localhost:1883`)
  - `MQTT_USER` (default `hospital_backend`)
  - `MQTT_PASS` (default `123456`)
  - `MQTT_STACK_ROBOT_ID` (default `AGV-01`)
  - `MQTT_STACK_CMD_TOPIC` / `MQTT_STACK_EVT_TOPIC` / `MQTT_STACK_RETURN_TOPIC` (defaults in code)
  - `MQTT_STRICT_SCHEMA` (opt-in `=1`, auto-on when `NODE_ENV=staging`)

**Build:**
- None — runs directly from source

#### Platform Requirements

**Development:**
- Node.js runtime
- Running MongoDB on `127.0.0.1:27017` (or cloud MongoDB URI in `.env`)
- Running MQTT broker on `localhost:1883` (Mosquitto expected — see `start-hospital-stack.bat`)

**Production:**
- Same deps as dev. SPA served from `Hospital Dashboard/Frontend/dist/` by the backend (unified on port 3000). Separate frontend dev server (port 5173) only used during local development.

---

### B.2 Frontend (`Hospital Dashboard/Frontend/`)

#### Languages

**Primary:**
- TypeScript 5.x (inferred from `tsconfig.json` features `ignoreDeprecations: "6.0"` and `moduleResolution: "bundler"`) — all code under `Hospital Dashboard/Frontend/src/`
- TSX (React) — components, hooks, pages

#### Runtime

**Environment:**
- Browser (ES2020 target per `Hospital Dashboard/Frontend/tsconfig.json`)
- Dev server: Vite on port `5173` (`Hospital Dashboard/Frontend/vite.config.ts`)
- Production: static bundle in `Hospital Dashboard/Frontend/dist/`, served by backend on port 3000

**Package Manager:**
- npm (based on `package-lock.json` size 199,268 bytes)
- pnpm override present (`pnpm.overrides` pins `vite: 6.3.5` in `Hospital Dashboard/Frontend/package.json`) but no `pnpm-lock.yaml` — primary tool is npm
- Lockfile: `Hospital Dashboard/Frontend/package-lock.json` present

#### Frameworks / Libraries

**Core UI (from `Hospital Dashboard/Frontend/package.json`):**
- `react 18.3.1` + `react-dom 18.3.1` (declared as `peerDependencies`, marked optional — unusual for an app; see `CONCERNS.md`)
- `@vitejs/plugin-react 4.7.0` (dev)
- `vite 6.3.5` (dev, pinned via pnpm override)

**Design system & primitives:**
- `@mui/material 7.3.5` + `@mui/icons-material 7.3.5` — Material UI
- `@emotion/react 11.14.0` + `@emotion/styled 11.14.1` — CSS-in-JS runtime for MUI
- Radix UI primitives (25+ packages, all at `@radix-ui/react-*` 1.x/2.x) — accordion, alert-dialog, aspect-ratio, avatar, checkbox, collapsible, context-menu, dialog, dropdown-menu, hover-card, label, menubar, navigation-menu, popover, progress, radio-group, scroll-area, select, separator, slider, slot, switch, tabs, toggle, toggle-group, tooltip
- `lucide-react 0.487.0` — icon set
- `class-variance-authority 0.7.1` + `clsx 2.1.1` + `tailwind-merge 3.2.0` — shadcn-style class composition
- `tailwindcss 4.1.12` + `@tailwindcss/vite 4.1.12` — utility CSS (Tailwind v4 + Vite plugin)
- `tw-animate-css 1.3.8` — keyframe utilities for Tailwind v4
- `next-themes 0.4.6` — dark/light mode provider
- `motion 12.23.24` — Framer Motion successor (animation library)

**Forms & inputs:**
- `react-hook-form ^7.55.0`
- `react-datepicker ^9.1.0` + `@types/react-datepicker ^6.2.0`
- `react-day-picker 8.10.1`
- `input-otp 1.4.2`
- `cmdk 1.1.1` — command palette

**Data viz / layout:**
- `recharts 2.15.2` — charts
- `embla-carousel-react 8.6.0` — carousel
- `react-resizable-panels 2.1.7`
- `react-responsive-masonry 2.7.1`
- `react-slick 0.31.0` — slick carousel wrapper
- `@popperjs/core 2.11.8` + `react-popper 2.3.0` — popper positioning
- `react-dnd 16.0.1` + `react-dnd-html5-backend 16.0.1` — drag & drop
- `vaul 1.1.2` — drawer
- `sonner 2.0.3` — toast notifications

**Utilities:**
- `date-fns 3.6.0` — date helpers

**Testing:** Not detected.

**Build/Dev:**
- `vite build` → emits `Hospital Dashboard/Frontend/dist/`
- `vite` (dev) — Vite dev server on `:5173` with proxy for `/api` and `/uploads` → `http://localhost:3000` (`Hospital Dashboard/Frontend/vite.config.ts` lines 19–34)
- PostCSS config present (`Hospital Dashboard/Frontend/postcss.config.mjs`)

#### Key Dependencies

**Critical:**
- `vite 6.3.5` — dev server + production bundler
- `react 18.3.1` — app runtime (`Hospital Dashboard/Frontend/src/main.tsx` uses `createRoot`)
- `@mui/material` + Radix + Tailwind v4 — three overlapping styling systems; the codebase uses all three (see `CONVENTIONS.md` once produced)

**Infrastructure:**
- Path alias `@` → `./src` configured in both `Hospital Dashboard/Frontend/vite.config.ts` and `Hospital Dashboard/Frontend/tsconfig.json`

#### Configuration

**Environment:**
- `Hospital Dashboard/Frontend/.env.example` documents `VITE_API_URL` (only needed for production build if backend lives on a different host)
- No `.env` committed

**Build:**
- `Hospital Dashboard/Frontend/vite.config.ts` — React + Tailwind v4 plugins, path alias, dev proxy
- `Hospital Dashboard/Frontend/tsconfig.json` — strict mode, bundler resolution, `target: ES2020`, `jsx: react-jsx`
- `Hospital Dashboard/Frontend/postcss.config.mjs`

#### Platform Requirements

**Development:**
- Node.js (Vite 6 requires Node 18+)
- Backend running on 3000 for `/api` proxy to work during `npm run dev`

**Production:**
- Any static host; in this project the static bundle is served by the Express backend via `app.use(express.static(frontendDist))` (`Hospital Dashboard/Backend/src/index.js` line 67)

---

## Cross-Cutting Infrastructure

### Orchestration Script

- `start-hospital-stack.bat` (repo root) — Windows CMD launcher that:
  1. Starts Mosquitto broker from `C:\Program Files\mosquitto\mosquitto.exe` if port 1883 is free
  2. Warns (does not start) if MongoDB is not listening on 27017
  3. Starts backend (`cd Hospital Dashboard\Backend && npm run dev`) if port 3000 is free
  4. Starts frontend Vite dev server (`cd Hospital Dashboard\Frontend && npm run dev`) if port 5173 is free
  5. Opens `http://localhost:3000` in the browser
- `stop-hospital-stack.bat` — counterpart teardown script

### Pre-Built Artifacts

- `stm32_slave.bin` (repo root, 46,516 bytes) — compiled STM32 firmware for OTA flashing via ESP32 (`otaDownloadAndFlashSTM32` URL argument in `AGV/carry_final/esp32_master/src/ota_manager.cpp`)
- `flash_stm32.cfg` — OpenOCD script for direct ST-Link flashing

### Python Virtual Environments

- `.venv/` (repo root) and `AGV/.venv/` — Python virtual environments, likely PlatformIO's internal python runtime; not part of application code

### Version Control

- Git repository rooted at `C:/Users/ironc/Desktop/Hospital`
- `.gitignore` at repo root
- `.cursor/` and `.planning/` directories used by the GSD tooling

---

*Stack analysis: 2026-04-20*
