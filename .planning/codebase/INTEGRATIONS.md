# External Integrations

**Analysis Date:** 2026-04-20

Three integration layers stack in this project:

1. **Backend ↔ MQTT broker (Mosquitto)** — command/event bus for robot fleet
2. **ESP32 Master ↔ STM32 Slave** — custom UART framing protocol over Serial2
3. **ESP32 ↔ Sensors/Actuators** — vendor SDKs over I²C/SPI/UART/PWM

The Frontend integrates with the Backend exclusively over HTTP + SSE.

---

## APIs & External Services

### Backend REST API (Express)

Base URL: `http://<host>:3000` (default port, bound to `0.0.0.0`). Frontend dev server proxies `/api` and `/uploads` to the backend (`Hospital Dashboard/Frontend/vite.config.ts` lines 21–34).

Mounted routers in `Hospital Dashboard/Backend/src/index.js` lines 51–58:

**`/api/users`** (`Hospital Dashboard/Backend/src/routes/users.js`)
- `POST /api/users` — create user
- `GET /api/users` — list users
- `GET /api/users/:uid/exists` — existence probe
- `DELETE /api/users/:uid`
- `GET /api/users/:uid/detail`

**`/api/events`** (`Hospital Dashboard/Backend/src/routes/events.js`)
- `POST /api/events/button` — record button-press event
- `GET /api/events/stats/daily`
- `GET /api/events/stats/daily-by-user`

**`/api/patients`** (`Hospital Dashboard/Backend/src/routes/patients.js`)
- `GET /api/patients/beds`
- `GET /api/patients/by-bed/:bedId`
- `GET /api/patients/meta`
- `GET /api/patients/`
- `GET /api/patients/mrn/generate`
- `POST /api/patients/` (multipart: `photo` via `multer`)
- `PUT /api/patients/:id` (multipart: `photo`)
- `DELETE /api/patients/:id`
- `GET /api/patients/:id/details`
- `POST /api/patients/:id/timeline`
- `DELETE /api/patients/:id/timeline/:tid`
- `POST /api/patients/:id/prescriptions`
- `PUT /api/patients/:id/prescriptions/:pid`
- `DELETE /api/patients/:id/prescriptions/:pid`
- `POST /api/patients/:id/notes`
- `DELETE /api/patients/:id/notes/:nid`

**`/api/robots`** (`Hospital Dashboard/Backend/src/routes/robots.js`)
- `GET /api/robots/live` — **SSE stream** of `{robotId, status, batteryLevel?, currentNodeId, ts, stackEvent?, stackLogLine?, debug?}` (15 s heartbeat). Registered via `sseClients` Set, emitted by `emitRobotPosition` which is handed to the MQTT service through `setRobotPositionEmitter` (`Hospital Dashboard/Backend/src/index.js` line 87)
- `PUT /api/robots/:id/telemetry` — legacy REST telemetry ingest (not used by carry stack; kept for non-MQTT robots)
- `GET /api/robots/carry/status` — snapshot of all online carry robots (online = `lastSeenAt` within `ROBOT_ONLINE_TIMEOUT_MS = 30000` ms, `Hospital Dashboard/Backend/src/utils/constants.js`)
- `POST /api/robots/:id/command` — translates dashboard command into MQTT publish. Supports:
  - Generic commands (bridged to carry stack when `robotId === STACK_ROBOT_ID`): `set_mode`, `goto_mode`, `follow`, `idle`, `stop`, `resume`, `tune_turn`, `test_dashboard`
  - Stack-specific commands (carry stack only): `stack_route_test`, `stack_route_now`, `stack_start`, `stack_cancel`, `stack_status`, `relay_set {which, on}`, `relay_resume`

**`/api/maps`** (`Hospital Dashboard/Backend/src/routes/maps.js`)
- `POST /api/maps/` — create map graph
- `POST /api/maps/:mapId/import`
- `GET /api/maps/:mapId`
- `GET /api/maps/:mapId/route?from=<node>&to=<node>` — Dijkstra shortest path (implemented inline in `maps.js` lines 22–68)

**`/api/missions`** (`Hospital Dashboard/Backend/src/routes/missions.js`)
- `GET /api/missions/transport`
- `POST /api/missions/delivery`
- `GET /api/missions/carry/next`
- `PUT /api/missions/carry/:missionId/progress`
- `POST /api/missions/carry/:missionId/complete`
- `POST /api/missions/carry/:missionId/cancel`
- `POST /api/missions/carry/:missionId/returned`
- `GET /api/missions/delivery/history`

**`/api/alerts`** (`Hospital Dashboard/Backend/src/routes/alerts.js`)
- `GET /api/alerts/`
- `POST /api/alerts/`
- `PUT /api/alerts/:id/resolve`

**Static:**
- `GET /uploads/*` — patient photos from `Hospital Dashboard/Backend/uploads/` via `express.static`
- `GET /*` (non-`/api`, non-`/uploads`) — SPA fallback to `Hospital Dashboard/Frontend/dist/index.html` (`Hospital Dashboard/Backend/src/index.js` lines 62–81)

---

## Data Storage

### Databases

**MongoDB** (via `mongoose ^8.4.0`)
- Connection: `MONGO_URI` env var (default `mongodb://127.0.0.1:27017/hospital`) — `Hospital Dashboard/Backend/src/db.js`
- Connection timeout: `serverSelectionTimeoutMS: 8000`
- Collections (schemas in `Hospital Dashboard/Backend/src/models/`):
  - `Robot.js` — robot fleet state (robotId, type, status, batteryLevel, currentLocation, transportData, firmwareVersion, lastSeenAt)
  - `TransportMission.js` — carry-robot mission record (missionId, carryRobotId, bedId, currentNodeId, destinationNodeId, status in {`pending`, `en_route`, `arrived`, `completed`, `cancelled`}, returnedAt)
  - `MapGraph.js` — node/edge graph used for routing (Dijkstra in `routes/maps.js`)
  - `Patient.js`, `User.js`, `Event.js`, `Alert.js` — dashboard domain records
- Docker fallback documented in `Hospital Dashboard/Backend/src/db.js`: `docker run -d -p 27017:27017 --name mongo mongo:7`

**MongoDB startup check:** `start-hospital-stack.bat` lines 37–44 warn (does not launch) if port 27017 is not listening.

### File Storage

**Local filesystem:**
- Patient photo uploads saved to `Hospital Dashboard/Backend/uploads/` by `multer` (configured in `Hospital Dashboard/Backend/src/routes/patients.js`)
- Exposed read-only via `app.use('/uploads', express.static('uploads'))` (`Hospital Dashboard/Backend/src/index.js` line 60)

### Caching

None. MQTT message dedup / republish cooldowns are implemented in-memory via `Map` objects in `Hospital Dashboard/Backend/src/services/mqttService.js` (`lastStackCpByRobot`, `lastMissionRepublishAt`, `MISSION_REPUBLISH_COOLDOWN_MS = 5000`).

---

## Authentication & Identity

**No authentication layer.** The backend has:
- CORS open to all origins (`app.use(cors())` with no options, `Hospital Dashboard/Backend/src/index.js` line 42)
- No session/JWT/OAuth middleware
- `users` routes create/delete users by arbitrary `uid` with no credential verification

**Service-account style credentials exist only for MQTT and OTA:**
- MQTT broker user/pass (see § MQTT below)
- ESP32 OTA password: `agv_ota_123` (`OTA_PASSWORD` in `AGV/carry_final/esp32_master/src/config.h`)

---

## Monitoring & Observability

### Error Tracking

None. Errors are surfaced via:
- `console.log` / `console.error` in backend (`Hospital Dashboard/Backend/src/index.js` line 47 request logger)
- `Serial.printf` on ESP32 (mirrored to an optional TCP log stream on port 2323 via `netMonPrintf` in `AGV/carry_final/esp32_master/src/net_monitor.cpp`)
- STM32 `CMD_DEBUG_MSG (0x87)` frames → ESP32 → forwarded to MQTT topic `carry/robot/debug` (`mqttPublishDebug` in `AGV/carry_final/esp32_master/src/mqtt_client.cpp` line 470)

### Logs

- Backend: plain console; every incoming HTTP request is logged (`Hospital Dashboard/Backend/src/index.js` lines 46–49)
- ESP32: USB serial at 115200 baud; optional WiFi log reader at `tcp://<esp32-ip>:2323`
- STM32: no direct serial (USB CDC disabled); tunneled to ESP32 via UART debug frames

---

## CI/CD & Deployment

### Hosting

- On-premises / local — no CI workflow files detected; no `.github/`, `.gitlab-ci.yml`, `azure-pipelines.yml`.

### CI Pipeline

None detected.

### Deployment / Upgrade Paths

- **ESP32 WiFi OTA**: `[env:ota]` in `AGV/carry_final/esp32_master/platformio.ini`; `pio run -e ota -t upload` to `192.168.1.77` with auth `agv_ota_123`
- **ESP32 over MQTT (in-band trigger)**: dashboard publishes `{"action":"ota_esp32"}` to `carry/robot/cmd`; firmware responds with `{"evt":"ota_esp32_ready"}` then accepts ArduinoOTA push (`AGV/carry_final/esp32_master/src/mqtt_client.cpp` lines 288–294)
- **STM32 over MQTT** (`{"action":"ota_stm32","url":"http://…/slave.bin"}`): ESP32 fetches the binary over HTTP and flashes STM32 via the AN3155 UART bootloader on USART1 (PA9/PA10), using GPIO26→BOOT0 and GPIO27→NRST (`otaDownloadAndFlashSTM32` in `AGV/carry_final/esp32_master/src/ota_manager.cpp`, pin map in `AGV/carry_final/esp32_master/src/config.h` lines 91–103)
- **STM32 via ST-Link**: `flash_stm32.cfg` at repo root — OpenOCD `stm32f1x unlock 0 && program firmware.bin 0x08000000 verify reset`

---

## MQTT Broker Integration

**Broker:** Mosquitto (inferred from `start-hospital-stack.bat` which launches `C:\Program Files\mosquitto\mosquitto.exe`)

**URL:**
- Backend: `MQTT_BROKER` env (default `mqtt://localhost:1883`) — `Hospital Dashboard/Backend/src/services/mqttService.js` line 17
- ESP32: persisted in NVS (key `mqtt_srv`); factory default `192.168.137.1:1883` (`MQTT_DEFAULT_SERVER` / `MQTT_DEFAULT_PORT` in `AGV/carry_final/esp32_master/src/config.h`); user-editable via WiFiManager portal

**Credentials:**
- Backend: `MQTT_USER=hospital_backend`, `MQTT_PASS=123456` (defaults in `mqttService.js` lines 18–19; also in `Backend/.env.example`)
- ESP32: `MQTT_DEFAULT_USER=hospital_robot`, `MQTT_DEFAULT_PASS=123456` (`config.h` lines 54–55)
- MQTT buffer on ESP32: `MQTT_BUFFER_SIZE=4096` (mission payloads exceed default 256 B)
- Reconnect interval: `MQTT_RECONNECT_MS=3000` (ESP32); `reconnectPeriod: 5000` + `connectTimeout: 10000` (backend, `mqttService.js` lines 84–85)

### Topic Map

Two parallel topic schemes coexist because the backend supports both a legacy fleet schema and the carry stack:

**Carry stack (single robot, current AGV):**
| Topic | Publisher | Subscriber | Purpose |
|---|---|---|---|
| `carry/robot/cmd` | Backend | ESP32 | Commands (JSON) — route, return_route, cancel, start, stop, resume, set_mode, tune_turn, test_dashboard, relay, relay_resume, status, direct_vel, wheel_set, servo_set, servo_center, ota_esp32, ota_stm32, or full `{"mission":{…}}` |
| `carry/robot/evt` | ESP32 | Backend | Events (JSON) — `checkpoint`, `idle_scan`, `battery`, `mission_done`, `mission_rejected`, `cancelled`, `route_accept`, `route_pending`, `line_lost`, `obstacle`, `cp_mismatch`, `recovery_nfc`, `relay_ack`, `relay_resume`, `servo_ack`, `ota_esp32_ready`, `ota_stm32_start`/`done`/`error`, `telemetry` |
| `carry/robot/debug` | ESP32 | Backend | STM32 debug passthrough: `{"dbg":"<text>"}` |
| `robot/return_request` | ESP32 | Backend | Returning from destination: `{"checkpoint_id":<id>}` — backend replies by publishing `{"action":"return_route","ids":[...]}` to `carry/robot/cmd` |

Carry-stack topics are overridable via env vars: `MQTT_STACK_CMD_TOPIC`, `MQTT_STACK_EVT_TOPIC`, `MQTT_STACK_RETURN_TOPIC` (`Hospital Dashboard/Backend/src/services/mqttService.js` lines 32–35). Robot ID used for stack bridging: `MQTT_STACK_ROBOT_ID` (default `AGV-01`).

**Legacy fleet scheme (subscribed but not currently driven by the AGV firmware):**
| Topic pattern | Direction | Purpose |
|---|---|---|
| `hospital/robots/+/telemetry` | Robot → Backend | Periodic status |
| `hospital/robots/+/mission/progress` | Robot → Backend | Mission updates |
| `hospital/robots/+/mission/complete` | Robot → Backend | Mission done |
| `hospital/robots/+/mission/returned` | Robot → Backend | Returned to home |
| `hospital/robots/+/position/waiting_return` | Robot → Backend | Waiting for return route |
| `hospital/robots/+/alert` | Robot → Backend | Alerts |

Handled by `handleMessage`/`handleTelemetry`/etc. in `Hospital Dashboard/Backend/src/services/mqttService.js` (TOPICS map at lines 22–29).

### Message Schemas (key examples)

Payloads are UTF-8 JSON. Reference implementations:

**Backend → ESP32 on `carry/robot/cmd`:**
```json
// Outbound route (ids-only form)
{"action":"route","missionId":"M123","patient":"John","destination":"R4M3","ids":[32899, 13723]}

// Outbound route (full form from dashboard)
{"mission":{"missionId":"M123","patientName":"John","bedId":"R4M3",
  "outboundRoute":[{"nodeId":"MED","rfidUid":"45:54:80:83","action":"F"}, ...]}}

// Return route (backend response to return_request)
{"action":"return_route","route":[{"rfidUid":"45:54:80:83","action":"R"}, ...]}
// or legacy ids-only:
{"action":"return_route","ids":[13723, 32899]}

// Misc
{"action":"cancel"} | {"action":"start"} | {"action":"stop"} | {"action":"resume"}
{"action":"set_mode","mode":"auto|follow|recovery"}
{"action":"tune_turn","spinMs":950,"brakeMs":80,"wallCm":20}
{"action":"relay","which":"vision|line|nfc","on":true}
{"action":"direct_vel","vx":100,"vy":0,"vr":-50}
{"action":"wheel_set","fl":120,"fr":-120,"bl":120,"br":-120}
{"action":"servo_set","y":100} | {"action":"servo_center"}
{"action":"ota_esp32"} | {"action":"ota_stm32","url":"http://host:8080/slave.bin"}
```

**ESP32 → Backend on `carry/robot/evt`:**
```json
{"evt":"checkpoint","id":32899}
{"evt":"idle_scan","id":32899}
{"evt":"battery","pct":85}
{"evt":"mission_done","mission":"M123","success":true}
{"evt":"mission_rejected","reason":"low_battery","pct":25}
{"evt":"route_accept","n":2}
{"evt":"telemetry","debug":{"battEsp":90,"tofMm":450,"line":2,
  "spinMs":950,"brakeMs":80,"wallCm":20,"mode":"auto","run":false,
  "testDash":false,"r1":0,"r2":1,"r3":1,"servoY":100}}
```

Publish helpers: `mqttPublishCheckpoint`, `mqttPublishIdleScan`, `mqttPublishBattery`, `mqttPublishReturnRequest`, `mqttPublishStatus`, `mqttPublishMissionDone`, `mqttPublishDebug`, `mqttPublishEvent`, `mqttPublishTelemetry` (all in `AGV/carry_final/esp32_master/src/mqtt_client.cpp`).

Backend publish helpers: `publishCommand`, `publishCarryStackJson` (`Hospital Dashboard/Backend/src/services/mqttService.js` line 271) — the latter publishes with QoS 1 to `STACK_CMD_TOPIC`.

### Checkpoint ID encoding

Map nodes are identified by the **last 2 bytes of a 4-byte MIFARE UID** cast to a `uint16`. The canonical mapping lives in `Hospital Dashboard/Backend/seed/checkpointsF1.js` (35 nodes: `R1M1..R4D2`, `MED`, `J4`, `H_TOP`, `H_BOT`, `H_MED`). Helpers: `uidStringToCpId` and `checkpointIdToName` in `Hospital Dashboard/Backend/src/utils/checkpointIds.js`; ESP32 equivalent `uidStringToId` in `AGV/carry_final/esp32_master/src/mqtt_client.cpp` lines 35–48. Home node is `MED = "45:54:80:83"` → `0x8083` (= 32899); referenced as `MED_CHECKPOINT_ID` on both sides.

---

## UART Protocol: ESP32 ↔ STM32

**Physical link:**
- ESP32 `Serial2` on GPIO16 (RX) / GPIO17 (TX), 115200 8N1 (`PIN_STM32_RX`, `PIN_STM32_TX`, `STM32_BAUD` in `AGV/carry_final/esp32_master/src/config.h`)
- STM32 `USART2` on PA2 (TX) / PA3 (RX), 115200 (`PIN_UART_TX`, `PIN_UART_RX`, `ESP_BAUD` in `AGV/carry_final/stm32_slave/src/config.h`)

**Frame format:** `[STX=0x7E] [LEN] [CMD] [DATA…] [CRC8]`
- `LEN` = `sizeof(CMD) + sizeof(DATA)` — CRC is not counted
- `CRC8` polynomial `0x07` over `[LEN][CMD][DATA...]`
- Max frame: `UART_MAX_FRAME = 128` bytes (both sides)
- Endianness: **big-endian** for all multi-byte integers
- Implementation: `crc8`, `uartSendFrame`, `uartReceiveFrame` in `AGV/carry_final/esp32_master/src/uart_protocol.cpp` and `AGV/carry_final/stm32_slave/src/uart_protocol.cpp`

### Command Table: ESP32 → STM32

| CMD | Name | Data Bytes | Format | Handler (STM32) |
|---|---|---|---|---|
| `0x01` | `CMD_SET_MODE` | 1 | `uint8 mode` — `0=AUTO`, `1=FOLLOW`, `2=RECOVERY` | `switchMode` in `AGV/carry_final/stm32_slave/src/main.cpp` line 54 |
| `0x02` | `CMD_SEND_ROUTE` | 2 + 3N | `uint8 type (0=outbound, 1=return)` + `uint8 count` + N × `[id_hi, id_lo, action]` where action ∈ `{0=F continue, 1=L left, 2=R right, 3=B back-180°}` | `parseRouteFrame` + `auto_onRouteReceived` (`main.cpp` lines 33–85) |
| `0x03` | `CMD_DIRECT_VEL` | 6 | `int16 Vx, int16 Vy, int16 Vr` (BE) | `follow_onDirectVel` — only used in FOLLOW mode |
| `0x04` | `CMD_REQUEST_STATUS` | 0 | — | Replies with `CMD_BATTERY` + `CMD_LINE_STATUS` + `CMD_TOF_DIST` + `CMD_ACK` |
| `0x05` | `CMD_CANCEL_MISSION` | 0 | — | Stops motors, aborts route; in FOLLOW zeroes Vx/Vy/Vr |
| `0x06` | `CMD_CONFIRM_ARRIVAL` | 2 | `uint16 checkpointId` (BE) | ESP32 acknowledges an earlier `CMD_CHECKPOINT`; unblocks STM32 to execute the route action |
| `0x07` | `CMD_SERVO_SET` | 2 | `uint8 x_angle, uint8 y_angle` (x ignored — only Y servo) | `servo_setY(buf[1])` |
| `0x08` | `CMD_WHEEL_SET` | 8 | `int16 FL, FR, BL, BR` (raw PWM −255..255) | `follow_onWheelSet` — test-bench wheel control |
| `0x09` | `CMD_SERVO_SWEEP` | 4 | `uint8 active, uint8 center, uint8 amplitude, uint8 freqX10` | Defined in header; no handler on current STM32 (falls through to `uart_send_ack`) |
| `0x0A` | `CMD_TUNE_SPEED` | 2 | `uint8 runSpeed, uint8 turnSpeed` | Updates `g_runSpeed`, `g_turnSpeed` |

### Command Table: STM32 → ESP32

| CMD | Name | Data Bytes | Format | ESP32 action |
|---|---|---|---|---|
| `0x81` | `CMD_BATTERY` | 1 | `uint8 percent` (0–100) | Stored in `g_batteryPercent` (currently suppressed — see `AGV/carry_final/esp32_master/src/main.cpp::handleSTM32` line 114; STM32 hard-codes 100%) |
| `0x82` | `CMD_CHECKPOINT` | 2 | `uint16 checkpointId` | Publishes `{"evt":"checkpoint","id":<id>}` to `carry/robot/evt`; auto-replies with `CMD_CONFIRM_ARRIVAL` |
| `0x83` | `CMD_OBSTACLE` | 0 | — | Sets `g_stm32Obstacle`; beeps; publishes `{"evt":"obstacle"}` (via telemetry) |
| `0x84` | `CMD_ACK` | 1 | `uint8 cmd_ref` | No-op (debug only) |
| `0x85` | `CMD_MISSION_DONE` | 0 | — | Sets `g_stm32MissionDone`; publishes `{"evt":"mission_done"}` |
| `0x86` | `CMD_MISMATCH` | 4 | `uint16 received, uint16 expected` (BE) | Sets mismatch flags; publishes `{"evt":"cp_mismatch"}` |
| `0x87` | `CMD_DEBUG_MSG` | 0–N | ASCII text | Forwards to MQTT `carry/robot/debug` as `{"dbg":"..."}` |
| `0x88` | `CMD_LINE_LOST` | 0 | — | Publishes `{"evt":"line_lost"}` |
| `0x89` | `CMD_HUSKY_STATUS` | 11 | `uint8 detected, int16 xCenter, int16 yCenter, int16 width, int16 height, int16 id` | Stored in `g_husky*` globals (follow-mode gimbal tracking) |
| `0x8A` | `CMD_TAG_LOST` | 0 | — | Sets `g_stm32TagLost` (follow→find trigger) |
| `0x8B` | `CMD_TAG_FOUND` | 0 | — | Sets `g_stm32TagFound` |
| `0x8C` | `CMD_LINE_STATUS` | 1 | `uint8 bits` — bit0=L, bit1=C, bit2=R | Stored in `g_stm32LineBits` (dashboard telemetry) |
| `0x8D` | `CMD_TOF_DIST` | 2 | `int16 distMm` (BE) | Stored in `g_tofMm` (dashboard telemetry) |

ESP32 dispatch: `handleSTM32` in `AGV/carry_final/esp32_master/src/main.cpp` lines 109–207.
STM32 dispatch: `dispatchFrame` in `AGV/carry_final/stm32_slave/src/main.cpp` lines 73–155.

Legacy CMD table in `AGV/docs/agent_skill.txt` lines 98–112 documents the original minimum set (`0x01–0x06`, `0x81–0x86`); the live firmware has extended this with `0x07–0x0A` and `0x87–0x8D`.

---

## Hardware Integrations (AGV Firmware)

### WiFi

- **Chip:** ESP32 internal 2.4 GHz radio
- **Library:** `WiFi.h` (ESP32 Arduino core) + `tzapu/WiFiManager ^2.0.17`
- **Provisioning:** captive portal on SoftAP `AGV_hospital` (open, no password) at `192.168.4.1`. Infinite portal timeout (`WM_PORTAL_TIMEOUT = 0`). One custom parameter added to the portal: `mqtt_srv` (MQTT broker IP).
- **Credential storage:** WiFiManager persists SSID/PSK; MQTT broker IP persisted separately in NVS namespace `robotcfg` (key `mqtt_srv`) via `Preferences` (`AGV/carry_final/esp32_master/src/main.cpp::startPortal`).
- **Portal triggers:** long button press (>1500 ms), first boot, or MQTT connection failure (`startMqttPortal` only re-prompts for MQTT IP without dropping WiFi).
- **Boot flow (documented):** `AGV/docs/setup_wifi_MQTT.txt` — OLED shows portal status `Dòng3: "AP: Robot_Setup"` / `Dòng4: "IP: 192.168.4.1"` (note: the doc references the legacy AP name `Robot_Setup`; live firmware uses `AGV_hospital`).

### NFC Reader — PN532 (STM32)

- **Library:** `adafruit/Adafruit PN532 @ ^1.3.3` + `adafruit/Adafruit BusIO @ ^1.14.5`
- **Bus:** SPI1 on STM32
- **Pins** (`AGV/carry_final/stm32_slave/src/config.h` lines 36–40):
  - SCK = `PA5`
  - MISO = `PA6`
  - MOSI = `PA7`
  - SS = `PB1`
- **Usage:** `AGV/carry_final/stm32_slave/src/pn532_nfc.cpp` reads MIFARE card UID; `nfc_init()` called in `setup()` (`main.cpp` line 170). Checkpoint ID is the last 2 bytes of the 4-byte UID (big-endian).
- **Power:** gated by ESP32 relay R2 (`PIN_RELAY_LINE_NFC = GPIO23`) per `AGV/carry_final/esp32_master/src/config.h` line 41; firmware powers relay before STM32 boots (`main.cpp::setup` line 279) to give the PN532 module time to initialize.
- **Read cadence:** `NFC_READ_MS = 100` ms, `NFC_REPEAT_GUARD_MS = 700` ms debounce per tag (`config.h`).

### Vision — HuskyLens (ESP32)

- **Library:** `https://github.com/HuskyLens/HUSKYLENSArduino.git` (HEAD, no pinned version)
- **Bus:** UART (`Serial1` on ESP32)
- **Pins** (`AGV/carry_final/esp32_master/src/config.h` lines 12–14):
  - TX = `GPIO4` → HuskyLens RX
  - RX = `GPIO5` ← HuskyLens TX
  - Baud = 9600
- **Usage:** `AGV/carry_final/esp32_master/src/huskylens_uart.cpp` — tag recognition in FOLLOW mode (target distance via tag area %, target centering via X coordinate). Used to produce Vx/Vy/Vr which are sent to STM32 via `CMD_DIRECT_VEL (0x03)`.
- **Screen geometry constants:** `HUSKY_SCREEN_W=320`, `HUSKY_SCREEN_H=240`, `HUSKY_CX=160`, `HUSKY_TARGET_AREA_PCT=20` (`config.h` lines 74–78)
- **Power:** gated by ESP32 relay R1 (`PIN_RELAY_VISION = GPIO18`, `config.h` line 40)
- **Legacy path:** STM32 also has HuskyLens wiring on USART3 (PB10/PB11) for an earlier design; current firmware leaves it unused (`AGV/carry_final/stm32_slave/src/main.cpp` line 12 comment).

### Time-of-Flight — VL53L0X (STM32)

- **Library:** `pololu/VL53L0X @ ^1.3.1`
- **Bus:** I²C1 on STM32 (`TOF_SDA = PB7`, `TOF_SCL = PB6`; `AGV/carry_final/stm32_slave/src/config.h` lines 48–50)
- **Thresholds:** `TOF_STOP_MM = 200` (stop when obstacle ≤ 20 cm), `TOF_RESUME_MM = 300` (resume when clear ≥ 30 cm)
- **Poll cadence:** `TOF_READ_MS = 50` ms
- **Enable flag:** compile-time `#define USE_TOF 1` in `config.h`
- **Driver:** `AGV/carry_final/stm32_slave/src/tof_sensor.cpp` — `tof_init`, `tof_poll`, `tof_lastDistance`, `tof_isReady`
- **Reporting:** STM32 emits `CMD_OBSTACLE (0x83)` on stop threshold and periodic `CMD_TOF_DIST (0x8D)` for dashboard

### OLED — SH1106 128×64 (ESP32)

- **Library:** `olikraus/U8g2 @ ^2.35.7`
- **Bus:** I²C (`PIN_OLED_SDA = GPIO21`, `PIN_OLED_SCL = GPIO22`; `config.h` lines 27–28)
- **Refresh rate:** `OLED_UPDATE_MS = 200` ms
- **Driver:** `AGV/carry_final/esp32_master/src/oled_display.cpp`

### Servo (ESP32 + STM32)

**ESP32 side** (`AGV/carry_final/esp32_master/src/servo_control.cpp`):
- Library: `madhephaestus/ESP32Servo @ ^3.0.5`
- Pin: `PIN_SERVO_Y = GPIO14` (PWM)
- Angles (shared with STM32): `SERVO_Y_LEVEL=100`, `SERVO_Y_TILT_DOWN=45`, `SERVO_Y_LOOK_UP=115`, range `0..180`

**STM32 side** (`AGV/carry_final/stm32_slave/src/servo_y.cpp`):
- Pin: `PIN_SERVO_Y = PB4` using `TIM3_CH1` hardware PWM. Requires disabling JTAG in `motor_init()` — call order is enforced (motor init before servo init) in `AGV/carry_final/stm32_slave/src/main.cpp` lines 163–165.

### Motor Drivers — 2× L298N (STM32)

Mecanum 4-wheel drive, PWM frequency 20 kHz, 8-bit resolution (`PWM_FREQ`, `PWM_RES` in `config.h`).

**L298N #1 — Front motors** (`config.h` lines 16–22):
| Signal | STM32 Pin | Function |
|---|---|---|
| `L1_IN1` | `PA0` | FL direction |
| `L1_IN2` | `PA1` | FL direction |
| `L1_ENA` | `PA8` | FL PWM (`TIM1_CH1`) |
| `L1_IN3` | `PA9`  | FR direction |
| `L1_IN4` | `PA10` | FR direction |
| `L1_ENB` | `PB0`  | FR PWM (`TIM3_CH3`) |

**L298N #2 — Back motors** (`config.h` lines 24–34):
| Signal | STM32 Pin | Function |
|---|---|---|
| `L2_IN1` | `PB12` | BL direction |
| `L2_IN2` | `PB13` | BL direction |
| `L2_ENA` | `PB3`  | BL PWM (`TIM2_CH2`, SWD-safe) |
| `L2_IN3` | `PB15` | BR direction |
| `L2_IN4` | `PA12` | BR direction (standard GPIO — rebinds USB pin, hence USB CDC disabled) |
| `L2_ENB` | `PB5`  | BR PWM (`TIM3_CH2`) |

Driver: `AGV/carry_final/stm32_slave/src/motor_control.cpp`. Soft-stop parameters: `MOTOR_SOFTSTOP_STEP=40`, `MOTOR_SOFTSTOP_MS=15`. Turn-time calibration: `MOTOR_TURN_90_MS=950`, `MOTOR_TURN_180_MS=1900`. Default speed: `MOTOR_SPEED=165`.

### Line Sensor — 3-eye digital (STM32)

3 digital inputs, active-LOW:
- `LINE_S1 = PB8` (left)
- `LINE_S2 = PB9` (center)
- `LINE_S3 = PA4` (right)

Driver: `AGV/carry_final/stm32_slave/src/line_sensor.cpp` — `line_init`, `line_readBits`. PID constants `LF_KP=100`, `LF_KI=0`, `LF_KD=30`, `LF_MAX_CORR=120` (`config.h` lines 67–71).

Power: gated by ESP32 relay R2 (`PIN_RELAY_LINE_NFC = GPIO23`).

### Relays (ESP32)

2-channel active-HIGH relay module — `AGV/carry_final/esp32_master/src/relay_control.cpp`. Pin names follow `AGV/carry_final/esp32_master/src/config.h`:
- `PIN_RELAY_VISION = GPIO18` → HuskyLens + servo
- `PIN_RELAY_LINE_NFC = GPIO23` → line sensors + PN532

Note: the skill doc in `AGV/docs/agent_skill.txt` lines 138–147 originally specified 3 relays (R1 vision GPIO18, R2 line GPIO19, R3 NFC GPIO23). Current firmware collapses R2+R3 onto a single GPIO23 channel controlling both line sensors and PN532; the MQTT `relay` action still accepts `which ∈ {vision, line, nfc}` for legacy compatibility, and `relayGetLine()`/`relayGetNfc()` are reported separately in telemetry (`AGV/carry_final/esp32_master/src/mqtt_client.cpp::mqttPublishTelemetry` lines 523–525).

### Battery Monitor (ESP32)

- ADC: `PIN_BATTERY = GPIO35` (ADC1_CH7, input-only)
- Calibration: `BATT_ADC_MIN_V=3.036 V → BATT_PCT_AT_MIN=10%`; `BATT_ADC_MAX_V=3.6 V → 100%` (linear interpolation between these points; see `AGV/carry_final/esp32_master/src/battery.cpp`)
- Mission gate: `BATT_MIN_PERCENT=30` — MQTT route / mission commands are rejected when `g_batteryPercent ≤ 30` with `{"evt":"mission_rejected","reason":"low_battery","pct":<n>}` (mqtt_client.cpp lines 82–90 and 345–353)
- Follow-mode warning window: `BATT_FOLLOW_WARN_S=10 s`
- **Currently suppressed:** battery reading from STM32 is ignored on ESP32 side (`handleSTM32` line 114 comment "tạm tắt – luôn giữ 100%"); STM32 also sends dummy 100% (`stm32_slave/src/main.cpp` line 219).

### Buzzer (ESP32)

- Pin: `PIN_BUZZER = GPIO25`
- Driver: `AGV/carry_final/esp32_master/src/buzzer.cpp` — `buzzerBeep`, `buzzerBeepN`

### Button (ESP32)

- Pin: `PIN_BUTTON = GPIO15` (internal pull-up)
- Debounce: `BTN_DEBOUNCE_MS=50`; double-click window `BTN_DOUBLE_MS=400`; long-press `BTN_LONG_MS=1500` (portal / mode switch)
- Driver: `AGV/carry_final/esp32_master/src/button_handler.cpp`

---

## Environment Configuration Summary

### Required env vars

**Backend** (`Hospital Dashboard/Backend/.env` — committed file exists):
- `MONGO_URI` — MongoDB connection string
- `PORT` — HTTP port (default 3000)
- `NODE_ENV` — `development | staging | production`
- `MQTT_BROKER` — e.g. `mqtt://localhost:1883`
- `MQTT_USER`, `MQTT_PASS` — broker credentials
- `MQTT_STACK_ROBOT_ID` — robot ID bound to carry stack topics (default `AGV-01`)
- `MQTT_STACK_CMD_TOPIC`, `MQTT_STACK_EVT_TOPIC`, `MQTT_STACK_RETURN_TOPIC` — optional topic overrides
- `MQTT_STRICT_SCHEMA` — set `=1` to reject legacy `nodeId`/`prevNodeId` fields

**Frontend** (`Hospital Dashboard/Frontend/.env.example`):
- `VITE_API_URL` — only for production build when backend is on a different host; unused in dev (proxy handles it)

**ESP32 firmware:** no env file — defaults in `AGV/carry_final/esp32_master/src/config.h`; user-editable via captive portal and persisted in NVS namespace `robotcfg`.

### Secrets location

- `Hospital Dashboard/Backend/.env` — **committed to repo** (`.env` file present in `Hospital Dashboard/Backend/`). Contains MQTT password. See `CONCERNS.md` (concerns focus) for remediation.
- WiFi credentials — persisted on the ESP32 via WiFiManager (flash, not in repo)
- MQTT broker creds — persisted in ESP32 NVS (namespace `robotcfg`)
- OTA password — hardcoded `agv_ota_123` in `AGV/carry_final/esp32_master/src/config.h` line 86

---

## Webhooks & Callbacks

**Incoming:** None — no webhook endpoints defined.

**Outgoing:** None at the HTTP layer. Asynchronous callbacks are delivered via:
- MQTT publish from backend → robot (commands)
- MQTT publish from robot → backend (events/telemetry)
- SSE push from backend → browser at `/api/robots/live`

---

*Integration audit: 2026-04-20*
