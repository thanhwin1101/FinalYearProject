# Sơ đồ hoạt động hệ thống Robot Bệnh Viện

## Mục lục
1. [Kiến trúc tổng quan](#1-kiến-trúc-tổng-quan)
2. [Luồng khởi động hệ thống](#2-luồng-khởi-động-hệ-thống)
3. [State Machine – Master ESP32](#3-state-machine--master-esp32)
4. [Chế độ Auto (Delegated Mission)](#4-chế-độ-auto-delegated-mission)
5. [Huỷ nhiệm vụ giữa chặng](#5-huỷ-nhiệm-vụ-giữa-chặng)
6. [Phát hiện vật cản (Obstacle)](#6-phát-hiện-vật-cản-obstacle)
7. [Chế độ Follow Mode](#7-chế-độ-follow-mode)
8. [Follow-Return (Thoát Follow về MED)](#8-follow-return-thoát-follow-về-med)
9. [Recovery Flow (Mất Line)](#9-recovery-flow-mất-line)
10. [Giám sát pin](#10-giám-sát-pin)
11. [Bảng nút bấm vật lý](#11-bảng-nút-bấm-vật-lý)
12. [Giao tiếp MQTT – Backend](#12-giao-tiếp-mqtt--backend)
13. [Luồng dữ liệu Frontend](#13-luồng-dữ-liệu-frontend)

---

## 1. Kiến trúc tổng quan

```
┌─────────────────────────────────────────────────────────────────┐
│                      Hospital Dashboard                          │
│  ┌──────────────────────┐    ┌────────────────────────────────┐ │
│  │  Frontend (React)    │◄──►│  Backend (Node.js / Express)   │ │
│  │  - PatientDashboard  │    │  - REST API (:3001)            │ │
│  │  - RobotCenter       │    │  - MQTT Service                │ │
│  │  - RobotLiveTracking │    │  - MongoDB (Mongoose)          │ │
│  │  - MissionHistory    │    │  - Models: Robot, Mission,     │ │
│  │  - Alert Panel       │    │    Patient, Map, Alert         │ │
│  └──────────────────────┘    └──────────┬─────────────────────┘ │
└─────────────────────────────────────────┼───────────────────────┘
                                          │ MQTT (port 1883)
                                          │ WiFi 2.4 GHz
                          ┌───────────────▼──────────────────┐
                          │       Master ESP32 (CARRY-01)     │
                          │  - State Machine                  │
                          │  - HuskyLens (Tag Recognition)    │
                          │  - ToF VL53L0X (obstacle)         │
                          │  - Ultrasonic x2 (side)           │
                          │  - Servo Gimbal X/Y               │
                          │  - OLED Display                   │
                          │  - Buzzer / Button                │
                          │  - Battery ADC (GPIO35)           │
                          └───────────────┬──────────────────┘
                                          │ ESP-NOW (2.4 GHz)
                          ┌───────────────▼──────────────────┐
                          │       Slave ESP32                 │
                          │  - Line follower (TCRT5000)       │
                          │  - RFID reader                    │
                          │  - Motor driver (omni wheels)     │
                          │  - Route execution (autonomous)   │
                          └──────────────────────────────────┘
```

---

## 2. Luồng khởi động hệ thống

```
[Power ON]
     │
     ▼
[Master ESP32 boot]
     │── Khởi tạo I2C (OLED, ToF VL53L0X)
     │── Khởi tạo Serial2 (HuskyLens UART)
     │── Khởi tạo Servo X/Y (vị trí SERVO_Y_TILT_DOWN = 45°)
     │── Khởi tạo Buzzer, Button (GPIO4)
     │── Chờ ESP-NOW relock beacon từ Slave (5 s)
     │
     ▼
[Kết nối WiFi]
     │── Thử SSID đã lưu
     │── Timeout 30 s → IDLE dù chưa có WiFi
     │
     ▼
[Kết nối MQTT Broker]
     │── Thử 3 địa chỉ: configured → .100 → .102
     │── Subscribe topics:
     │     hospital/robots/CARRY-01/mission/assign
     │     hospital/robots/CARRY-01/mission/cancel
     │     hospital/robots/CARRY-01/mission/return_route
     │     hospital/robots/CARRY-01/command
     │
     ▼
[Khởi tạo HuskyLens]
     │── Timeout 7 s
     │── Algorithm: LINE_TRACKING (mặc định)
     │── Settle timer 350 ms sau khi đổi algo
     │
     ▼
[smInit() → ST_IDLE]
     │── Servo Y → 9090° (cúi xuống)
     │── OLED: "IDLE | [1x]Start [2x]Follow [hold]Follow"
     │── Gửi beacon ESP-NOW liên tục cho Slave
```

---

## 3. State Machine – Master ESP32

```
                         ┌──────────────────────────────────────────────────────┐
                         │                    ST_IDLE                            │
                         │  OLED: "Ready"                                        │
                         │  Servo Y: 45° (cúi xuống)                            │
                         └──────────────────────────────────────────────────────┘
                           │              │              
              [1x click]   │    [2x click]│ 
          (có route sẵn)   │              │              
                           ▼              ▼              
                 ST_MISSION_DELEGATED  ST_FOLLOW     
                  (slave tự chạy)    (follow mode)  


                    ┌─────────────────────────────────────────────────────────────┐
                    │  Các state chính (ModePriority)                             │
                    │                                                             │
                    │  EMERGENCY (3): ST_OBSTACLE                                 │
                    │  SAFETY    (2): —                                           │
                    │  MISSION   (1): ST_OUTBOUND, ST_WAIT_AT_DEST, ST_BACK,      │
                    │                 ST_RECOVERY_*, ST_MISSION_DELEGATED         │
                    │  MANUAL    (0): ST_IDLE, ST_FOLLOW, ST_FOLLOW_RETURN        │
                    └─────────────────────────────────────────────────────────────┘


Toàn bộ chuyển trạng thái:

  ST_IDLE ──[1x, có route]──────────────────► ST_MISSION_DELEGATED
  ST_IDLE ──[2x / hold]─────────────────────► ST_FOLLOW
  ST_IDLE ──[MQTT assign]───────────────────► ST_IDLE (route loaded, chờ nút)

  ST_MISSION_DELEGATED ──[slave→dest]────────► (turn 180°, beep) → chờ [1x]
  ST_MISSION_DELEGATED ──[1x, ở đích]────────► Slave startReturn → về MED → ST_IDLE
  ST_MISSION_DELEGATED ──[obstacle]──────────► ST_OBSTACLE
  ST_MISSION_DELEGATED ──[MQTT cancel]────────► (2 CP nữa) → ST_RECOVERY_CALL → ST_MISSION_DELEGATED (return)

  ST_OUTBOUND ──[RFID đích]─────────────────► ST_WAIT_AT_DEST
  ST_OUTBOUND ──[1x cancel]─────────────────► (CP kế tiếp) → ST_RECOVERY_CALL
  ST_OUTBOUND ──[2x abort]──────────────────► ST_IDLE
  ST_OUTBOUND ──[obstacle]──────────────────► ST_OBSTACLE

  ST_WAIT_AT_DEST ──[1x]────────────────────► ST_BACK
  ST_WAIT_AT_DEST ──[2x]────────────────────► ST_IDLE (hard abort)

  ST_BACK ──[RFID MED]──────────────────────► (turn 180°, beep) → ST_IDLE
  ST_BACK ──[2x abort]──────────────────────► ST_IDLE
  ST_BACK ──[obstacle]──────────────────────► ST_OBSTACLE
  ST_BACK ──[mất line]──────────────────────► ST_RECOVERY_VIS

  ST_FOLLOW ──[1x stop]─────────────────────► ST_IDLE
  ST_FOLLOW ──[2x recover]──────────────────► ST_FOLLOW_RETURN
  ST_FOLLOW ──[hold]────────────────────────► ST_IDLE
  ST_FOLLOW ──[pin yếu 3s]──────────────────► ST_FOLLOW_RETURN

  ST_FOLLOW_RETURN ──[line found]───────────► ST_RECOVERY_VIS
  ST_FOLLOW_RETURN ──[1x/2x]────────────────► ST_IDLE

  ST_RECOVERY_VIS ──[line docked]───────────► ST_RECOVERY_BLIND
  ST_RECOVERY_VIS ──[2x abort]──────────────► ST_IDLE

  ST_RECOVERY_BLIND ──[2 RFID scanned]──────► ST_RECOVERY_CALL

  ST_RECOVERY_CALL ──[backend route]────────► ST_BACK (hoặc ST_MISSION_DELEGATED nếu delegated)
  ST_RECOVERY_CALL ──[timeout 5s]───────────► ST_BACK (dùng reverse route)

  ST_OBSTACLE ──[dist ≥ 300mm]──────────────► Khôi phục state trước đó
```

---

## 4. Chế độ Auto (Delegated Mission)

```
[Backend Dashboard]
        │
        │  POST /api/missions  →  publish MQTT
        │  topic: hospital/robots/CARRY-01/mission/assign
        │  payload: { missionId, patientName, destBed, outRoute[], retRoute[] }
        ▼
[Master: mqttCallback → mission/assign]
        │── routeParseAssign(doc)          — lưu outRoute[], retRoute[]
        │── missionDelegateSendRoutesOnly() — gửi route chunks đến Slave qua ESP-NOW
        │   (type=0x01: outbound, type=0x02: return, chunked ROUTE_CHUNK_MAX_POINTS)
        │── OLED: "Route loaded, press SW"
        │
        ▼ [User bấm nút 1x trên xe]
[Master: handleButtonEvent → ST_IDLE → missionDelegateStartMission()]
        │── Kiểm tra pin ≥ 30%  (nếu không → alert + reject)
        │── Gửi masterMsg { missionStart=1, enableLine=1, enableRFID=1, baseSpeed=165 }
        │── setStateWithBeep(ST_MISSION_DELEGATED)
        │── s_wasDelegated = true
        │── Servo Y → 45° (cúi xuống)
        │── OLED: "Slave running..."
        │
        ▼
[Slave tự thực hiện route]
        │── Đọc TCRT5000 → bám line
        │── Quét RFID → đối chiếu route points
        │── Rẽ theo action: L/R/B
        │── Gửi SlaveToMasterMsg qua ESP-NOW:
        │     { missionStatus, routeIndex, routeSegment, rfid_new, rfid_uid[] }
        │
        ▼
[Master: processSlaveMsg() — mỗi vòng Task_Logic]
        │
        ├──[rfid_new] → smOnDelegatedRfid(uid)
        │       │── Cập nhật lastCheckpointNode, previousCheckpointNode
        │       │── Nếu cancelPending: tăng s_delegateCancelCPs
        │       │── Nếu s_delegateCancelCPs ≥ 2 → enterCancelWait()
        │       └── MQTT mqttSendProgress(nodeId, idx, total) → Backend
        │
        ├──[missionStatus=1 + routeIndex thay đổi]
        │       └── MQTT mqttSendProgress() → Backend → cập nhật bản đồ web
        │
        ├──[missionStatus=2] ← Slave đến đích
        │       │── mqttSendComplete(missionId)
        │       │── smOnDelegatedDestReached():
        │       │     stop slave (baseSpeed=0)
        │       │     gửi turnCmd='B' → slave xoay 180°
        │       │     chờ TURN_180_MS + 200ms
        │       │     buzzerBeep(90)
        │       │     OLED: "WAIT AT DEST [1x]Return"
        │       │     s_waitingAtDestForSw = true
        │       └── Chờ nút bấm
        │
        ├──[1x click, s_waitingAtDestForSw=true]
        │       │── gửi startReturn=1 → Slave bắt đầu về theo retRoute
        │       └── OLED: "Returning..."
        │
        └──[missionStatus=3] ← Slave về đến MED
                │── mqttSendReturned(missionId)
                └── smEnterIdle() → ST_IDLE
```

---

## 5. Huỷ nhiệm vụ giữa chặng

### 5a. Huỷ khi đang Delegated (ST_MISSION_DELEGATED)

```
[Backend gửi MQTT: mission/cancel]
        │
        ▼
[Master: mqttCallback → smStartDelegatedCancel()]
        │── cancelPending = true
        │── s_delegateCancelCPs = 0
        │── OLED: "CANCEL PENDING | 2 more CPs..."
        │
        ▼ Slave vẫn tiếp tục chạy
        │
[Slave quét RFID → smOnDelegatedRfid() ×2 lần]
        │── s_delegateCancelCPs++ mỗi lần
        │── Khi đạt 2:
        │
        ▼
[enterCancelWait()]
        │── vX=vY=vR=0, enableLine=0
        │── gửi missionCancel=1 (one-shot) → Slave dừng
        │── mqttSendWaitingReturn(lastCheckpointNode, previousCheckpointNode)
        │── Chuyển ST_RECOVERY_CALL
        │
        ▼
[Backend tính route về]
        │── Dùng previousNode→currentNode vector xác định hướng
        │── Nếu ngược hướng MED: thêm firstAction='B' (quay 180°)
        │── Publish: hospital/robots/CARRY-01/mission/return_route
        │
        ▼
[Master: updateRecoveryCall()]
        │── Nhận retRoute[] từ backend
        │── Nếu firstAction=L/R/B: gửi turnCmd trước
        │── s_wasDelegated=true → enterDelegatedReturn()
        │     missionDelegateSendReturnRouteOnly() → gửi retRoute chunks đến Slave
        │     gửi startReturn=1 → Slave thực hiện hành trình về
        │── Chuyển ST_MISSION_DELEGATED
        │
        └── Khi Slave về MED → missionStatus=3 → ST_IDLE
```

### 5b. Huỷ khi đang Outbound (ST_OUTBOUND – master controlled)

```
[MQTT cancel  hoặc  User bấm 1x]
        │── cancelPending = true
        │
        ▼ Tại RFID checkpoint kế tiếp
[processRfidEvent()]
        │── cancelPending = false
        │── enterCancelWait()
        │     dừng slave, gửi vị trí → backend
        │
        ▼
[Nhận retRoute → enterBack()] → về MED → ST_IDLE
```

---

## 6. Phát hiện vật cản (Obstacle)

```
[Task_Sensors — mỗi 50ms]
        │── Đọc ToF VL53L0X
        │── g_tofMm, g_tofValid
        │
        ▼
[smUpdate() → checkObstacle()]
        │── dist < 220mm → EVT_OBSTACLE_HIT
        │
        ▼
[enterObstacle()]
        │── stateBeforeObstacle = robotState
        │── ST_MISSION_DELEGATED? → gửi baseSpeed=0 (dừng slave)
        │── Các state khác: vX=vY=vR=0
        │── OLED: "OBSTACLE DETECTED"
        │── buzzerObstacle()
        │── Vào ST_OBSTACLE
        │
        ▼ Liên tục đọc ToF
[updateObstacle()]
        │── dist ≥ 300mm → EVT_OBSTACLE_CLEAR
        │── Beep mỗi 600ms khi còn vật cản
        │
        ▼
[exitObstacle()]
        │── Khôi phục state trước đó
        │── ST_OUTBOUND / ST_BACK / ST_RECOVERY_BLIND:
        │     turnCmd=0? → vX = LINE_BASE_SPEED (tiếp tục bám line)
        │── ST_MISSION_DELEGATED:
        │     baseSpeed = LINE_BASE_SPEED (slave tiếp tục tự chạy)
        │── ST_FOLLOW / ST_FOLLOW_RETURN:
        │     updateFollow() tự điều khiển
```

---

## 7. Chế độ Follow Mode

```
[Trigger]
        │── User 2x click từ ST_IDLE  ──┐
        │── User hold từ ST_IDLE      ──┤
        │── MQTT command "follow"     ──┘  (cần quét MED trước ≤ 30s)
        │
        ▼
[enterFollow()]
        │── Servo Y → 115° (nhìn lên, theo người)
        │── Servo X → khóa tâm
        │── huskySwitchToTagRecognition()
        │    (settle 350ms trước khi nhận frame đầu)
        │── followPidReset()
        │── OLED: "FOLLOW | Searching..."
        │
        ▼ Mỗi 50ms
[updateFollow()]
        │── huskyRequest() → HuskyTarget { xCenter, yCenter, detected, id }
        │── followPidUpdate(xCenter, yCenter, detected, tofDist, dt)
        │     │── PID pan: sai số X → vR (xoay)
        │     │── PID dist: tofDist → vX (tiến/lùi)
        │     │── Y tilt: sai số Y → gimbalSetY() (giữ target giữa frame)
        │     └── Trả FollowOutput { vX, vY, vR }
        │── Ultrasonic side check: ngăn lệch ngang khi cạnh hẹp
        │── Gửi masterMsg { vX, vY, vR } → Slave
        │── OLED: "Locked / Lost | dist cm"
        │
        ├── [1x click] → enterIdle()
        ├── [2x click] → enterFollowReturn()
        ├── [hold]     → enterIdle()
        └── [pin yếu ≥ 3s] → enterFollowReturn()
```

---

## 8. Follow-Return (Thoát Follow về MED)

```
[Trigger: 2x click trong ST_FOLLOW  hoặc  pin yếu 3s]
        │
        ▼
[enterFollowReturn()]
        │── s_recoveryFromFollow = true
        │── huskySwitchToLineTracking()
        │── Servo Y → 45° (SERVO_Y_LINE_SEARCH — tìm line)
        │── Servo X → bắt đầu sweep từ 20°→160°
        │── OLED: "FOLLOW RETURN | Searching..."
        │
        ▼ Mỗi 50ms
[updateFollowReturn()]
        │── huskyRequest() → HuskyLine
        │── Chưa thấy line:
        │     Servo X sweep 40°/s: 20°↔160°
        │── Thấy line:
        │     Servo X khoá tại góc tìm thấy
        │     → enterRecoveryVis()
        │
        └── [1x / 2x] → enterIdle()
```

---

## 9. Recovery Flow (Mất Line)

```
                    ┌─────────────────────────────────┐
                    │  Bước 1: RECOVERY_VIS            │
                    │  (Docking bằng camera)           │
                    └────────────────┬────────────────┘
                                     │
        [Từ ST_FOLLOW_RETURN]        │  [Từ ST_BACK mất line]
        s_recoveryFromFollow=true    │  s_recoveryFromFollow=false
        Servo Y/X giữ nguyên         │  Servo Y→45°, X→center
                                     │
                         ┌───────────▼────────────┐
                         │   HuskyLens LINE MODE   │
                         │   Mỗi 50ms:             │
                         │   - Thấy line:          │
                         │     vY = lateralErr×1.5 │
                         │     vR = angleErr×1.0   │
                         │     vX = 40 (creep)     │
                         │   - Không thấy:         │
                         │     vX=0, vY=0, vR=60   │
                         │     (quay tại chỗ)      │
                         └───────────┬────────────┘
                                     │
                         [EVT_SLAVE_SYNC – line docked]
                                     │
                    ┌────────────────▼────────────────┐
                    │  Bước 2: RECOVERY_BLIND          │
                    │  (Đi mù quét 2 RFID checkpoint) │
                    │  enableLine=1, vX=LINE_BASE_SPEED│
                    └────────────────┬────────────────┘
                                     │
                         [2 RFID scanned]
                         recoveryCpUids[0], [1]
                                     │
                    ┌────────────────▼────────────────┐
                    │  Bước 3: RECOVERY_CALL           │
                    │  Dừng robot                      │
                    │  mqttSendWaitingReturn(           │
                    │    currentNode, previousNode)     │
                    │  → Backend tính return route      │
                    └────────────────┬────────────────┘
                                     │
                   ┌─────────────────┴──────────────────┐
                   │                                     │
          [Backend trả route]                  [Timeout 5s]
          (≤ 5000ms)                        routeBuildReverseReturn()
                   │                                     │
                   └──────────────┬──────────────────────┘
                                  │
                     ┌────────────▼──────────────┐
                     │  Nếu firstAction=B/L/R:   │
                     │    gửi turnCmd trước       │
                     └────────────┬──────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │  s_wasDelegated?           │
                    ├── YES → enterDelegatedReturn()
                    │         gửi retRoute → Slave
                    │         startReturn=1
                    │         ST_MISSION_DELEGATED
                    └── NO  → enterBack()
                              Master bám line về MED
```

---

## 10. Giám sát pin

```
[Task_Sensors — mỗi 5000ms]
        │── batteryReadMv() → đọc ADC GPIO35
        │── Nếu adcMv < 500mV (USB only, không có pin) → bỏ qua
        │── batteryMvToPercent(adcMv):
        │     0%  ← 1580mV
        │     100% ← 3250mV
        │── g_battMv, g_battPct cập nhật
        │
        ├──[adcMv < 2100mV (~30%)] → g_battWarnSent=true
        │     mqttSendAlert("battery_warning", ...)
        │
        └──[adcMv < 1750mV (~10%)] → g_battCritSent=true
              mqttSendAlert("battery_critical", ...)

Hành vi theo state:

  ST_IDLE (lúc nhận mission assign hoặc 1x click):
        pin < 30% → reject, alert "mission_rejected_low_battery"
        OLED: "LOW BATTERY" → không khởi động mission

  ST_OUTBOUND (đang chạy):
        g_battWarnSent → s_battReturnOnArrival = true
        OLED: "LOW BATTERY"
        → Khi đến đích: bỏ qua chờ nút → tự động enterBack()

  ST_FOLLOW (đang follow):
        g_battWarnSent → bắt đầu đếm 3s
        OLED: "LOW BATTERY"
        Sau 3s → enterFollowReturn() → về MED

  ST_BACK / ST_RECOVERY_CALL (đang về):
        g_battCritSent → block mọi event MANUAL/MISSION
        Robot không thể bị interrupt, phải về MED
```

---

## 11. Bảng nút bấm vật lý

| State hiện tại | 1× Click | 2× Click | Hold (3s) |
|---|---|---|---|
| `ST_IDLE` | Start mission (cần route, pin ≥ 30%) | Vào Follow mode | Vào Follow mode |
| `ST_MISSION_DELEGATED` (chờ ở đích) | Gửi `startReturn` → Slave về | — | — |
| `ST_WAIT_AT_DEST` | `enterBack()` về MED | Hard abort → `ST_IDLE` | — |
| `ST_OUTBOUND` | Cancel mềm (chờ CP kế tiếp rồi về) | Hard abort → `ST_IDLE` | — |
| `ST_BACK` | Hard abort → `ST_IDLE` | Hard abort → `ST_IDLE` | — |
| `ST_RECOVERY_*` | Hard abort → `ST_IDLE` | Hard abort → `ST_IDLE` | — |
| `ST_FOLLOW` | Dừng → `ST_IDLE` | Vào `ST_FOLLOW_RETURN` | Dừng → `ST_IDLE` |
| `ST_FOLLOW_RETURN` | Dừng → `ST_IDLE` | Dừng → `ST_IDLE` | — |

> **Chú ý Priority Guard:** Khi pin tới hạn (`g_battCritSent`) và robot đang về (`ST_BACK` / `ST_RECOVERY_CALL`), mọi event có priority < `MODE_SAFETY` đều bị chặn.

---

## 12. Giao tiếp MQTT – Backend

### Topics Master → Backend (Publish)

| Topic | Khi nào | Payload chính |
|---|---|---|
| `.../telemetry` | Mỗi 1s | `batteryLevel`, `batteryMv`, `status`, `robotMode`, `currentNodeId`, `destBed` |
| `.../alert` | Khi có sự kiện | `alertType`, `message`, `batteryMv`, `batteryLevel` |
| `.../mission/progress` | Mỗi RFID checkpoint | `missionId`, `currentNodeId`, `routeIndex`, `routeTotal` |
| `.../mission/complete` | Slave đến đích | `missionId` |
| `.../mission/returned` | Slave / Robot về MED | `missionId` |
| `.../position/waiting_return` | Sau cancel / sau 2 RFID | `currentNodeId`, `previousNodeId`, `missionId` |

### Topics Backend → Master (Subscribe)

| Topic | Nội dung | Hành động của Master |
|---|---|---|
| `.../mission/assign` | `outRoute[]`, `retRoute[]`, `missionId` | Load route, pre-send chunks đến Slave |
| `.../mission/cancel` | `missionId` | `smStartDelegatedCancel()` hoặc `routeHandleCancel()` |
| `.../mission/return_route` | `retRoute[]`, `firstAction` | `routeParseReturn()` → `updateRecoveryCall()` |
| `.../command` | `stop`, `resume`, `follow`, `idle` | Điều khiển trực tiếp state machine |

### Backend xử lý `position/waiting_return`

```
[Nhận currentNodeId + previousNodeId]
        │
        ▼
[calculateReturnRouteFromNode(currentNode, previousNode, mapId)]
        │── Lấy node hiện tại từ bản đồ
        │── Tính vector hướng đi: previousNode → currentNode
        │── Xác định hướng robot đang nhìn
        │── Tìm đường về MED (BFS/pathfinding trên map graph)
        │── Nếu hướng ngược MED: firstAction = 'B' (quay 180°)
        │── Nếu previousNode=null: mặc định firstAction = 'B'
        │
        ▼
[Publish MQTT: mission/return_route]
        { retRoute: [...], firstAction: 'B'/'F'/'L'/'R' }
```

---

## 13. Luồng dữ liệu Frontend

```
[React App]
        │
        ├── useRobots()  ──► GET /api/robots         → danh sách robot, trạng thái
        │                ──► Socket.IO "robotUpdate"  → realtime update
        │
        ├── usePatients() ─► GET /api/patients        → danh sách bệnh nhân
        │
        ├── useAlerts()  ──► GET /api/alerts?resolved=false → alerts đang active
        │                    resolveAlert(id)          → PATCH /api/alerts/:id
        │                    resolveAllAlerts(ids[])   → PATCH × n (parallel)
        │
        ├── RobotCenter
        │     │── Assign mission → POST /api/missions
        │     │── Cancel mission → DELETE /api/missions/:id
        │     └── Send command   → POST /api/robots/:id/command
        │
        ├── RobotLiveTracking
        │     └── Hiển thị bản đồ, vị trí robot realtime (currentNodeId)
        │
        ├── MissionHistoryTab
        │     └── GET /api/missions/history  → lịch sử nhiệm vụ
        │
        └── Alert Panel (Bell button ở header)
              │── Badge: số alerts chưa xử lý
              │── Dropdown: danh sách alert + Dismiss từng cái
              └── "Dismiss all" → resolveAllAlerts()

Alert types:
  carry_low_battery         | level: medium
  battery_warning           | level: medium
  battery_critical          | level: high
  mission_rejected_low_battery | level: medium
  rescue_required           | level: high
  route_deviation           | level: medium
  info                      | level: low
```

---

## Tóm tắt luồng hoạt động chính (Happy Path)

```
[Dashboard] Điều dưỡng tạo nhiệm vụ cho CARRY-01
      │
      ▼
[Backend] Publish MQTT mission/assign
      │
      ▼
[Master] Load route → Gửi route chunks đến Slave qua ESP-NOW
      │   OLED: "Route loaded, press SW"
      │
      ▼ [User bấm nút trên xe]
[Master] Pin kiểm tra ≥ 30% ✓ → missionDelegateStartMission()
      │   Gửi { missionStart=1, enableLine=1, baseSpeed=165 } → Slave
      │   Trạng thái: ST_MISSION_DELEGATED
      │
      ▼
[Slave] Tự bám line, quét RFID, rẽ theo route
      │   Gửi progress (routeIndex, rfid_uid) → Master mỗi checkpoint
      │
      ▼
[Master] Forward progress → Backend (MQTT mission/progress)
      │   Backend → Frontend: cập nhật bản đồ web realtime
      │
      ▼ [Slave đến đích]
[Master] mqttSendComplete() → Backend cập nhật trạng thái
      │   Gửi turnCmd='B' → Slave xoay 180° + beep
      │   OLED: "WAIT AT DEST [1x]Return"
      │
      ▼ [User bấm nút]
[Master] Gửi startReturn=1 → Slave chạy retRoute về MED
      │
      ▼ [Slave về MED, quét RFID "MED"]
[Master] mqttSendReturned() → enterIdle()
      │   Backend → Frontend: nhiệm vụ hoàn thành
      │
      ▼
[Dashboard] Cập nhật lịch sử, trạng thái robot = "idle"
```
