# Coding Conventions

**Analysis Date:** 2026-04-20

This project contains two distinct subsystems with independent conventions. Follow the section that matches the file you are editing. Cross-system contract artifacts (UART frame format, MQTT topic payloads, checkpoint IDs) must stay byte-for-byte identical on both sides.

- **AGV firmware** — C++ on Arduino framework, built with PlatformIO. Two environments:
  - `AGV/carry_final/esp32_master/` — ESP32 (platform `espressif32`, Arduino)
  - `AGV/carry_final/stm32_slave/` — STM32F103C8 Bluepill (platform `ststm32`, Arduino-STM32)
- **Hospital Dashboard** — JavaScript / TypeScript web app:
  - `Hospital Dashboard/Backend/` — Node.js 18+, ES modules (`"type": "module"`), Express 4 + Mongoose 8 + mqtt.js
  - `Hospital Dashboard/Frontend/` — React 18 + TypeScript, bundled with Vite 6, Tailwind CSS v4, Radix + shadcn-style UI

---

## 1. AGV Firmware (C++ / Arduino)

### Module Layout Pattern

Each hardware feature or state machine is one `module_name.cpp` + `module_name.h` pair inside the corresponding `src/` directory. No sub-folders. Both sides mirror the same naming where a feature spans both MCUs (e.g., `uart_protocol.{cpp,h}` in both `AGV/carry_final/esp32_master/src/` and `AGV/carry_final/stm32_slave/src/`).

Examples:
- `AGV/carry_final/esp32_master/src/buzzer.cpp` + `buzzer.h` — function-style API (`buzzerInit`, `buzzerBeep`).
- `AGV/carry_final/esp32_master/src/mqtt_client.cpp` + `mqtt_client.h` — feature module with file-static state.
- `AGV/carry_final/stm32_slave/src/motor_control.cpp` + `motor_control.h` — function-style API (`motor_init`, `motor_drive`).

`main.cpp` on each side holds `setup()` + `loop()`, dispatches UART frames, and pumps mode-specific loops — no business logic lives there.

### Header Guards

Always `#pragma once` as the first non-comment line. Examples:
- `AGV/carry_final/esp32_master/src/uart_protocol.h` (line 1)
- `AGV/carry_final/stm32_slave/src/config.h` (line 1)
- `AGV/carry_final/esp32_master/src/globals.h` (line 1)

Do **not** use `#ifndef FOO_H / #define FOO_H / #endif`-style guards anywhere in this codebase.

### Include Order

Header files follow:
1. `#pragma once`
2. `#include <Arduino.h>`
3. `#include "config.h"`
4. Other local headers required by the public API

Example from `AGV/carry_final/esp32_master/src/globals.h`:

```cpp
#pragma once
#include <Arduino.h>
#include "config.h"
```

`.cpp` files include their own header first, then globals/helpers as needed. Example from `AGV/carry_final/esp32_master/src/auto_mode.cpp`:

```cpp
#include "auto_mode.h"
#include "globals.h"
#include "config.h"
#include "relay_control.h"
#include "uart_protocol.h"
...
```

Arduino core / external libraries are included from the header (`<Arduino.h>`, `<U8g2lib.h>`, `<PubSubClient.h>`, `<ArduinoJson.h>`, `<Preferences.h>`) — never from the `.cpp` alone.

### Naming

| Kind | Style | Example |
|------|-------|---------|
| ESP32 functions | `camelCase` | `buzzerInit()`, `autoModeLoop()`, `mqttPublishEvent()` in `buzzer.cpp`, `auto_mode.cpp`, `mqtt_client.cpp` |
| STM32 functions | `snake_case` with module prefix | `motor_init()`, `line_readBits()`, `uart_send_ack()` in `motor_control.cpp`, `line_sensor.cpp`, `uart_protocol.cpp` |
| Shared globals | `g_` prefix, `camelCase` tail | `g_mode`, `g_batteryPercent`, `g_routeLen` in `globals.h` |
| File-static variables | `s_` prefix | `s_lastBatt`, `s_pidI`, `s_returnSent` in `main.cpp`, `auto_runner.cpp`, `auto_mode.cpp` |
| Static/internal helpers | `static` + `camelCase` | `static void parseRouteFrame()` in `stm32_slave/src/main.cpp` |
| Enums | `UPPER_SNAKE` members, `PascalCase` typedef | `enum RobotMode { MODE_AUTO, MODE_FOLLOW, MODE_RECOVERY }` in `globals.h` |
| `#define` constants | `UPPER_SNAKE` | `UART_STX`, `MAX_ROUTE_LEN`, `PIN_STM32_TX` in `config.h` |
| UART command macros | `CMD_<VERB>` (ESP→STM) or `CMD_<EVENT>` (STM→ESP) | `CMD_SET_MODE`, `CMD_CHECKPOINT` in both `uart_protocol.h` files |
| Source files | `lowercase_snake` | `auto_mode.cpp`, `mqtt_client.cpp`, `uart_protocol.cpp` |

**Rule: do not mix styles inside one subsystem.** When adding a new ESP32 module, follow `camelCase`. When adding a new STM32 module, follow `snake_case` with a module prefix (`motor_`, `line_`, `nfc_`, `tof_`, `servo_`, `uart_`, `auto_`, `follow_`).

### Globals & Shared State

All cross-module shared variables live in `globals.h` + `globals.cpp` on each side. Pattern:

```47:73:AGV/carry_final/esp32_master/src/globals.h
extern volatile bool     g_stm32Obstacle;
extern volatile bool     g_stm32MissionDone;
extern volatile uint16_t g_stm32MismatchGot;
extern volatile uint16_t g_stm32MismatchExp;
extern volatile bool     g_stm32MismatchFlag;

// button events
extern volatile bool     g_btnSingleClick;
extern volatile bool     g_btnDoubleClick;
```

Rules:
- Flags set from UART dispatch/ISR-like contexts use `volatile`. See `g_newCheckpoint`, `g_stm32Obstacle`, `g_btnLongPress` in `globals.cpp`.
- Initialize every global in `globals.cpp` with an explicit value.
- Route/mission buffers are fixed-size arrays sized by a `config.h` constant (`MAX_ROUTE_LEN`) — never dynamically allocated.
- String globals use `char[N]` + `strlcpy` — never `String`. See `g_patientName[32]`, `g_destination[16]`, `g_missionId[24]` in `globals.cpp:14-16`.

**STM32 side exposes a `globals_init()` function** (declared in `stm32_slave/src/globals.h:58`). Call it first in `setup()` before any peripheral init — see `stm32_slave/src/main.cpp:161`.

### Pin & Constant Configuration

All pins, baud rates, timing constants, MQTT defaults, thresholds live in `config.h` on each side as `#define`. Never hard-code a pin number or timeout in a `.cpp` file.

- `AGV/carry_final/esp32_master/src/config.h` — ESP32 pins, MQTT defaults, OTA config, UART protocol bytes (`UART_STX`, `UART_MAX_FRAME`).
- `AGV/carry_final/stm32_slave/src/config.h` — STM32 pins (`PA2`, `PB10`, …), motor tuning (`MOTOR_SPEED`, `LF_KP`), UART bytes, route action codes (`ACTION_TURN_LEFT` …).

Both `config.h` files duplicate UART protocol constants (`UART_STX = 0x7E`, `UART_MAX_FRAME = 128`) — **any change must be applied in both places**.

### UART Framing Pattern (Contract)

Frame layout is a byte-level protocol shared by ESP32 and STM32:

```
[STX 0x7E] [LEN] [CMD] [DATA ...] [CRC8]
  LEN = sizeof(CMD + DATA)   // CRC not counted in LEN
  CRC8 polynomial 0x07, init 0x00, computed over CMD+DATA
```

Reference implementations are **byte-identical** on both sides — keep it that way when editing:
- `AGV/carry_final/esp32_master/src/uart_protocol.cpp` (`crc8`, `uartSendFrame`, `uartReceiveFrame`)
- `AGV/carry_final/stm32_slave/src/uart_protocol.cpp` (same three functions + `uart_send_*` high-level helpers)

**Command opcode convention:**
- `0x00–0x7F` — ESP32 → STM32 (commands, imperative verbs: `CMD_SET_MODE`, `CMD_SEND_ROUTE`, `CMD_CONFIRM_ARRIVAL`).
- `0x80–0xFF` — STM32 → ESP32 (events/reports: `CMD_BATTERY`, `CMD_CHECKPOINT`, `CMD_ACK`).

When adding a new opcode:
1. Append it to **both** `uart_protocol.h` files with identical value and comment.
2. Add a `case` in the receiver's dispatch (`handleSTM32()` in `esp32_master/src/main.cpp:112`, `dispatchFrame()` in `stm32_slave/src/main.cpp:73`).
3. Multi-byte integers are **big-endian**: `(uint8_t)(val >> 8), (uint8_t)(val & 0xFF)`. See `uart_send_checkpoint()` in `stm32_slave/src/uart_protocol.cpp:88-92`.
4. Every STM32→ESP32 event that comes from a command should trigger a matching `uart_send_ack(cmd_ref)`. ESP32 acknowledges checkpoints via `CMD_CONFIRM_ARRIVAL` — see `esp32_master/src/main.cpp:125-128`.

### Receive State Machine

Both sides use the same 3-state parser (`rxState` 0→1→2, static file-scope buffers). When extending framing, update both `uartReceiveFrame()` implementations together. Do not introduce dynamic allocation — buffer is fixed at `UART_MAX_FRAME = 128` bytes.

### Error Handling (Firmware)

- **No exceptions, no asserts that abort.** All failure modes are soft: invalid frames are silently dropped (`rxState = 0`), CRC mismatch returns false (`uart_protocol.cpp:69`), unknown opcodes are ACKed-and-ignored (STM32 side `main.cpp:150-153`) or logged on ESP32 (`main.cpp:203`).
- **Serial logging via `Serial.printf`** for ESP32 boot/events: `Serial.println("[BOOT] ready")`, `Serial.printf("[MODE] AUTO → FOLLOW (last CP=0x%04X)\n", g_lastCheckpointId)` in `esp32_master/src/main.cpp:324, 218`.
- **Tag format** `[SUBSYSTEM]` in brackets: `[BOOT]`, `[WM]`, `[UART]`, `[MODE]`, `[MQTT]`, `[STM32]`, `[AUTO]`, `[ToF]`, `[REQ]`. Used consistently across `esp32_master/src/main.cpp` and `mqtt_client.cpp`.
- **STM32 cannot use Serial directly** (USART1 is reserved for the bootloader, USB CDC is disabled — see comment in `stm32_slave/platformio.ini:13`). Use `uart_send_debug(const char *)` which forwards a `CMD_DEBUG_MSG` frame to the ESP32, which then prints it and republishes on MQTT. Example: `snprintf(dbg, sizeof(dbg), "mode=%u", ...); uart_send_debug(dbg);` in `stm32_slave/src/main.cpp:67-69`.
- **Dual logging for network-visible events.** ESP32 uses `netMonPrintf()` (TCP log stream on port `OTA_MONITOR_PORT = 2323`) for events that are also useful during OTA monitoring, and `Serial.println` for boot-time/local-only messages. See `esp32_master/src/main.cpp:122-128` for the pattern.
- **Acks over fire-and-forget.** STM32 acks every command it handles: `uart_send_ack(CMD_SET_MODE)` in `stm32_slave/src/main.cpp:66`. ESP32 uses acks as watchdog evidence but does not block on them.

### Comment Style (Firmware)

- File headers are big box-drawing banners with the subsystem role and notes. Pattern from `AGV/carry_final/esp32_master/src/main.cpp:1-4`:

```cpp
// ====================================================================
//  carry_final  –  ESP32 Master  –  main.cpp
//  WiFiManager portal, MQTT, UART↔STM32, mode state machine
// ====================================================================
```

- Section separators inside a file use shorter dashes, typically introducing a group of declarations or a function (see `AGV/carry_final/esp32_master/src/globals.h:5, 12, 23, 29`):

```cpp
// ── Robot modes ─────────────────────────────────────────────────────
```

- Inline comments are **bilingual** — Vietnamese is used liberally for rationale/TODO-style notes; English is used for technical identifiers, command names, and protocol descriptions. Do not translate existing Vietnamese comments; match the existing style when adding new ones (see `stm32_slave/src/motor_control.cpp:8-9, 103` for examples).
- Multi-line frame/packet descriptions go in header files above the opcode list — see `esp32_master/src/uart_protocol.h:5-8`.
- Do **not** add trivial line-by-line commentary. Existing code follows "comment the why, not the what" — e.g. `// static: avoid 4KB stack alloc` at `esp32_master/src/mqtt_client.cpp:57`.

### Formatting (Firmware)

- 4-space indentation (never tabs). Consistent across every `.cpp`/`.h` file inspected.
- Opening brace on the same line for `if`/`for`/`while`/`switch`/function definitions:
  ```cpp
  if (now - lastBatt >= 5000UL) {
      lastBatt = now;
      batteryRead();
  }
  ```
- Column-aligned assignments when declaring a block of related variables — see `globals.cpp:3-7`:
  ```cpp
  volatile RobotMode  g_mode            = MODE_AUTO;
  volatile AutoState  g_autoState       = AUTO_IDLE;
  volatile uint8_t    g_batteryPercent  = 100;
  ```
- Line length is flexible but most files stay under ~110 columns.
- `const` for read-only pointers (`static const char *T_EVT = ...` in `mqtt_client.cpp:28`).
- `static inline` for small helpers used inside one `.cpp` (e.g. `clampPwm`, `driveOne` in `motor_control.cpp:15, 22`).

### Timing Patterns

- Non-blocking polling using `millis()` delta — never `delay()` inside the main loop except for short hardware stabilization (see `relay_control.cpp:22` where `delay(5000)` is deliberate for PN532/sensor boot).
- Rate-limited telemetry uses a file-static `uint32_t s_last*` timestamp and `now - s_last > THRESHOLD` guard. Example: `publishLineBits()` in `stm32_slave/src/auto_runner.cpp:28-37`.

---

## 2. Hospital Dashboard (TypeScript / JavaScript)

### Toolchain

- **No ESLint config. No Prettier config. No Biome. No `.editorconfig`.** Zero lint/format automation exists in the repo (`Glob("**/.eslintrc*")`, `**/.prettierrc*`, `**/eslint.config.*`, `**/biome.json*`, `**/.editorconfig` all return 0 files). Style is enforced only by reviewer convention and observed precedent — document and match existing patterns below.
- **TypeScript config** — `Hospital Dashboard/Frontend/tsconfig.json`:
  - `"strict": true`
  - `"target": "ES2020"`, `"module": "ESNext"`, `"moduleResolution": "bundler"`
  - `"jsx": "react-jsx"` — do not import `React` just for JSX
  - Path alias `@/*` → `./src/*` (used in imports as `@/app/...`)
  - `"allowImportingTsExtensions": true` — `.ts`/`.tsx` extensions appear in some imports (see `main.tsx:2: import App from "./app/App.tsx"`)
  - `"noUnusedLocals": false`, `"noUnusedParameters": false` (intentionally loose)
- **Backend has no TypeScript.** Pure ES modules, `"type": "module"` in `Hospital Dashboard/Backend/package.json:5`. All imports must include the `.js` extension (`./db.js`, `./routes/users.js`) because Node ESM requires explicit extensions.
- **Build / dev scripts:**
  - Frontend (`Hospital Dashboard/Frontend/package.json:6-9`): `npm run dev` → `vite`, `npm run build` → `vite build`. No lint/format/test script.
  - Backend (`Hospital Dashboard/Backend/package.json:6-9`): `npm run dev` and `npm start` both run `node src/index.js`. No hot-reload, no lint, no test.

### Module Layout

**Backend (`Hospital Dashboard/Backend/src/`):**

```
src/
├── index.js          # Express app bootstrap, router mounting
├── db.js             # Mongoose connection helper
├── models/           # Mongoose schemas, PascalCase filenames (Robot.js, Patient.js, TransportMission.js)
├── routes/           # Express routers, lowercase plural filenames (robots.js, patients.js)
├── services/         # Long-lived singletons (mqttService.js)
└── utils/            # Pure helpers (bedUtils.js, checkpointIds.js, constants.js)
```

- One router per domain, mounted under `/api/<domain>` in `index.js`. Adding a new domain = new `routes/<domain>.js` + one `app.use('/api/<domain>', router)` line.
- Models use singular PascalCase (`Robot.js`, `Patient.js`, `TransportMission.js`), default-export `mongoose.model('Name', schema)`.
- Utilities are flat — no subfolders. Shared magic numbers live in `utils/constants.js` (`LOW_BATTERY_PCT`, `ROBOT_ONLINE_TIMEOUT_MS`, `DEFAULT_MAP_ID`).

**Frontend (`Hospital Dashboard/Frontend/src/`):**

```
src/
├── main.tsx              # ReactDOM.createRoot entry
├── vite-env.d.ts
├── styles/               # Global CSS + Tailwind imports
└── app/
    ├── App.tsx           # Root component + module-switcher state
    ├── api/              # Pure fetch wrappers (http.ts, config.ts, <domain>.ts)
    ├── components/       # Feature components (PascalCase.tsx)
    │   ├── figma/        # Third-party/vendored UI
    │   └── ui/           # shadcn-style primitives (button, card, etc.)
    ├── contexts/         # React contexts (RFIDContext.tsx)
    ├── hooks/            # `use*` custom hooks (useRobots.ts, usePatients.ts)
    ├── types/            # Shared TS interfaces (patient.ts, robot.ts)
    └── utils/
```

- Each API module in `app/api/` wraps one backend domain (`robots.ts`, `patients.ts`, …) and exports typed functions + TS interfaces.
- Each hook in `app/hooks/` owns the lifecycle (fetch, state, polling, refresh) and returns `{ data, loading, error, refresh, ...mutations }`. See `useRobots.ts`, `usePatients.ts`.

### Naming (TS/JS)

| Kind | Style | Example |
|------|-------|---------|
| Variables, functions | `camelCase` | `fetchRobots`, `makeId`, `emitRobotPosition` |
| React components | `PascalCase` | `PatientDashboard`, `RobotCenter`, `ConnectionStatus` |
| Hooks | `use` + `PascalCase` tail | `useRobots`, `usePatients`, `useAlerts`, `useMissions` |
| TS interfaces / types | `PascalCase` | `BackendRobot`, `CarryRobotStatus`, `Patient`, `ApiError` |
| Constants (module-level) | `UPPER_SNAKE` for true constants, `camelCase` for computed | `LOW_BATTERY_PCT`, `API_ENDPOINTS`, `STACK_COMMANDS`, `DEFAULT_URI` |
| Route files | lowercase plural | `routes/robots.js`, `routes/patients.js` |
| Model files | singular PascalCase | `models/Robot.js`, `models/Patient.js` |
| Component files | `PascalCase.tsx` | `components/PatientDashboard.tsx` |
| Hook files | matches export | `hooks/useRobots.ts` |
| API client files | domain name lowercase | `api/robots.ts`, `api/patients.ts` |

### Imports

Frontend: use the `@/` alias for anything under `src/` — not relative paths.

```typescript
import { Button } from '@/app/components/ui/button';
import { Patient } from '@/app/types/patient';
import { usePatients } from '@/app/hooks/usePatients';
```

(see `Hospital Dashboard/Frontend/src/app/App.tsx:3-11`)

Backend: relative paths with mandatory `.js` extensions.

```javascript
import Robot from '../models/Robot.js';
import { publishCommand } from '../services/mqttService.js';
import { LOW_BATTERY_PCT } from '../utils/constants.js';
```

(see `Hospital Dashboard/Backend/src/routes/robots.js:2-6`)

Import order (observed, not enforced):
1. Node/third-party (`express`, `mongoose`, `react`, `@radix-ui/...`)
2. Local modules via alias / relative
3. Types-only imports inline with value imports (no separate `import type` convention — everything is value-import)

### Formatting (TS/JS)

Observed from existing files; mirror these when editing:
- 2-space indentation in all `.ts`/`.tsx`/`.js` files.
- **Backend uses single quotes** (`'express'`, `'MongoDB connected:'`) — see `index.js`, `db.js`, every route file.
- **Frontend uses single quotes for TS strings** but occasionally double quotes in JSX — e.g. `main.tsx:2 import App from "./app/App.tsx"` uses double. Prefer single for new code to match the majority.
- Opening brace on the same line for functions, `if`, `for`, arrow function bodies.
- Trailing commas in multi-line object/array literals (see `Hospital Dashboard/Frontend/src/app/api/config.ts:3-36`).
- Semicolons at statement ends, universally.
- JSX one attribute per line when the opening tag is long (not machine-enforced).

### Backend Request/Response Shape

Every Express handler in `Hospital Dashboard/Backend/src/routes/` follows this pattern (see `robots.js:59-122` for the canonical example):

```javascript
router.put('/:id/telemetry', async (req, res) => {
  try {
    const robotId = String(req.params.id || '').trim();
    if (!robotId) return res.status(400).send('robotId required');

    // ... validate + normalize body ...

    await Robot.updateOne({ robotId }, { $set: update }, { upsert: true });
    res.json({ ok: true, status });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});
```

Rules:
- Every handler is `async (req, res) => { try { ... } catch (e) { res.status(500)... } }`. **Never let exceptions escape** — no Express error middleware is installed.
- Input validation is **inline** at the top of the handler. Use `String(x || '').trim()`, `cleanString(v, max)`, `normalizeType(t)`, `normalizeStatus(s)` helpers (defined at top of `robots.js:43-57`).
- Reject with short, plain-text `res.status(400).send('robotId required')` or `res.status(400).json({ error: '...' })` — both appear; `.json({ error })` is preferred for new endpoints because the frontend `handleResponse<T>` parser reads `errorData.message` / `errorData.error`.
- Successful responses are JSON objects, **not bare values**. Typical success shape: `{ ok: true, ...fields }` for commands, or the resource itself for `GET /resource/:id`.
- Errors always return 4xx (validation) or 500 (server). 503 is used for "external dependency unavailable" — e.g. MQTT broker disconnected: `return res.status(503).json({ error: 'MQTT broker not connected' })` in `robots.js:263, 312, 321`.
- Use `Robot.updateOne({ ... }, { $set: update }, { upsert: true })` for idempotent telemetry writes. `lean()` is used on reads when the document is purely for serialization (`robots.js:128`).

### Frontend API / HTTP Client

All network I/O goes through `Hospital Dashboard/Frontend/src/app/api/http.ts`. Never call `fetch()` directly from a component or hook. Pattern:

1. Add the endpoint to `API_ENDPOINTS` in `api/config.ts`.
2. Declare typed request/response interfaces in `api/<domain>.ts`.
3. Export a small async function that calls one of `get<T>`, `post<T>`, `put<T>`, `patch<T>`, `del<T>`, `upload<T>`, `uploadPut<T>` from `http.ts`.
4. Consume it from a hook in `app/hooks/`, never directly from a component.

Error contract: `ApiError` (in `http.ts:3-12`) carries `status`, `statusText`, `message`. Hooks catch it and expose `error: string | null` — see `useRobots.ts:54-57`:

```typescript
catch (err) {
  const message = err instanceof Error ? err.message : 'Failed to fetch robots';
  setError(message);
  console.error('Failed to fetch robots:', err);
}
```

### Hook Pattern

Every data hook returns `{ data, loading, error, refresh, ...mutations }`. Follow the shape in `Hospital Dashboard/Frontend/src/app/hooks/usePatients.ts` / `useRobots.ts`:
- `useState` for data, `loading`, `error`.
- `useCallback` for fetch and mutation functions (stable identity for `useEffect` deps).
- `useEffect(() => { fetchData(); }, [fetchData])` for initial load.
- Optional polling via `setInterval` ref — see `useRobots.ts:43, 63-75` (`pollInterval: number = 5000` default).
- Always `console.error(label, err)` on catch before setting `error` — uniform across hooks.

### Data Mapping (Backend ↔ Frontend)

Backend stores snake-ish MongoDB fields (`roomBed`, `photoUrl`, `lastSeenAt`). Frontend types use frontend-idiomatic names (`roomBedId`, `photo`, `lastUpdated`). Convert inside the hook via `toFrontendPatient()` / `toBackendPatientData()` helpers — see `usePatients.ts:12-69` and `useRobots.ts:8-37`. **Never leak `BackendPatient`/`BackendRobot` shapes into components.**

### Mongoose Schemas

Pattern from `Hospital Dashboard/Backend/src/models/Robot.js`:
- Always `{ timestamps: true }` at the schema-level options.
- Use `enum: [...]` for bounded string fields (`status`, `type`, `robotMode`).
- Numeric fields with ranges: `{ type: Number, min: 0, max: 100, default: 100 }` (see `Robot.js:39`).
- Nested documents are declared inline as object literals for simple shapes; use sub-schemas only when they need their own `_id` (see `timelineSchema`, `prescriptionSchema`, `noteSchema` in `Patient.js:3-25`).
- Add explicit indexes below the schema when a field is used in a `find` filter (`Robot.js:58-60`).
- Default-export `mongoose.model('Name', schema)`.

### Logging (Backend)

- `console.log('[TAG]', message, data)` with a bracketed uppercase tag — matches firmware convention. Examples:
  - `console.log('[REQ]', req.method, req.url)` in `index.js:47`
  - `console.log(`[CMD] ${command} → ${robotId}`, params)` in `robots.js:323`
  - `console.log(`[Telemetry] Received from ${robotId}:`, JSON.stringify(body))` in `robots.js:67`
  - `console.warn('[MQTT] Legacy field ...')` in `mqttService.js:50`
- `console.error('\n[db] MongoDB connection failed.')` for boot-critical failures — with multi-line remediation hints (see `db.js:17-26`). Follow this pattern for any "service can't start" error.
- No logging library (no `winston`, `pino`, `bunyan`). Do not introduce one without broader discussion.
- No redaction utility exists except one inline regex in `db.js:16` that masks credentials in the Mongo URI before logging: `uri.replace(/\/\/([^:]+):[^@]+@/, '//***:***@')`. Apply the same mask to any URI logged in new code.

### Comment Style (TS/JS)

- JSDoc `/** ... */` blocks used for exported functions with non-obvious semantics — see `robots.js:14, 22, 203-208` and `mqttService.js:31, 46`. Short one-liners are fine: `/** Build Vite — luôn tính từ ... */`.
- Inline `//` comments are bilingual Vietnamese/English — matches firmware. Do not translate; mirror the style of the file you're editing.
- Do **not** add obvious narrative comments (`// Save patient`, `// Import mongoose`). Comment intent or non-obvious contracts only.

### Environment Configuration

- Backend reads env via `dotenv.config()` (called at top of `index.js:23` and `db.js:3`). Key vars: `PORT`, `MONGO_URI`, `MQTT_BROKER`, `MQTT_USER`, `MQTT_PASS`, `MQTT_STRICT_SCHEMA`, `MQTT_STACK_ROBOT_ID`, `MQTT_STACK_CMD_TOPIC`, `MQTT_STACK_EVT_TOPIC`, `MQTT_STACK_RETURN_TOPIC`. `.env.example` is committed, `.env` is present but must not be read by agents.
- Frontend reads env via `import.meta.env.VITE_API_URL` (see `api/config.ts:1`). Vite dev server proxies `/api` and `/uploads` to `http://localhost:3000` — see `vite.config.ts:19-34`. In production the Express server in `Backend/src/index.js:60-67` serves the built frontend from `Frontend/dist` statically, so `VITE_API_URL` stays empty and requests are same-origin.

### File-Level Commentary (Rationale Comments)

Both subsystems leave **long prose comments explaining "why"** in tricky places rather than in separate docs. Examples to match when adding tricky code:
- `stm32_slave/src/config.h:25-33` — pin remap rationale (JTAG release, PC13 unsuitable)
- `esp32_master/src/mqtt_client.cpp:51-55` — MQTT command payload shapes as a comment block above the parser
- `Backend/src/index.js:26-27, 72` — why the frontend path is resolved from `__dirname` and why `app.get('*')` is avoided
- `Backend/src/routes/robots.js:142-159` — long block explaining the "stale busy" patch

Continue this convention: prefer a short rationale comment above surprising code to an external doc.

---

*Convention analysis: 2026-04-20*
