# Hospital Robot System

## Overview

Hospital medication delivery system with two integrated components:
1. **Carry Robot** (`CarryRobot/`) â€” Dual-ESP32 transport robot with Mecanum wheels, HuskyLens vision, NFC checkpoint navigation, and MQTT communication
2. **Hospital Dashboard** (`Hospital Dashboard/`) â€” Full-stack web app (Express.js + React/Vite + MongoDB) for patient management, robot monitoring, and mission control

## Architecture

```
[Dashboard Frontend:5173] â”€â”€Vite Proxyâ”€â”€> [Backend API:3000] â”€â”€Mongooseâ”€â”€> [MongoDB]
                                                â”‚
                                          MQTT (PubSubClient)
                                                â”‚
                                       [Carry Master ESP32]
                                          WiFi + MQTT + HuskyLens
                                          OLED + Servo Gimbal
                                                â”‚
                                            ESP-NOW
                                                â”‚
                                       [Carry Slave ESP32]
                                          Mecanum + NFC + Line Follow
```

### Workflow
1. Dashboard creates delivery mission via `/api/missions/delivery`
2. Backend computes hard-coded route, publishes `mission/assign` via MQTT
3. Carry Master receives mission, streams route to Slave via ESP-NOW
4. Slave navigates via NFC checkpoints + line following
5. Robot delivers to bed, returns to MED station

## Carry Robot (`CarryRobot/`)

Dual-ESP32 architecture communicating via ESP-NOW.

### Master (`carry_master/`)

| Setting | Value |
|---------|-------|
| Board | ESP32 DevKit |
| Framework | Arduino (PlatformIO) |
| Upload speed | 921600 baud |

**Libraries**: WiFiManager, ArduinoJson, PubSubClient, U8g2, VL53L0X, ESP32Servo, HUSKYLENSArduino

**Responsibilities**: WiFi/MQTT, HuskyLens vision (face auth + follow), servo gimbal, OLED display, VL53L0X + ultrasonic sensors, mission state machine, route management, ESP-NOW master

**Pin Mapping**:

| Pin | Function |
|-----|----------|
| GPIO 21/22 | I2C SDA/SCL (OLED SH1106, VL53L0X) |
| GPIO 16/17 | HuskyLens UART (RX/TX, 9600 baud) |
| GPIO 25/26 | Left ultrasonic (TRIG/ECHO) |
| GPIO 32/33 | Right ultrasonic (TRIG/ECHO) |
| GPIO 13/12 | Servo gimbal (X/Y) |
| GPIO 34 | Servo X feedback ADC |
| GPIO 14 | Buzzer |
| GPIO 4 | Cargo switch |

**State Machine**:
```
ST_IDLE -> ST_OUTBOUND -> ST_WAIT_AT_DEST -> ST_BACK -> ST_IDLE
                |
            ST_FOLLOW / ST_RECOVERY_VIS / ST_RECOVERY_BLIND / ST_RECOVERY_CALL
                |
            ST_OBSTACLE (pause on VL53L0X < 220mm)
            ST_MISSION_DELEGATED
```

**MQTT Topics** (prefix `hospital/robots/CARRY-01/`):

| Topic | Direction |
|-------|-----------|
| `telemetry` | Publish (every 1s) |
| `mission/assign` | Subscribe |
| `mission/progress` | Publish |
| `mission/complete` | Publish |
| `mission/returned` | Publish |
| `mission/cancel` | Subscribe |
| `mission/return_route` | Subscribe |
| `position/waiting_return` | Publish |
| `command` | Subscribe |

**WiFi/MQTT Config**:
- Portal SSID: `CarryMaster-Setup` / `carry123`
- MQTT: `192.168.137.1:1883`, user `hospital_robot` / `123456`
- MQTT server saved in NVS (`carrycfg` namespace)

### Slave (`carry_slave/`)

| Setting | Value |
|---------|-------|
| Board | ESP32 DevKit |
| Framework | Arduino (PlatformIO) |

**Library**: Adafruit PN532

**Responsibilities**: Mecanum wheel drive (2x L298N, 4 motors), PN532 NFC reader (SPI), 3-sensor line follower with PID, route execution, ESP-NOW slave

**Pin Mapping**:

| Pin | Function |
|-----|----------|
| GPIO 18/19/23/5 | SPI (SCK/MISO/MOSI/SS) for PN532 NFC |
| GPIO 35/36/39 | Line sensors (S1/S2/S3) |
| GPIO 13/12/14/27/26/25 | L298N #1 (ENA/IN1/IN2/IN3/IN4/ENB) |
| GPIO 33/4/16/17/22/21 | L298N #2 (ENA/IN1/IN2/IN3/IN4/ENB) |

**Motor Config**: 20kHz PWM, 8-bit resolution, 974ms for 90-degree turn, 1980ms for 180-degree turn

**Line Follower PID**: KP=0.35, KI=0.0, KD=0.20, max correction 180

### ESP-NOW Messages

| Struct | Direction | Content |
|--------|-----------|---------|
| `MasterToSlaveMsg` | Master -> Slave | state, vX/vY/vR, enableLine, enableRFID, turnCmd, baseSpeed, missionStart/Cancel, startReturn |
| `SlaveToMasterMsg` | Slave -> Master | rfid_uid, rfid_new, line_detected, sync_docking, lineError, lineBits, turnDone, missionStatus, routeIndex/Total/Segment |
| `MasterToSlaveRouteChunk` | Master -> Slave | Route points in chunks of 4 (nodeId + uid + action per point) |

### NFC Node Naming
`MED` (home), `H_MED`/`H_BOT`/`H_TOP` (hallway), `J1`-`J4` (junctions), `R{1-4}M{1-3}` (M-side beds), `R{1-4}O{1-3}` (O-side beds), `R{1-4}D{1-2}` (doors)

### Build
```bash
pio run -d CarryRobot/carry_master
pio run -d CarryRobot/carry_slave
pio run -d CarryRobot/carry_master -t upload --upload-port COM15
pio run -d CarryRobot/carry_slave -t upload --upload-port COM13
```

## Hospital Dashboard

### Backend (`Hospital Dashboard/Backend/`)

Express.js + MongoDB + MQTT. Port 3000. ES Modules.

**Dependencies**: express, mongoose, mqtt, cors, dotenv, multer

**Models**: Robot, TransportMission, Patient, Alert, MapGraph, User, Event, Prescription, PatientNote, PatientTimeline, ChargingStation

**Routes**:

| Route | Purpose |
|-------|---------|
| `/api/patients` | Patient CRUD, photo upload, timeline, prescriptions, notes |
| `/api/patients/by-card/:cardNumber` | RFID lookup |
| `/api/robots/:id/telemetry` | Robot heartbeat (PUT, upsert) |
| `/api/robots/carry/status` | Online carry robots (15s threshold) |
| `/api/missions/delivery` | Create delivery mission (compute route + MQTT publish) |
| `/api/missions/carry/:id/cancel` | Cancel mission |
| `/api/missions/transport` | List transport missions |
| `/api/maps` | Floor map CRUD, Dijkstra routing |
| `/api/alerts` | System alerts |
| `/api/users` | RFID user management |
| `/api/events` | Button event logging |

**Mission Route**: Hard-coded paths in `missions.js` (`buildHardRouteNodeIds()`, `roomProfile()`, `actionsForLeg()`). Rooms 1/3 are left-side, rooms 2/4 are right-side.

**MQTT Service** (`mqttService.js`): Subscribes to telemetry/progress/complete/returned/position topics. Publishes mission/assign, mission/cancel, return_route, command. Computes return routes via `buildReturnPath()`.

**Bed ID format**: `R{room}{M|O}{1-3}` (canonical). Also accepts legacy `R1-Bed1` format.

### Frontend (`Hospital Dashboard/Frontend/`)

React + Vite + TypeScript + Tailwind CSS + shadcn/ui. Port 5173 (proxies `/api` to backend).

**Structure**:
- `src/app/api/` — API service layer
- `src/app/components/` — PatientDashboard, RobotCenter, BedMap, RobotManagement, RobotHistory, PatientForm, PatientDetails
- `src/app/types/` — TypeScript interfaces
- `src/app/hooks/` — Custom hooks (usePatients, useRobots, useAlerts, useMissions)
- `src/app/contexts/` — RFID context (Web Serial API)

**Features**: Patient management with photo/RFID, 4-room bed map (24 beds), carry robot monitoring, delivery mission control, biped robot status display, alert system

### Start Development
```bash
cd "Hospital Dashboard"
cd Backend && npm run dev    # Port 3000
cd Frontend && npm run dev   # Port 5173
```

### Seed Database
```bash
cd "Hospital Dashboard/Backend"
node seed/seedMap.js         # Creates floor1 map with 37 nodes
```

### MQTT Broker (Mosquitto)
Port 1883. Users: `hospital_robot` (ESP32), `hospital_backend` (Backend). Password: `123456`.
