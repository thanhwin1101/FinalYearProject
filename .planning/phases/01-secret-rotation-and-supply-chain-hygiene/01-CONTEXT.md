# Phase 01: Secret Rotation & Supply-Chain Hygiene — Context

**Gathered:** 2026-04-20
**Status:** Ready for planning
**Source:** ROADMAP.md + operator decisions (inline, replaces discuss-phase)

<domain>
## Phase Boundary

Phase 01 xoá credential leak khỏi repo và mọi binary cho Hospital AGV project. Cover toàn bộ 4 requirement `SEC-01..04` (xem REQUIREMENTS.md). KHÔNG chạm backend AuthN (Phase 03), MQTT TLS (Phase 02), OTA signing flow (Phase 05) — chỉ làm nền tảng secret hygiene để các phase sau không xây trên cát lún.

**Input state (brownfield realities — xem CONCERNS.md §1.1 + §2.1):**
- `Hospital Dashboard/Backend/.env` đang tracked trong git history, các commit `50550088`, `d0f9fb65`, `af94fa41` có nội dung: `MONGO_URI`, `MQTT_USER=hospital_backend`, `MQTT_PASS=123456`.
- `Hospital Dashboard/Backend/.env.example` có real-looking values (`MQTT_PASS=123456` v.v).
- `AGV/carry_final/esp32_master/src/config.h:52-55`: `MQTT_DEFAULT_SERVER`, `MQTT_DEFAULT_USER="hospital_backend"`, `MQTT_DEFAULT_PASS="123456"` hardcoded.
- `AGV/carry_final/esp32_master/src/config.h:86`: `OTA_PASSWORD "agv_ota_123"` hardcoded, compiled into every binary.
- `AGV/carry_final/esp32_master/src/mqtt_client.cpp:18-23`: cùng defaults duplicated — 2 source of truth.
- `Hospital Dashboard/Backend/src/services/mqttService.js:17-19`: defaults duplicated ở Node service.

**Output state (sau khi phase done):**
- Git history không còn chứa `.env` thật (kiểm tra bằng `git log --all -p -- "Hospital Dashboard/Backend/.env"` trả về rỗng).
- Mọi credentials ngoài đời đã rotated: Mosquitto user `hospital_backend` password mới, MongoDB credentials mới (nếu áp dụng), OTA password default đã xoá khỏi binary.
- Firmware ESP32 mới build FAIL-FAST nếu NVS Preferences thiếu `wifi_ssid/wifi_pass/mqtt_server/mqtt_port/mqtt_user/mqtt_pass` — OLED "CRED MISSING", publish `boot_error`, không fallback default.
- Backend Node fail-fast nếu thiếu env `MQTT_PASS/MONGO_URI/JWT_SECRET` (throw at boot, không fallback).
- Gitleaks CI workflow (local runner + pre-commit hook, vì repo không push remote M1) reject PR chứa `test_leak_sentinel_123`. Trufflehog v3 pre-commit hook catch secrets trước commit.
- Mỗi ESP32 unit có OTA password riêng derived từ `ESP.getEfuseMac()` + deployment salt; flash cross-unit reject.

</domain>

<decisions>
## Implementation Decisions (LOCKED bởi operator)

### Git history rewrite
- **Tool:** `git filter-repo` (recommended default). Nếu user change ý sau có thể đổi sang BFG. Không dùng `git filter-branch` (deprecated, chậm).
- **Scope:** Purge `Hospital Dashboard/Backend/.env` khỏi toàn bộ lịch sử. Không đụng file khác.
- **Verification:** `git log --all -p -- "Hospital Dashboard/Backend/.env"` rỗng. Tagged commits và branches được audit.

### Secret scanning
- **CI tool:** gitleaks. Vì repo **local-only trong M1** (chưa push remote), gitleaks chạy qua **script `scripts/scan-secrets.ps1`** gọi binary gitleaks, hook vào git pre-push hook + tài liệu hoá để chạy manual trước khi push lần đầu. Nếu sau pilot push lên GitHub thì gitleaks chuyển sang GitHub Actions workflow (outside M1 scope).
- **Pre-commit hook:** trufflehog v3 (`filesystem` mode) gọi qua `pre-commit` framework. Config `.pre-commit-config.yaml`. Dev phải cài `pre-commit install` sau clone.
- **Test:** Một PR test (chỉ để verify, không merge) chứa `MQTT_PASS=test_leak_sentinel_123` phải bị cả 2 tool reject rõ ràng.

### Git hosting context
- **M1 assumption:** Repo chưa push lên remote — local only. Vì vậy không cần coordinate force-push với collaborators ngoài machine này. Sau M1 khi push lên GitHub lần đầu, chắc chắn đẩy phiên bản history đã rewrite — quy trình viết trong operator runbook.

### Broker credential rotation
- **Broker:** Mosquitto local chạy qua `start-hospital-stack.bat` → `C:\Program Files\mosquitto\mosquitto.exe`.
- **Rotation method:** Dùng `mosquitto_passwd` CLI manual. Script `scripts/rotate-mosquitto-password.ps1` generate random password (32+ chars từ `/dev/urandom` qua .NET `RNGCryptoServiceProvider`), update password file, ghi ra `.env` local (gitignored), printout lệnh phải set vào firmware NVS.
- **Users cần rotate:** `hospital_backend`. Nếu phase 02 sau này thêm user `robot_<id>`, cũng dùng cùng script.
- **Ghi chú M1:** Vì broker đang fresh local + không có user thật, rotation = tạo mới password mạnh + update backend `.env` + document quy trình cho IT.

### Firmware fail-fast behavior
- ESP32 `setup()` đọc Preferences. Nếu bất kỳ field nào trong `{wifi_ssid, wifi_pass, mqtt_server, mqtt_port, mqtt_user, mqtt_pass}` empty/missing → không fallback về `MQTT_DEFAULT_*` nữa. Hiện OLED "CRED MISSING", publish `boot_error` (qua Serial nếu WiFi chưa có), chờ WiFiManager portal.
- Constants `MQTT_DEFAULT_SERVER/USER/PASS` và `OTA_PASSWORD` xoá khỏi `config.h`. Source of truth duy nhất: NVS Preferences + build-time secret salt (per SEC-04 decision below).
- Backend Node `src/index.js` gọi `require('dotenv').config()` rồi assert required env vars — throw + exit code 2 nếu thiếu, in rõ danh sách fields missing.

### Per-device OTA password derivation (SEC-04)
- **Algo:** `otaPassword = base64url(HMAC_SHA256(deploymentSalt, chipMac))[:16]`. Output 16 chars, printable.
- **Salt strategy — Claude's discretion:** **Hybrid**. Deployment salt:
  - **Not compiled** vào binary (vì compile-in = anyone reading binary có salt = derive được pass của robot khác).
  - Ghi vào NVS trong bước provisioning qua serial, key `ota_salt`. Provisioning script `scripts/provision-esp32.ps1` asks IT admin nhập salt (1 lần, hoặc từ env), ghi NVS + log SHA256 fingerprint.
  - Salt lưu trong một file không track git (`deployment-salt.secret`, gitignored) trên máy IT admin; fingerprint ghi kèm deployment record per robot để verify.
- **Boot-time derivation:** Firmware `otaManager::begin()` đọc `ota_salt` từ NVS + `ESP.getEfuseMac()`, HMAC → 16-char password → `ArduinoOTA.setPassword(pwd)`. Nếu `ota_salt` empty → OLED "OTA DISABLED" + skip `ArduinoOTA.begin()`.
- **Cross-unit protection:** Unit A password (salt A + mac A) ≠ Unit B password (salt A + mac B) vì mac khác nhau. Flash unit B bằng password unit A → handshake reject. Chứng minh bằng test cross-unit trong success criteria #4.

### Claude's Discretion (planner decides)
- Cụ thể file names, script structure, whether to use a single `scripts/secrets/` directory hay spread.
- Test harness cho fail-fast (một test chạy firmware không provision → expect OLED text + boot_error Serial line).
- Rollback plan chi tiết cho `git filter-repo` (tag backup ref trước khi rewrite, document restore commands).
- Template cho `deployment-salt.secret` file (1 dòng base64 random 32 bytes).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & decisions
- `.planning/REQUIREMENTS.md` — SEC-01 through SEC-04 full acceptance criteria.
- `.planning/ROADMAP.md` — Phase 01 section (goal, success criteria, plan list, ADR linkage).
- `.planning/intel/decisions.md` — ADRs 001-012 không được vi phạm; đặc biệt ADR-005 (battery ownership — credentials liên quan OTA + broker, không liên quan battery nhưng đọc cả set để ensure consistency).
- `.planning/PROJECT.md` — Core Value + Constraints + Out of Scope (FreeRTOS, multi-fleet, etc.).

### Codebase reality
- `.planning/codebase/CONCERNS.md` — §1.1 A-SEC-01, A-SEC-02, A-SEC-04; §2.1 H-SEC-01, H-SEC-02; §3 R-HYG-01. These are the exact defects this phase closes.
- `.planning/codebase/STACK.md` + `.planning/codebase/ARCHITECTURE.md` — build system (PlatformIO), provision system (WiFiManager + Preferences).

### Source files touched (read before writing plans)
- `AGV/carry_final/esp32_master/src/config.h` — remove MQTT_DEFAULT_* + OTA_PASSWORD.
- `AGV/carry_final/esp32_master/src/mqtt_client.cpp` — remove duplicated defaults, fail-fast Preferences read.
- `AGV/carry_final/esp32_master/src/main.cpp` + `net_monitor.cpp` — wire "CRED MISSING" OLED screen, boot_error publish.
- `AGV/carry_final/esp32_master/src/ota_manager.cpp` — HMAC-derived password flow.
- `Hospital Dashboard/Backend/src/index.js` + `src/services/mqttService.js` + `src/db.js` — fail-fast env assertion.
- `Hospital Dashboard/Backend/.env` + `.env.example` — purge + placeholder.
- `.gitignore` — verify `.env`, `deployment-salt.secret`, `AGV/.venv/`, `.venv/` all ignored.
- `start-hospital-stack.bat` — reference Mosquitto path (mentioned in R-HYG-05).

</canonical_refs>

<specifics>
## Specific Ideas

- Pre-commit framework: `pip install pre-commit` → `pre-commit install`. Config `.pre-commit-config.yaml` references trufflehog v3 official repo.
- Gitleaks binary installed via `scoop install gitleaks` on Windows or manual download; script detects path.
- For local secret-scan run before push: `.git/hooks/pre-push` calls `scripts/scan-secrets.ps1`.
- Provisioning tool: `scripts/provision-esp32.ps1` opens serial to ESP32, sends `NVS_WRITE wifi_ssid=...`, `NVS_WRITE ota_salt=...` etc. ESP32 needs a serial provisioning mode (can be existing or minimal new command, e.g. long-press during boot).
- Firmware changes are ESP32-only; STM32 không có credential gì.

## Test scaffolding needed

- `tests/secret-scan-test-pr/` — fake commit chứa sentinel để gitleaks + trufflehog phải bắt. Document cách chạy test.
- Firmware "cred-missing" bootloop test: wipe NVS (serial command `NVS_ERASE`) → reboot → expect OLED text + Serial log `boot_error_cred_missing`. Manual test, ghi trong PLAN verify section.

</specifics>

<deferred>
## Deferred Ideas

- Secret rotation rotation-cadence policy (quarterly, yearly?) — đợi pilot feedback.
- HSM / Yubikey based signing key custody cho OTA (Phase 05 worry, không phải Phase 01).
- GitHub Actions CI (khi repo push remote, post-M1).
- Secrets management service (Vault / Doppler) — over-engineered cho 1 robot pilot, revisit khi fleet > 5.

</deferred>

---

*Phase: 01-secret-rotation-and-supply-chain-hygiene*
*Context gathered: 2026-04-20 — inline decisions (no discuss-phase needed)*
