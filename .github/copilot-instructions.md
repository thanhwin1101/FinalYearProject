# Hospital Robot System - AI Coding Instructions

## System Overview

This is a **hospital rehabilitation system** with three integrated components:
1. **Biped Robot** (`BipedRobot/`) - 10-servo walking robot for patient rehabilitation assistance
2. **Carry Robot** (`Carry_robot/`) - ESP32 transport robot with Mecanum wheels for medication delivery & biped transport
3. **Hospital Dashboard** (`Hospital Dashboard/`) - Full-stack web app (Express.js + React/Vite) for management

## Architecture & Data Flow

```
[Dashboard Frontend:5173] <--> [Backend API:3000] <--> [MongoDB]
                                       ^
                                       | HTTP REST
                        +--------------+---------------+
                        v                              v
                [Carry Robot ESP32]            [Biped Robot ESP32s]
                        ^                       (Walking + User Auth)
                        | ESP-NOW                      |
                        v                              | RFID checkpoint
                [ESP32-CAM Module]                     v
                                              [Location to Dashboard]
```

### Main Workflows
- **Rehabilitation Session**: User scans RFID on Biped → Dashboard logs session → Biped tracks steps → Session ends
- **Biped Transport**: Biped scans checkpoint RFID → Dashboard knows location → Commands Carry Robot to go to that checkpoint → Carry Robot returns to designated location
- **Medication Delivery**: Dashboard creates mission → Carry Robot navigates via NFC → Delivers to bed

### Communication Protocols
- **Biped ESP32s**: UART between Walking Controller ↔ User Manager
- **Biped → Dashboard**: WiFi HTTPS (from User Manager ESP32)
- **Carry Robot → Dashboard**: WiFi HTTP REST API
- **Carry Robot ↔ ESP32-CAM**: ESP-NOW wireless (for Follow Mode)

## Biped Robot Firmware (`BipedRobot/`)

### Hardware Architecture
- **Dual ESP32 System** (UART communication between them):
  - **ESP32 Walking Controller**: Servo control, balance, locomotion ✅ Implemented
  - **ESP32 User Manager**: RFID authentication, OLED display, checkpoint scanning, WiFi HTTP to Dashboard ✅ Implemented (`BipedUserManager/`)

### Servo Configuration (10 servos via PCA9685 I2C)
| Index | Joint | Pin | Function |
|-------|-------|-----|----------|
| 0 | HIP_PITCH_L | 0 | Left leg swing forward/backward |
| 1 | HIP_ROLL_L | 1 | Left leg lateral movement |
| 2 | KNEE_PITCH_L | 2 | Left knee bend |
| 3 | ANKLE_PITCH_L | 3 | Left foot tilt front/back |
| 4 | ANKLE_ROLL_L | 4 | Left foot tilt left/right |
| 5 | HIP_PITCH_R | 5 | Right leg swing (inverted) |
| 6 | HIP_ROLL_R | 6 | Right leg lateral |
| 7 | KNEE_PITCH_R | 7 | Right knee (inverted) |
| 8 | ANKLE_PITCH_R | 8 | Right foot pitch (inverted) |
| 9 | ANKLE_ROLL_R | 9 | Right foot roll |

### Physical Dimensions (mm)
- Thigh (L1): 60mm | Shank (L2): 70mm | Ankle Height (L3): 62mm | Foot (L4): 80mm

### Joint Limits (degrees, 0° = standing straight)
- Hip Pitch: -45° to +45° | Knee: -5° to +140° | Ankle Pitch: -30° to +30°
- Hip Roll: -30° to +30° | Ankle Roll: -30° to +30°

### Sensors
- **FSR402 × 8** (4 per foot): Ground contact detection, weight distribution for balance
- **MPU6050**: IMU for body tilt angle, used with ankle servos for active balance
- **RFID Module**: User authentication + checkpoint location scanning

**FSR402 Foot Sensor Layout (per foot):**
```
        [TOE]           ← Front sensor (toe contact)
          |
    [L]-------[R]       ← Left/Right edge sensors (lateral balance)
          |
        [HEEL]          ← Rear sensor (heel contact)
```
| Position | Left Foot | Right Foot | Purpose |
|----------|-----------|------------|---------|
| Toe | FSR_L_TOE | FSR_R_TOE | Detect forward tilt, toe-off phase |
| Left Edge | FSR_L_LEFT | FSR_R_LEFT | Detect lateral roll to left |
| Right Edge | FSR_L_RIGHT | FSR_R_RIGHT | Detect lateral roll to right |
| Heel | FSR_L_HEEL | FSR_R_HEEL | Detect heel strike, backward tilt |

### User Interface
- **OLED Display**: Shows username, step count, session status
- **4 Buttons**: Forward, Backward, Turn Left, Turn Right
- **Rotary Encoder**: Speed adjustment

### Key Files
- [BipedRobot.ino](BipedRobot/BipedRobot.ino) - Walking Controller: servo control, balance, UART receive
- [ServoController.h](BipedRobot/ServoController.h) - PCA9685 servo abstraction
- [Kinematics.h](BipedRobot/Kinematics.h) - Leg inverse kinematics
- [config.h](BipedRobot/config.h) - Servo limits, PID gains, pin definitions
- [BipedUserManager.ino](BipedUserManager/BipedUserManager.ino) - User Manager: RFID, OLED, WiFi, buttons

### Balance System (Kalman Filter + PD Control)
The balance system runs at 50Hz and uses sensor fusion:

**Kalman Filter for MPU6050:**
```
1. Predict: angle += gyroRate * dt
2. Update: fuse with accelerometer angle
3. Output: filtered pitch & roll angles
```

**Control Loop:**
- Read MPU6050 → Kalman filter → Get pitch/roll error
- PD control calculates ankle correction: `output = KP*error + KD*gyroRate`
- Ankle servos compensate to keep body upright
- Parameters: KP_PITCH=0.40, KD_PITCH=0.06, KP_ROLL=0.55, KD_ROLL=0.05

**Safety Features:**
- Soft-start ramp (2s) prevents jerky startup
- Slew-rate limiting: Hip 12°/s, Knee 8°/s, Ankle 18°/s
- Deadband: ±1° to prevent oscillation
- FSR402 sensors detect foot contact for gait phase detection

### Gait System (Predefined Patterns + Dynamic Balance)
Movement patterns are predefined keyframe sequences, but balance is calculated in real-time:

**Gait Phases (per step cycle):**
```
PHASE_DOUBLE_SUPPORT  → Both feet on ground (stable)
PHASE_RIGHT_SWING     → Right foot lifting, left foot supporting
PHASE_DOUBLE_SUPPORT  → Transfer weight
PHASE_LEFT_SWING      → Left foot lifting, right foot supporting
```

**Predefined Movement Patterns:**
| Command | Pattern Description |
|---------|---------------------|
| FORWARD | Hip pitch sequence + knee lift + weight shift |
| BACKWARD | Reverse hip pitch + backward step |
| TURN_LEFT | Left hip roll out + right step forward + rotate |
| TURN_RIGHT | Right hip roll out + left step forward + rotate |

**Balance During Movement:**
- Each gait phase has target joint angles (keyframes)
- Kalman filter continuously monitors body tilt
- Ankle servos compensate in real-time while following gait pattern
- FSR402 sensors confirm foot placement before weight transfer
- If balance error exceeds threshold → pause movement → recover → continue

### UART Protocol (Walking ↔ User Manager)
```
Walking → User: "STEP:123\n"           # Step count update
Walking → User: "BALANCE:OK\n"         # Balance status
User → Walking: "CMD:FWD\n"            # Forward command
User → Walking: "CMD:BACK\n"           # Backward command
User → Walking: "CMD:LEFT\n"           # Turn left
User → Walking: "CMD:RIGHT\n"          # Turn right
User → Walking: "SPEED:75\n"           # Speed (0-100)
User → Walking: "STOP\n"               # Emergency stop
```

### User Manager ESP32 (`BipedUserManager/`)
**Hardware Components:**
- RFID-RC522 (SPI): User authentication + checkpoint scanning
- OLED SSD1306 0.96" (I2C): Display patient name + step count
- 4 Buttons (GPIO): Forward, Backward, Left, Right, Stop
- Rotary Encoder: Speed adjustment
- WiFi + WiFiManager: HTTP to Dashboard API

**Pin Configuration:**
| Component | Pins |
|-----------|------|
| RFID RC522 | SS=5, RST=4, SCK=18, MISO=19, MOSI=23 |
| OLED I2C | SDA=21, SCL=22 |
| Buttons | FWD=32, BACK=33, LEFT=25, RIGHT=26, STOP=27 |
| Encoder | CLK=34, DT=35, SW=39 |
| UART | TX=17, RX=16 |

**Key Features:**
- **WiFiManager**: Hold FORWARD button 3s → Config portal (`BipedRobot-Setup` / `biped123`)
- **Patient Lookup**: RFID scan → API `/api/patients/by-card/:cardNumber` → Start session
- **Session Toggle**: Same card scan ends session
- **Access Control**: Unregistered cards denied
- **Simple OLED**: Shows only patient name (centered) + step count (large font)

**OLED Display States:**
```
[IDLE]                    [SESSION]
+------------------+      +------------------+
|   BIPED ROBOT    |      |  Nguyen Van A    |  <- Patient name
|------------------|      |------------------|  
| San sang su dung |      |      12345       |  <- Steps (large)
| Quet the bat dau |      |       buoc       |
+------------------+      +------------------+
```

**API Endpoints Used:**
```
GET  /api/patients/by-card/:cardNumber  # Lookup patient by RFID
POST /api/biped/session/start           # Start rehabilitation session
PUT  /api/biped/session/:id/step        # Update step count
POST /api/biped/session/:id/end         # End session
POST /api/biped/location                # Report checkpoint location
```

## Carry Robot Firmware (`Carry_robot/`)

### Key Patterns
- **State Machine** ([StateMachine.h](Carry_robot/src/StateMachine.h)): `STATE_IDLE_AT_MED` → `STATE_WAIT_CARGO` → `STATE_RUN_OUTBOUND` → `STATE_WAIT_AT_DEST` → `STATE_RUN_RETURN`
- **Config-driven**: All pins, constants, and NFC UIDs defined in [config.h](Carry_robot/src/config.h)
- **Modular components**: Each feature isolated (`MecanumDrive`, `GyroTurn`, `NFCReader`, `ObstacleDetector`, `FollowMode`)
- **Robot modes**: `MODE_AUTO` (NFC navigation), `MODE_MANUAL` (RF remote), `MODE_FOLLOW` (AprilTag tracking via ESP32-CAM)

### Hardware Pin Mapping (ESP32 DevKit 38-pin)
- I2C (shared): SDA=21, SCL=22 (OLED SH1106 + VL53L0X + MPU6050)
- SPI (NFC): SCK=18, MISO=19, MOSI=23, SS=5
- Motors: L298N LEFT (EN=17, FL=32/33, RL=25/26), RIGHT (EN=16, FR=27/14, RR=13/4)
- **Warning**: GPIO 1,3 (TX/RX) used for ultrasonics - disconnect when flashing

### NFC Checkpoint System
UIDs are hard-coded in `CHECKPOINT_TABLE[]` in config.h. Node naming: `MED`, `H_MED`, `H_BOT`, `H_TOP`, `J4`, `R{1-4}M{1-3}`, `R{1-4}O{1-3}`, `R{1-4}D{1-2}`

### Build Commands
```bash
cd Carry_robot
pio run                    # Build
pio run -t upload          # Flash
pio device monitor         # Serial monitor (115200 baud)
```

## Backend API (Express.js + MongoDB)

### Route Structure
| Endpoint | Purpose |
|----------|---------|
| `/api/robots/:id/telemetry` | Robot heartbeat (PUT) |
| `/api/missions/next/:robotId` | Poll for pending mission |
| `/api/missions/:id/progress` | Update mission progress |
| `/api/patients` | Patient CRUD with photo uploads || `/api/patients/by-card/:cardNumber` | Lookup patient by RFID card || `/api/maps` | Graph-based floor maps |
| `/api/biped/session/*` | Biped rehabilitation sessions |
| `/api/biped/location` | Biped checkpoint location report |

### BipedSession Schema
```javascript
{
  sessionId: String,        // Unique session ID
  robotId: String,          // Biped robot ID
  userId: String,           // RFID user ID
  userName: String,         // Display name
  patientId: String,        // Linked patient
  startTime: Date,          // Session start
  endTime: Date,            // Session end
  totalSteps: Number,       // Step count from Walking ESP32
  duration: Number,         // Minutes
  status: 'active' | 'completed' | 'interrupted',
  telemetry: {
    avgHeartRate, maxHeartRate, minHeartRate,
    caloriesBurned, distanceWalked
  }
}
```

### Key Models
- `Robot` - tracks status, battery, location (type: `carry` or `biped`)
- `TransportMission` - full route with `outboundRoute[]` and `returnRoute[]`
- `BipedSession` - rehabilitation session (user, steps, duration)
- `MapGraph` - nodes/edges for pathfinding

### Conventions
- Robot types: `carry`, `biped`
- Robot status values: `idle`, `busy`, `charging`, `maintenance`, `offline`, `low_battery`, `follow`, `manual`, `waiting`
- Bed ID format: `R{room}{M|O}{1-3}` (e.g., `R1M2` = Room 1, M-side, bed 2)

## Frontend (React + Vite + TypeScript)

### Structure
- `src/app/api/` - API service layer (mirrors backend routes)
- `src/app/components/` - Feature components (`PatientDashboard`, `RobotManagement`, `BedMap`)
- `src/app/types/` - TypeScript interfaces (`robot.ts`, `patient.ts`)

### Start Development
```bash
cd "Hospital Dashboard"
# Option 1: Use batch script
start-servers.bat

# Option 2: Manual
cd Backend && npm run dev    # Port 3000
cd Frontend && npm run dev   # Port 5173 (proxies /api to backend)
```

## Common Tasks

### Adding a New NFC Checkpoint
1. Add entry to `CHECKPOINT_TABLE[]` in [config.h](Carry_robot/src/config.h)
2. Update `MapGraph` in MongoDB via seed script or API

### Adding New Carry Robot Feature
1. Create `FeatureName.h/cpp` in `Carry_robot/src/`
2. Include in `main.cpp`, add timing variable if needed
3. Integrate with `StateMachine` if affects mission flow

### Adding New Biped Servo/Gait
1. Update servo indices in [config.h](BipedRobot/config.h)
2. Modify [Kinematics.h](BipedRobot/Kinematics.h) for leg position calculations
3. Tune PID gains in config.h for balance

### Modifying Mission Routes
Routes are computed in [missions.js](Hospital%20Dashboard/Backend/src/routes/missions.js) using `buildAdj()` for graph traversal. Hard-coded routes exist for the 4-room hospital layout.

## Critical Notes

- **Never use GPIO 1/3** for peripherals during development (serial conflict)
- **Motor gain tuning**: `LEFT_GAIN`/`RIGHT_GAIN` in config.h for straight-line correction
- **Gyro calibration**: 500 samples at startup - robot must be stationary
- **API base URL**: Stored in NVS, configurable via WiFiManager portal
