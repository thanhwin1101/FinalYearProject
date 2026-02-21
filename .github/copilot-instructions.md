# Hospital Robot System

## System Overview

This is a **hospital rehabilitation and medication delivery system** with five integrated components:
1. **Biped Robot** (`BipedRobot/`) — 10-servo bipedal walking robot (ESP32 Walking Controller) for patient rehabilitation assistance
2. **Biped User Manager** (`BipedUserManager/`) — ESP32 module handling RFID authentication, OLED display, buttons, and WiFi communication for the Biped Robot
3. **Carry Robot** (`CarryRobot/carry_cpp/`) — ESP32 transport robot with Mecanum wheels for medication delivery via NFC checkpoint navigation
4. **ESP32-CAM** (`Carry_robot_CAM/`) — Camera module for AprilTag-based follow mode, communicating with Carry Robot via ESP-NOW
5. **Hospital Dashboard** (`Hospital Dashboard/`) — Full-stack web app (Express.js + React/Vite + MongoDB) for patient management, robot monitoring, and mission control

## Architecture and Data Flow

```
[Dashboard Frontend:5173] <──Vite Proxy──> [Backend API:3000] <──Mongoose──> [MongoDB]
                                                  │
                                    ┌─────────────┼──────────────────┐
                                    │ MQTT (PubSubClient)            │ HTTP REST (WiFi)
                                    ▼                                ▼
                           [Carry Robot ESP32]              [Biped User Manager ESP32]
                             NFC navigation                   RFID + OLED + Buttons
                                    │                                │
                              ESP-NOW                          UART (Serial2)
                                    ▼                                ▼
                           [ESP32-CAM Module]              [Biped Walking Controller ESP32]
                           AprilTag detection                 Servos + IMU + Balance
```

### Main Workflows
- **Rehabilitation Session**: Patient scans RFID on Biped → User Manager calls API `/api/patients/by-card/:cardNumber` → Dashboard logs session → Biped tracks steps via UART → Session ends on re-scan
- **Medication Delivery**: Dashboard creates delivery mission via `/api/missions/delivery` → Backend computes hard-coded routes → MQTT publishes `mission/assign` to Carry Robot → Robot navigates via NFC checkpoints → Delivers to bed → Returns to MED station
- **Biped Transport**: Biped scans checkpoint RFID → User Manager reports location via API → API triggers `POST /api/missions/biped-pickup` → Carry Robot fetches Biped
- **Follow Mode**: ESP32-CAM detects AprilTag markers → Sends position data via ESP-NOW to Carry Robot → Robot follows target

### Communication Protocols
- **Biped Walking Controller ↔ User Manager**: UART Serial2 (GPIO 16/17, 115200 baud)
- **User Manager → Dashboard**: WiFi HTTP REST API (HTTPClient)
- **Carry Robot ↔ Dashboard**: MQTT over WiFi (PubSubClient, Mosquitto broker port 1883)
- **ESP32-CAM → Carry Robot**: ESP-NOW wireless (12-byte packed struct at 20Hz)

---

## Carry Robot Firmware (`CarryRobot/carry_cpp/`)

### Platform and Libraries

| Setting | Value |
|---------|-------|
| Platform | `espressif32` (ESP32 DevKit 38-pin) |
| Framework | Arduino |
| Monitor/Upload Speed | 115200 / 921600 baud |
| Build flag | `-DCORE_DEBUG_LEVEL=0` |

| Library | Version | Purpose |
|---------|---------|---------|
| `tzapu/WiFiManager` | ^2.0.17 | WiFi config portal |
| `bblanchon/ArduinoJson` | ^6.21.3 | JSON serialization |
| `adafruit/Adafruit PN532` | ^1.3.0 | NFC reader |
| `pololu/VL53L0X` | ^1.3.1 | Time-of-Flight distance sensor |
| `olikraus/U8g2` | ^2.35.7 | OLED display (SH1106 128x64) |
| `knolleary/PubSubClient` | ^2.8 | MQTT client |

### Hardware Pin Mapping

| Pin | Component | Function |
|-----|-----------|----------|
| GPIO 21 | I2C SDA | Shared: OLED SH1106, VL53L0X, MPU6050 |
| GPIO 22 | I2C SCL | Shared: OLED SH1106, VL53L0X, MPU6050 |
| GPIO 18 | SPI SCK | PN532 NFC clock |
| GPIO 19 | SPI MISO | PN532 NFC data in |
| GPIO 23 | SPI MOSI | PN532 NFC data out |
| GPIO 5 | SPI SS | PN532 NFC chip select |
| GPIO 17 | EN_LEFT | Left motors enable (PWM) |
| GPIO 32 | FL_IN1 | Front-left motor direction 1 |
| GPIO 33 | FL_IN2 | Front-left motor direction 2 |
| GPIO 25 | RL_IN1 | Rear-left motor direction 1 |
| GPIO 26 | RL_IN2 | Rear-left motor direction 2 |
| GPIO 16 | EN_RIGHT | Right motors enable (PWM) |
| GPIO 27 | FR_IN1 | Front-right motor direction 1 |
| GPIO 14 | FR_IN2 | Front-right motor direction 2 |
| GPIO 13 | RR_IN1 | Rear-right motor direction 1 |
| GPIO 4 | RR_IN2 | Rear-right motor direction 2 |
| GPIO 1 | TRIG_LEFT | Left ultrasonic trigger (TX pin - disconnect when flashing) |
| GPIO 34 | ECHO_LEFT | Left ultrasonic echo (input only) |
| GPIO 3 | TRIG_RIGHT | Right ultrasonic trigger (RX pin - disconnect when flashing) |
| GPIO 35 | ECHO_RIGHT | Right ultrasonic echo (input only) |
| GPIO 15 | CARGO_SWITCH | Cargo presence switch (active LOW = loaded) |
| GPIO 2 | BUZZER | Piezo buzzer via LEDC PWM channel 2 |

### Robot Identity and WiFi

| Constant | Value |
|----------|-------|
| `ROBOT_ID` | `"CARRY-01"` |
| `SERIAL_DEBUG` | `0` (disabled because TX/RX used by ultrasonics) |
| WiFi Portal SSID | `"CarryRobot-Setup"` |
| WiFi Portal Password | `"carry123"` |
| Portal Timeout | 180 seconds |
| Connect Timeout | 25 seconds |
| Config Reset Hold | 5000ms |
| NVS Namespace | `"carrycfg"` |
| NVS Key for MQTT Server | `"mqtt_server"` |

### MQTT Configuration

| Constant | Value |
|----------|-------|
| `MQTT_DEFAULT_SERVER` | `"192.168.0.102"` |
| `MQTT_DEFAULT_PORT` | `1883` |
| `MQTT_DEFAULT_USER` | `"hospital_robot"` |
| `MQTT_DEFAULT_PASS` | `"123456"` |
| `MQTT_RECONNECT_MS` | `5000` |
| Buffer Size | 2048 bytes |
| Client ID Format | `"CarryRobot-CARRY-01-{random hex}"` |

### MQTT Topics

| Topic Pattern (%s = ROBOT_ID) | Direction | Purpose |
|-------------------------------|-----------|---------|
| `hospital/robots/%s/telemetry` | Publish | Periodic heartbeat (every 2s) |
| `hospital/robots/%s/mission/assign` | Subscribe | Receive new mission |
| `hospital/robots/%s/mission/progress` | Publish | Report checkpoint progress |
| `hospital/robots/%s/mission/complete` | Publish | Report delivery complete |
| `hospital/robots/%s/mission/returned` | Publish | Report returned to MED |
| `hospital/robots/%s/mission/cancel` | Subscribe | Receive cancellation |
| `hospital/robots/%s/mission/return_route` | Subscribe | Receive backend-computed return route |
| `hospital/robots/%s/position/waiting_return` | Publish | Report position when waiting for return route |
| `hospital/robots/%s/command` | Subscribe | Remote commands (stop/resume) |

### Telemetry Payload (Published)

```json
{
  "robotId": "CARRY-01",
  "name": "Carry-01",
  "type": "carry",
  "batteryLevel": 100,
  "firmwareVersion": "carry-mqtt-v1",
  "status": "idle|busy",
  "mqttConnected": true,
  "currentNodeId": "MED|R4M2|...",
  "destBed": "R4M2"
}
```

### Motor Control Parameters

| Constant | Value | Purpose |
|----------|-------|---------|
| `INVERT_LEFT` | `true` | Reverse left motor polarity |
| `INVERT_RIGHT` | `true` | Reverse right motor polarity |
| `PWM_FWD` | `165` | Forward PWM duty (0-255) |
| `PWM_TURN` | `168` | Full-speed turn PWM |
| `PWM_BRAKE` | `150` | Brake reverse PWM |
| `BRAKE_FORWARD_MS` | `80` | Forward brake duration (ms) |
| `TURN_90_MS` | `974` | Time for 90 degree turn |
| `TURN_180_MS` | `1980` | Time for 180 degree U-turn |
| `PWM_TURN_SLOW` | `120` | Slow zone turn PWM |
| `PWM_TURN_FINE` | `90` | Fine zone turn PWM |
| `SLOW_ZONE_RATIO` | `0.25` | Last 25% of turn is slow |
| `FINE_ZONE_RATIO` | `0.10` | Last 10% of turn is fine |
| `leftGain` | `1.00` | Left motor straight-line gain |
| `rightGain` | `1.011` | Right motor straight-line gain |
| `MOTOR_PWM_FREQ` | `20000` Hz | PWM frequency |
| `MOTOR_PWM_RES` | `8` bit | PWM resolution (0-255) |

#### Turn Speed Profile
```
|--- Full speed (168) 75% ---|--- Slow zone (120) 15% ---|--- Fine zone (90) 10% ---|
```

### Motor Control Functions

| Function | Description |
|----------|-------------|
| `motorPwmInit()` | Configure LEDC PWM channels (20kHz, 8-bit). Supports ESP32 Core v2 and v3 |
| `motorsStop()` | All 8 direction pins LOW, both enables to 0 |
| `driveForward(pwm)` | Apply gain to both sides, set all 4 wheels forward |
| `driveBackward(pwm)` | Reverse direction |
| `applyForwardBrake(brakePwm=150, brakeMs=80)` | Active braking via reverse thrust |
| `applyHardBrake(wasTurningLeft, brakePwm, brakeMs=50)` | Counter-rotation brake after turn |
| `rotateByTime(totalMs, isLeft)` | Time-based turn with 3-zone speed profile |
| `turnByAction(a)` | `'L'` = 90 degree left, `'R'` = 90 degree right, `'B'` = 180 degree U-turn |

### Obstacle Detection (VL53L0X Time-of-Flight)

| Constant | Value | Purpose |
|----------|-------|---------|
| `OBSTACLE_MM` / `TOF_STOP_DIST` | `220` mm | Stop distance |
| `OBSTACLE_RESUME_MM` / `TOF_RESUME_DIST` | `300` mm | Resume distance |
| `TOF_INTERVAL` | `50` ms | Read interval |
| `OBSTACLE_BEEP_PERIOD_MS` | `600` ms | Beep interval when blocked |
| Measurement timing budget | `20000` us (20ms) | VL53L0X setting |

### Timing Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `TELEMETRY_INTERVAL` | `2000` ms | Telemetry publish interval |
| `OLED_MS` | `200` ms | OLED refresh interval |
| `WEB_OK_SHOW_MS` | `3000` ms | Show "web OK" display duration |
| `WEB_OK_ALIVE_MS` | `10000` ms | Web connection alive threshold |
| `SWITCH_DEBOUNCE_MS` | `60` ms | Cargo switch debounce |
| `NFC_REPEAT_GUARD_MS` | `700` ms | NFC read guard after turn |
| `RETURN_ROUTE_TIMEOUT_MS` | `5000` ms | Max wait for backend return route before fallback |

### State Machine

```
enum RunState { IDLE_AT_MED, RUN_OUTBOUND, WAIT_AT_DEST, RUN_RETURN, WAIT_FOR_RETURN_ROUTE };
```

#### State Transitions

```
IDLE_AT_MED ──(MQTT mission/assign)──> RUN_OUTBOUND
                                           |
                     (NFC checkpoint matches expected UID)
                                           |
                     +─(cancel pending)────+
                     v                     v
          WAIT_FOR_RETURN_ROUTE    (reached last outbound node)
                     |                     |
       (MQTT return_route /                v
        5s timeout fallback)        WAIT_AT_DEST
                     |                     |
                     v        (cargo button / cancel)
                RUN_RETURN <───────────────+
                     |
           (reached MED / H_MED / last return node)
                     v
                IDLE_AT_MED
```

#### State Machine Functions

| Function | Description |
|----------|-------------|
| `startOutbound()` | State = `RUN_OUTBOUND`. Reset `routeIndex=0`, clear flags, ignore NFC 600ms, drive forward |
| `enterWaitAtDest()` | State = `WAIT_AT_DEST`. Stop motors. Play arrived beep (3x 140ms 1800Hz) |
| `startReturn(note, doUturn)` | State = `RUN_RETURN`. If `retRoute` less than 2 nodes then reverse outbound as fallback. U-turn if needed. Drive forward |
| `goIdleReset()` | State = `IDLE_AT_MED`. Stop motors. Clear all mission data |

### NFC Checkpoint Logic (`handleCheckpointHit(uid)`)

1. **HOME_MED_UID** check (`"04381AD2060000"` = H_MED):
   - If `RUN_RETURN` then `sendReturned()` + `goIdleReset()` + beep
   - If `IDLE_AT_MED` then short beep (already home)
2. **Node lookup**: `uidLookupByUid(uid)` (39-entry table)
3. **UID matching**: Compare scanned UID against `expectedNextUid()` (next route point)
4. **On match**: Active brake, `routeIndex++`, `sendProgress()`, beep
5. **Cancel handling** (outbound only): If `cancelPending` then brake, U-turn, send `positionWaitingReturn`, state = `WAIT_FOR_RETURN_ROUTE`
6. **Turn execution**: If action is `'L'` or `'R'` then OLED turn overlay, beep, `turnByAction()`, NFC guard 700ms
7. **Destination reached** (outbound, last node): Brake, U-turn, `enterWaitAtDest()`
8. **Home reached** (return, last node): `sendReturned()`, `goIdleReset()`, beep

### Route Logic Functions

| Function | Description |
|----------|-------------|
| `currentRoute()` | Returns `retRoute` if `RUN_RETURN`, else `outbound` |
| `expectedNextUid()` | Returns `rfidUid` of `route[routeIndex + 1]` |
| `currentNodeIdSafe()` | Returns `nodeId` of `route[routeIndex]` |
| `upcomingTurnAtNextNode()` | Returns action char for next node |
| `buildReturnFromVisited()` | Reverses visited portion of outbound, inverts turns (L to R and R to L), stores in `retRoute` |

### RoutePoint Structure

```cpp
struct RoutePoint {
  String nodeId;    // Node name (e.g., "R4M1")
  String rfidUid;   // UID hex string for matching
  float x, y;       // Coordinates
  char action;      // 'F'=forward, 'L'=left, 'R'=right, 'B'=U-turn
};
```

### MQTT Message Parsers (Incoming)

| Parser | Topic | Behavior |
|--------|-------|----------|
| `parseMissionPayload()` | `mission/assign` | Parse `missionId`, `patientName`, `destBed`, `outboundRoute[]`, `returnRoute[]`. Only accepts in `IDLE_AT_MED` |
| `parseCancelPayload()` | `mission/cancel` | Sets `cancelPending=true` if `RUN_OUTBOUND`. Matches by `missionId` |
| `parseReturnRoutePayload()` | `mission/return_route` | Parse backend return route. Fallback to `buildReturnFromVisited()` if invalid |
| `parseCommandPayload()` | `command` | `"stop"` = motorsStop + obstacleHold. `"resume"` = clear hold |

### Complete NFC UID Map (39 entries)

| Node | UID | Description |
|------|-----|-------------|
| `MED` | `5378CF0F` | Medicine station (home base) |
| `H_MED` | `04381AD2060000` | Hallway near MED |
| `H_BOT` | `042A1AD2060000` | Hallway bottom |
| `H_TOP` | `041019D2060000` | Hallway top |
| `J1` | `B388930E` | Junction 1 |
| `J2` | `04DF17D2060000` | Junction 2 |
| `J3` | `043F1AD2060000` | Junction 3 |
| `J4` | `04221AD2060000` | Junction 4 |
| `R1M1`-`R1O3` | 6 entries | Room 1 beds (M-side: `046817D2060000`, `046017D2060000`, `045017D2060000`; O-side: `045817D2060000`, `044817D2060000`, `044017D2060000`) |
| `R1D1`-`R1D2` | 2 entries | Room 1 doors (`04F618D2060000`, `04EE18D2060000`) |
| `R2M1`-`R2O3` | 6 entries | Room 2 beds |
| `R2D1`-`R2D2` | 2 entries | Room 2 doors |
| `R3M1`-`R3O3` | 6 entries | Room 3 beds |
| `R3D1`-`R3D2` | 2 entries | Room 3 doors |
| `R4M1`-`R4O3` | 6 entries | Room 4 beds |
| `R4D1`-`R4D2` | 2 entries | Room 4 doors |

### Node Naming Convention
- **MED**: Medicine station (home base)
- **H_MED, H_BOT, H_TOP**: Hallway corridor checkpoints
- **J1-J4**: Junctions (hallway intersections)
- **R{1-4}M{1-3}**: Room beds, M-side (3 beds per room)
- **R{1-4}O{1-3}**: Room beds, O-side (3 beds per room)
- **R{1-4}D{1-2}**: Room doors (2 doors per room)
- **Total**: 40 nodes (4 rooms x 8 + 3 hallway + 4 junctions + 1 MED)

### OLED Display (SH1106 128x64 via I2C)

Font: `u8g2_font_6x12_tr` (standard), `u8g2_font_10x20_tr` (turn overlay)

| Function | Display Content |
|----------|-----------------|
| `displayInit()` | "Carry Robot / Initializing..." |
| `drawCentered(l1,l2,l3,l4)` | Up to 4 centered text lines, 14px spacing |
| `drawState(state, info)` | "State:" + state name |
| `drawRouteProgress(phase,idx,total,node)` | "OUTBOUND 3/8" + "-> R4M2" |
| `drawMissionInfo(id, dest, status)` | Mission ID (first 8 chars) + destination + status |
| `drawWaitingCargo(id, dest)` | "WAITING CARGO" + destination + "Press button" |
| `drawWaitingForReturnRoute()` | "CANCELLED / Waiting for / return route..." |
| `showTurnOverlay(direction)` | Large "TURN LEFT/RIGHT/UTURN" |

### Buzzer Patterns

| Pattern | Frequency | Duration | Meaning |
|---------|-----------|----------|---------|
| Standard beep | 2200 Hz | 80ms | Default action |
| Startup beep | 2200 Hz | 120ms | Boot complete |
| MQTT connect | 2400 Hz | 60ms | MQTT connected |
| Checkpoint hit | 2200 Hz | 60ms | NFC checkpoint matched |
| Arrived pattern | 1800 Hz | 3x 140ms + 90ms gaps | Reached destination |

### Sensor Functions

| Function | Description |
|----------|-------------|
| `nfcInit()` | Initialize PN532 via SPI. SAMConfig(). Continues even if not found |
| `readNFC(uid*, uidLen*)` | Read ISO14443A tag with 50ms timeout |
| `tofInit()` | Initialize VL53L0X with 500ms timeout, 20ms measurement budget |
| `tofReadDistance(dist&)` | Single-shot range read |

### Setup Initialization Order
1. `Serial.begin(115200)`
2. `initPins()` - All GPIO configuration
3. `motorPwmInit()` / `motorsStop()` - LEDC PWM channels
4. `displayInit()` - OLED splash screen
5. `nfcInit()` - PN532 NFC reader
6. `tofInit()` - VL53L0X distance sensor
7. Load MQTT server from NVS (`"carrycfg"` namespace, `"mqtt_server"` key)
8. `wifiInit()` - WiFiManager autoConnect
9. `mqttInit()` - PubSubClient setup + subscribe
10. `goIdleReset()` - Set `IDLE_AT_MED`
11. Startup beep (120ms, 2200Hz)

### Main Loop Order
1. `mqttLoop()` - Reconnect + process messages
2. `checkObstacle()` - VL53L0X read every 50ms
3. `processNFC()` - Read NFC tags (skipped if obstacleHold)
4. `checkCargoButton()` - Cargo switch in `WAIT_AT_DEST` state
5. `checkReturnRouteTimeout()` - 5s timeout fallback in `WAIT_FOR_RETURN_ROUTE`
6. `sendPeriodicTelemetry()` - Every 2s
7. OLED update - Every 1s in idle
8. `delay(5)` - Yield

---

## Biped Robot Walking Controller (`BipedRobot/`)

### Hardware
- **ESP32** with PCA9685 I2C PWM driver (address `0x40`)
- **MPU6500** IMU for balance (I2C, default address)
- **10 servos** (5 per leg) via PCA9685

### Servo Configuration (PCA9685)

| Index | Joint Name | Pin | Zero Offset (degrees) | Inverted | Function |
|-------|------------|-----|-----------------------|----------|----------|
| 0 | `HIP_PITCH_L` | 0 | 40.50 | No | Left leg swing forward/backward |
| 1 | `HIP_ROLL_L` | 1 | 56.00 | No | Left leg lateral movement |
| 2 | `KNEE_PITCH_L` | 2 | 49.00 | No | Left knee bend |
| 3 | `ANKLE_PITCH_L` | 3 | 53.50 | No | Left foot tilt front/back |
| 4 | `ANKLE_ROLL_L` | 4 | 55.50 | No | Left foot tilt left/right |
| 5 | `HIP_PITCH_R` | 5 | 63.50 | **Yes** | Right leg swing (inverted) |
| 6 | `HIP_ROLL_R` | 6 | 88.50 | No | Right leg lateral |
| 7 | `KNEE_PITCH_R` | 7 | 53.50 | **Yes** | Right knee (inverted) |
| 8 | `ANKLE_PITCH_R` | 8 | 57.00 | **Yes** | Right foot pitch (inverted) |
| 9 | `ANKLE_ROLL_R` | 9 | 59.00 | No | Right foot roll |

### Servo PWM
| Setting | Value |
|---------|-------|
| `SERVO_FREQ` | 50 Hz |
| `SERVOMIN_PULSE` | 150 (pulse for 0 degrees) |
| `SERVOMAX_PULSE` | 600 (pulse for 180 degrees) |
| PCA9685 Oscillator | 27 MHz |

### Physical Dimensions (mm)
| Segment | Define | Length |
|---------|--------|--------|
| Thigh | `THIGH_LENGTH_MM` (L1) | 60 mm |
| Shank | `SHANK_LENGTH_MM` (L2) | 70 mm |
| Ankle Height | `ANKLE_HEIGHT_MM` (L3) | 62 mm |
| Foot | `FOOT_LENGTH_MM` (L4) | 80 mm |

### Joint Limits (degrees, 0 degrees = standing straight)

| Joint | Min | Max |
|-------|-----|-----|
| Hip Pitch | -45 | +45 |
| Knee Pitch | -5 | +140 |
| Ankle Pitch | -30 | +30 |
| Hip Roll | -30 | +30 |
| Ankle Roll | -30 | +30 |

### Inverse Kinematics (`Kinematics.h`)

**`calculateIK_3D(leg, x, y, z, ankle_pitch_rad, ankle_roll_rad)`** - 5-DOF IK per leg:

1. **Hip Roll**: `atan2(z, -y)` - Solve in frontal plane
2. **Effective leg length**: `sqrt(y*y + z*z)`
3. **Reach check**: `D = sqrt(x*x + (Leff - L3)*(Leff - L3))` must be less than or equal to L1 + L2
4. **Knee pitch**: Law of cosines: `PI - acos((L1*L1 + L2*L2 - D*D) / (2*L1*L2))`
5. **Hip pitch**: `atan2(x, Leff - L3) - acos((L1*L1 + D*D - L2*L2) / (2*L1*D))`
6. **Ankle pitch**: Compensation = -hip - knee + active_input
7. Constrain all angles to limits

Parameters: `x` = forward/backward, `y` = up/down (negative), `z` = left/right (mm)

### LegTarget Struct (prepared for future gait use)
```cpp
struct LegTarget { float x, y, z, footPitchRad, footRollRad; };
```

### Kalman Filter (`Kalman.h`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `Q_angle` | 0.0007 | Gyro angle noise covariance |
| `Q_bias` | 0.005 | Gyro bias noise covariance |
| `R_measure` | 0.09 | Accelerometer measurement noise |

Algorithm: Predict (angle += rate * dt) then Update (Kalman gain from P matrix) then Fuse accelerometer reading

### PID Controller (`PID.h`)

```cpp
class PID {
  float kp, ki, kd;
  float out_min = -255, out_max = 255;
  // Uses derivative-on-measurement (not error) to avoid derivative kick
  float compute(float setpoint, float input, float dt);
};
```
**Note**: This class is defined but **not used** in the current code. The main loop uses inline PD calculations instead.

### Balance System (50Hz Control Loop)

#### IMU Configuration
| Setting | Value |
|---------|-------|
| Accelerometer Range | plus/minus 2G |
| Gyroscope Range | plus/minus 250 degrees/s |
| Control Loop Rate | 50Hz (20ms interval) |

#### Filter Pipeline
1. Read MPU6500 accelerometer, compute `pitchAcc`, `rollAcc` from `atan2`
2. Read gyroscope: `gyro.x` (roll rate), `gyro.y` (pitch rate) in deg/s
3. Kalman filter fusion: `pitch = kPitch.getAngle(pitchAcc, gyro.y, dt)`
4. Low-pass filter: `pitchFilt = current + alpha * (target - current)`, tau = 0.12s

#### Calibration
- `calibrateReferenceAngles()`: 80 samples at 10ms intervals (0.8s), averages to get `pitchRef` / `rollRef`

#### PD Balance Gains

| Parameter | Value | Tuning Range | Purpose |
|-----------|-------|--------------|---------|
| `KP_PITCH` | 0.40 | 0.25-0.60 | Pitch proportional gain |
| `KD_PITCH_RATE` | 0.06 | 0.03-0.10 | Pitch derivative (gyro rate) damping |
| `KP_ROLL` | 0.55 | - | Roll proportional gain |
| `KD_ROLL_RATE` | 0.05 | - | Roll derivative damping |

#### Correction Limits

| Limit | Value |
|-------|-------|
| `MAX_HIP_PITCH_CORR` | plus/minus 6 degrees |
| `MAX_KNEE_PITCH_CORR` | plus/minus 2 degrees |
| `MAX_HIP_ROLL_CORR` | plus/minus 8 degrees |
| `MAX_ANKLE_PITCH_CORR` | plus/minus 10 degrees |
| `KNEE_PITCH_GAIN` | 0.10 (10% of pitch correction) |

#### Slew-Rate Limits (degrees/s)

| Joint | Max Rate |
|-------|----------|
| Hip Pitch | 12 deg/s |
| Knee Pitch | 8 deg/s |
| Hip Roll | 18 deg/s |
| Ankle Pitch | 18 deg/s |

#### Safety Features
- **Deadband**: plus/minus 1 degree (`DEADBAND_DEG`) - zero correction within deadband
- **Soft-start**: 2.0s ramp (`SOFTSTART_S`) - `strength` 0.0 to 1.0 over 2s
- **PWM threshold**: Only update servo if change >= 0.20 degrees (`SEND_EPS_DEG`)
- **Ankle roll**: Always forced to 0 degrees (no active roll correction)

#### Ankle Foot-Leveling (Blended Mode)

| Parameter | Value | Description |
|-----------|-------|-------------|
| `ANKLE_DIR` | -1.0 | Ankle direction inversion |
| `ANKLE_LEVEL_GAIN` | 0.55 | Foot-leveling strength |
| `LEVEL_WINDOW_DEG` | 6 | Within plus/minus 6 degrees: 100% leveling mode |
| `LEVEL_FADE_DEG` | 8 | 6-14 degrees: linear blend to recovery |
| `KP_ANKLE_REC` | 0.25 | Recovery PD proportional gain |
| `KD_ANKLE_RATE_REC` | 0.04 | Recovery PD derivative gain |

Within plus/minus 6 degrees tilt: ankle prioritizes keeping foot level. Beyond 14 degrees: ankle switches to recovery mode (actively correct tilt).

#### `updateControl(dt)` - Main Balance Loop

1. **Soft-start**: `strength = clamp(elapsed / 2.0, 0, 1)`
2. **Read sensors**: `readPitchRoll()` using Kalman + low-pass filtered angles
3. **Error**: `pitchErr = pitchRef - pitch`, `rollErr = rollRef - roll`
4. **Deadband**: Zero error if |error| less than 1 degree
5. **PD Hip Pitch**: `corrPitch = strength * (KP * error - KD * rate)`, clamped plus/minus 6 degrees
6. **PD Hip Roll**: `corrRoll = strength * (KP * error - KD * rate)`, clamped plus/minus 8 degrees
7. **Knee**: `corrKnee = corrPitch * 0.10`, clamped plus/minus 2 degrees
8. **Slew-rate limit** all corrections
9. **Ankle blended**:
   - Level term: `(pitchRef - pitch - hipP - kneeP) * 0.55 * -1`
   - Recovery term: `(0.25 * error - 0.04 * rate) * -1`
   - Blend: within plus/minus 6 degrees = 100% level; 6-14 degrees = linear; more than 14 degrees = 100% recovery
10. **Apply to servos** (both L and R symmetrically, except hip roll is independent)
11. **Debug output**: Every 10th cycle on USB Serial

### UART Protocol (Walking Controller to/from User Manager)

**UART Configuration**: Serial2, GPIO RX=16, TX=17, 115200 baud, 64-byte buffer

#### Incoming Commands (User Manager to Walking Controller)

| Message | Action | Response |
|---------|--------|----------|
| `CMD:FWD` | Set `CMD_FORWARD`, increment `stepCount` | `ACK:FWD` |
| `CMD:BACK` | Set `CMD_BACKWARD`, increment `stepCount` | `ACK:BACK` |
| `CMD:LEFT` | Set `CMD_LEFT` | `ACK:LEFT` |
| `CMD:RIGHT` | Set `CMD_RIGHT` | `ACK:RIGHT` |
| `STOP` | Set `CMD_STOP` | `ACK:STOP` |
| `SPEED:xx` | Set `moveSpeed` (0-100) | `ACK:SPEED` |
| `BALANCE:ON` | Enable balance, recalibrate | `BALANCE:OK` |
| `BALANCE:OFF` | Disable balance, stand straight | `BALANCE:OFF` |
| `CALIBRATE` | Recalibrate reference angles | `STATUS:CALIBRATED` |
| `STATUS` | Report current state | Step count + balance status |

#### Outgoing Messages (Walking Controller to User Manager)

| Message | Description |
|---------|-------------|
| `STEP:<count>` | Step count (sent every 1s if changed) |
| `BALANCE:OK` | Balance error less than 5 degrees |
| `BALANCE:WARN` | Balance error 5-10 degrees |
| `BALANCE:ERROR` | Balance error more than 10 degrees |
| `BALANCE:OFF` | Balance disabled |
| `STATUS:READY` | System initialized |
| `STATUS:CALIBRATED` | Calibration complete |
| `ERROR:IMU_FAIL` | IMU initialization failed |
| `ERROR:UNKNOWN_CMD` | Unrecognized command |

#### USB Serial Debug Commands
| Key | Action |
|-----|--------|
| `b` / `B` | Toggle `balanceEnabled` |
| `r` / `R` | Recalibrate reference angles |

### Setup Sequence
1. `Serial.begin(115200)` - USB debug
2. `Wire.begin()` - I2C bus
3. `Serial2.begin(115200, 8N1, RX=16, TX=17)` - UART to User Manager
4. `servoCtrl.begin()` - PCA9685 init
5. `standStraight()` - All servos to 0 degrees with 60ms delay each
6. `initIMU()` - MPU6500 init, auto-offsets, plus/minus 2G/250 deg/s, seed Kalman filters. Halts + sends `ERROR:IMU_FAIL` on failure
7. `calibrateReferenceAngles()` - 80-sample calibration
8. Send `STATUS:READY` + balance status to User Manager

---

## Biped User Manager (`BipedUserManager/`)

### Hardware

| Component | Pins | Protocol |
|-----------|------|----------|
| RFID RC522 | SS=GPIO 5, RST=GPIO 4, SCK=18, MISO=19, MOSI=23 | SPI |
| OLED SSD1306 0.96" | SDA=GPIO 21, SCL=GPIO 22, Address=0x3C | I2C |
| Button Forward | GPIO 32 | INPUT_PULLUP |
| Button Backward | GPIO 33 | INPUT_PULLUP |
| Button Left | GPIO 25 | INPUT_PULLUP |
| Button Right | GPIO 26 | INPUT_PULLUP |
| Button Stop | GPIO 27 | INPUT_PULLUP |
| Encoder CLK | GPIO 34 | Input (needs external pullup) |
| Encoder DT | GPIO 35 | Input (needs external pullup) |
| Encoder SW | GPIO 39 | INPUT_PULLUP |
| UART TX | GPIO 17 | Serial2 TX to Walking RX |
| UART RX | GPIO 16 | Serial2 RX from Walking TX |
| Status LED | GPIO 2 | Built-in LED |

### Libraries
| Library | Purpose |
|---------|---------|
| `MFRC522` (miguelbalboa) | RFID RC522 reader |
| `U8g2` (olikraus) | OLED SSD1306 |
| `ArduinoJson` | JSON for API |
| `WiFiManager` (tzapu) | Config portal |
| `Preferences` | NVS storage |
| `HTTPClient` | HTTP REST calls |

### Identity and Configuration

| Constant | Value |
|----------|-------|
| `ROBOT_ID` | `"BIPED-001"` |
| `ROBOT_NAME` | `"Biped Robot 1"` |
| `ROBOT_TYPE` | `"biped"` |
| WiFi Portal SSID | `"BipedRobot-Setup"` |
| WiFi Portal Password | `"biped123"` |
| Portal Timeout | 300 seconds |
| Default API URL | `"http://192.168.1.100:3000/api"` |
| API Timeout | 5000 ms |
| NVS Namespace | `"biped"` |
| NVS Keys | `"ssid"`, `"pass"`, `"api"` |

### Timing Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `DEBOUNCE_MS` | 50 ms | Button debounce |
| `RFID_SCAN_INTERVAL` | 500 ms | RFID polling rate |
| `HEARTBEAT_INTERVAL` | 5000 ms | API heartbeat |
| `STEP_UPDATE_INTERVAL` | 2000 ms | Step count push to API |
| `DISPLAY_UPDATE_INTERVAL` | 200 ms | OLED refresh |
| `WIFI_SETUP_HOLD_TIME` | 3000 ms | Forward button hold to trigger WiFiManager |

### Speed Control

| Constant | Value |
|----------|-------|
| `SPEED_MIN` | 10 |
| `SPEED_MAX` | 100 |
| `SPEED_DEFAULT` | 50 |
| `SPEED_STEP` | 5 |

Encoder ISR increments/decrements `encoderPos` (range 2-20), mapped to speed via `pos * SPEED_STEP`.

### State Machine

```cpp
enum SystemState { STATE_IDLE, STATE_SESSION_ACTIVE, STATE_ERROR };
```

### Data Structures

```cpp
struct UserInfo { char cardUid[20]; char patientId[32]; char userName[48]; char roomBed[16]; bool isValid; };
struct SessionData { char sessionId[32]; char userId[16]; char userName[32]; char patientId[16]; uint32_t stepCount; uint32_t startTime; bool isActive; };
struct CheckpointInfo { byte uid[4]; const char* checkpointId; const char* description; };
struct ButtonState { int pin; bool lastState; unsigned long lastDebounce; bool pressed; };
```

### Hardcoded Checkpoint Database (6 entries)

| UID (bytes) | Checkpoint ID | Description |
|-------------|---------------|-------------|
| C1 C2 C3 C4 | `CP_LOBBY` | Sanh chinh |
| D1 D2 D3 D4 | `CP_R1` | Phong 1 |
| E1 E2 E3 E4 | `CP_R2` | Phong 2 |
| F1 F2 F3 F4 | `CP_R3` | Phong 3 |
| A1 A2 A3 A4 | `CP_R4` | Phong 4 |
| B1 B2 B3 B4 | `CP_HALL` | Hanh lang |

**Note**: These are placeholder values. Actual checkpoint UIDs need to be updated.

### RFID Logic

1. **Scan card** every 500ms via RFID RC522
2. **Check local checkpoint database** first:
   - Match: `handleCheckpointCard()`, update `currentCheckpoint`, call `reportLocation()`
3. **If not checkpoint** then `handlePatientCard()`:
   - If session active + same card UID: end session ("completed"), show "KET THUC!" 2s
   - If session active + different card: ignore
   - If IDLE: call `apiGetPatientByCard()`. If found: `startSessionWithUser()`. If not registered: show "KHONG HOP LE!" 3s

### Session Management

| Function | Description |
|----------|-------------|
| `startSessionWithUser(user)` | Copy user data, set `stepCount=0`, call `apiStartSession()`, state = `STATE_SESSION_ACTIVE`, send `"BALANCE:ON"` to Walking Controller, LED ON. On API fail: use `sessionId="LOCAL-{millis}"` |
| `endSession(status)` | Send `"STOP"` + `"BALANCE:OFF"` to Walking Controller, call `apiEndSession()`, clear session data, state = `STATE_IDLE`, LED OFF |
| `updateStepCount(steps)` | Update `session.stepCount` if changed |

### Button Handling

| Button | Session Active | No Session |
|--------|---------------|------------|
| Forward (32) | Send `"CMD:FWD"` + `isMoving=true` | Long-press 3s = WiFiManager |
| Backward (33) | Send `"CMD:BACK"` + `isMoving=true` | Disabled |
| Left (25) | Send `"CMD:LEFT"` + `isMoving=true` | Disabled |
| Right (26) | Send `"CMD:RIGHT"` + `isMoving=true` | Disabled |
| Stop (27) | Send `"STOP"`. Long-press 2s = end session | Disabled |

On button release (Forward/Back/Left/Right): send `"STOP"`, clear `isMoving`.

### WiFi Setup (Long-Press Forward in IDLE)

1. Shows progress bar on OLED (1s-3s hold)
2. At 3s: launch WiFiManager portal
3. Portal SSID: `"BipedRobot-Setup"`, password: `"biped123"`
4. Custom parameter: API URL
5. On success: save to NVS, show IP address
6. On timeout (300s): show "WiFi FAIL!" message

### API Endpoints Used

| Function | Method | Endpoint | Purpose |
|----------|--------|----------|---------|
| `apiGetPatientByCard()` | GET | `/api/patients/by-card/{cardUid}` | Lookup patient by RFID |
| `apiStartSession()` | POST | `/api/robots/biped/session/start` | Start rehab session |
| `apiUpdateSteps()` | PUT | `/api/robots/biped/session/{sessionId}/update` | Push step count (every 2s) |
| `apiEndSession(status)` | POST | `/api/robots/biped/session/{sessionId}/end` | End session |
| `apiReportLocation()` | PUT | `/api/robots/{ROBOT_ID}/telemetry` | Report checkpoint location |
| `apiTriggerCarryRobotFetch()` | POST | `/api/missions/biped-pickup` | Trigger Carry Robot pickup |
| `apiHeartbeat()` | PUT | `/api/robots/{ROBOT_ID}/telemetry` | Heartbeat every 5s (battery hardcoded to 100) |

### OLED Display States (128x64 SSD1306)

```
[IDLE]                         [SESSION]
+-------------------+          +-------------------+
|   BIPED ROBOT     |          |  Nguyen Van A     |  <-- centered, max 17 chars
|___________________|          |___________________|
| San sang su dung  |          |                   |
|                   |          |      12345        |  <-- large font (logisoso28)
|-> Quet the bat dau|          |       buoc        |
+-------------------+          +-------------------+

[WIFI SETUP]                   [CARD NOT REGISTERED]
+-------------------+          +-------------------+
|   WIFI SETUP      |          | KHONG HOP LE!     |
|___________________|          |                   |
| 1. Ket noi WiFi:  |          | The chua dang ky. |
|   BipedRobot-Setup|          | Lien he nhan vien.|
| 2. Vao 192.168.4.1|          +-------------------+
+-------------------+
```

### Main Loop Order
1. `handleRFID()` - every 500ms
2. `checkForwardLongPress()` - every loop
3. `handleButtons()` - every loop (debounced)
4. `handleEncoder()` - every loop
5. `handleUARTReceive()` - every loop (parse Walking Controller messages)
6. `handleAPITasks()` - heartbeat (5s), step update (2s)
7. `updateDisplay()` - every 200ms
8. WiFi reconnect check - every 10s if disconnected

---

## ESP32-CAM Module (`Carry_robot_CAM/`)

### Platform and Hardware

| Setting | Value |
|---------|-------|
| Board | `esp32cam` (AI-Thinker ESP32-CAM) |
| Framework | Arduino |
| PSRAM | Enabled (`-DBOARD_HAS_PSRAM`) |
| Camera | OV2640 |
| Frame format | Grayscale QVGA (320x240) |
| XCLK | 20 MHz |
| Frame buffers | 2 (in PSRAM) |

### Camera Pin Mapping (AI-Thinker)

| Pin | GPIO | Function |
|-----|------|----------|
| PWDN | 32 | Power down |
| XCLK | 0 | External clock |
| SIOD | 26 | SCCB data |
| SIOC | 27 | SCCB clock |
| Y2-Y9 | 5,18,19,21,36,39,34,35 | Data bus |
| VSYNC | 25 | Vertical sync |
| HREF | 23 | Horizontal reference |
| PCLK | 22 | Pixel clock |
| LED_BUILTIN | 33 | Red LED (active LOW) |
| LED_FLASH | 4 | Flash LED |

### Camera Sensor Settings

| Parameter | Value |
|-----------|-------|
| Brightness | 0 |
| Contrast | 1 (increased) |
| Gain ceiling | 4x |
| White balance | Enabled |
| Auto exposure | Enabled |
| Bad pixel correction | Enabled |

### AprilTag Detection (Simplified)

The detector uses a **simplified edge-based approach** (not full AprilTag decoding):

1. **Sobel edge detection**: 3x3 kernel, |gx| + |gy| magnitude, threshold x4 (effective 200)
2. **Quadrilateral scanning**: 8px step, candidate sizes from `MIN_TAG_SIZE` to `min(w/3, h/3)`, step 4px
3. **Detection criteria**: Edge ratio 60%-95% along 4 sides
4. **Tag ID**: Always returns `1` (decoding is stub)
5. **Confidence**: Hardcoded to 70

| Setting | Value |
|---------|-------|
| `TAG_FAMILY` | `"tag36h11"` |
| `MIN_TAG_SIZE` | 20 pixels |
| `MAX_TAGS` | 4 per frame |
| Edge threshold | 50 (effective 200 after x4) |

### ESP-NOW Communication

| Setting | Value |
|---------|-------|
| `ROBOT_MAC_ADDR` | `{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}` (broadcast - **must update**) |
| `ESPNOW_CHANNEL` | 1 |
| `SEND_INTERVAL_MS` | 50 ms (20 Hz) |
| WiFi Mode | `WIFI_STA` (no AP connection) |

### ESP-NOW Data Packet (12 bytes, packed)

```cpp
struct AprilTagData {
  uint8_t tagId;        // Tag ID (0 = no detection)
  int16_t centerX;      // X relative to image center (-160 to +160)
  int16_t centerY;      // Y relative to image center (-120 to +120)
  uint16_t tagSize;     // Tag size in pixels
  uint8_t confidence;   // 0-100
  uint32_t timestamp;   // millis()
} __attribute__((packed));
```

### Main Loop Flow
1. `processFrame()` - Capture + detect (every iteration)
2. `sendTagData()` - Every 50ms (20 Hz) via ESP-NOW
3. FPS calculation - Print every 1s
4. LED on when tag detected, off otherwise

### Error Patterns
- **Camera init fail**: Infinite loop + 5-blink pattern
- **ESP-NOW init fail**: Infinite loop + 2-blink pattern
- **Ready**: 2 blinks

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

- **`handleTelemetry`**: Upserts Robot doc. Mission-aware status. Low battery alert at 20% or below. Sets `currentLocation.room` from `currentNodeId` or payload. Sets `transportData.destination.room` from `destBed` or active mission
- **`handleProgress`**: Updates TransportMission progress. Sets robot position and mission destination
- **`handleComplete`**: Sets mission to `completed` or `failed`. Robot status to `idle`
- **`handleReturned`**: Sets `returnedAt` on mission. Robot status to `idle`, location to `MED`
- **`handleWaitingReturnRoute`**: Robot reports position after cancellation. Backend computes return route via `buildReturnPath()` + `computeReturnActions()`, publishes via MQTT

#### Return Route Calculation
- `buildReturnPath(fromNodeId)`: Builds path from any node back to MED using hard-coded hospital layout. Parses `R{room}{M|O|D}{idx}` for room nodes, corridor nodes handled directly
- `computeReturnActions(routePoints, startNode)`: Assigns turn actions based on room geometry (rooms 1/3 are left-side, rooms 2/4 are right-side)

### MongoDB Models

#### Robot (`src/models/Robot.js`)

| Field | Type | Details |
|-------|------|---------|
| `robotId` | String | **Required**, unique |
| `name` | String | **Required** |
| `type` | String | Enum: `biped`, `carry` |
| `status` | String | `idle`, `busy`, `charging`, `maintenance`, `offline`, `low_battery` |
| `lastSeenAt` | Date | For online detection |
| `batteryLevel` | Number | 0-100, default 100 |
| `currentLocation` | Object | `{ building, floor, room, coordinates: {x, y} }` |
| `transportData` | Object | `{ carryingItem, weight, destination: { building, floor, room }, estimatedArrival }` |
| `recoveryData` | Object | `{ currentPatient: { patientId, assignedAt }, recoveryProgram, sessionDuration, progress, difficultyLevel }` |
| `firmwareVersion` | String | |
| `totalSessions` | Number | Incremented on biped session end |
| `totalDeliveries` | Number | Incremented on carry mission return |
| `totalDistance`, `totalOperatingHours` | Number | Statistics |

Indexes: `{ type: 1, status: 1 }`, `{ batteryLevel: 1 }`, `{ lastSeenAt: -1 }`

#### TransportMission (`src/models/TransportMission.js`)

| Field | Type | Details |
|-------|------|---------|
| `missionId` | String | **Required**, unique (format: `TM-{12 hex}`) |
| `mapId` | String | **Required** |
| `carryRobotId` | String | **Required** |
| `patientName` | String | |
| `bedId` | String | **Required** (e.g., `R1M1`) |
| `destinationNodeId` | String | |
| `outboundRoute` | [RoutePoint] | Array of route points with `nodeId, x, y, rfidUid, action, actions` |
| `returnRoute` | [RoutePoint] | Same format |
| `status` | String | `pending`, `en_route`, `arrived`, `completed`, `failed`, `cancelled` |
| `returnedAt` | Date | When robot returned to base |
| `cancelRequestedAt`, `cancelledAt`, `cancelledBy` | Date/String | Cancellation tracking |

RoutePoint sub-schema: `{ nodeId(req), x(req), y(req), rfidUid, kind, label, action(F|L|R), actions([F|L|R]) }`

#### Patient (`src/models/Patient.js`)

| Field | Type | Details |
|-------|------|---------|
| `fullName` | String | **Required** |
| `mrn` | String | **Required**, unique (Medical Record Number, format `MRN-{year}-{3 digits}`) |
| `cardNumber` | String | **Required** (RFID UID) |
| `admissionDate` | String | **Required**, YYYY-MM-DD |
| `status` | String | **Required** (`Stable`, `Critical`, `Recovering`, `Under Observation`, `Discharged`) |
| `primaryDoctor` | String | **Required** |
| `roomBed` | String | Canonical format `R{1-4}{M|O}{1-3}` |
| `relativeName`, `relativePhone` | String | **Required** |
| `photoPath`, `photoUrl` | String | Patient photo |
| `timeline` | [timelineSchema] | `{ at, title(req), createdBy, description }` |
| `prescriptions` | [prescriptionSchema] | `{ medication(req), dosage(req), frequency(req), startDate, endDate, prescribedBy, instructions, status }` |
| `notes` | [noteSchema] | `{ createdAt, createdBy, text(req) }` |

#### BipedSession (`src/models/BipedSession.js`)

| Field | Type | Details |
|-------|------|---------|
| `sessionId` | String | **Required**, unique (format: `BIPED-{hex}`) |
| `robotId`, `robotName` | String | |
| `userId`, `userName` | String | From RFID card |
| `patientId`, `patientName` | String | |
| `startTime` | Date | **Required** |
| `endTime` | Date | Set on session end |
| `totalSteps` | Number | From Walking Controller UART |
| `duration` | Number | Minutes, auto-computed |
| `status` | String | `active`, `completed`, `interrupted` |
| `telemetry` | Object | `{ avgHeartRate, maxHeartRate, minHeartRate, caloriesBurned, distanceWalked }` |

#### Other Models

| Model | Purpose |
|-------|---------|
| `Alert` | System alerts with types: `carry_low_battery`, `biped_low_battery`, `rescue_required`, `route_deviation`, `info` |
| `MapGraph` | Graph-based floor map with nodes (checkpoint/bed/station/room) and edges with auto-computed weights |
| `ChargingStation` | Charging station management with capacity, slots, and history |
| `RecoverySession` | Detailed rehabilitation session with patient feedback, therapist assessment, sensor data |
| `RescueMission` | Carry Robot rescue of Biped Robot (low_battery, mechanical_failure, stuck, emergency_stop) |
| `PatientNote`, `PatientTimeline`, `Prescription` | Standalone collections mirroring embedded sub-docs in Patient |
| `User` | RFID user registration (uid, name, email) |
| `Event` | Button press events from ESP32 |

### REST API Routes

#### Patients (`/api/patients`)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/patients` | List with search, filter, sort. Fuzzy search on name/MRN/card/doctor |
| GET | `/patients/by-card/:cardNumber` | Lookup by RFID card (used by Biped User Manager) |
| GET | `/patients/by-bed/:bedId` | Find patient in bed (canonical + legacy format) |
| GET | `/patients/beds` | Valid bed IDs from map |
| GET | `/patients/meta` | Filter metadata (statuses, doctors, departments) |
| GET | `/patients/mrn/generate` | Generate unique MRN |
| POST | `/patients` | Create with photo upload (multer, max 5MB, jpg/png/webp) |
| PUT | `/patients/:id` | Update with optional new photo |
| DELETE | `/patients/:id` | Delete with photo cleanup |
| GET | `/patients/:id/details` | Full details with timeline/prescriptions/notes |
| POST/DELETE | `/patients/:id/timeline/:tid` | Add/remove timeline entries |
| POST/PUT/DELETE | `/patients/:id/prescriptions/:pid` | CRUD prescriptions |
| POST/DELETE | `/patients/:id/notes/:nid` | Add/remove notes |

**Bed ID dual format support**: Both `R1M1` (canonical) and `R1-Bed1` (legacy) are accepted throughout. Mapping: Bed1=M1, Bed2=O1, Bed3=M2, Bed4=O2, Bed5=M3, Bed6=O3.

#### Robots (`/api/robots`)

| Method | Path | Description |
|--------|------|-------------|
| PUT | `/robots/:id/telemetry` | ESP32 heartbeat. Upserts robot. Auto-detects busy/idle from missions |
| GET | `/robots/carry/status` | Online carry robots (seen within 15s). Includes active mission destination |
| GET | `/robots/biped/active` | Online biped robots. Aggregates today's session steps |
| POST | `/robots/biped/session/start` | Start rehabilitation session |
| PUT | `/robots/biped/session/:sid/update` | Update step count + telemetry |
| POST | `/robots/biped/session/:sid/end` | End session |
| GET | `/robots/biped/sessions/history` | Paginated session history |

**Online detection**: Robot is "online" if `lastSeenAt` within 15s (status) or 30s (mission assignment).

#### Missions (`/api/missions`)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/missions/transport` | List transport missions (filter by status, robotId) |
| POST | `/missions/delivery` | **Create delivery mission**: Find idle carry robot, compute hard-coded routes, publish via MQTT |
| GET | `/missions/carry/next` | Poll next mission for robot firmware |
| PUT | `/missions/carry/:id/progress` | Update progress. Battery 20% or below creates alert |
| POST | `/missions/carry/:id/complete` | Mark delivered |
| POST | `/missions/carry/:id/cancel` | Cancel + publish MQTT cancel |
| POST | `/missions/carry/:id/returned` | Mark returned to base. Increments `totalDeliveries` if completed |
| GET | `/missions/delivery/history` | Paginated delivery history |

#### Mission Route Computation

Routes use **hard-coded paths** (not Dijkstra) for the 4-room hospital layout:

**Corridor**: `MED -> H_MED -> H_BOT -> (J4 -> H_TOP for rooms 1/2)`

**Room navigation**:
- Rooms 1 and 3: left-side rooms (enter with left turn)
- Rooms 2 and 4: right-side rooms (enter with right turn)
- Rooms 1 and 2: hub node is `H_TOP`
- Rooms 3 and 4: hub node is `H_BOT`, skip `J4->H_TOP`
- M-side beds: `D1 -> M1 -> M2 -> M3`
- O-side beds: `D1 -> D2 -> O1 -> O2 -> O3`

Key functions: `buildHardRouteNodeIds()`, `roomProfile()`, `actionsForLeg()`, `computeActionsMap()`, `toPoints()`

#### Maps (`/api/maps`)

| Method | Path | Description |
|--------|------|-------------|
| POST | `/maps` | Create map metadata |
| POST | `/maps/:mapId/import` | Import nodes/edges with auto-computed weights |
| GET | `/maps/:mapId` | Get map |
| GET | `/maps/:mapId/route` | Dijkstra shortest path (available but not used by missions) |

Helper functions: `buildAdj()`, `dijkstra()`, `findNearestNode()`, `resolveDestinationNode()`

#### Alerts (`/api/alerts`)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/alerts` | List (filter: `active=1`, limit max 200) |
| POST | `/alerts` | Create alert |
| PUT | `/alerts/:id/resolve` | Resolve alert |

Alert types: `carry_low_battery`, `biped_low_battery`, `rescue_required`, `route_deviation`, `info`

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
| POST | `/events/button` | Register button press (UID must exist) |
| GET | `/events/stats/daily` | Daily event count aggregation |
| GET | `/events/stats/daily-by-user` | Daily count by user |

### Seed Data

#### `seed/seedMap.js` (run: `node seed/seedMap.js`)

Creates map `floor1` with 37 nodes and corridor/room edges:

**Corridor nodes** (x=400): MED(y=500), H_MED(y=400), H_BOT(y=300), J4(y=200), H_TOP(y=100)

**Room positions**:
| Room | baseX | baseY | Hub |
|------|-------|-------|-----|
| Room 1 | 100 | 100 | H_TOP |
| Room 2 | 700 | 100 | H_TOP |
| Room 3 | 100 | 300 | H_BOT |
| Room 4 | 700 | 300 | H_BOT |

Each room: D1, D2 (checkpoints), M1-M3 (beds), O1-O3 (beds). All edges weight=1.

#### `seed/checkpointsF1.js`

Exports `CHECKPOINT_UID` map of 37 node IDs to NFC UIDs (colon-separated hex format).

---

## Frontend (`Hospital Dashboard/Frontend/`)

### Stack and Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| React | 18.3.1 | UI framework |
| Vite | 6.3.5 | Build tool, dev server (port 5173) |
| TypeScript | - | Type safety |
| Tailwind CSS | 4.1.12 | Utility-first CSS |
| Radix UI | 20+ primitives | Accessible UI components (shadcn/ui pattern) |
| MUI Material | 7.3.5 | Available but primary UI is Tailwind/Radix |
| react-hook-form | 7.55 | Form state management |
| recharts | 2.15 | Charts |
| lucide-react | 0.487 | Icons |
| date-fns | 3.6 | Date formatting |
| sonner | 2.0 | Toast notifications |
| motion (Framer) | 12.23 | Animations |
| react-dnd | - | Drag-and-drop |
| cmdk | 1.1 | Command palette |
| react-datepicker | 9.1 | Date inputs |

### Vite Configuration

| Setting | Value |
|---------|-------|
| Dev server port | 5173 |
| Path alias | `@` maps to `./src` |
| Proxy `/api` | forwards to `http://localhost:3000` |
| Proxy `/uploads` | forwards to `http://localhost:3000` |

### Architecture

| Aspect | Detail |
|--------|--------|
| Routing | Tab-based SPA (useState with 'patients' or 'robot') - no router |
| State Management | Local `useState` + custom hooks per domain |
| Data Fetching | Custom hooks with `fetch()` wrapper + `setInterval` polling |
| Real-time | Robots: 5s, Alerts: 10s, History: 5s, Connection: 30s |
| UI Framework | Tailwind CSS v4 + shadcn/ui (Radix + CVA) |
| Theme | Medical Blue (#0277BD), light/dark mode |
| Base Font Size | 20px globally (hospital readability) |
| RFID | Web Serial API (Chrome/Edge) for USB readers |
| Camera | `getUserMedia` for patient photos |

### Application Structure

```
App.tsx
├── RFIDProvider (context)
├── Header (logo, ConnectionStatus, alert badge, refresh)
├── Tab Navigation ("Patients Manager" | "Robot Center")
├── PatientDashboard
│   ├── Search & Filter Panel
│   ├── Patient Table (11 columns)
│   ├── PatientForm (dialog, react-hook-form + RFID + camera)
│   └── PatientDetails (dialog, 3 tabs: Medications/Allergens/Notes)
├── RobotCenter
│   ├── BedMap (4-room floor plan, 24 beds)
│   └── RobotManagement
│       ├── Selected Patient Card + Send/Cancel Robot button
│       ├── Biped Robot Panel (status table + BipedHistoryTable)
│       └── Carry Robot Panel (status table + DeliveryHistoryTable)
└── Footer
```

### Custom Hooks

| Hook | Polling | Returns |
|------|---------|---------|
| `usePatients()` | - | `{ patients, loading, addPatient, updatePatient, deletePatient, refresh }` |
| `useRobots(5000)` | 5s | `{ robots, missions, carryStatus, bipedStatus, loading, cancelTask, refresh }` |
| `useAlerts(10000)` | 10s | `{ alerts, alertCounts, loading, resolveAlert, refresh }` |
| `useMissions()` | - | `{ missions, createDeliveryMission, cancelDeliveryMission, getActiveMissions, ... }` |
| `useSerialRFID()` | - | `{ status, lastUID, connect, disconnect, sendCommand }` |

### RFID Integration (Web Serial API)

- USB vendor filters: Arduino (0x2341), CH340 (0x1A86), FTDI (0x0403), CP210x (0x10C4)
- Protocol: `RFID:XXXXXXXX\n` prefix or raw hex UIDs (8+ chars)
- `RFIDContext`: Pub/sub pattern for multi-component subscription
- Auto-fills card number in `PatientForm` on scan

### Key Components

#### BedMap
- 4 rooms in 2x2 grid, 6 beds per room (3 M-side + 3 O-side)
- Rooms 1 and 3: O-beds left, M-beds right. Rooms 2 and 4: M-beds left, O-beds right
- Color coding: Green=occupied, Blue=robot active, Gray=available

#### PatientDashboard
- Search: fuzzy match on name/MRN/RFID/doctor/relative
- Sort: bed order, name asc/desc, date newest/oldest
- Status badges: Stable=green, Critical=red, Recovering=blue, Under Observation=yellow, Discharged=gray
- Export CSV functionality

#### RobotManagement
- Carry Robot table: Device, Status, Current Location, Destination, Battery
- Biped Robot table: Device, Status, Current User, Steps, Battery
- Battery colors: more than 50% green, more than 20% yellow, 20% or below red
- History tables with pagination

### ConnectionStatus
- Polls `GET /api/patients/meta` every 30s
- Shows Wifi/WifiOff icon + connection text

### TypeScript Types

```typescript
type PatientStatus = 'Stable' | 'Critical' | 'Recovering' | 'Under Observation' | 'Discharged'
type RobotType = 'Biped' | 'Carry'
type RobotStatus = 'Idle' | 'Moving' | 'At Destination' | 'Task in Progress' | 'Error'
const BED_MAP: string[] = ['R1M1', 'R1M2', 'R1M3', 'R1O1', 'R1O2', 'R1O3', ..., 'R4O3'] // 24 beds
```

### UI Component Library (48 shadcn/ui components)

`accordion`, `alert-dialog`, `alert`, `aspect-ratio`, `avatar`, `badge`, `breadcrumb`, `button`, `calendar`, `card`, `carousel`, `chart`, `checkbox`, `collapsible`, `command`, `context-menu`, `dialog`, `drawer`, `dropdown-menu`, `form`, `hover-card`, `input-otp`, `input`, `label`, `menubar`, `navigation-menu`, `pagination`, `popover`, `progress`, `radio-group`, `resizable`, `scroll-area`, `select`, `separator`, `sheet`, `sidebar`, `skeleton`, `slider`, `sonner`, `switch`, `table`, `tabs`, `textarea`, `toggle-group`, `toggle`, `tooltip`

---

## Development

### Start Development

```bash
cd "Hospital Dashboard"
# Option 1: Use batch script
start-servers.bat

# Option 2: Manual
cd Backend && npm run dev    # Port 3000
cd Frontend && npm run dev   # Port 5173 (proxies /api to backend)
```

### Build and Flash ESP32

```bash
# Carry Robot
cd CarryRobot/carry_cpp
pio run                    # Build
pio run -t upload          # Flash
pio device monitor         # Serial monitor (115200 baud)

# ESP32-CAM
cd Carry_robot_CAM
pio run -t upload

# Biped Robot - Use Arduino IDE (BipedRobot/BipedRobot.ino)
# BipedUserManager - Use Arduino IDE (BipedUserManager/BipedUserManager.ino)
```

### Seed Database

```bash
cd "Hospital Dashboard/Backend"
node seed/seedMap.js       # Creates floor1 map with 37 nodes
```

### MQTT Broker (Mosquitto)

- **Port**: 1883
- **Users**: `hospital_robot` (ESP32), `hospital_backend` (Backend), password: `123456`
- **Config**: `listener 1883`, `allow_anonymous false`, `password_file {path}/passwordfile`
- **Firewall**: Inbound TCP rule required for port 1883

---

## Common Tasks

### Adding a New NFC Checkpoint
1. Add entry to `UID_MAP[]` in `CarryRobot/carry_cpp/src/uid_lookup.cpp`
2. Add to `CHECKPOINT_UID` in `Hospital Dashboard/Backend/seed/checkpointsF1.js`
3. Update MapGraph in MongoDB via `seed/seedMap.js` or `POST /api/maps/:mapId/import`

### Adding New Carry Robot Feature
1. Create `FeatureName.h/cpp` in `CarryRobot/carry_cpp/src/`
2. Include in `main.cpp`, add timing variable in `globals.h/cpp`
3. Integrate with `StateMachine` if affects mission flow

### Adding New Biped Servo/Gait
1. Update servo indices and `JointCfg` array in `BipedRobot/config.h`
2. Modify `Kinematics.h` for IK calculations
3. Tune PD gains and correction limits in `BipedRobot.ino`

### Modifying Mission Routes
Hard-coded routes in `Hospital Dashboard/Backend/src/routes/missions.js`:
- `buildHardRouteNodeIds()` - outbound + return node sequences
- `roomProfile()` - per-room turn directions
- `actionsForLeg()` - turn action computation
- Also in `mqttService.js`: `buildReturnPath()` and `computeReturnActions()`

### Adding a New Patient Field
1. Add to Patient schema in `Backend/src/models/Patient.js`
2. Update `routes/patients.js` POST/PUT handlers
3. Add to `BackendPatient` interface in `Frontend/src/app/api/patients.ts`
4. Update `toFrontendPatient()` / `toBackendPatientData()` in `Frontend/src/app/hooks/usePatients.ts`
5. Add to `Patient` interface in `Frontend/src/app/types/patient.ts`
6. Add UI field in `Frontend/src/app/components/PatientForm.tsx`

---

## Critical Notes

- **Never use GPIO 1/3** on Carry Robot for peripherals during development (serial conflict with ultrasonics)
- **Motor gain tuning**: `leftGain`/`rightGain` in `CarryRobot/carry_cpp/src/config.h` for straight-line correction
- **Gyro calibration**: IMU must be stationary during startup calibration (500 samples on Carry, 80 samples on Biped)
- **MQTT buffer size**: 2048 bytes on ESP32 PubSubClient - mission payloads with full routes can be large
- **Return route**: Cancel triggers `WAIT_FOR_RETURN_ROUTE` state - robot waits up to 5s for Backend MQTT return route, then falls back to reversing visited outbound nodes
- **API base URL**: Stored in NVS on both ESP32 modules, configurable via WiFiManager portal
- **Online detection**: 15s for status endpoints, 30s for mission assignment eligibility
- **Battery level**: Hardcoded to 100 on both ESP32 firmwares (no real battery monitoring)
- **Bed ID formats**: Both canonical (`R1M1`) and legacy (`R1-Bed1`) supported throughout backend
- **Photo upload**: Max 5MB, jpg/jpeg/png/webp only, stored in `uploads/patients/`
- **ESP32-CAM MAC**: Default is broadcast - must update `ROBOT_MAC_ADDR` in `Carry_robot_CAM/src/config.h` with actual Carry Robot MAC
- **No router in frontend**: Tab-based navigation only (patients vs robot center)
- **PCA9685 servo zero-offsets**: Calibrated per-joint in `BipedRobot/config.h` - recalibrate if servos are replaced
