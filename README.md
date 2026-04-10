# Hospital Robot System

## Table of Contents
1. [System Overview](#1-system-overview)
2. [Architecture](#2-architecture)
3. [MoSCoW Requirements](#3-moscow-requirements)
4. [Functional Requirements](#4-functional-requirements)
5. [Non-Functional Requirements](#5-non-functional-requirements)
6. [Carry Robot – ESP32 Master](#6-carry-robot--esp32-master)
7. [Carry Robot – STM32 Slave](#7-carry-robot--stm32-slave)
8. [Hospital Dashboard – Backend](#8-hospital-dashboard--backend)
9. [Hospital Dashboard – Frontend](#9-hospital-dashboard--frontend)
10. [Communication Protocols](#10-communication-protocols)
11. [Development & Build](#11-development--build)

---

## 1. System Overview

An autonomous hospital medication delivery system consisting of two tightly integrated components:

| Component | Location | Role |
|-----------|----------|------|
| **Carry Robot** | `CarryRobot/carry_final/` | Dual-MCU mobile robot (ESP32 Master + STM32 Slave) that physically transports medication from the medicine station to patient beds and returns autonomously |
| **Hospital Dashboard** | `Hospital Dashboard/` | Full-stack web application (Express.js + React + MongoDB) for patient management, robot monitoring, mission dispatch, and live tracking |

---

## 2. Architecture

```
+---------------------------------------------------------------------+
|                       Hospital Dashboard                             |
|  +---------------------+    +----------------------------------+   |
|  |  Frontend (React)   |<-->|  Backend (Node.js / Express)     |   |
|  |  Port 5173          |    |  Port 3000                       |   |
|  |  - PatientDashboard |    |  - REST API                      |   |
|  |  - RobotCenter      |    |  - MQTT Service (Mosquitto 1883) |   |
|  |  - RobotLiveTracking|    |  - MongoDB / Mongoose            |   |
|  |  - RobotTestLab     |    |  - SSE live-stream               |   |
|  |  - MissionHistory   |    |  - Models: Robot, Mission,       |   |
|  |  - BedMap (24 beds) |    |    Patient, Alert, Map, User     |   |
|  +---------------------+    +----------------+-----------------+   |
+------------------------------------------------|--------------------+
                                                 | MQTT over WiFi 2.4 GHz
                             +-------------------v---------------------+
                             |        ESP32 Master (CARRY-01)          |
                             |  - State Machine (AUTO/FOLLOW/RECOVERY) |
                             |  - WiFiManager + MQTT                   |
                             |  - HuskyLens (tag + line tracking)      |
                             |  - Servo Gimbal X/Y                     |
                             |  - SR05 Ultrasonic x2 (wall avoidance)  |
                             |  - OLED SH1106 display                  |
                             |  - Buzzer + Push button                 |
                             |  - Relay R1 (vision) / R2 (line+NFC)   |
                             +-------------------+---------------------+
                                                 | UART (115200 baud)
                                                 | TX=GPIO17 / RX=GPIO16
                             +-------------------v---------------------+
                             |        STM32F103 Slave (Blue Pill)      |
                             |  - Mecanum drive (2x L298N, 4 motors)   |
                             |  - PN532 NFC/RFID reader (SPI)          |
                             |  - 3-sensor line follower (PID)         |
                             |  - VL53L0X ToF (obstacle detection)     |
                             |  - Route runner (autonomous navigation) |
                             +-----------------------------------------+
```

### Data Flow – Medication Delivery
```
Staff selects patient on Dashboard
    |
    v
POST /api/missions/delivery  -->  Backend computes route (Dijkstra)
    |                             publishes MQTT mission/assign
    v
ESP32 Master receives route via MQTT
    |  stores route, waits for single-click confirmation
    v
[Button click] --> sends CMD_SET_MODE + CMD_SEND_ROUTE to STM32 via UART
    |
    v
STM32 Slave executes route (line follow + NFC checkpoint matching)
    |  reports CMD_CHECKPOINT, CMD_BATTERY back over UART
    v
ESP32 publishes mission/progress, mission/complete --> Dashboard updated
    |
    v
Robot waits at bed for pickup confirmation --> returns to MED station
    |
    v
ESP32 publishes mission/returned --> Dashboard marks delivered
```

---

## 3. MoSCoW Requirements

### Must Have (M)
> Core features without which the system cannot function

| ID | Requirement |
|----|-------------|
| M-01 | Robot must autonomously navigate from the medicine station (MED) to a specified patient bed using NFC checkpoints and line following |
| M-02 | Robot must return to MED station automatically after medication is collected |
| M-03 | Dashboard must allow staff to assign a delivery mission to the robot by selecting a patient and bed |
| M-04 | Patient records must be stored and retrievable (name, MRN, bed assignment, status, prescriptions) |
| M-05 | Real-time robot status (idle/busy/offline) must be visible on the Dashboard |
| M-06 | ESP32 Master and STM32 Slave must communicate reliably via UART (framed protocol with STX + CRC) |
| M-07 | MQTT communication between the Dashboard Backend and the robot must be maintained over WiFi |
| M-08 | Obstacle detection must pause robot movement when an object is <= 200 mm in front (VL53L0X ToF) |
| M-09 | Missions must be cancellable from the Dashboard at any time |
| M-10 | System must support at least one carry robot with the full delivery/return lifecycle |

### Should Have (S)
> Important features that significantly increase system value

| ID | Requirement |
|----|-------------|
| S-01 | Follow Mode: robot tracks a HuskyLens ArUco/AprilTag to escort a person |
| S-02 | Recovery Mode: if line is lost, servo sweep detects line via HuskyLens, robot re-aligns and requests a return route from the backend |
| S-03 | Live robot position must update on a floor-plan SVG map in real time via SSE |
| S-04 | Mission history must be stored and viewable (paginated delivery log) |
| S-05 | Alert system must notify staff of robot anomalies (obstacle blocked, lost line, battery low) |
| S-06 | OLED display must show current state, patient name, destination, and obstacle warnings |
| S-07 | WiFiManager portal allows reconfiguring WiFi SSID and MQTT server IP without reflashing |
| S-08 | Robot Test Lab panel must allow manual command injection from the Dashboard for debugging |

### Could Have (C)
> Desirable features that can be deferred

| ID | Requirement |
|----|-------------|
| C-01 | Patient photo upload and display in PatientForm |
| C-02 | Dual relay control to power-cycle peripheral modules (vision relay R1, line+NFC relay R2) |
| C-03 | RFID/NFC card used to look up patient records directly from the Dashboard (Web Serial API) |
| C-04 | Bed map visualisation showing occupied / available / robot-active beds on the Dashboard |
| C-05 | Prescriptions, clinical notes, and timeline entries can be added and edited per patient |
| C-06 | Map graph stored in MongoDB; Dijkstra-based shortest-path API alongside hard-coded routes |
| C-07 | Idle NFC scans are published to the Dashboard even when not on a mission |

### Won't Have (W)
> Explicitly out of scope for the current version

| ID | Requirement |
|----|-------------|
| W-01 | Multi-robot simultaneous delivery (only one carry robot supported at a time) |
| W-02 | Authentication or role-based access control on any API route or Dashboard page |
| W-03 | Real battery voltage measurement on ESP32 (battery level is currently hardcoded to 100%) |
| W-04 | 3D or dynamic map generation; map is static and pre-seeded |
| W-05 | Voice or audio commands to the robot |
| W-06 | Over-the-air (OTA) firmware updates to either ESP32 or STM32 |

---

## 4. Functional Requirements

### FR-01 – Autonomous Medication Delivery
- System shall compute a route from MED to the target bed node when a delivery mission is created via `POST /api/missions/delivery`.
- Route shall include ordered NFC checkpoint IDs and per-node actions (straight, turn-left, turn-right, stop).
- STM32 Slave shall follow the line and match NFC UIDs to advance along the route.
- Robot shall stop and wait at the destination bed for a configurable timeout or button press before returning.

### FR-02 – Autonomous Return
- After delivery, STM32 shall navigate the return route (reversed outbound path) back to MED.
- On cancellation mid-mission, ESP32 shall publish `position/waiting_return`; Backend shall compute and send a return route via MQTT `mission/return_route`.

### FR-03 – Follow Mode
- Single-click from IDLE activates Follow Mode if a tag is in HuskyLens view.
- ESP32 shall read HuskyLens tag position (X-centre, area) and compute Vx / Vy / Vr velocity commands.
- Velocity commands are sent to STM32 over UART every ~50 ms.
- Side wall distances (SR05 L/R) shall contribute a lateral correction component (Vx).
- If tag is lost for > 10 s, mode transitions to Find Mode.

### FR-04 – Recovery Mode
- When line is lost on the STM32 (lineBits = 0) or double-click in Follow, ESP32 enters Recovery.
- Servo X sweeps 0 to 180 degrees until HuskyLens detects a line; STM32 aligns to it.
- Robot reads the next NFC checkpoint, sends it to Backend to compute a return route, and navigates home.
- If no line is found after 3 sweeps, the buzzer alerts staff.

### FR-05 – Obstacle Avoidance
- STM32 VL53L0X continuously polls; if distance <= 200 mm the robot stops immediately.
- Movement resumes only when distance > 300 mm (hysteresis).

### FR-06 – Patient Management
- Create, read, update, and delete patient records including: full name, MRN, DOB, gender, status, doctor, room/bed, relative contact, insurance, photo.
- Associate or remove prescriptions, timeline entries, and clinical notes per patient.
- Lookup patient by RFID card number via `GET /api/patients/by-card/:cardNumber`.

### FR-07 – Mission Management
- Dashboard shall display all transport missions with status filter (pending / en_route / arrived / completed / failed / cancelled).
- Staff may cancel any active mission; Backend publishes MQTT `mission/cancel` to the robot.
- Delivery history is paginated and displayable.

### FR-08 – Robot Telemetry
- ESP32 publishes telemetry every 5 s: robotId, status, batteryLevel, currentNodeId, destBed, firmware version.
- Backend upserts the Robot document; marks online if `lastSeenAt` within 30 s.
- Low battery (<= 30%) creates an Alert and sets robot status to `low_battery`.

### FR-09 – Alert System
- Alerts are created for: obstacle blocked, line lost (rescue required), low battery.
- Unresolved alerts are displayed on the Dashboard with severity level.
- Staff can resolve alerts individually via `PUT /api/alerts/:id/resolve`.

### FR-10 – Live Position Tracking
- Backend emits Server-Sent Events (SSE) on `GET /api/robots/live` on every telemetry/progress update.
- Frontend RobotLiveTracking subscribes to SSE and animates the robot icon on the SVG floor plan.
- SVG contains all 37 nodes (MED, corridor, junctions, 4 rooms x 8 nodes each).

### FR-11 – Debug / Test Lab
- RobotTestLab panel allows direct command injection: set mode, adjust spin/brake timing, toggle relays, read sensor values.
- Commands are sent to the robot via `POST /api/robots/:id/command` -> MQTT `command` topic.

### FR-12 – Map Management
- Floor map with nodes and weighted edges is stored in MongoDB (MapGraph model).
- `GET /api/maps/:mapId/route?from=&to=` returns Dijkstra shortest path.
- Initial `floor1` map with 37 nodes is seeded via `node seed/seedMap.js`.

---

## 5. Non-Functional Requirements

### NFR-01 – Performance

| Metric | Target |
|--------|--------|
| UART round-trip latency (ESP32 <-> STM32) | < 10 ms at 115200 baud |
| MQTT telemetry publish interval | 5 s |
| MQTT message delivery (QoS 1) | < 500 ms on local LAN |
| Backend REST API response (CRUD) | < 300 ms (MongoDB local) |
| SSE live-position update lag | < 1 s |
| Frontend initial page load | < 3 s on LAN |

### NFR-02 – Reliability
- UART framing uses STX header (0x7E) + CRC16; corrupt frames are silently discarded.
- MQTT client auto-reconnects every 3 s on disconnection; mission state is preserved in NVS.
- WiFiManager portal is triggered on connection failure, ensuring the robot is never permanently offline.
- STM32 UART RX uses interrupt-based reception with a ring buffer for zero-loss frame capture.
- Robot online detection uses a 30 s sliding window to tolerate transient WiFi dropout.

### NFR-03 – Usability
- Dashboard is a single-page application with two top-level tabs (Patients Manager, Robot Center) requiring no navigation knowledge.
- All robot status changes (idle -> busy -> completed) are reflected on the Dashboard without manual page refresh.
- OLED display on the robot shows the current operating mode, patient name, and destination at all times.
- Buzzer provides audible feedback for key events (obstacle, mode change, recovery failure).

### NFR-04 – Maintainability
- Firmware is modular: each mode (auto, follow, find, recovery) is a separate .cpp/.h pair.
- All pin assignments and tunable constants are centralised in `config.h` for each MCU.
- Backend uses ES Modules throughout; routes, models, services, and utils are in separate files.
- Frontend uses TypeScript with typed API layer (`src/app/api/`) separated from UI components.

### NFR-05 – Portability
- Robot is configurable without reflashing: WiFi SSID and MQTT server IP stored in NVS (ESP32) and set via WiFiManager captive portal.
- MQTT broker address defaults to `192.168.137.1` (Windows hotspot) but is overridable via NVS or environment variable.
- Backend database URI and MQTT broker are fully configurable via `.env`.

### NFR-06 – Safety
- Obstacle detection (ToF <= 200 mm) is implemented in firmware and cannot be overridden by software commands from the Dashboard.
- Motors are stopped immediately on `CMD_CANCEL_MISSION` regardless of current state.
- Relay R2 (line sensors + NFC) is power-cycled on mode change to force re-initialisation of the NFC reader, preventing stale reads.

### NFR-07 – Scalability
- MQTT topic structure uses a wildcard prefix `hospital/robots/+/` enabling multiple robots with different IDs to share the same broker.
- Backend Robot model with indexed `type`, `status`, and `lastSeenAt` fields supports future addition of other robot types without schema migration.

### NFR-08 – Security (baseline)
- MQTT broker requires username/password authentication (`hospital_robot` / `hospital_backend`).
- Patient photo upload validates MIME type and limits size to 5 MB.
- No authentication or authorisation on REST routes or Dashboard (explicitly out of scope per W-02).

---

## 6. Carry Robot – ESP32 Master

**Location**: `CarryRobot/carry_final/esp32_master/`  
**Platform**: ESP32 DevKit, Arduino framework (PlatformIO)

### Operating Modes

| Mode | Trigger | Behaviour |
|------|---------|-----------|
| `MODE_IDLE` | Boot / mission complete | Waits for MQTT route or button press |
| `MODE_AUTO` | Single-click (route ready) | Sends route to STM32; supervises NFC progress |
| `MODE_FOLLOW` | Double-click | Tracks HuskyLens tag; controls STM32 velocity |
| `MODE_FIND` | Tag lost > 10 s in FOLLOW | Servo sweep to re-acquire tag |
| `MODE_RECOVERY` | Double-click in FOLLOW | Servo sweep for line; navigates back to MED |

### Pin Mapping

| GPIO | Function |
|------|----------|
| 17 / 16 | UART2 TX/RX -> STM32 (115200 baud) |
| 4 / 5 | UART1 TX/RX -> HuskyLens (9600 baud) |
| 13 / 14 | Servo X / Servo Y (PWM) |
| 34 | Servo X ADC feedback |
| 26 / 27 | SR05 Left TRIG / ECHO |
| 32 / 33 | SR05 Right TRIG / ECHO |
| 21 / 22 | I2C SDA/SCL (OLED SH1106) |
| 25 | Buzzer |
| 15 | Push button (pull-up) |
| 18 / 23 | Relay R1 (vision) / Relay R2 (line+NFC) |

### MQTT Topics (prefix `hospital/robots/CARRY-01/`)

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `telemetry` | Publish (5 s) | Status, battery, current node, dest bed |
| `mission/assign` | Subscribe | Receive route payload from Backend |
| `mission/progress` | Publish | Per-checkpoint update |
| `mission/complete` | Publish | Arrived at destination |
| `mission/returned` | Publish | Back at MED station |
| `mission/cancel` | Subscribe | Abort active mission |
| `mission/return_route` | Subscribe | Backend-computed return route after cancel |
| `position/waiting_return` | Publish | Current position when awaiting return route |
| `command` | Subscribe | Test lab / manual command injection |

### Key Libraries

| Library | Purpose |
|---------|---------|
| WiFiManager ^2.0 | WiFi + MQTT IP configuration portal |
| PubSubClient ^2.8 | MQTT client |
| ArduinoJson ^6 | JSON serialisation for MQTT payloads |
| U8g2 ^2.35 | OLED SH1106 128x64 |
| ESP32Servo ^3.0 | Servo gimbal X/Y |
| HUSKYLENSArduino (git) | HuskyLens tag + line detection API |

### Source File Map

| File | Responsibility |
|------|---------------|
| `main.cpp` | Setup, WiFiManager, boot sequence, button ISR |
| `config.h` | All pin definitions and tunable constants |
| `globals.h/cpp` | Shared state variables across modes |
| `auto_mode.cpp` | AUTO state machine; sends route/cancel to STM32 |
| `follow_mode.cpp` | PID follow controller using HuskyLens tag |
| `find_mode.cpp` | Servo sweep to re-acquire lost tag |
| `recovery_mode.cpp` | Line re-acquisition and return-route navigation |
| `mqtt_client.cpp` | Connect, subscribe, publish, MQTT callbacks |
| `uart_protocol.cpp` | Frame builder/parser (STX + CRC16) |
| `huskylens_uart.cpp` | HuskyLens UART wrapper (tag + line modes) |
| `servo_control.cpp` | X/Y servo with ADC feedback |
| `sr05.cpp` | SR05 ultrasonic distance read (L/R) |
| `oled_display.cpp` | All OLED screen states |
| `relay_control.cpp` | Relay R1 / R2 power sequences per mode |
| `buzzer.cpp` | Tone patterns |
| `button_handler.cpp` | Debounce, single/double/long-press detection |
| `battery.cpp` | Battery ADC read (placeholder: 100%) |

---

## 7. Carry Robot – STM32 Slave

**Location**: `CarryRobot/carry_final/stm32_slave/`  
**Platform**: STM32F103C8 (Blue Pill), Arduino framework (PlatformIO)

### Pin Mapping

| Pin | Function |
|-----|---------|
| PA2 / PA3 | USART2 TX/RX -> ESP32 (115200 baud) |
| PA8, PA9, PA10 | L298N #1 motor control |
| PB12, PB13, PB14, PB15 | L298N #2 motor control |
| PB0 / PC13 / PC14 | PWM channels for motor enable |
| PA5 / PA6 / PA7 / PB1 | SPI1 SCK/MISO/MOSI/SS -> PN532 NFC |
| PB8 / PB9 / PA4 | Line sensors S1 (left) / S2 (center) / S3 (right) |
| PB7 / PB6 | I2C SDA/SCL -> VL53L0X ToF |

### Motor Parameters

| Parameter | Value |
|-----------|-------|
| PWM frequency | 20 kHz |
| PWM resolution | 8-bit |
| Run speed | 200 / 255 |
| Turn speed | 180 / 255 |
| 90-degree turn time | 950 ms |
| 180-degree turn time | 1900 ms |
| Brake PWM | 150 for 80 ms |

### Line Follower PID

| Parameter | Value |
|-----------|-------|
| KP | 0.35 |
| KI | 0.00 |
| KD | 0.20 |
| Max correction | +/- 180 |

### UART Command Set

| CMD | Hex | Direction | Payload |
|-----|-----|-----------|---------|
| `CMD_SET_MODE` | 0x01 | ESP32 -> STM32 | 1 byte: mode enum |
| `CMD_SEND_ROUTE` | 0x02 | ESP32 -> STM32 | [count][id_hi id_lo action] x N |
| `CMD_DIRECT_VEL` | 0x03 | ESP32 -> STM32 | 6 bytes: Vx Vy Vr (int16 each) |
| `CMD_REQUEST_STATUS` | 0x04 | ESP32 -> STM32 | empty |
| `CMD_CANCEL_MISSION` | 0x05 | ESP32 -> STM32 | empty |
| `CMD_ACK` | 0x06 | STM32 -> ESP32 | 1 byte: echoed cmd |
| `CMD_CHECKPOINT` | 0x07 | STM32 -> ESP32 | 2 bytes: checkpoint ID |
| `CMD_BATTERY` | 0x08 | STM32 -> ESP32 | 1 byte: percent |

### Source File Map

| File | Responsibility |
|------|---------------|
| `main.cpp` | Setup, UART frame dispatcher, main loop |
| `config.h` | All pin and constant definitions |
| `globals.h/cpp` | Shared state (mode, route, sensor data) |
| `uart_protocol.cpp` | Frame encode/decode (STX + CRC16) |
| `mecanum.cpp` | Mecanum drive vector computation |
| `motor_control.cpp` | L298N PWM, direction, turn, brake primitives |
| `line_sensor.cpp` | 3-sensor read, PID error calculation |
| `pn532_reader.cpp` | PN532 SPI, UID read with repeat guard (700 ms) |
| `tof_sensor.cpp` | VL53L0X distance read and obstacle logic |
| `auto_runner.cpp` | Route execution state machine (checkpoint matching) |

---

## 8. Hospital Dashboard – Backend

**Location**: `Hospital Dashboard/Backend/`  
**Stack**: Node.js (ES Modules), Express 4, Mongoose 8, MQTT 5, Multer 2  
**Port**: 3000

### MongoDB Models

| Model | Key Fields |
|-------|-----------|
| `Robot` | robotId, name, type, status, batteryLevel, lastSeenAt, currentLocation, transportData, totalDeliveries |
| `TransportMission` | missionId, carryRobotId, patientName, bedId, destinationNodeId, outboundRoute, returnRoute, status, returnedAt |
| `Patient` | fullName, mrn, dob, gender, admissionDate, status, roomBed, primaryDoctor, relativeName, photoPath, timeline[], prescriptions[], notes[] |
| `Alert` | type, level, robotId, missionId, message, resolvedAt |
| `MapGraph` | mapId, nodes[], edges[] (Dijkstra-ready with weights) |
| `User` | uid, name, email (RFID user registry) |
| `Event` | type, uid, robotId, timestamp (button/NFC events) |

### REST API Summary

| Method | Route | Purpose |
|--------|-------|---------|
| GET/POST | `/api/patients` | List (search/filter) + create with photo upload |
| PUT/DELETE | `/api/patients/:id` | Update / delete patient record |
| GET | `/api/patients/:id/details` | Full patient with sub-documents |
| GET | `/api/patients/by-card/:cardNumber` | RFID card lookup |
| POST/DELETE | `/api/patients/:id/prescriptions/:pid` | Manage prescriptions |
| POST/DELETE | `/api/patients/:id/timeline/:tid` | Manage timeline entries |
| POST/DELETE | `/api/patients/:id/notes/:nid` | Manage clinical notes |
| PUT | `/api/robots/:id/telemetry` | Robot heartbeat upsert |
| GET | `/api/robots/carry/status` | Online carry robots with active mission |
| GET | `/api/robots/live` | SSE live position stream |
| POST | `/api/missions/delivery` | Create delivery mission + publish MQTT route |
| GET | `/api/missions/transport` | List missions (filter by status/robotId) |
| POST | `/api/missions/carry/:id/cancel` | Cancel mission + publish MQTT cancel |
| POST | `/api/missions/carry/:id/returned` | Mark returned; increment totalDeliveries |
| GET | `/api/maps/:mapId/route` | Dijkstra shortest path query |
| GET/POST | `/api/alerts` | List active / create new alert |
| PUT | `/api/alerts/:id/resolve` | Resolve alert by ID |

### MQTT Service

Connects to Mosquitto broker (`mqtt://localhost:1883`) as `hospital_backend`.

**Subscribes to** (wildcard `hospital/robots/+/`):
- `telemetry` – upsert Robot document; create alert on low battery
- `mission/progress` – update TransportMission progress fields
- `mission/complete` – set mission status to `completed` or `failed`
- `mission/returned` – set `returnedAt`; increment robot `totalDeliveries`
- `position/waiting_return` – compute return route via Dijkstra; publish back to robot

**Publishes**:
- `mission/assign` (QoS 1) – full route payload to robot on new delivery
- `mission/cancel` (QoS 1) – cancel active mission
- `mission/return_route` (QoS 1) – computed return route after cancellation
- `command` (QoS 1) – test lab manual commands (mode change, relay, etc.)

---

## 9. Hospital Dashboard – Frontend

**Location**: `Hospital Dashboard/Frontend/`  
**Stack**: React 18 + Vite 6 + TypeScript + Tailwind CSS v4 + shadcn/ui  
**Port**: 5173 (Vite proxies `/api` and `/uploads` to backend port 3000)

### Component Map

| Component | Purpose |
|-----------|---------|
| `PatientDashboard` | Patient list with search, filter, sort, and CSV export |
| `PatientForm` | Create / edit patient with RFID scan and camera photo capture |
| `PatientDetails` | Patient detail dialog: prescriptions, timeline, clinical notes |
| `BedMap` | 4-room x 6-bed visual map with occupied / robot-active states |
| `RobotCenter` | Tab container for robot management panels |
| `RobotManagement` | Carry robot status table; send / cancel delivery mission |
| `RobotLiveTracking` | SVG floor plan (900x630) with real-time robot position via SSE |
| `RobotTestLab` | Manual command panel: mode, velocity, relay toggle, sensor readback |
| `MissionHistoryTab` | Paginated delivery history table |
| `RobotHistory` | Per-robot historical statistics |
| `ConnectionStatus` | Header badge polling backend health every 30 s |

### Custom Hooks

| Hook | Polling | Purpose |
|------|---------|---------|
| `usePatients()` | – | Patient CRUD with optimistic updates |
| `useRobots(5000)` | 5 s | Robot status + active mission data |
| `useAlerts(10000)` | 10 s | Active alert list |
| `useMissions()` | – | Mission create, cancel, history |
| `useSerialRFID()` | – | Web Serial API RFID card reader (USB) |

---

## 10. Communication Protocols

### UART Frame Format (ESP32 <-> STM32)
```
[ STX=0x7E | LEN | CMD | DATA (0-N bytes) | CRC16_HI | CRC16_LO ]
```
- CRC16-CCITT (polynomial 0x1021, init 0xFFFF) over LEN + CMD + DATA
- Max frame size: 128 bytes
- Baud rate: 115200, 8N1

### MQTT Payload – `mission/assign` (example)
```json
{
  "missionId": "TM-XXXXXXXXXXXX",
  "patientName": "Nguyen Van A",
  "bedId": "R2M1",
  "route": [
    { "checkpointId": 1, "action": "S" },
    { "checkpointId": 5, "action": "L" }
  ]
}
```

### MQTT Payload – `telemetry` (example)
```json
{
  "robotId": "CARRY-01",
  "status": "busy",
  "batteryLevel": 100,
  "currentNodeId": "H-BOT",
  "destBed": "R2M1",
  "fw": "carry-final-v1",
  "ts": 1712400000000
}
```

---

## 11. Development & Build

### Prerequisites
- Node.js 18+, npm
- PlatformIO Core (pio)
- Mosquitto MQTT broker
- MongoDB (local or Atlas)
- ST-Link V2 (for STM32 flashing)

### Start Development
```bash
# Backend
cd "Hospital Dashboard/Backend"
npm install
npm run dev          # Express on port 3000

# Frontend (separate terminal)
cd "Hospital Dashboard/Frontend"
npm install
npm run dev          # Vite on port 5173
```

### Build and Flash Firmware (PlatformIO)
```bash
# Build
pio run -d CarryRobot/carry_final/esp32_master
pio run -d CarryRobot/carry_final/stm32_slave

# Flash ESP32 (UART, adjust COM port)
pio run -d CarryRobot/carry_final/esp32_master -t upload --upload-port COM15

# Flash STM32 (ST-Link)
pio run -d CarryRobot/carry_final/stm32_slave -t upload

# Serial monitor
pio device monitor -p COM15 -b 115200   # ESP32 Master
```

### Seed Database
```bash
cd "Hospital Dashboard/Backend"
node seed/seedMap.js     # Creates floor1 map with 37 nodes + edges
```

### MQTT Broker (Mosquitto) Config
```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
# Create users: hospital_robot and hospital_backend with password 123456
```

### Environment Variables (Backend `.env`)
```
MONGO_URI=mongodb://localhost:27017/hospital
MQTT_BROKER=mqtt://localhost:1883
MQTT_USER=hospital_backend
MQTT_PASS=123456
PORT=3000
```

### NFC Node Naming Convention

| Pattern | Description | Examples |
|---------|-------------|---------|
| `MED` | Medicine station (home base) | `MED` |
| `H_MED`, `H_BOT`, `H_TOP` | Hallway corridor checkpoints | `H_BOT` |
| `J1`-`J4` | Junctions (hallway intersections) | `J4` |
| `R{1-4}M{1-3}` | Room beds, M-side (3 per room) | `R1M2` |
| `R{1-4}O{1-3}` | Room beds, O-side (3 per room) | `R2O1` |
| `R{1-4}D{1-2}` | Room doors (2 per room) | `R3D1` |

Total: 37 nodes across 4 rooms (8 nodes each: D1, D2, M1-M3, O1-O3), 3 corridor nodes, 4 junctions, 1 MED.