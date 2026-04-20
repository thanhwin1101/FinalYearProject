# Roadmap: Hospital AGV Carrier Robot

## Overview

Đây là roadmap cho **Milestone M1 — Safety + Pilot Readiness**. Codebase đã brownfield: toàn bộ happy-path (Auto line-follow, Follow Huskylens, MQTT bridge, OLED, Battery ADC, WiFiManager, OTA Arduino, dashboard CRUD) đang chạy ổn và đợt safety wave 2026-04 đã đóng 9 concern (ADR-008..012). M1 tập trung duy nhất vào **hardening** — xoá credential leak, bật auth + TLS + RBAC, ký firmware, viết Recovery Mode v2 thật (thay stub), cứng hoá robustness (IWDG, reconnect backoff), và kết bằng bộ integration test hành lang + operator/IT runbook. Cuối M1 pilot được 1 hành lang test 4-6 NFC với criteria: **50 chuyến liên tiếp, ≥49 thành công, 0 va chạm, CANCEL latency < 50 ms median**.

Roadmap gồm 8 phase với dependency chain tuyến tính xoay quanh "secrets-first": Phase 01 xoá credential leak khỏi git + rotate → Phase 02 TLS/ACL/HMAC cho broker đã có creds sạch → Phase 03-04 lock backend (AuthN + baseline security) → Phase 05 OTA ký Ed25519 dựa trên OTA password derived từ Phase 01 → Phase 06 Recovery v2 (firmware-only, có thể parallel với 03-05 nhưng xếp sau để dùng kênh MQTT đã authenticated) → Phase 07 cứng hoá firmware (IWDG + reconnect) → Phase 08 integration test + runbook chứng minh pilot ready.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 01: Secret Rotation & Supply-Chain Hygiene** - Rotate toàn bộ MQTT/OTA/Mongo credentials, purge `.env` khỏi git history, bật secret-scan CI, derive per-device OTA password.
- [ ] **Phase 02: MQTT Broker Hardening (TLS + ACL + HMAC)** - Bật mTLS/TLS Mosquitto, broker ACL per-robot, HMAC+nonce cho motion commands, zod schema cho inbound payload.
- [ ] **Phase 03: Backend AuthN/AuthZ** - JWT middleware toàn Express API + SSE, user model password-hash + role, RBAC nurse/admin, login page frontend.
- [ ] **Phase 04: Backend Baseline Security** - Helmet CSP + rate-limit, CORS allowlist, file-upload magic-byte validate + EXIF strip + UUID filename.
- [ ] **Phase 05: STM32 OTA Signing & Secure Firmware Delivery** - Ed25519 sign tooling + manifest, ESP32 HTTPS fetch + sha256 + signature verify trước `blMassErase`, OTA Arduino dùng derived password.
- [ ] **Phase 06: Recovery Mode v2 (Huskylens Scan + NFC Anchor)** - Thay stub bằng 2-giai-đoạn: servo-X sweep tìm line → PN532 anchor → return route; MQTT `abort_recovery` admin-only.
- [ ] **Phase 07: Firmware Robustness (IWDG + Boot Events + Reconnect Backoff)** - STM32 IWDG 2 s + feed, publish boot_event kèm reason code, ESP32 MQTT reconnect exponential backoff + offline cancel.
- [ ] **Phase 08: Integration Tests & Pilot Runbooks** - Bộ E2E hành lang đo success/CANCEL/collision/recovery/battery, 50-chuyến pass criteria, STM32 Unity unit tests ≥80 % branch, operator + IT runbook tiếng Việt.

## Phase Details

### Phase 01: Secret Rotation & Supply-Chain Hygiene
**Goal**: Loại bỏ hoàn toàn credential leak khỏi repo và mọi binary; provision flow fail-fast nếu thiếu env.
**Depends on**: Nothing (first phase — blocker cho tất cả security work khác)
**Requirements**: SEC-01, SEC-02, SEC-03, SEC-04
**Success Criteria** (what must be TRUE):
  1. `git log --all -p -- "Hospital Dashboard/Backend/.env"` không còn trả về nội dung credential (history đã rewrite).
  2. Secret-scan CI (gitleaks/trufflehog) reject một PR test chứa chuỗi fake credential (`MQTT_PASS=test_leak_sentinel_123`) → build FAIL visible ở PR check.
  3. Firmware ESP32 flash mới mà chưa provision `Preferences` → OLED hiện "CRED MISSING" + publish `boot_error`, không kết nối broker bằng default password.
  4. 2 unit ESP32 khác nhau sau provisioning có 2 OTA password khác nhau (derived từ chip-ID + deployment salt, lưu NVS); flash cross-unit bằng password unit A vào unit B → reject.
**Plans**: 4 plans
**ADR linkage**: Khép khoảng "Deferred — Credential rotation" trong `.planning/intel/decisions.md` và đóng CONCERNS H-SEC-01, A-SEC-01, A-SEC-02, A-SEC-04, R-HYG-01.

Plans:
- [ ] 01-01-PLAN.md — Rotate Mosquitto password via scripts/rotate-mosquitto-password.ps1 + scripts/generate-secure-password.ps1 + docs/SECURITY-ROTATION.md runbook (SEC-02 rotation portion). Wave 1, autonomous.
- [ ] 01-02-PLAN.md — `git filter-repo` history purge + sanitized .env.example + remove MQTT_DEFAULT_*/OTA_PASSWORD + firmware/backend fail-fast + g_credsMissing + OLED "CRED MISSING" screen + .gitignore hardening (SEC-01 + SEC-02 repo portion). Wave 2, non-autonomous (history-rewrite checkpoint).
- [ ] 01-03-PLAN.md — gitleaks runner + trufflehog v3 pre-commit + pre-push hook + sentinel fixture + test-scan-rejects-sentinel.ps1 + docs/DEV-SETUP.md (SEC-03). Wave 3, autonomous.
- [ ] 01-04-PLAN.md — Per-device OTA password via HMAC-SHA256(deployment salt, chip MAC) + provisioning console (NVS_WRITE/READ/ERASE/FINGERPRINT) + scripts/provision-esp32.ps1 + OLED fingerprint display + 3-second BOOT-hold provisioning entry (SEC-04). Wave 3, non-autonomous (salt-strategy decision checkpoint).

### Phase 02: MQTT Broker Hardening (TLS + ACL + HMAC)
**Goal**: Broker Mosquitto chỉ chấp nhận kết nối TLS authenticated; mỗi robot chỉ publish vào subtree của chính nó; motion command bắt buộc HMAC+nonce; backend validate payload schema trước khi ghi DB.
**Depends on**: Phase 01 (broker cần creds đã rotated và secret-scan bật; không có ý nghĩa nào khi đẩy cert mới lên broker vẫn dùng password trong git history).
**Requirements**: MQT-01, MQT-02, MQT-03, MQT-04
**Success Criteria** (what must be TRUE):
  1. Một MQTT client dùng `mqtt://` (no TLS) hoặc cert không trust-chain → bị Mosquitto reject trước authentication; robot và backend đều nối `mqtts://` OK.
  2. User `robot_01` publish vào `carry/robot/02/evt` → broker reject với ACL error (lắng qua Mosquitto log).
  3. Gửi command `{"action":"direct_vel","vx":0.5}` thiếu HMAC hoặc timestamp lệch > 5 s → ESP32 log rejected, motor không chạy; command hợp lệ cùng nonce replay trong 32 entries → cũng reject.
  4. Backend nhận `/telemetry` với field sai schema (e.g. `battery: "high"`) → reject + log, không ghi DB.
**Plans**: 4 plans
**ADR linkage**: Mở rộng ADR-004 (MQTT topic namespace) bằng layer transport TLS + ACL; đóng CONCERNS A-SEC-05, H-SEC-04, H-SEC-07.

Plans:
- [ ] 02-01: Mosquitto TLS setup — cert nội bộ tự ký, trust chain phân phối cho robot + backend, đổi URL sang `mqtts://` cả 2 phía (MQT-01).
- [ ] 02-02: Broker ACL file — user `robot_<id>` read/write giới hạn `carry/robot/<id>/*`, user `backend` full read + publish `carry/robot/+/cmd` (MQT-02).
- [ ] 02-03: HMAC-SHA256 + nonce ring-buffer 32 entries + timestamp skew check trên ESP32 cho `relay`, `relay_resume`, `direct_vel`, `wheel_set`; backend ký trước khi publish (MQT-03).
- [ ] 02-04: zod schema cho `/alert`, `/mission/progress`, `/telemetry` inbound trong `mqttService.js`; reject + structured log nếu invalid (MQT-04).

### Phase 03: Backend AuthN/AuthZ
**Goal**: Dashboard Express + SSE không còn bất kỳ route nào unauthenticated; user model có password-hash + role; RBAC tối thiểu nurse/admin hoạt động end-to-end từ login page.
**Depends on**: Phase 01 (DB credential đã rotated, env fail-fast đã có). Có thể parallel với Phase 02 nhưng giữ sequential để Phase 04 baseline security kế thừa middleware đã land.
**Requirements**: AUTH-01, AUTH-02, AUTH-03, AUTH-04
**Success Criteria** (what must be TRUE):
  1. GET `/api/patients` không kèm JWT → 401; với token role `nurse` → 200 + list; với token expired → 401.
  2. DELETE `/api/maps/:id` với role `nurse` → 403; với role `admin` → 200. Log audit ghi rõ userId + action.
  3. Điều dưỡng login được qua frontend, JWT persist qua refresh page, logout clear token; SSE `/api/robots/live` disconnect khi token expired.
  4. Seed script tạo được 1 admin account với argon2/bcrypt hash; password plain-text không bao giờ xuất hiện trong DB document.
**Plans**: 4 plans
**ADR linkage**: Đóng CONCERNS H-SEC-03, H-SEC-09.
**UI hint**: yes

Plans:
- [ ] 03-01: User model Mongoose + passwordHash (argon2) + role enum + seed admin script (AUTH-02).
- [ ] 03-02: JWT auth middleware gắn toàn Express router (trừ `/health`) + SSE token verification (AUTH-01).
- [ ] 03-03: RBAC guard helper `requireRole(['admin'])` + apply trên routes users/maps/robots DELETE/PUT + 403 tests (AUTH-03).
- [ ] 03-04: `/api/auth/login` + `/api/auth/me` + frontend React login page + token storage + auth context provider (AUTH-04).

### Phase 04: Backend Baseline Security
**Goal**: Backend chống được DoS cơ bản, CORS chỉ cho frontend allowlist, file upload không chịu được magic-byte spoof hoặc EXIF payload.
**Depends on**: Phase 03 (rate-limit phải biết auth context để rate-limit login endpoint riêng; CORS allowlist áp trên credentials=true chỉ hữu dụng khi đã có auth; upload validate chạy sau `requireAuth`).
**Requirements**: WEB-01, WEB-02, WEB-03
**Success Criteria** (what must be TRUE):
  1. Response header của `/api/robots` chứa `Content-Security-Policy`, `X-Frame-Options`, `Strict-Transport-Security` (helmet output).
  2. Gọi `/api/auth/login` 21 lần trong 15 phút từ 1 IP → lần 21 trả 429; route write khác giới hạn 100 req/15 min.
  3. Origin `https://evil.example` → CORS preflight reject (không có header `Access-Control-Allow-Origin`); origin frontend trong allowlist → OK + credentials allowed.
  4. Upload file `.png` có magic-byte thực là PDF → reject 400; upload PNG hợp lệ → sau re-encode strip hết EXIF, filename là UUID v4, kích thước ≤ 5 MB.
**Plans**: 3 plans
**ADR linkage**: Đóng CONCERNS H-SEC-05, H-SEC-06, H-SEC-08.

Plans:
- [ ] 04-01: `helmet()` với CSP basic + `express-rate-limit` 100/15min write, 20/15min login (WEB-01).
- [ ] 04-02: CORS allowlist qua env `CORS_ORIGIN` + credentials gate theo origin match (WEB-02).
- [ ] 04-03: Multer + `file-type` magic-byte + `sharp` re-encode strip EXIF + UUID filename + `limits.fileSize` 5 MB trên patient upload (WEB-03).

### Phase 05: STM32 OTA Signing & Secure Firmware Delivery
**Goal**: Không một ai có thể flash STM32 trừ khi gói firmware được ký bởi private key build-server và ESP32 verify đầy đủ signature + sha256 + HTTPS cert pin.
**Depends on**: Phase 02 (manifest published qua `carry/robot/<id>/cmd` đã được ACL + HMAC) + Phase 01 (OTA-03 dùng derived OTA password từ SEC-04).
**Requirements**: OTA-01, OTA-02, OTA-03
**Success Criteria** (what must be TRUE):
  1. Tampered firmware (1 byte thay đổi so với lúc ký) → ESP32 reject, publish `ota_verify_failed` event, STM32 giữ nguyên firmware cũ, không chạm `blMassErase`.
  2. Manifest có signature hợp lệ nhưng URL HTTP (không HTTPS) hoặc cert không pin → reject.
  3. ESP32 OTA Arduino upload với password sai chip-ID → reject ngay ở handshake, không write flash.
  4. Happy path: backend publish manifest đã ký → trong 60 s STM32 nhận firmware mới, version bump, boot_event "ota_success".
**Plans**: 3 plans
**ADR linkage**: Đóng CONCERNS A-SEC-03; preserve ADR-002 UART protocol (bootloader vẫn dùng command set sẵn có).

Plans:
- [ ] 05-01: Python signing script Ed25519 + manifest JSON generator + key management doc (OTA-01).
- [ ] 05-02: ESP32 `ota_manager` mới — HTTPS fetch với cert pin, mbedtls sha256 stream, Ed25519 verify trước khi gọi `blMassErase` + rollback event nếu fail (OTA-02).
- [ ] 05-03: ESP32 OTA Arduino handler dùng password đọc từ NVS (derived Phase 01), reject mismatched password + OLED feedback (OTA-03).

### Phase 06: Recovery Mode v2 (Huskylens Scan + NFC Anchor)
**Goal**: Khi robot off-line giữa hành lang, Recovery tự động tìm lại line bằng vision sweep, neo vị trí bằng NFC, và xin route về — không cần operator chạm robot.
**Depends on**: Phase 02 (dùng MQTT `abort_recovery` qua kênh đã authenticated + HMAC). Firmware work độc lập với Phase 03-05 nhưng xếp sau để kênh command đã secure, tránh rework.
**Requirements**: REC-01, REC-02, REC-03, REC-04
**Success Criteria** (what must be TRUE):
  1. Robot cố ý kéo off line giữa hành lang → trong ≤ 30 s Recovery sweep tìm thấy line bằng Huskylens servo-X, tự căn giữa qua line sensor, OLED hiện "Line found", chuyển Phase 2.
  2. Operator đặt thẻ NFC trên robot trong 30 s → PN532 đọc CP id → robot xin route về MED và chạy Auto tới MED; không bỏ sót thẻ hợp lệ.
  3. Full sweep không thấy line → buzzer × 3, retry tối đa 3 lần, rồi publish `recovery_failed` event + giữ Recovery state để operator can thiệp.
  4. Admin gửi `{"action":"abort_recovery"}` → robot exit về Follow IDLE trong < 500 ms với safe stop; role non-admin gửi cùng command → reject (403 tầng backend + HMAC reject tầng firmware).
**Plans**: 4 plans
**ADR linkage**: Preserve ADR-006 (không SR05 body sweep — dùng servo-X sweep vision thay vì quay thân robot mù); extend ADR-003 (vision relay profile `relaySetRecovery`); đóng CONCERNS A-BUG-04, A-BUG-05.

Plans:
- [ ] 06-01: Recovery entry — thay `relaySetAuto()` bằng `relaySetRecovery()` (vision + line + NFC đều ON), emergency stop + reset state machine (REC-01).
- [ ] 06-02: Phase 1 — Huskylens line-tracking mode + servo-X sweep 0°-180° @ 1°/50 ms + Vx/Vy/Vr centering + eye-center stop + 3-retry buzzer (REC-02).
- [ ] 06-03: Phase 2 — OLED "Đặt thẻ NFC" + PN532 30 s poll + publish `robot/return_request` + đợi route 10 s + chuyển Auto hoặc error hold (REC-03).
- [ ] 06-04: MQTT `{"action":"abort_recovery"}` admin-only + exit → Follow IDLE + safe stop + event log (REC-04).

### Phase 07: Firmware Robustness (IWDG + Boot Events + Reconnect Backoff)
**Goal**: Firmware tự phục hồi khỏi mọi loop hang, lỗi mạng WiFi/MQTT, hoặc power glitch mà không cần ai bật tắt nguồn; backend luôn biết robot vừa reset.
**Depends on**: Phase 06 (không chạm watchdog paths trong khi Recovery state machine đang mutate) + Phase 02 (MQTT offline detection cần kênh có schema để publish reliable event).
**Requirements**: ROB-01, ROB-02, ROB-03
**Success Criteria** (what must be TRUE):
  1. Giả lập STM32 main loop block > 2 s (chèn `while(1){}` test firmware) → board tự reset trong ≤ 2.5 s; bình thường loop feed watchdog, không reset false-positive trong 30 phút idle.
  2. Sau mỗi reset STM32, ESP32 nhận `boot_event` kèm reason (IWDG/POR/SOFT) trong ≤ 3 s và forward lên MQTT → dashboard log thấy record.
  3. Pull WiFi access point → ESP32 OLED hiện "MQTT offline" sau 30 s, gửi CMD_CANCEL_MISSION cho STM32, reconnect exponential 1/2/4/8/16/30 s → khôi phục tự động khi WiFi trả lại.
  4. Không có chuyến delivery nào fail im lặng vì broker disconnect; luôn có event để backend audit.
**Plans**: 3 plans
**ADR linkage**: Preserve ADR-008 (ESP32 WDT 30 s không đổi); thêm layer STM32 IWDG 2 s. Đóng phần "Deferred — STM32 IWDG" trong decisions.md.

Plans:
- [ ] 07-01: STM32 HAL IWDG init 2 s + `HAL_IWDG_Refresh()` trong main loop + mọi handler > 100 ms (ROB-01).
- [ ] 07-02: STM32 `CMD_DEBUG_MSG` boot_event publish kèm `RCC->CSR` reset flag parse + ESP32 forward MQTT (ROB-02).
- [ ] 07-03: ESP32 MQTT offline > 30 s → cancel mission + OLED warning + exponential backoff 1/2/4/8/16/30 s reconnect (ROB-03).

### Phase 08: Integration Tests & Pilot Runbooks
**Goal**: Chứng minh pilot readiness qua bằng chứng số (50 chuyến pass criteria) và tài liệu vận hành điều dưỡng có thể làm mà không cần IT; đây là phase "sign-off" trước pilot thật.
**Depends on**: Phase 07 (toàn bộ hardening + Recovery v2 + OTA signing đã land — test hành lang phải đo performance của hệ thống đã complete).
**Requirements**: TEST-01, TEST-02, TEST-03, DOC-01, DOC-02
**Success Criteria** (what must be TRUE):
  1. Chạy integration suite script Python 50 chuyến liên tiếp trên hành lang test → report ≥ 49 delivery thành công, 0 ToF collision event, CANCEL latency median < 50 ms, recovery success rate ≥ 80 % khi ép off-line.
  2. STM32 Unity test chạy `pio test -e bluepill_f103c8` → ≥ 80 % branch coverage trên `crc8`, `route_parser`, `mecanum`, `line_state`; CI gate fail nếu dưới ngưỡng.
  3. 1 điều dưỡng (không tham gia dev) đọc operator runbook PDF tiếng Việt và hoàn thành được cả 4 kịch bản: boot + WiFi portal + send mission + cancel + xử lý alarm OLED — không cần gọi IT.
  4. IT admin dùng deployment runbook để dựng 1 Mosquitto TLS + seed admin + provision 1 robot (NVS write) + backup Mongo, hoàn thành từ clean laptop trong < 2 giờ.
**Plans**: 5 plans
**ADR linkage**: Không introduce ADR mới; validate rằng mọi ADR-001..012 vẫn đúng trong corridor test thật. Đóng phần "Deferred — Automated test harness" trong decisions.md.

Plans:
- [ ] 08-01: STM32 PlatformIO + Unity test harness cho crc8/route_parser/mecanum/line_state ≥ 80 % branch + CI gate (TEST-03).
- [ ] 08-02: Python E2E script publish route MQTT + đo success/CANCEL latency/ToF events/recovery/battery gate, output JSON report (TEST-01).
- [ ] 08-03: Chạy 50-chuyến dry-run + thu thập metric + verify pass criteria ≥ 49/50 & < 50 ms CANCEL (TEST-02).
- [ ] 08-04: Operator runbook tiếng Việt — boot/portal/mission/cancel/OLED alarm/OTA/khi nào gọi IT — PDF + web (DOC-01).
- [ ] 08-05: IT deployment runbook — Mosquitto TLS cert, seed admin, robot provisioning (NVS), Mongo backup (DOC-02).

## Progress

**Execution Order:**
Phases execute in numeric order: 01 → 02 → 03 → 04 → 05 → 06 → 07 → 08. Không có decimal insertion ở thời điểm tạo roadmap.

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 01. Secret Rotation & Supply-Chain Hygiene | 0/4 | Not started | - |
| 02. MQTT Broker Hardening | 0/4 | Not started | - |
| 03. Backend AuthN/AuthZ | 0/4 | Not started | - |
| 04. Backend Baseline Security | 0/3 | Not started | - |
| 05. STM32 OTA Signing | 0/3 | Not started | - |
| 06. Recovery Mode v2 | 0/4 | Not started | - |
| 07. Firmware Robustness | 0/3 | Not started | - |
| 08. Integration Tests & Runbooks | 0/5 | Not started | - |

**Total:** 30 plans across 8 phases; 30 / 30 v1 requirements mapped.

---
*Roadmap created: 2026-04-20 for Milestone M1 — Safety + Pilot Readiness.*
*Brownfield context: pre-pilot hardening only — no re-implementation of validated v0 features.*
