# Hospital AGV Carrier Robot

## What This Is

Robot tự hành (AGV) giao thuốc/đồ vật trong hành lang bệnh viện. Hệ hai vi điều khiển: ESP32 master (WiFi/MQTT/OLED/UX/pin) + STM32 slave (motor mecanum, line follow, PN532 NFC checkpoint, ToF, servo) giao tiếp qua UART 0x7E + CRC8. Backend Node.js + dashboard Vite/TypeScript điều phối route và theo dõi telemetry. Cho điều dưỡng/dược sĩ ra mission từ giường (MED) → phòng bệnh nhân và quay về.

## Core Value

**AGV hoàn thành 100 % route giao thuốc trong hành lang test mà không va chạm và không cần can thiệp tay** — mọi quyết định kiến trúc, fix, và phase ưu tiên đều phải bảo toàn lat ency thấp và safety.

## Requirements

### Validated

<!-- Đã ship & quan sát chạy ổn (v0 codebase pre-pilot) -->

- ✓ ESP32 ↔ STM32 UART protocol 0x7E + CRC8 với toàn bộ command set 0x01–0x0A / 0x82–0x8D — `existing` (verify trong `uart_protocol.cpp` cả 2 phía)
- ✓ Auto mode line-follow + PN532 checkpoint detection + ToF stop + 180° rotate khi mission complete — `existing` (`stm32_slave/src/auto_runner.cpp`)
- ✓ Follow mode Huskylens tag tracking + servo Y + Vx/Vy/Vr velocity command — `existing` (`esp32_master/src/follow_mode.cpp`)
- ✓ MQTT command channel `carry/robot/cmd` ↔ `carry/robot/evt` với route ingest, cancel, status — `existing` (`mqtt_client.cpp`)
- ✓ Battery monitoring qua ESP32 ADC GPIO35 với gate ≥30 % — `existing` (`battery.cpp`)
- ✓ OLED SH1106 per-mode screens U8g2 — `existing` (`oled_display.cpp`)
- ✓ WiFiManager portal (AP `Robot_Setup`) cho onboarding credentials — `existing` (`net_monitor.cpp`)
- ✓ ESP32 OTA Arduino + bootloader STM32 OTA flow — `existing` (`ota_manager.cpp`)
- ✓ Hospital Dashboard backend Express + MQTT bridge + MongoDB CRUD missions/patients/maps — `existing` (`Hospital Dashboard/Backend/`)
- ✓ Hospital Dashboard frontend Vite/React/TS với live SSE robot view — `existing` (`Hospital Dashboard/Frontend/`)
- ✓ Safety wave 2026-04: non-blocking turn (ADR-009), line-lost safe brake (ADR-011), MED-gate (ADR-010), task watchdog 30 s (ADR-008), emergency-stop MQTT (ADR-012) — `existing` (xem `.planning/codebase/CONCERNS.md` §1.5)

### Active

<!-- M1 = Safety + Pilot Readiness — đóng các blocker để pilot 1 hành lang test thật. -->

- [ ] Rotate toàn bộ credentials + purge `.env` khỏi git history
- [ ] Backend API authentication (JWT/session) + RBAC tối thiểu (nurse / admin)
- [ ] MQTT broker ACL + TLS bật giữa robot ↔ broker ↔ backend
- [ ] STM32 OTA signing (Ed25519) + HTTPS firmware fetch + signature verify trong bootloader
- [ ] Recovery mode 2 giai đoạn thật: Huskylens servo-X scan để tìm line → NFC anchor → request return route (thay stub hiện tại)
- [ ] Hardening MQTT command (`relay`, `direct_vel`, `wheel_set`) bằng HMAC + nonce, gate khi không phải IDLE
- [ ] Backend baseline security: helmet + rate-limit + magic-byte file upload validation + CORS allowlist
- [ ] Bộ integration test E2E hành lang (4-6 checkpoint) đo: tỉ lệ thành công route, latency CANCEL, no-collision, recovery success rate
- [ ] STM32 IWDG 2 s + watchdog feed trong main loop
- [ ] Operator runbook (boot, portal, mission, cancel, recovery, OTA, alarm) viết được cho điều dưỡng test pilot

### Out of Scope

- **Multi-fleet / multi-robot orchestration** — pilot 1 robot trong 1 hành lang. Schedule scaling sau khi v1 chứng minh giá trị lâm sàng.
- **Real-time chat / video streaming từ robot** — không thuộc bài toán giao thuốc. Tăng đáng kể bandwidth + privacy debt.
- **Voice control / NLP commands** — UX hiện tại là button + dashboard.
- **Mobile app native** — dashboard web đủ trong 12 tháng đầu.
- **FreeRTOS multi-task ESP32** — đã quyết định KHÔNG dùng (ADR-007). Single loop + WDT đủ throughput.
- **3-relay power split** — đã chuyển sang 2-relay (ADR-003). Tách riêng NFC sẽ override ADR.
- **Battery telemetry từ STM32 qua CMD 0x81** — ESP32 sở hữu (ADR-005). CMD 0x81 reserved cho pin motor riêng nếu sau này tách nguồn.
- **Mecanum holonomic full strafing trong Auto** — Auto chỉ line-follow + 90°/180° turn. Holonomic chỉ dùng ở Recovery + Follow.

## Context

**Tech ecosystem:**
- Embedded: PlatformIO + Arduino framework, ESP32-WROOM-32 (master), STM32F103C8 Bluepill (slave). C++/C.
- Backend: Node.js 18+, Express, Mongoose/MongoDB, mqtt.js, multer (uploads). Mosquitto broker.
- Frontend: Vite + React 18 + TypeScript, TanStack Query, Tailwind, Recharts, MapLibre.
- Local dev: Windows 10/11 + PowerShell + Mosquitto local; production target Linux server in hospital IT.

**Key sensors / actuators:** Huskylens (UART), PN532 NFC (SPI), VL53L0X ToF (I2C), 3× line sensor, 4× mecanum motor qua 2× L298N, servo Y, OLED SH1106, 2 relay (vision + line/NFC).

**Existing artifacts:**
- 5 docs trong `AGV/docs/*.txt` đã được ingest (PRD `agent_skill.txt`, SPECs `Auto_mode.txt` + `Follow_mode.txt`, DOC `setup_wifi_MQTT.txt`, Checklist mới rewrite).
- Codebase mapping: `.planning/codebase/{ARCHITECTURE,STACK,STRUCTURE,CONVENTIONS,INTEGRATIONS,TESTING,CONCERNS}.md`.
- Intel: `.planning/intel/{requirements,constraints,context,decisions,SYNTHESIS}.md` + 12 ADRs locked.
- Conflict report: `.planning/INGEST-CONFLICTS.md` (0 BLOCKERS sau synthesizer).

**Known issues (đã tracked trong CONCERNS.md):**
- 🔥 Critical: H-SEC-01 (`.env` trong git), A-SEC-01/02 (hardcoded MQTT/OTA password), A-SEC-03 (STM32 OTA không verify signature), H-SEC-03 (Backend không có auth).
- 🔥 High: A-BUG-04/05 (Recovery mode chỉ là stub).
- ✅ Đã fix wave 2026-04: A-BUG-01, A-BUG-07, A-BUG-06, A-ARCH-02, một phần A-ARCH-03, A-TD-01/02 — chi tiết §1.5 CONCERNS.md.

**Pilot context:** Pilot 1 hành lang test đầu tiên — 4-6 checkpoint, 1 robot, 1-2 điều dưỡng vận hành. Mục tiêu lâm sàng: 100 % delivery thành công không cần intervention trong 50 chuyến liên tiếp.

## Constraints

- **Tech stack**: PlatformIO + Arduino framework cho cả ESP32 và STM32 — đã ADR-001. Không đổi sang ESP-IDF / FreeRTOS thuần.
- **Tech stack**: Node.js 18+ cho backend, Vite/React 18 frontend — không đổi vì `package.json` lock.
- **Hardware**: Hardware đã cố định (xem ADR-001/003/005). Không thiết kế lại PCB; mọi fix phải làm được trên board hiện tại.
- **Communication**: UART 0x7E + CRC8 baud 115200 — đã ADR-002. Đổi protocol = đổi cả 2 firmware = breaking change.
- **Safety**: Mọi thay đổi motor / state machine PHẢI giữ được CANCEL latency < 50 ms và line-lost safe brake < 1.5 s (xem ADR-009/011).
- **Network**: Hospital WiFi 2.4 GHz, broker on-premise Mosquitto. Không phụ thuộc cloud.
- **Privacy/Security**: Patient PII không được log clear; broker bắt buộc auth + (mục tiêu M1) TLS.
- **Timeline**: Pilot trong test corridor trong 4-6 tuần kể từ M1 kickoff. Không có deadline cứng cho deployment thật, nhưng mọi blocker phải close trước pilot.
- **Operator skill**: Vận hành bởi điều dưỡng / dược sĩ — UI phải đơn giản, OLED + 1 button + dashboard web. Không yêu cầu IT skill khi vận hành ngày thường.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Two-MCU split (ESP32 + STM32) qua UART (ADR-001) | Tách real-time (1 ms) khỏi WiFi blocking | ✓ Good — đã chứng minh chạy ổn |
| 2-relay power split thay 3 (ADR-003) | Tiết kiệm GPIO; line + NFC cùng on/off theo mode AUTO | ✓ Good — không thấy cross-talk |
| ESP32 sở hữu battery (ADR-005), STM32 không gửi CMD 0x81 | Đơn nguồn chân lý, ADC ESP32 đã có sẵn | ✓ Good |
| KHÔNG dùng FreeRTOS task ESP32 (ADR-007) | Globals chưa atomic; throughput đủ với loop() | — Pending — re-evaluate nếu OLED stutter |
| Find mode fallback = 30 s time-based (ADR-006) | Hardware không có SR05 phù hợp sweep; rotation mù không an toàn | ✓ Good |
| Task watchdog 30 s panic=true (ADR-008) | Bắt loop runaway, đủ thời gian WiFi reconnect | ✓ Good |
| Non-blocking turn API (ADR-009) | Xử lý CANCEL + ToF trong khi quay; safety-critical | ✓ Good — fix A-BUG-01 |
| MED-gate cho mode switch nút (ADR-010) | Tránh chuyển Follow giữa hành lang gây mất route | ✓ Good — fix A-BUG-06 |
| Line-lost 3-tier safe brake (ADR-011) | Tránh drift > 1.5 s khi mất line | ✓ Good — fix A-BUG-07 |
| Emergency stop MQTT (ADR-012) | Điều dưỡng/dashboard cắt khẩn cấp | ✓ Good |
| **M1 = Safety + Pilot Readiness** | Đóng blocker để pilot thật, không chạy theo feature mới | — Pending — milestone đang plan |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions (và xem có cần ADR mới trong `intel/decisions.md` không)
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state (deployment, pilot feedback, metrics)

---
*Last updated: 2026-04-20 after initialization (post safety-fix wave + ADR lock)*
