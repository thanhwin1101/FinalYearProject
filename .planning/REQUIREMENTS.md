# Requirements: Hospital AGV Carrier Robot

**Defined:** 2026-04-20
**Core Value:** AGV hoàn thành 100 % route giao thuốc trong hành lang test mà không va chạm và không cần can thiệp tay.
**Milestone scope:** M1 — Safety + Pilot Readiness. Đóng các blocker để pilot 1 hành lang test thật.

---

## v1 Requirements (M1)

Tất cả requirements dưới đây phải DONE trước khi pilot. Mỗi req map vào đúng 1 phase trong ROADMAP.md.

### Security — Secrets & credentials

- [ ] **SEC-01**: Tất cả MQTT/OTA/MongoDB credentials trong source code và `.env.example` được thay bằng placeholder; firmware/backend FAIL-FAST nếu env không được provision (không fallback default).
- [ ] **SEC-02**: `.env` thật bị xóa khỏi git history (filter-repo / BFG); credentials cũ rotated trên broker + MongoDB; documented quy trình rotate.
- [ ] **SEC-03**: Secret-scan CI workflow (gitleaks hoặc trufflehog) bật trên mọi PR, fail build nếu phát hiện secret.
- [ ] **SEC-04**: Per-device OTA password derived từ ESP32 chip-ID + deployment salt, lưu trong NVS provisioning step (không compile vào binary).

### Security — Backend AuthN/AuthZ

- [ ] **AUTH-01**: Backend Express triển khai JWT (hoặc session) auth middleware; mọi route trừ `/health` đòi token hợp lệ; SSE `/api/robots/live` cũng auth.
- [ ] **AUTH-02**: User model có `passwordHash` (argon2 hoặc bcrypt) + role enum (`nurse`, `pharmacist`, `admin`); seed admin account qua script.
- [ ] **AUTH-03**: RBAC tối thiểu: chỉ `admin` được DELETE/PUT users + maps + robots; `nurse` chỉ POST mission delivery + GET telemetry; mọi route khác trả 403 cho role không phù hợp.
- [ ] **AUTH-04**: Login endpoint `/api/auth/login` (email + password → JWT) + `/api/auth/me` + frontend login page; session persist qua refresh.

### Security — MQTT hardening

- [ ] **MQT-01**: Mosquitto broker bật TLS (cert tự ký nội bộ chấp nhận); robot ↔ broker và backend ↔ broker đều dùng `mqtts://`.
- [ ] **MQT-02**: Broker ACL chỉ cho phép user `robot_<id>` publish vào subtree `carry/robot/<id>/*` của chính nó; backend user có quyền đọc tất cả + publish `carry/robot/+/cmd`.
- [ ] **MQT-03**: Mọi MQTT command motion-related (`relay`, `relay_resume`, `direct_vel`, `wheel_set`) gắn HMAC SHA256 với pre-shared key + nonce + timestamp; ESP32 reject nếu HMAC sai hoặc nonce replay (ring buffer 32 entries) hoặc timestamp lệch >5 s.
- [ ] **MQT-04**: Backend validate inbound MQTT payload (`/alert`, `/mission/progress`, `/telemetry`) bằng zod/joi schema trước khi ghi DB; reject + log nếu sai schema.

### Security — Backend baseline

- [ ] **WEB-01**: Backend bật `helmet()` với CSP cơ bản + `express-rate-limit` (100 req / 15 min mỗi IP cho write routes; 20 req / 15 min cho `/api/auth/login`).
- [ ] **WEB-02**: CORS đổi từ `*` sang allowlist qua env `CORS_ORIGIN`; credentials chỉ allow khi origin match.
- [ ] **WEB-03**: File upload (multer) validate magic-byte qua `file-type`, re-encode qua `sharp` để strip EXIF, random UUID filename, `limits.fileSize` 5 MB; reject non-image trên route patient.

### Firmware — OTA security

- [ ] **OTA-01**: Tooling sign STM32 firmware Ed25519 build-time (script Python); manifest JSON kèm `version`, `sha256`, `signature`, `target` published bởi backend qua `carry/robot/<id>/cmd` action `ota_stm32`.
- [ ] **OTA-02**: ESP32 fetch firmware qua HTTPS (cert pinning), verify `sha256` + Ed25519 signature trước khi gọi `blMassErase` STM32; reject + alert nếu bất kỳ check nào fail.
- [ ] **OTA-03**: ESP32 OTA Arduino dùng password derived (xem SEC-04); reject upload nếu password sai.

### Firmware — Recovery mode v2

- [ ] **REC-01**: Recovery entry: ESP32 gửi `relaySetRecovery()` (vision relay ON cho Huskylens + servo) thay vì `relaySetAuto()`; emergency stop trước khi reconfigure.
- [ ] **REC-02**: Recovery Phase 1 — Huskylens line-tracking mode + servo X sweep 0°→180° @ 1°/50 ms; khi Huskylens detect line, dừng servo, command Vx/Vy/Vr center line theo offset; khi line sensor center eye active → stop, OLED "Line found", chuyển Phase 2; full sweep không thấy line → buzzer×3 + retry max 3 lần → escalate MQTT error.
- [ ] **REC-03**: Recovery Phase 2 — OLED prompt "Đặt thẻ NFC", PN532 đọc trong 30 s; on success publish `robot/return_request` với CP id, đợi route 10 s; route OK → trở về Auto + chạy route; timeout NFC hoặc route → OLED error + giữ Recovery cho operator can thiệp.
- [ ] **REC-04**: Recovery cho phép thoát qua MQTT `{"action":"abort_recovery"}` (admin only) → quay về Follow IDLE + safe stop.

### Firmware — Robustness

- [ ] **ROB-01**: STM32 IWDG bật với timeout 2 s; feed trong main loop và mọi handler dài; nếu loop block > 2 s → board reset.
- [ ] **ROB-02**: STM32 publish `boot_event` (qua CMD_DEBUG_MSG) khi reset, kèm reason code (IWDG / power / soft); ESP32 forward lên MQTT để backend log.
- [ ] **ROB-03**: ESP32 detect MQTT broker mất kết nối > 30 s → command STM32 cancel mission + OLED warning; reconnect tự động với exponential backoff 1 / 2 / 4 / 8 / 16 / 30 s.

### Operations — Test & runbook

- [ ] **TEST-01**: Bộ integration test E2E hành lang: 4-6 NFC checkpoint thật, script Python publish route lên MQTT + đo: success rate (đến đúng CP), CANCEL latency, no-collision (ToF event count), recovery success (mất line cố ý → robot tự brake), battery gate (mock < 30 % → reject mission).
- [ ] **TEST-02**: Pass criteria pilot: 50 chuyến liên tiếp, ≥ 49 thành công không can thiệp, 0 va chạm, CANCEL latency < 50 ms median.
- [ ] **TEST-03**: STM32 unit test (PlatformIO + Unity) cho: CRC8, route parser, motor kinematics `mecanum(Vx,Vy,Vr)`, line_isLost state machine. Tối thiểu 80 % branch coverage trên 4 module này.
- [ ] **DOC-01**: Operator runbook viết bằng tiếng Việt cho điều dưỡng: boot sequence, WiFi portal, gửi mission từ dashboard, cancel, xử lý alarm OLED, khi nào gọi IT. PDF + bản web.
- [ ] **DOC-02**: Deployment runbook cho IT: install Mosquitto + cert, seed admin account, provision robot (NVS write credentials + OTA pass), backup MongoDB.

## v2 Requirements (deferred sau M1)

### Multi-fleet

- **MFLT-01**: Multi-robot coordination + collision avoidance giữa các robot trong cùng hành lang.
- **MFLT-02**: Map editor cho điều phối nhiều tầng / nhiều khu.

### UX nâng cao

- **UX-01**: Dashboard mission scheduling (queue + priority).
- **UX-02**: Push notification cho điều dưỡng khi mission complete / fail.
- **UX-03**: Voice prompt trên loa robot khi đến CP đích.

### Telemetry & analytics

- **ANA-01**: Dashboard analytics: avg delivery time, success rate per ward, battery cycle count.
- **ANA-02**: Predictive maintenance từ motor current + battery curve.

### Hardware refresh

- **HW-01**: Tách relay R3 cho NFC riêng (rollback ADR-003) nếu phát hiện cross-talk.
- **HW-02**: Battery 2nd-source telemetry qua STM32 ADC (re-activate CMD 0x81).

## Out of Scope

| Feature | Reason |
|---------|--------|
| Multi-fleet orchestration | Pilot 1 robot + 1 hành lang trong M1 (Out of Scope PROJECT.md) |
| Real-time chat / video stream từ robot | Không thuộc bài toán giao thuốc; bandwidth + privacy debt |
| Voice control / NLP commands | UX hiện tại là button + dashboard, đủ cho điều dưỡng |
| Mobile app native | Web dashboard đủ trong 12 tháng đầu |
| FreeRTOS multi-task ESP32 | ADR-007 quyết định KHÔNG dùng; loop() đủ throughput |
| 3-relay power split | ADR-003 đã chuyển sang 2-relay |
| STM32 battery telemetry CMD 0x81 | ESP32 sở hữu (ADR-005); CMD reserved cho HW future |
| Mecanum holonomic full strafing trong Auto | Auto chỉ line-follow + 90°/180° turn |

## Traceability

Phase mapping được điền bởi gsd-roadmapper sau khi tạo ROADMAP.md.

| Requirement | Phase | Status |
|-------------|-------|--------|
| SEC-01 | Phase 01 | Pending |
| SEC-02 | Phase 01 | Pending |
| SEC-03 | Phase 01 | Pending |
| SEC-04 | Phase 01 | Pending |
| AUTH-01 | Phase 03 | Pending |
| AUTH-02 | Phase 03 | Pending |
| AUTH-03 | Phase 03 | Pending |
| AUTH-04 | Phase 03 | Pending |
| MQT-01 | Phase 02 | Pending |
| MQT-02 | Phase 02 | Pending |
| MQT-03 | Phase 02 | Pending |
| MQT-04 | Phase 02 | Pending |
| WEB-01 | Phase 04 | Pending |
| WEB-02 | Phase 04 | Pending |
| WEB-03 | Phase 04 | Pending |
| OTA-01 | Phase 05 | Pending |
| OTA-02 | Phase 05 | Pending |
| OTA-03 | Phase 05 | Pending |
| REC-01 | Phase 06 | Pending |
| REC-02 | Phase 06 | Pending |
| REC-03 | Phase 06 | Pending |
| REC-04 | Phase 06 | Pending |
| ROB-01 | Phase 07 | Pending |
| ROB-02 | Phase 07 | Pending |
| ROB-03 | Phase 07 | Pending |
| TEST-01 | Phase 08 | Pending |
| TEST-02 | Phase 08 | Pending |
| TEST-03 | Phase 08 | Pending |
| DOC-01 | Phase 08 | Pending |
| DOC-02 | Phase 08 | Pending |

**Coverage:**
- v1 requirements: 30 total
- Mapped to phases: 30 ✓
- Unmapped: 0 ✓ — roadmapper đã map 100 % vào 8 phases của M1 (xem ROADMAP.md)

---
*Requirements defined: 2026-04-20*
*Last updated: 2026-04-20 after roadmapper mapped 30/30 to M1 phases*
