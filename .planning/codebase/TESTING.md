# Testing Patterns

**Analysis Date:** 2026-04-20

## Summary — There Is No Automated Test Suite

This project has **zero automated tests and no CI pipeline**. Verification is entirely manual and hardware-in-the-loop. Both subsystems were checked exhaustively:

| Search | Result |
|--------|--------|
| `**/*.{test,spec}.{ts,tsx,js,jsx}` | 0 files |
| `**/jest.config.*` | 0 files |
| `**/vitest.config.*` | 0 files |
| `**/.github/workflows/*` | 0 files (no `.github/` directory exists) |
| `**/eslint.config.*`, `.eslintrc*`, `.prettierrc*`, `biome.json` | 0 files |
| `AGV/**/test/**` or `**/tests/**` | 0 embedded test directories |

Neither `Hospital Dashboard/Frontend/package.json` nor `Hospital Dashboard/Backend/package.json` declares a `test` script. PlatformIO's `pio test` scaffolding is not present in either firmware environment.

This document captures **the manual test approach that IS in use** so planners can write verification steps that match it, and flags **gaps** that future phases should close.

---

## Test Framework

### Runner / Assertion Library

- **Not present.** No Jest, Vitest, Mocha, Unity, Ceedling, PlatformIO Unit Test, or similar installed anywhere.

### Run Commands

- **Firmware:** `pio run -e esp32dev` (build ESP32) and `pio run -e bluepill_f103c8` (build STM32) — defined in `AGV/carry_final/esp32_master/platformio.ini` and `AGV/carry_final/stm32_slave/platformio.ini`. An OTA upload environment `env:ota` exists for the ESP32 (see `esp32_master/platformio.ini:19-35`) for remote reflash, but it is an upload target, not a test target.
- **Backend:** `npm run dev` or `npm start` → `node src/index.js` (Hospital Dashboard/Backend/package.json:6-9). No test command.
- **Frontend:** `npm run dev` → `vite`, `npm run build` → `vite build` (Hospital Dashboard/Frontend/package.json:6-9). No test command.
- **Convenience scripts:** `start-hospital-stack.bat` and `stop-hospital-stack.bat` in the repo root orchestrate the local dev stack but do not run tests.

---

## Test File Organization

**Not applicable — no test files exist.**

When adding tests, these are the appropriate locations based on the stack of each subsystem:

- **STM32 firmware:** PlatformIO expects `AGV/carry_final/stm32_slave/test/test_<name>/test_<name>.cpp`, declared via `test_framework = unity` in `platformio.ini`. Currently not configured.
- **ESP32 firmware:** Same PlatformIO convention: `AGV/carry_final/esp32_master/test/test_<name>/`. Currently not configured.
- **Backend Node.js:** Co-locate tests next to the module under test (e.g. `Hospital Dashboard/Backend/src/utils/bedUtils.test.js`) or introduce a top-level `test/` directory. No precedent exists; this is a decision point for a future testing phase.
- **Frontend React/TS:** Standard Vite + Vitest convention would place `*.test.tsx` next to the component, with `vitest.config.ts` at `Hospital Dashboard/Frontend/`. React Testing Library is not installed.

---

## Test Structure

**Not applicable — no tests exist.**

---

## Mocking

**Not applicable — no tests exist.**

When tests are eventually added:
- `Hospital Dashboard/Backend/src/services/mqttService.js` is an in-process singleton driven by `initMqtt()` in `index.js:88`. A test harness should export a factory variant that accepts an injected `mqtt.Client` so tests can use `aedes` (in-process MQTT broker) or `mqtt-mock`.
- `Hospital Dashboard/Backend/src/db.js` uses `mongoose.connect(uri)` directly. Tests should use `mongodb-memory-server` or an ephemeral container; no abstraction exists today.
- Firmware UART protocol (`uartSendFrame` / `uartReceiveFrame` in both `AGV/carry_final/*/src/uart_protocol.cpp`) depends on `HardwareSerial &` — native host tests would need a `HardwareSerial` stub (both PlatformIO and Unity support native-build test environments).

---

## Fixtures and Factories

**Not applicable — no tests exist.**

The closest thing to a "fixture" today is the hard-coded route constant `ROUTE_TEST_MED_TO_R4M3` in `Hospital Dashboard/Backend/src/utils/checkpointIds.js`, used by the `stack_route_test` command in `Backend/src/routes/robots.js:234` to inject a deterministic 5-point route into the robot for manual smoke testing. This plays the role of an integration fixture for the AGV stack.

Seed data for MongoDB lives in `Hospital Dashboard/Backend/seed/` (the directory exists per repo listing) and is used to populate initial beds/patients/map — treat it as a fixture resource for manual QA.

---

## Coverage

**None enforced.** Not instrumented anywhere.

---

## Test Types

### Unit Tests
- **Not present.**

### Integration Tests
- **Not present as automated suites.**
- Manual integration coverage is driven by the checklist in `AGV/docs/Checklist.txt` (see "Manual Test Plan" section below).

### End-to-End Tests
- **Not present.** No Playwright, Cypress, Puppeteer, or similar.

### Hardware-in-the-Loop (HIL) Tests
- This is the **primary verification mode** for the AGV. All testing happens on physical hardware: ESP32 + STM32F103C8 + PN532 NFC + VL53L0X ToF + HuskyLens + 4 mecanum motors. See `AGV/docs/Checklist.txt` section 4 (reproduced below) for the HIL test matrix.

---

## CI Configuration

**None.** There is no `.github/`, no `.gitlab-ci.yml`, no `azure-pipelines.yml`, no `Jenkinsfile`, no PlatformIO CI config. Every build, flash, and integration check is run manually from the developer's workstation.

**Gap:** A future phase should introduce at minimum:
1. GitHub Actions workflow running `pio run -e esp32dev` and `pio run -e bluepill_f103c8` on push — catches compile breakage of either firmware.
2. A `node --check` or type-check pass across `Hospital Dashboard/Backend/src/` and a `tsc --noEmit` pass across `Hospital Dashboard/Frontend/` — catches import errors and TS regressions before they reach the field.

---

## Manual Test Plan (Authoritative)

The manual test matrix lives in **`AGV/docs/Checklist.txt`**. Section 4 ("Tích hợp và kiểm thử") enumerates the integration tests that must be executed on real hardware before release. Transcribing section 4 here so future Claude instances can reference it without reading the whole checklist:

**Section 4 — Integration & System Testing (`AGV/docs/Checklist.txt:44-53`):**

| ID  | Task | Description |
|-----|------|-------------|
| 4.1 | UART comms ESP32 ↔ STM32 | Send/receive frames, verify CRC, exercise basic commands (`CMD_SET_MODE`, `CMD_SEND_ROUTE`, `CMD_ACK`). |
| 4.2 | Auto mode with a fake route | STM32 runs line-follow + PN532 NFC, sends checkpoint events up to ESP32 → MQTT. |
| 4.3 | Follow mode | HuskyLens tag acquisition, servo X/Y gimbal, SR-05 wall avoidance, ESP32 sends `CMD_DIRECT_VEL` (Vx,Vy,Vr) to STM32. |
| 4.4 | Find mode | Tag lost handling: SR-05 delta triggers rotate-to-search, re-acquire tag. |
| 4.5 | Recovery mode | Double-click from Follow → scan for line + PN532 checkpoint → request route → return to Auto. |
| 4.6 | Return-to-base after mission cancel | MQTT cancel → robot stops → reads current checkpoint → requests return route → runs it, handling mismatches (stop, 180°, request-route). |
| 4.7 | Low-battery management (<30%) | Simulate low battery, verify command rejection, OLED + MQTT warnings (see `BATT_MIN_PERCENT` in `AGV/carry_final/esp32_master/src/config.h:48`). |
| 4.8 | WiFiManager portal | Factory reset → long-press → portal opens → invalid credentials don't persist → valid credentials → restart → connect. |

Earlier sections of the same file list per-module smoke tests (section 1 = ESP32 modules, section 2 = STM32 modules, section 3 = configs/libraries). Treat the whole file as the manual verification source-of-truth until automated tests exist.

### Supporting Runtime Verification Aids

These are not tests, but they are the primary observability tools operators use during manual testing — document and preserve them:

- **Serial log on both MCUs** — ESP32 uses `Serial.printf` with `[TAG]` prefixes (see `AGV/carry_final/esp32_master/src/main.cpp:218, 263`). STM32 cannot use `Serial` directly (its USARTs are taken) and instead forwards log text over UART via `uart_send_debug(const char *)` (`AGV/carry_final/stm32_slave/src/uart_protocol.cpp:120-125`); the ESP32 prints those lines and republishes them on MQTT.
- **Network monitor TCP stream** — ESP32 exposes a TCP log stream on port `OTA_MONITOR_PORT = 2323` (`AGV/carry_final/esp32_master/src/config.h:89`). `netMonPrintf(...)` in `net_monitor.cpp` streams debug output over Wi-Fi so operators can tail logs without a USB cable. Used in `main.cpp:122, 135, 146, 160, 166, 199` for every runtime event of interest.
- **MQTT debug topic** — `mqttPublishDebug(const char*)` republishes STM32 debug strings on MQTT for dashboard-side visibility (`main.cpp:161`).
- **Test Lab dashboard page** — `Hospital Dashboard/Frontend/src/app/components/RobotTestLab.tsx` (~25 KB) is a hand-built UI for exercising the AGV from the browser. It issues backend commands via `sendRobotCommandPayload()` (`Frontend/src/app/api/robots.ts:98-103`), which the backend bridges to MQTT via `publishCarryStackJson()` (`Backend/src/routes/robots.js:226-269`). Commands include `stack_route_test`, `stack_route_now`, `stack_start`, `stack_cancel`, `stack_status`, `relay_set`, `relay_resume`, `tune_turn`, `test_dashboard`. This page is the go-to manual test harness for AGV behaviors today.
- **Backend request logger** — every HTTP request is echoed via `console.log('[REQ]', req.method, req.url)` (`Backend/src/index.js:47`). Keep this; it is the backend's test audit trail.

---

## Common Patterns

### Async Testing
**Not applicable — no tests exist.** For future reference, all backend endpoints are `async (req, res) => { try { ... } catch { res.status(500)... } }`; any test harness must `await` them via `supertest` or equivalent.

### Error Testing
**Not applicable — no tests exist.** Runtime error handling to verify manually:
- UART CRC mismatch silently drops frames (`AGV/carry_final/esp32_master/src/uart_protocol.cpp:69`). Observe via the absence of downstream dispatch — no log on bad CRC today. **Gap:** consider adding a counter + periodic log for CRC failures.
- HTTP 4xx bodies take two shapes: plain text (`res.status(400).send('robotId required')`) and JSON (`res.status(400).json({ error: '...' })`). The frontend parser in `Hospital Dashboard/Frontend/src/app/api/http.ts:18-26` reads `errorData.message` with `errorData.message || 'HTTP error! status: ...'` fallback, so plain-text 4xx responses surface as generic messages in the UI. **Gap:** when writing new tests, assert on the JSON shape, not the plain-text shape.

---

## Test Coverage Gaps (Priority-Ordered)

These are the highest-leverage gaps for any future testing phase.

1. **UART protocol round-trip tests (HIGH).** `crc8`, `uartSendFrame`, `uartReceiveFrame` in both `AGV/carry_final/esp32_master/src/uart_protocol.cpp` and `AGV/carry_final/stm32_slave/src/uart_protocol.cpp` are pure, portable C++ and are trivially testable under PlatformIO's native environment (`platform = native` + Unity). Coverage target: every opcode round-trip, bad-CRC rejection, truncated-frame recovery, oversize-LEN rejection.
2. **Mecanum kinematics tests (HIGH).** `motor_drive(vy, vx, vr)` in `AGV/carry_final/stm32_slave/src/motor_control.cpp:85-100` performs peak-normalization that is pure arithmetic. Easy to assert: `motor_setPWM` produces correct signs + magnitudes for each (vy, vx, vr) combination. No hardware needed.
3. **Line sensor error mapping (HIGH).** `line_getError(bits)` in `AGV/carry_final/stm32_slave/src/line_sensor.cpp:24-37` is a pure 3-bit lookup — 8 input cases, easy to enumerate.
4. **Backend route handlers (MEDIUM).** `Hospital Dashboard/Backend/src/routes/robots.js` telemetry + command endpoints are stateful against MongoDB + MQTT, but the normalization helpers (`cleanString`, `normalizeType`, `normalizeStatus` at `robots.js:43-57`) are pure and testable. Adding `supertest` + `mongodb-memory-server` would cover the full request lifecycle.
5. **MQTT payload parsers (MEDIUM).** `parseCmdMsg()` in `AGV/carry_final/esp32_master/src/mqtt_client.cpp:56+` decodes JSON from the backend. Good candidate for native-environment testing with fixture JSON strings.
6. **Frontend hook behavior (LOW).** `useRobots`, `usePatients`, `useAlerts` have polling and optimistic state — testable with Vitest + React Testing Library. Lower priority because the UI is the thinnest layer and most bugs today surface from firmware/backend integration, not the UI.
7. **E2E robot-in-dashboard (LOW, HIGH VALUE).** Would require a simulated ESP32 that speaks the MQTT stack bridge (`carry/robot/cmd` / `carry/robot/evt` in `mqttService.js:32-34`). Huge effort; skip until 1–5 are done.

---

*Testing analysis: 2026-04-20*
