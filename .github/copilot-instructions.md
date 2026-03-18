# Hospital Robot System

## System Overview

Hospital medication delivery system with two integrated components:
1. **Carry Robot** (`CarryRobot/`) — Dual-ESP32 transport robot (Master + Slave via ESP-NOW) with Mecanum wheels, HuskyLens vision, NFC checkpoint navigation, and MQTT communication
2. **Hospital Dashboard** (`Hospital Dashboard/`) — Full-stack web app (Express.js + React/Vite + MongoDB) for patient management, robot monitoring, and mission control

## Architecture and Data Flow

```
[Dashboard Frontend:5173] <──Vite Proxy──> [Backend API:3000] <──Mongoose──> [MongoDB]
                                                  │
                                            MQTT (PubSubClient)
                                                  │
                                         [Carry Master ESP32]
                                           WiFi + MQTT + HuskyLens
                                           OLED + Servo Gimbal
                                                  │
                                              ESP-NOW
                                                  │
                                         [Carry Slave ESP32]
                                           Mecanum + NFC + Line Follow
```

### Main Workflows
- **Medication Delivery**: Dashboard creates delivery mission via `/api/missions/delivery` → Backend computes hard-coded routes → MQTT publishes `mission/assign` to Carry Robot → Master streams route to Slave via ESP-NOW → Slave navigates via NFC checkpoints + line following → Delivers to bed → Returns to MED station
- **Follow Mode**: HuskyLens on Master detects face → PID follow controller → Master commands Slave movement via ESP-NOW
- **Recovery**: If target lost during follow → visual search (rotate) → blind approach → call for help (buzzer)

### Communication Protocols
- **Master ↔ Slave**: ESP-NOW wireless (packed structs at 50ms intervals, channel 7)
- **Master ↔ Dashboard**: MQTT over WiFi (PubSubClient, Mosquitto broker port 1883)
- **Master ↔ HuskyLens**: UART (GPIO 16/17, 9600 baud)
- **Slave NFC**: SPI (Adafruit PN532)

---

## Carry Robot Master (`CarryRobot/carry_master/`)

### Platform and Libraries

| Setting | Value |
|---------|-------|
| Platform | `espressif32` (ESP32 DevKit) |
| Framework | Arduino (PlatformIO) |
| Monitor/Upload Speed | 115200 / 921600 baud |
| Build flag | `-DCORE_DEBUG_LEVEL=0` |

| Library | Version | Purpose |
|---------|---------|---------|
| `tzapu/WiFiManager` | ^2.0.17 | WiFi config portal |
| `bblanchon/ArduinoJson` | ^6.21.3 | JSON serialization |
| `knolleary/PubSubClient` | ^2.8 | MQTT client |
| `olikraus/U8g2` | ^2.35.7 | OLED display (SH1106 128x64) |
| `pololu/VL53L0X` | ^1.3.1 | Time-of-Flight distance sensor |
| `madhephaestus/ESP32Servo` | ^3.0.5 | Servo control |
| HUSKYLENSArduino | git | HuskyLens vision sensor |

### Hardware Pin Mapping

| Pin | Component | Function |
|-----|-----------|----------|
| GPIO 21 | I2C SDA | Shared: OLED SH1106, VL53L0X |
| GPIO 22 | I2C SCL | Shared: OLED SH1106, VL53L0X |
| GPIO 16 | HuskyLens RX | UART receive (9600 baud) |
| GPIO 17 | HuskyLens TX | UART transmit |
| GPIO 25 | US Left TRIG | Left ultrasonic trigger |
| GPIO 26 | US Left ECHO | Left ultrasonic echo |
| GPIO 32 | US Right TRIG | Right ultrasonic trigger |
| GPIO 33 | US Right ECHO | Right ultrasonic echo |
| GPIO 13 | Servo X | Gimbal horizontal servo |
| GPIO 12 | Servo Y | Gimbal vertical servo |
| GPIO 34 | Servo X FB | Servo X feedback ADC |
| GPIO 14 | Buzzer | Piezo buzzer |
| GPIO 4 | SW_PIN | Cargo switch (active LOW) |

### Robot Identity and WiFi

| Constant | Value |
|----------|-------|
| `ROBOT_ID` | `"CARRY-01"` |
| `ROBOT_NAME` | `"Carry-01"` |
| `FW_VERSION` | `"carry-dual-v1"` |
| WiFi Portal SSID | `"CarryMaster-Setup"` |
| WiFi Portal Password | `"carry123"` |
| Portal Timeout | 180 seconds |
| Default WiFi | SSID `"Test"`, password `"123456789"` |

### MQTT Configuration

| Setting | Value |
|---------|-------|
| Default Server | `192.168.137.1` |
| Port | 1883 |
| Username | `hospital_robot` |
| Password | `123456` |
| Buffer Size | 4096 bytes |
| Payload Version | 2 |
| NVS Namespace | `"carrycfg"` |
| NVS Key | `"mqtt_server"` |

### MQTT Topics (prefix `hospital/robots/CARRY-01/`)

| Topic | Direction | QoS |
|-------|-----------|-----|
| `telemetry` | Publish (every 1s) | 0 |
| `mission/assign` | Subscribe | 1 |
| `mission/progress` | Publish | 1 |
| `mission/complete` | Publish | 1 |
| `mission/returned` | Publish | 1 |
| `mission/cancel` | Subscribe | 1 |
| `mission/return_route` | Subscribe | 1 |
| `position/waiting_return` | Publish | 1 |
| `command` | Subscribe | 1 |

### State Machine

```
ST_IDLE (0)
  ├── MQTT mission/assign ──> ST_OUTBOUND (1)
  └── HuskyLens face auth ──> ST_FOLLOW (4)

ST_OUTBOUND (1)
  ├── NFC match destination ──> ST_WAIT_AT_DEST (2)
  └── VL53L0X < 220mm ──> ST_OBSTACLE (8)

ST_WAIT_AT_DEST (2)
  └── Button press / timeout ──> ST_BACK (3)

ST_BACK (3)
  └── NFC match MED ──> ST_IDLE (0)

ST_FOLLOW (4)
  ├── Target lost ──> ST_RECOVERY_VIS (5)
  └── Button / command ──> ST_IDLE (0)

ST_RECOVERY_VIS (5) ──> ST_RECOVERY_BLIND (6) ──> ST_RECOVERY_CALL (7) ──> ST_IDLE (0)

ST_OBSTACLE (8)
  └── VL53L0X > 300mm ──> resume previous state

ST_MISSION_DELEGATED (9)
  └── Slave reports complete ──> ST_WAIT_AT_DEST (2) or ST_IDLE (0)
```

### Sensor Configuration

| Sensor | Setting | Value |
|--------|---------|-------|
| VL53L0X | Stop distance | 220 mm |
| VL53L0X | Resume distance | 300 mm |
| VL53L0X | Measurement budget | 20000 µs |
| Ultrasonic | Side warning | 150 mm |
| Ultrasonic | Timeout | 25000 µs |
| HuskyLens | Init timeout | 7000 ms |
| HuskyLens | Reconnect interval | 700 ms |
| HuskyLens | Face auth ID | 1 |
| HuskyLens | Face auth streak | 3 consecutive detections |

### Follow Mode PID

| Parameter | Value |
|-----------|-------|
| Camera KP | 0.15 |
| Camera KI | 0.001 |
| Camera KD | 0.05 |
| Camera I max | 30.0 |
| Angle KP | 2.0 |
| Angle KD | 0.3 |
| Distance KP | 0.5 |
| Distance KD | 0.15 |
| Target distance | 500 mm |

### Servo Gimbal

| Setting | Value |
|---------|-------|
| X center | 90° |
| Y level | 90° |
| Y tilt down | 45° |
| Y look up | 115° |
| Range | 0-180° both axes |
| ADC feedback range | 300-3700 |

### OLED Display Functions

| Function | Content |
|----------|---------|
| `displayInit()` | Splash screen |
| `displayIdle()` | Idle status |
| `displayOutbound(patient, nextNode)` | Patient name + next node |
| `displayObstacle()` | Obstacle warning |
| `displayWaitAtDest()` | Waiting at destination |
| `displayFollow(target, dist)` | Follow mode info |
| `displayFaceAuth(phase, streak, needed)` | Face auth progress |
| `displayRecovery(step)` | Recovery mode step |
| `displayBack(nextNode)` | Return route info |
| `displayBootChecklist(wifi, mqtt, slave, ms)` | Boot status |

### Buzzer Patterns

| Constant | Duration |
|----------|----------|
| Obstacle beep | 600 ms |

### Button Behavior

| Action | Result |
|--------|--------|
| Single click | Context-dependent (confirm, toggle) |
| Double click | Context-dependent |
| Long press (3s) | Enter WiFi portal |
| Debounce | 50 ms |

### ESP-NOW Configuration

| Setting | Value |
|---------|-------|
| Slave MAC | `B0:CB:D8:C9:9F:14` |
| Channel | 7 |
| TX interval | 50 ms |
| Slave timeout | 1500 ms |
| Boot wait | 5000 ms |
| Relock beacon interval | 10 ms |
| Relock beacon duration | 5000 ms |

### Timing Constants

| Interval | Value |
|----------|-------|
| Telemetry | 1000 ms |
| OLED update | 200 ms |
| VL53L0X read | 50 ms |
| Ultrasonic read | 100 ms |
| HuskyLens poll | 50 ms |
| ESP-NOW TX | 50 ms |
| WiFi link check | 300 ms |
| MQTT reconnect | 2000 ms |
| System ready stable | 2000 ms |
| System ready max wait | 30000 ms |

### Route Management

| Constant | Value |
|----------|-------|
| `MAX_ROUTE_LEN` | 30 points |
| Route chunk size | 4 points per ESP-NOW message |
| Route point fields | nodeId (10 chars), uid (14 chars), action (char) |
| Home UID | `"45:54:80:83"` (MED node) |
| Turn 90° time | 974 ms |
| Turn 180° time | 1980 ms |

### Source Files

| File | Purpose |
|------|---------|
| `main.cpp` | Setup, loop, button handler, WiFi portal |
| `config.h` | All pin definitions, constants, MAC addresses |
| `globals.h/cpp` | Shared state variables |
| `state_machine.h/cpp` | State transitions, click/RFID/MQTT handlers |
| `mqtt_comm.h/cpp` | MQTT connect, subscribe, publish, message handlers |
| `route_manager.h/cpp` | Route parsing, UID lookup, route navigation |
| `mission_delegate.h/cpp` | Send route chunks to slave, start mission |
| `espnow_comm.h/cpp` | ESP-NOW init, send/receive packed structs |
| `espnow_msg.h` | MasterToSlaveMsg, SlaveToMasterMsg, MasterToSlaveRouteChunk |
| `display.h/cpp` | OLED display functions (U8g2) |
| `sensors.h/cpp` | VL53L0X and ultrasonic read functions |
| `servo_gimbal.h/cpp` | Servo X/Y control with feedback |
| `huskylens_wrapper.h/cpp` | HuskyLens face detection API |
| `follow_pid.h/cpp` | PID controller for follow mode |
| `buzzer.h` | Inline buzzer tone macros |

---

## Carry Robot Slave (`CarryRobot/carry_slave/`)

### Platform and Libraries

| Setting | Value |
|---------|-------|
| Platform | `espressif32` (ESP32 DevKit) |
| Framework | Arduino (PlatformIO) |

| Library | Version | Purpose |
|---------|---------|---------|
| `adafruit/Adafruit PN532` | ^1.3.0 | NFC reader (SPI) |

### Hardware Pin Mapping

| Pin | Component | Function |
|-----|-----------|----------|
| GPIO 18 | SPI SCK | PN532 NFC clock |
| GPIO 19 | SPI MISO | PN532 NFC data in |
| GPIO 23 | SPI MOSI | PN532 NFC data out |
| GPIO 5 | SPI SS | PN532 NFC chip select |
| GPIO 35 | LINE_S1 | Line sensor 1 |
| GPIO 36 | LINE_S2 | Line sensor 2 |
| GPIO 39 | LINE_S3 | Line sensor 3 |
| GPIO 13 | L1_ENA | L298N #1 enable A (PWM) |
| GPIO 12 | L1_IN1 | L298N #1 input 1 |
| GPIO 14 | L1_IN2 | L298N #1 input 2 |
| GPIO 27 | L1_IN3 | L298N #1 input 3 |
| GPIO 26 | L1_IN4 | L298N #1 input 4 |
| GPIO 25 | L1_ENB | L298N #1 enable B (PWM) |
| GPIO 33 | L2_ENA | L298N #2 enable A (PWM) |
| GPIO 4 | L2_IN1 | L298N #2 input 1 |
| GPIO 16 | L2_IN2 | L298N #2 input 2 |
| GPIO 17 | L2_IN3 | L298N #2 input 3 |
| GPIO 22 | L2_IN4 | L298N #2 input 4 |
| GPIO 21 | L2_ENB | L298N #2 enable B (PWM) |

### Motor Configuration

| Setting | Value |
|---------|-------|
| PWM frequency | 20 kHz |
| PWM resolution | 8-bit |
| Run speed | 100% |
| Rotate speed | 100% |
| Turn 90° time | 974 ms |
| Turn 180° time | 1980 ms |
| Turn PWM | 168 |
| Brake PWM | 150 |
| Brake duration | 80 ms |
| LEDC channels | FL=0, BL=1, FR=2, BR=3 |

### Line Follower PID

| Parameter | Value |
|-----------|-------|
| KP | 0.35 |
| KI | 0.0 |
| KD | 0.20 |
| Max correction | 180.0 |
| Invert | true |

### NFC Configuration

| Setting | Value |
|---------|-------|
| Read interval | 100 ms |
| Repeat guard | 700 ms |
| Protocol | ISO14443A via SPI |

### ESP-NOW Configuration

| Setting | Value |
|---------|-------|
| Master MAC | `20:E7:C8:68:55:F0` |
| Channel | 7 |
| TX interval | 50 ms |
| Main loop delay | 2 ms |

### ESP-NOW Message Structs

#### MasterToSlaveMsg (packed)

| Field | Type | Description |
|-------|------|-------------|
| `state` | uint8_t | Current robot state |
| `vX` | float | Forward/backward velocity |
| `vY` | float | Left/right velocity |
| `vR` | float | Rotation velocity |
| `enableLine` | uint8_t | Enable line following |
| `enableRFID` | uint8_t | Enable NFC reading |
| `turnCmd` | uint8_t | Turn command: 'L', 'R', 'B', or 0 |
| `baseSpeed` | uint8_t | Base speed for line following |
| `missionStart` | uint8_t | Start mission flag |
| `missionCancel` | uint8_t | Cancel mission flag |
| `startReturn` | uint8_t | Start return route flag |

#### SlaveToMasterMsg (packed)

| Field | Type | Description |
|-------|------|-------------|
| `rfid_uid` | char[24] | Last read NFC UID string |
| `rfid_new` | uint8_t | New UID flag |
| `line_detected` | uint8_t | Line sensor active |
| `sync_docking` | uint8_t | Line centred (docking ready) |
| `lineError` | int16_t | PID error value |
| `lineBits` | uint8_t | Raw sensor bits |
| `turnDone` | uint8_t | Turn complete flag |
| `missionStatus` | uint8_t | Mission execution status |
| `routeIndex` | uint8_t | Current route point index |
| `routeTotal` | uint8_t | Total route points |
| `routeSegment` | uint8_t | Current route segment |

#### MasterToSlaveRouteChunk (packed)

| Field | Type | Description |
|-------|------|-------------|
| `type` | uint8_t | Message type |
| `segment` | uint8_t | Route segment (outbound/return) |
| `chunkIndex` | uint8_t | Chunk index |
| `chunkTotal` | uint8_t | Total chunks |
| `numPoints` | uint8_t | Points in this chunk (max 4) |
| `points[4]` | struct | nodeId[10] + uid[14] + action (char) |

### Source Files

| File | Purpose |
|------|---------|
| `main.cpp` | Setup, loop, drive loop, turn execution |
| `config.h` | Pin definitions, motor/PID constants, MAC |
| `globals.h/cpp` | Shared state (masterCmd, slaveReport) |
| `espnow_comm.h/cpp` | ESP-NOW init, callbacks, route chunk reception |
| `espnow_msg.h` | Shared message struct definitions |
| `mecanum.h/cpp` | Mecanum drive, turn functions (mecanumDrive, mecanumTurnLeft90, etc.) |
| `line_follower.h/cpp` | 3-sensor PID line following |
| `rfid_reader.h/cpp` | PN532 NFC read with repeat guard |
| `route_runner.h/cpp` | Route point matching, segment execution |

### NFC Node Naming Convention

| Pattern | Description | Example |
|---------|-------------|---------|
| `MED` | Medicine station (home base) | `MED` |
| `H_MED`, `H_BOT`, `H_TOP` | Hallway corridor checkpoints | `H_MED` |
| `J1`-`J4` | Junctions (hallway intersections) | `J4` |
| `R{1-4}M{1-3}` | Room beds, M-side (3 per room) | `R1M2` |
| `R{1-4}O{1-3}` | Room beds, O-side (3 per room) | `R2O1` |
| `R{1-4}D{1-2}` | Room doors (2 per room) | `R3D1` |

Total: ~40 nodes (4 rooms × 8 + 3 hallway + 4 junctions + 1 MED)

### Build Commands

```bash
pio run -d CarryRobot/carry_master              # Build master
pio run -d CarryRobot/carry_slave               # Build slave
pio run -d CarryRobot/carry_master -t upload --upload-port COM15   # Flash master
pio run -d CarryRobot/carry_slave -t upload --upload-port COM13    # Flash slave
pio device monitor -p COM15 -b 115200           # Monitor master
pio device monitor -p COM13 -b 115200           # Monitor slave
```

---

## Backend API (`Hospital Dashboard/Backend/`)

### Stack and Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| express | ^4.19.2 | Web framework |
| mongoose | ^8.4.0 | MongoDB ODM |
| mqtt | ^5.15.0 | MQTT client (Mosquitto) |
| cors | ^2.8.5 | Cross-origin support |
| dotenv | ^16.4.0 | Environment variables |
| multer | ^2.0.2 | File upload handling |

- **Type**: ES Modules (`"type": "module"`)
- **Entry**: `src/index.js`
- **Port**: `process.env.PORT || 3000`, bound to `0.0.0.0`
- **MongoDB**: `process.env.MONGO_URI`, timeout 5000ms
- **No authentication/authorization** on any route

### MQTT Service (`src/services/mqttService.js`)

| Setting | Default | Env Override |
|---------|---------|--------------|
| Broker | `mqtt://localhost:1883` | `MQTT_BROKER` |
| Username | `hospital_backend` | `MQTT_USER` |
| Password | `123456` | `MQTT_PASS` |
| Client ID | `hospital-backend-{timestamp}` | - |

#### Subscribed Topics (from robots)

| Topic Pattern | Handler |
|---------------|---------|
| `hospital/robots/+/telemetry` | `handleTelemetry()` |
| `hospital/robots/+/mission/progress` | `handleProgress()` |
| `hospital/robots/+/mission/complete` | `handleComplete()` |
| `hospital/robots/+/mission/returned` | `handleReturned()` |
| `hospital/robots/+/position/waiting_return` | `handleWaitingReturnRoute()` |

#### Published Topics (to robots)

| Function | Topic | QoS |
|----------|-------|-----|
| `publishMissionAssign(robotId, mission)` | `hospital/robots/{robotId}/mission/assign` | 1 |
| `publishMissionCancel(robotId, missionId)` | `hospital/robots/{robotId}/mission/cancel` | 1 |
| `publishReturnRoute(robotId, missionId, route, status)` | `hospital/robots/{robotId}/mission/return_route` | 1 |
| `publishCommand(robotId, command, params)` | `hospital/robots/{robotId}/command` | 1 |

#### Handler Details

- **`handleTelemetry`**: Upserts Robot doc. Mission-aware status (busy if active mission, idle otherwise). Low battery alert at 20% or below. Sets `currentLocation.room` from `currentNodeId`. Sets `transportData.destination.room` from `destBed` or active mission
- **`handleProgress`**: Updates TransportMission progress. Sets robot position and mission destination
- **`handleComplete`**: Sets mission to `completed` or `failed`. Robot status to `idle`
- **`handleReturned`**: Sets `returnedAt` on mission. Robot status to `idle`, location to `MED`
- **`handleWaitingReturnRoute`**: Robot reports position after cancellation. Backend computes return route via `buildReturnPath()` + `computeReturnActions()`, publishes via MQTT

#### Return Route Calculation
- `buildReturnPath(fromNodeId)`: Builds path from any node back to MED using hard-coded hospital layout. Parses `R{room}{M|O|D}{idx}` for room nodes, corridor nodes handled directly
- `computeReturnActions(routePoints, startNode)`: Assigns turn actions based on room geometry (rooms 1/3 left-side, rooms 2/4 right-side)

### MongoDB Models

#### Robot (`src/models/Robot.js`)

| Field | Type | Details |
|-------|------|---------|
| `robotId` | String | Required, unique |
| `name` | String | Required |
| `type` | String | Enum: `carry` |
| `status` | String | `idle`, `busy`, `charging`, `maintenance`, `offline`, `low_battery` |
| `lastSeenAt` | Date | For online detection |
| `batteryLevel` | Number | 0-100, default 100 |
| `currentLocation` | Object | `{ building, floor, room, coordinates: {x, y} }` |
| `nearestChargingStation` | Object | `{ stationId, location, distance }` |
| `transportData` | Object | `{ carryingItem, weight, destination, estimatedArrival }` |
| `firmwareVersion` | String | |
| `totalDeliveries` | Number | Incremented on carry mission return |
| `totalDistance`, `totalOperatingHours` | Number | Statistics |
| `notes` | [String] | |

Indexes: `{ type: 1, status: 1 }`, `{ batteryLevel: 1 }`, `{ lastSeenAt: -1 }`

#### TransportMission (`src/models/TransportMission.js`)

| Field | Type | Details |
|-------|------|---------|
| `missionId` | String | Required, unique (format: `TM-{12 hex}`) |
| `mapId` | String | Required |
| `carryRobotId` | String | Required |
| `patientName` | String | |
| `bedId` | String | Required (e.g., `R1M1`) |
| `destinationNodeId` | String | |
| `outboundRoute` | [RoutePoint] | `{ nodeId, x, y, rfidUid, action, actions }` |
| `returnRoute` | [RoutePoint] | Same format |
| `status` | String | `pending`, `en_route`, `arrived`, `completed`, `failed`, `cancelled` |
| `returnedAt` | Date | When robot returned to base |
| `cancelRequestedAt`, `cancelledAt`, `cancelledBy` | Date/String | Cancellation tracking |

#### Patient (`src/models/Patient.js`)

| Field | Type | Details |
|-------|------|---------|
| `fullName` | String | Required |
| `mrn` | String | Required, unique (`MRN-{year}-{3 digits}`) |
| `cardNumber` | String | Required (RFID UID) |
| `admissionDate` | String | Required, YYYY-MM-DD |
| `status` | String | `Stable`, `Critical`, `Recovering`, `Under Observation`, `Discharged` |
| `primaryDoctor` | String | Required |
| `roomBed` | String | Canonical `R{1-4}{M|O}{1-3}` |
| `relativeName`, `relativePhone` | String | Required |
| `photoPath`, `photoUrl` | String | Patient photo |
| `timeline` | [embedded] | `{ at, title, createdBy, description }` |
| `prescriptions` | [embedded] | `{ medication, dosage, frequency, startDate, endDate, prescribedBy, instructions, status }` |
| `notes` | [embedded] | `{ createdAt, createdBy, text }` |

#### Alert (`src/models/Alert.js`)

| Field | Type | Details |
|-------|------|---------|
| `type` | String | `carry_low_battery`, `rescue_required`, `route_deviation`, `info` |
| `level` | String | `low`, `medium`, `high` |
| `robotId`, `missionId` | String | |
| `message` | String | Required |
| `data` | Mixed | |
| `resolvedAt` | Date | |

#### Other Models

| Model | Purpose |
|-------|---------|
| `MapGraph` | Graph-based floor map with nodes and edges, auto-computed weights |
| `ChargingStation` | Charging station management with capacity, slots, history |
| `User` | RFID user registration (uid, name, email) |
| `Event` | Button press events from ESP32 |
| `PatientNote`, `PatientTimeline`, `Prescription` | Standalone collections mirroring Patient sub-docs |

### REST API Routes

#### Patients (`/api/patients`)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/patients` | List with search, filter, sort. Fuzzy search on name/MRN/card/doctor |
| GET | `/patients/by-card/:cardNumber` | Lookup by RFID card |
| GET | `/patients/by-bed/:bedId` | Find patient in bed |
| GET | `/patients/beds` | Valid bed IDs from map |
| GET | `/patients/meta` | Filter metadata (statuses, doctors) |
| GET | `/patients/mrn/generate` | Generate unique MRN |
| POST | `/patients` | Create with photo upload (max 5MB, jpg/png/webp) |
| PUT | `/patients/:id` | Update with optional new photo |
| DELETE | `/patients/:id` | Delete with photo cleanup |
| GET | `/patients/:id/details` | Full details with timeline/prescriptions/notes |
| POST/DELETE | `/patients/:id/timeline/:tid` | Timeline entries |
| POST/PUT/DELETE | `/patients/:id/prescriptions/:pid` | Prescriptions |
| POST/DELETE | `/patients/:id/notes/:nid` | Notes |

**Bed ID dual format**: Both `R1M1` (canonical) and `R1-Bed1` (legacy). Mapping: Bed1=M1, Bed2=O1, Bed3=M2, Bed4=O2, Bed5=M3, Bed6=O3.

#### Robots (`/api/robots`)

| Method | Path | Description |
|--------|------|-------------|
| PUT | `/robots/:id/telemetry` | ESP32 heartbeat. Upserts robot. Auto-detects busy/idle from missions |
| GET | `/robots/carry/status` | Online carry robots (seen within 15s). Includes active mission |

**Online detection**: Robot is "online" if `lastSeenAt` within 15s (status) or 30s (mission assignment).

#### Missions (`/api/missions`)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/missions/transport` | List transport missions (filter by status, robotId) |
| POST | `/missions/delivery` | Create delivery mission: find idle carry robot, compute route, publish MQTT |
| GET | `/missions/carry/next` | Poll next mission for robot firmware |
| PUT | `/missions/carry/:id/progress` | Update progress. Battery ≤20% creates alert |
| POST | `/missions/carry/:id/complete` | Mark delivered |
| POST | `/missions/carry/:id/cancel` | Cancel + publish MQTT cancel |
| POST | `/missions/carry/:id/returned` | Mark returned to base. Increments `totalDeliveries` if completed |
| GET | `/missions/delivery/history` | Paginated delivery history |

#### Mission Route Computation (Hard-Coded)

Routes use hard-coded paths for the 4-room hospital layout (not Dijkstra):

- **Corridor**: `MED -> H_MED -> H_BOT -> (J4 -> H_TOP for rooms 1/2)`
- Rooms 1/3: left-side (enter with left turn)
- Rooms 2/4: right-side (enter with right turn)
- Rooms 1/2: hub node `H_TOP`, rooms 3/4: hub node `H_BOT`
- M-side beds: `D1 -> M1 -> M2 -> M3`
- O-side beds: `D1 -> D2 -> O1 -> O2 -> O3`

Key functions: `buildHardRouteNodeIds()`, `roomProfile()`, `actionsForLeg()`, `computeActionsMap()`, `toPoints()`

#### Maps (`/api/maps`)

| Method | Path | Description |
|--------|------|-------------|
| POST | `/maps` | Create map metadata |
| POST | `/maps/:mapId/import` | Import nodes/edges |
| GET | `/maps/:mapId` | Get map |
| GET | `/maps/:mapId/route` | Dijkstra shortest path |

#### Alerts (`/api/alerts`)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/alerts` | List (filter: `active=1`, limit max 200) |
| POST | `/alerts` | Create alert |
| PUT | `/alerts/:id/resolve` | Resolve alert |

Alert types: `carry_low_battery`, `rescue_required`, `route_deviation`, `info`

#### Users (`/api/users`)

| Method | Path | Description |
|--------|------|-------------|
| POST | `/users` | Create/upsert user (UID + name) |
| GET | `/users` | List all users |
| GET | `/users/:uid/exists` | Check UID registered |
| DELETE | `/users/:uid` | Delete user + events |

#### Events (`/api/events`)

| Method | Path | Description |
|--------|------|-------------|
| POST | `/events/button` | Register button press |
| GET | `/events/stats/daily` | Daily event count |
| GET | `/events/stats/daily-by-user` | Daily count by user |

### Seed Data

#### `seed/seedMap.js`

Creates map `floor1` with 37 nodes:
- Corridor nodes (x=400): MED(y=500), H_MED(y=400), H_BOT(y=300), J4(y=200), H_TOP(y=100)
- Room 1 (baseX=100, baseY=100, hub=H_TOP), Room 2 (700, 100, H_TOP)
- Room 3 (100, 300, H_BOT), Room 4 (700, 300, H_BOT)
- Per room: D1, D2, M1-M3, O1-O3. All edges weight=1.

#### `seed/checkpointsF1.js`

Exports `CHECKPOINT_UID` map of 37 node IDs to NFC UIDs (colon-separated hex format).

---

## Frontend (`Hospital Dashboard/Frontend/`)

### Stack

| Library | Purpose |
|---------|---------|
| React 18 + Vite 6 | Framework + build tool |
| TypeScript | Type safety |
| Tailwind CSS v4 + shadcn/ui | Styling (Radix primitives + CVA) |
| MUI Material 7 | Available (secondary) |
| react-hook-form | Form state |
| recharts | Charts |
| lucide-react | Icons |
| date-fns | Date formatting |
| sonner | Toast notifications |
| motion (Framer) | Animations |

### Vite Configuration

| Setting | Value |
|---------|-------|
| Dev server port | 5173 |
| Path alias | `@` → `./src` |
| Proxy `/api` | → `http://localhost:3000` |
| Proxy `/uploads` | → `http://localhost:3000` |

### Architecture

| Aspect | Detail |
|--------|--------|
| Routing | Tab-based SPA (useState) — no router |
| State | Local useState + custom hooks per domain |
| Data Fetching | Custom hooks with fetch wrapper + setInterval polling |
| Polling | Robots: 5s, Alerts: 10s, History: 5s, Connection: 30s |
| Theme | Medical Blue (#0277BD), light/dark mode |
| Base Font Size | 20px (hospital readability) |
| RFID | Web Serial API (Chrome/Edge) for USB readers |
| Camera | getUserMedia for patient photos |

### Application Structure

```
App.tsx
├── RFIDProvider (context)
├── Header (logo, ConnectionStatus, alert badge)
├── Tab Navigation ("Patients Manager" | "Robot Center")
├── PatientDashboard
│   ├── Search & Filter Panel
│   ├── Patient Table
│   ├── PatientForm (dialog + react-hook-form + RFID + camera)
│   └── PatientDetails (dialog: Medications/Allergens/Notes)
├── RobotCenter
│   ├── BedMap (4-room floor plan, 24 beds)
│   └── RobotManagement
│       ├── Selected Patient Card + Send/Cancel Robot
│       ├── Biped Robot Panel (status + history)
│       └── Carry Robot Panel (status + delivery history)
└── Footer
```

### Custom Hooks

| Hook | Polling | Purpose |
|------|---------|---------|
| `usePatients()` | - | Patient CRUD |
| `useRobots(5000)` | 5s | Robot status + missions |
| `useAlerts(10000)` | 10s | Alert management |
| `useMissions()` | - | Mission create/cancel |
| `useSerialRFID()` | - | Web Serial RFID reader |

### RFID Integration (Web Serial API)

- USB vendor filters: Arduino (0x2341), CH340 (0x1A86), FTDI (0x0403), CP210x (0x10C4)
- Protocol: `RFID:XXXXXXXX\n` prefix or raw hex UIDs (8+ chars)
- RFIDContext: pub/sub for multi-component subscription
- Auto-fills card number in PatientForm on scan

### Key Components

| Component | Description |
|-----------|-------------|
| BedMap | 4 rooms × 6 beds. Rooms 1/3: O-beds left, M-beds right. Rooms 2/4: M-beds left, O-beds right. Color: green=occupied, blue=robot active, gray=available |
| PatientDashboard | Search (fuzzy), sort, status badges, CSV export |
| RobotManagement | Carry status table, biped status table, history tables with pagination |
| ConnectionStatus | Polls `/api/patients/meta` every 30s |

### TypeScript Types (key)

```typescript
type PatientStatus = 'Stable' | 'Critical' | 'Recovering' | 'Under Observation' | 'Discharged'
type RobotType = 'Biped' | 'Carry'
type RobotStatus = 'Idle' | 'Moving' | 'At Destination' | 'Task in Progress' | 'Error'
const BED_MAP: string[] = ['R1M1', ..., 'R4O3']  // 24 beds
```

---

## Development

### Start Development

```bash
cd "Hospital Dashboard"
cd Backend && npm run dev    # Port 3000
cd Frontend && npm run dev   # Port 5173 (proxies /api → backend)
```

### Build and Flash ESP32

```bash
pio run -d CarryRobot/carry_master               # Build master
pio run -d CarryRobot/carry_slave                # Build slave
pio run -d CarryRobot/carry_master -t upload --upload-port COM15
pio run -d CarryRobot/carry_slave -t upload --upload-port COM13
```

### Seed Database

```bash
cd "Hospital Dashboard/Backend"
node seed/seedMap.js         # Creates floor1 map with 37 nodes
```

### MQTT Broker (Mosquitto)

- Port 1883
- Users: `hospital_robot` (ESP32), `hospital_backend` (Backend), password: `123456`
- Config: `listener 1883`, `allow_anonymous false`, `password_file`
- Firewall: inbound TCP rule required for port 1883

---

## Common Tasks

### Adding a New NFC Checkpoint
1. Add entry to UID lookup in `CarryRobot/carry_master/src/route_manager.cpp`
2. Add to `CHECKPOINT_UID` in `Hospital Dashboard/Backend/seed/checkpointsF1.js`
3. Update MapGraph via `seed/seedMap.js` or `POST /api/maps/:mapId/import`

### Modifying Mission Routes
Hard-coded routes in two places:
- `Hospital Dashboard/Backend/src/routes/missions.js`: `buildHardRouteNodeIds()`, `roomProfile()`, `actionsForLeg()`
- `Hospital Dashboard/Backend/src/services/mqttService.js`: `buildReturnPath()`, `computeReturnActions()`

### Adding a New Patient Field
1. Add to schema in `Backend/src/models/Patient.js`
2. Update `routes/patients.js` POST/PUT handlers
3. Add to `BackendPatient` in `Frontend/src/app/api/patients.ts`
4. Update `toFrontendPatient()` / `toBackendPatientData()` in `Frontend/src/app/hooks/usePatients.ts`
5. Add to `Patient` interface in `Frontend/src/app/types/patient.ts`
6. Add UI in `Frontend/src/app/components/PatientForm.tsx`

---

## Critical Notes

- **MQTT buffer size**: 4096 bytes on ESP32 PubSubClient — mission payloads with full routes can be large
- **Return route**: Cancel triggers wait state — robot waits for Backend MQTT return route, then falls back to reversing outbound nodes
- **MQTT server**: Stored in NVS (`carrycfg` namespace), configurable via WiFiManager portal or saved on first connect
- **Online detection**: 15s for status endpoints, 30s for mission assignment eligibility
- **Battery level**: Hardcoded to 100 on ESP32 (no real battery monitoring)
- **Bed ID formats**: Both canonical (`R1M1`) and legacy (`R1-Bed1`) supported throughout backend
- **Photo upload**: Max 5MB, jpg/jpeg/png/webp only, stored in `uploads/patients/`
- **No router in frontend**: Tab-based navigation only (patients vs robot center)
- **ESP-NOW channel**: Must match on both master (7) and slave (7)
- **Slave MAC**: Hardcoded in master `config.h`, master MAC hardcoded in slave `config.h` — update if hardware changes
