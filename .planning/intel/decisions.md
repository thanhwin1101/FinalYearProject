# Decisions (ADRs)

Các quyết định dưới đây đã được LOCK qua đợt alignment 2026-04 sau khi review
codebase thực tế vs doc gốc. Bất kỳ thay đổi nào trái ngược phải có ADR mới
override cụ thể.

---

## ADR-001 — Two-MCU architecture (ESP32 master + STM32 slave)

**Status:** LOCKED
**Decision:** ESP32 lo WiFi/MQTT/UX/OTA/pin/relay; STM32 lo motor real-time,
line sensor, NFC, ToF, servo Y, Huskylens UART. Tách qua UART 0x7E + CRC8.

**Rationale:** Tách real-time (STM32 deterministic ~1 ms) khỏi IO blocking
(ESP32 WiFi). ESP32 không chịu được PID loop 1 kHz khi WiFi stack wake.

**Source:** AGV/docs/agent_skill.txt, AGV/docs/Checklist.txt (1.4, 2.2),
thực tế trong esp32_master/src/*, stm32_slave/src/uart_protocol.cpp.

---

## ADR-002 — UART protocol 0x7E frame + CRC8 (poly 0x07)

**Status:** LOCKED
**Decision:** `[STX 0x7E][LEN][CMD][DATA…][CRC8]`. LEN = sizeof(CMD + DATA).
CRC over CMD+DATA. Baud 115200.

Command space:
- `0x01–0x0A`: ESP32 → STM32 (SET_MODE, SEND_ROUTE, DIRECT_VEL, REQUEST_STATUS,
  CANCEL_MISSION, CONFIRM_ARRIVAL, SERVO_SET, WHEEL_SET, SERVO_SWEEP, TUNE_SPEED).
- `0x81–0x8D`: STM32 → ESP32 (BATTERY reserved, CHECKPOINT, OBSTACLE, ACK,
  MISSION_DONE, MISMATCH, DEBUG_MSG, LINE_LOST, HUSKY_STATUS, TAG_LOST,
  TAG_FOUND, LINE_STATUS, TOF_DIST).

**Source:** esp32_master/src/uart_protocol.h, stm32_slave/src/uart_protocol.h.

---

## ADR-003 — Two-relay power-domain split (NOT three)

**Status:** LOCKED (override từ doc gốc)
**Decision:** Chỉ 2 relay:
- **R1 (GPIO18)** = HuskyLens + servo power ("vision domain").
- **R2 (GPIO23)** = Line sensors + PN532 power ("auto domain").

Line và NFC dùng chung vì cùng được bật/tắt theo mode AUTO. Tiết kiệm GPIO
+ đơn giản hoá logic relay. Doc gốc (agent_skill.txt) mô tả 3 relay
(R1/R2/R3) — **không còn áp dụng**.

**Trade-off:** Không thể off riêng NFC (chỉ off line). Chấp nhận được vì
PN532 low-power và không gây cross-talk với line.

**Rollback:** Nếu cần tách → thêm 1 GPIO digital + cập nhật
`relay_control.cpp` (thêm `relayNfcOn/Off()` thật thay vì alias).

**Source:** esp32_master/src/relay_control.cpp, esp32_master/src/config.h
(chỉ có `PIN_RELAY_VISION` + `PIN_RELAY_LINE_NFC`, không có R3).

---

## ADR-004 — MQTT topic namespace

**Status:** LOCKED
**Decision:**

| Topic                  | Hướng          | Mục đích                             |
|------------------------|----------------|--------------------------------------|
| `carry/robot/cmd`      | backend→robot  | Mọi command (route, cancel, stop,…)  |
| `carry/robot/evt`      | robot→backend  | Event + telemetry (JSON)             |
| `carry/robot/debug`    | robot→backend  | STM32 debug messages                 |
| `robot/return_request` | robot→backend  | Xin route về khi cancel/mismatch     |

**Rationale:** Prefix `carry/robot/*` để không xung đột nếu hospital có
nhiều fleet. `robot/return_request` giữ tên cũ để tương thích backend.

**Source:** esp32_master/src/mqtt_client.cpp, đã verify trong codebase.

---

## ADR-005 — Battery ownership: ESP32 qua ADC GPIO35

**Status:** LOCKED
**Decision:** ESP32 là nguồn duy nhất đo pin. ADC GPIO35, voltage divider,
16 samples/read, đọc mỗi 5 s, publish telemetry mỗi 5 s.

Map tuyến tính:
- 3.036 V → 10 % (floor)
- 3.600 V → 100 %

Gate: `BATT_MIN_PERCENT = 30 %` — từ chối mission assign nếu ≤ 30 %,
warn + 10 s countdown trong Follow mode rồi Recovery.

STM32 KHÔNG gửi `CMD_BATTERY` nữa. CMD 0x81 reserved cho pin motor riêng
nếu sau này tách nguồn.

**Source:** esp32_master/src/battery.cpp, main.cpp handler CMD_BATTERY.

---

## ADR-006 — Find mode fallback: 30 s time-based (KHÔNG quay SR05)

**Status:** LOCKED (override từ doc gốc)
**Decision:** Follow mode mất tag → 30 s countdown với buzzer nhịp 300/150.
Nếu thấy lại tag trong 30 s → tiếp tục Follow. Nếu không → chuyển Recovery.

Doc gốc (Follow_mode.txt) đề xuất "3 lần quay SR05 trái/phải" — **không áp
dụng** vì:
- Hardware không có SR05 (ToF VL53L0X thay thế, không phù hợp sweep).
- Rotation mù có thể làm robot lao vào tường hoặc người.

**Trade-off:** Không tự tìm lại tag qua body rotation. Chấp nhận — người
bám robot trong Follow nên sẽ tự đi lại vào tầm nhìn.

**Source:** esp32_master/src/follow_mode.cpp (s_tagLostAt logic).

---

## ADR-007 — FreeRTOS: KHÔNG dùng (single-threaded loop)

**Status:** LOCKED
**Decision:** ESP32 dùng `loop()` đơn thay vì task FreeRTOS. Checklist mục
1.18 đề xuất tách MQTT/UART/Control/OLED/Button/Battery thành task —
**không áp dụng**.

**Rationale:**
- Globals đều `volatile` nhưng không dùng mutex → task hoá sẽ gây race.
- Throughput hiện tại đủ: MQTT ~100 Hz poll, UART ~500 Hz, OLED 5 Hz.
- Watchdog 30 s đủ bắt runaway (xem ADR-008).

**Rollback:** Nếu latency MQTT bị ảnh hưởng bởi OLED render → tách chỉ
riêng OLED task. Không tách toàn bộ.

**Source:** esp32_master/src/main.cpp (loop() monolithic).

---

## ADR-008 — Task Watchdog (ESP32) = 30 s, panic=true

**Status:** LOCKED
**Decision:** `esp_task_wdt_init(30, true)` trong setup(). Feed mỗi loop() +
trong các boot phase có delay dài (WiFi connect, MQTT wait, relay settle).

**Rationale:** 30 s rộng rãi cho WiFi reconnect (10-20 s worst case).
Panic=true → hard reset thay vì chỉ print warning.

**Source:** esp32_master/src/main.cpp (setup + loop).

---

## ADR-009 — Non-blocking motor turns (safety fix A-BUG-01)

**Status:** LOCKED
**Decision:** `motor_turnInPlace()` blocking được thay bằng API non-blocking:
- `motor_startTurn(dir)` — kick off
- `motor_turnStep()` — poll mỗi tick, return true nếu đang quay
- `motor_abortTurn()` — cắt giữa chừng (cho cancel mission)

AUTO_DO_TURN state trong auto_runner giờ thực sự chạy. Trong lúc turn, UART
RX vẫn hoạt động → CANCEL_MISSION có latency < 10 ms thay vì 950-1900 ms.

**Source:** stm32_slave/src/motor_control.cpp, auto_runner.cpp (AUTO_DO_TURN).

---

## ADR-010 — MED-gate cho mode switch nút bấm

**Status:** LOCKED
**Decision:** Long-press chuyển `AUTO → FOLLOW` CHỈ khi:
- `g_autoState == AUTO_IDLE`, VÀ
- `g_lastCheckpointId == MED_CHECKPOINT_ID (0x8083)` hoặc == 0 (chưa scan CP)

`FOLLOW → AUTO` vẫn cho phép tự do. `RECOVERY` không thể exit qua nút —
chỉ backend `return_route` mới exit được.

**Rationale:** Robot ở giữa hành lang không nên chuyển Follow vì sẽ mất
vị trí route.

**Source:** esp32_master/src/main.cpp (checkModeSwitch).

---

## ADR-011 — Line-lost safe brake (safety fix A-BUG-07)

**Status:** LOCKED
**Decision:** 3-tier response khi line_isLost(bits):
- 0–400 ms: giữ hướng cũ, 100 % tốc độ.
- 400–1500 ms: creep 1/3 tốc độ + publish `line_lost` event.
- >1500 ms: **BRAKE cứng** + chuyển `AUTO_IDLE` để operator/MQTT xử lý.

Doc gốc (Auto_mode.txt) nói "phanh mềm rồi dừng" — giờ explicit 3 stages.

**Source:** stm32_slave/src/auto_runner.cpp (lineFollowStep).

---

## ADR-012 — Emergency stop: MQTT `{"action":"stop"}`

**Status:** LOCKED
**Decision:** `g_stopped = true` → ESP32 gửi `CMD_CANCEL_MISSION` + zero
`CMD_DIRECT_VEL` + zero `CMD_WHEEL_SET` cho STM32; OLED hiện "EMERGENCY
STOP"; loop() bỏ qua mọi mode-loop (early return).

`{"action":"resume"}` → `g_stopped = false` + beep + resume bình thường.

**Rationale:** Trước đây flag `g_stopped` được set nhưng không có handler
— robot vẫn chạy. An toàn nghiệm trọng.

**Source:** esp32_master/src/main.cpp (checkEmergencyStop).

---

## Deferred (chưa ADR, nhưng cần trước production)

- **Credential rotation** — MQTT_DEFAULT_PASS, OTA_PASSWORD, Hospital
  Dashboard `.env` trong git history. Cần scrub + rotate.
- **Dashboard API auth** — chưa có JWT/session trên Express routes.
- **OTA firmware signing** — chưa verify signature trước khi flash STM32.
- **Automated test harness** — hiện chỉ manual. Pytest/unity cho
  unit tests motor kinematics + PID + CRC là bước đầu dễ làm.
- **STM32 IWDG** — 2 s timeout + feed trong main loop.
