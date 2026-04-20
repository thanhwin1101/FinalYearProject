# CONCERNS.md — Hospital Robot System

> Technical debt, bugs, security, performance, fragility, and repo-hygiene concerns.
> Generated: **2026-04-20**
> Last revised: **2026-04-20** (sau đợt safety fix — xem §1.5)
> Scope: `C:\Users\ironc\Desktop\Hospital`

Severity legend: **CRITICAL** = production-blocker / security leak / data loss risk · **HIGH** = actively fragile, mitigate before next release · **MEDIUM** = real issue with known workaround · **LOW** = hygiene / nice-to-have.

Concerns are grouped by subsystem. Each entry cites `path:line` so fixes can be tracked against git blame.

> **2026-04 Update: Concerns ✅ FIXED đánh dấu STRIKETHROUGH trong bảng.**
> Xem §1.5 "Đã fix trong đợt 2026-04" cuối file và
> `.planning/intel/decisions.md` (ADR-009, ADR-010, ADR-011, ADR-012) cho
> chi tiết rationale + reference vào code.

---

## 1. AGV Firmware (ESP32 master + STM32 slave)

### 1.1 Security

| # | Sev | File:Line | Description | Remediation |
|---|-----|-----------|-------------|-------------|
| A-SEC-01 | **CRITICAL** | `AGV/carry_final/esp32_master/src/config.h:52-55` | `MQTT_DEFAULT_SERVER`, `MQTT_DEFAULT_USER="hospital_backend"`, `MQTT_DEFAULT_PASS="123456"` are hardcoded defaults. Shared weak password (`123456`) is compiled into every firmware image. | Strip defaults; fail closed if `Preferences` has no user-provisioned credentials. Provision via WiFiManager config portal or QR-based onboarding. Rotate broker credentials. |
| A-SEC-02 | **CRITICAL** | `AGV/carry_final/esp32_master/src/config.h:86` | `OTA_PASSWORD "agv_ota_123"` hardcoded and compiled into every binary. Anyone decompiling a single unit can OTA-flash the entire fleet. | Per-device OTA password derived from chip ID + deployment salt; store in NVS provisioning step, not source. |
| A-SEC-03 | **CRITICAL** | `AGV/carry_final/esp32_master/src/ota_manager.cpp:84-280` (STM32 bootloader flow) + `mqtt_client.cpp:297-298` (`ota_stm32` MQTT action) | STM32 OTA accepts an arbitrary `url` over MQTT and performs full mass-erase + flash of whatever bytes the server returns. **No signature, no checksum/MAC of the image, HTTP (not HTTPS).** Any attacker able to publish to `<robotId>/cmd` (or MITM the HTTP stream) permanently owns the motion controller. | Require signed firmware (Ed25519), verify before calling `blMassErase`. Use HTTPS with pinned cert. Reject absolute-URL overrides; only accept firmware served by the authenticated backend with a version manifest. |
| A-SEC-04 | **HIGH** | `AGV/carry_final/esp32_master/src/mqtt_client.cpp:18-23` | Same weak MQTT defaults repeated in the .cpp (not only `config.h`). Two sources of truth for credentials. | Single source via `config.h`, then remove fallbacks entirely — connect must fail loudly if not provisioned. |
| A-SEC-05 | **HIGH** | `AGV/carry_final/esp32_master/src/mqtt_client.cpp:206-260` | `relay`, `relay_resume`, `direct_vel`, `wheel_set` actions are accepted without any authenticated origin check and without robotId scoping (topic = `<robotId>/cmd`, but subscribing to the wildcard lets any tenant on the broker inject). Payloads can disable the vision relay mid-mission, drive wheels arbitrarily, or set raw velocities. | Add HMAC/nonce on command payloads signed by backend. At minimum, drop motion-related commands when `g_autoState != AUTO_IDLE && g_followState != FOLLOW_IDLE`. |
| A-SEC-06 | **MEDIUM** | `AGV/carry_final/stm32_slave/src/main.cpp:151` (`// Lệnh chưa hỗ trợ → ACK rồi bỏ qua`) | Unknown UART commands are silently ACKed. Hides protocol drift between ESP32/STM32 and swallows malformed/attack frames. | Return NACK with reason code; log to ESP32 which should forward to backend for alerting. |
| A-SEC-07 | **MEDIUM** | `AGV/carry_final/esp32_master/src/uart_protocol.cpp:29` / `stm32_slave/src/uart_protocol.cpp` (same) | CRC8 covers `CMD+DATA` only — `STX` and `LEN` bytes are outside the CRC. A corrupted `LEN` with correct payload CRC can desynchronize the receiver. | Include LEN in CRC; add a timeout-based resync on the state machine. |

### 1.2 Safety-critical bugs

| # | Sev | File:Line | Description | Remediation |
|---|-----|-----------|-------------|-------------|
| ~~A-BUG-01~~ | ~~**CRITICAL**~~ | `stm32_slave/src/motor_control.cpp` | **✅ FIXED 2026-04** — Non-blocking turn API (`motor_startTurn` / `motor_turnStep` / `motor_abortTurn`). AUTO_DO_TURN state giờ thực sự poll mỗi tick. UART vẫn xử lý trong khi quay → CANCEL latency < 10 ms (trước ~1-2 s). Xem ADR-009. | _fixed_ |
| A-BUG-02 | **MEDIUM** (downgraded) | `stm32_slave/src/main.cpp`, `esp32_master/src/main.cpp` | **✅ PARTIAL FIX 2026-04** — Dummy `uart_send_battery(100)` từ STM32 đã loại bỏ + comment "tạm tắt" đã clarify thành "reserved for future motor battery". ESP32 ADC path vẫn là single source. **Còn lại:** `BATT_PCT_AT_MIN=10 %` floor vẫn hide voltage critical low — nhưng gate >=30 % ở trên floor nên safety không bị ảnh hưởng. Xem ADR-005. | Tùy chọn: hạ `BATT_PCT_AT_MIN` xuống 0 để hiển thị chính xác hơn khi pin < 3.036 V (tránh người dùng hiểu nhầm 10 % khi pin dead). |
| A-BUG-03 | **HIGH** | `AGV/carry_final/esp32_master/src/auto_mode.cpp:149-156` | Inside `case AUTO_RUNNING:` there is `if (g_autoState == AUTO_WAIT_START) { … }` — **unreachable code** (state is `AUTO_RUNNING` by definition of the case). The intended "new checkpoint arrived mid-run" handling never fires. | Restructure the check outside the switch, or set `g_newCheckpoint` via the MQTT callback and handle only via `g_mqttCancel` + route replacement at `AUTO_IDLE`. |
| A-BUG-04 | **HIGH** | `AGV/carry_final/esp32_master/src/recovery_mode.cpp:14-31` (vs `AGV/docs/Follow_mode.txt` §Recovery §GIAI ĐOẠN 1-3) | Recovery Mode in code is a stub: it sends `CMD_SET_MODE(AUTO) + CMD_CANCEL_MISSION` and blindly follows the line to any CP. The design doc specifies Huskylens servo-X scan to find the line, then line-follow to nearest known NFC. Divergence means robot cannot recover if it is **off the line** (exact case the doc targets). | Implement the doc'd two-stage recovery: (1) servo-scan + Huskylens line-hunt, (2) NFC-anchored return. |
| A-BUG-05 | **HIGH** | `AGV/carry_final/esp32_master/src/recovery_mode.cpp:22` | Enters Recovery with `relaySetAuto()` — auto profile powers OFF the vision relay (Huskylens + servo lose power). Even the stub recovery can't use vision if you extend it later. | Call `relaySetRecovery()` (or equivalent all-on profile) at recovery entry. |
| ~~A-BUG-06~~ | ~~**HIGH**~~ | `esp32_master/src/main.cpp` (`checkModeSwitch`) | **✅ FIXED 2026-04** — MED-gate. Long press AUTO→FOLLOW chỉ cho phép khi `g_autoState == AUTO_IDLE && (g_lastCheckpointId == MED_CHECKPOINT_ID || g_lastCheckpointId == 0)`. FOLLOW→AUTO chỉ khi `g_followState == FOLLOW_IDLE`. Recovery KHÔNG cho thoát qua nút. Xem ADR-010. | _fixed_ |
| ~~A-BUG-07~~ | ~~**HIGH**~~ | `stm32_slave/src/auto_runner.cpp` (`lineFollowStep`) | **✅ FIXED 2026-04** — 3-tier line-lost policy: 0-400 ms creep + report; 400-1500 ms creep tiếp; > 1500 ms hard brake + AUTO_IDLE + `uart_send_event(error)`. Robot không còn drift quá 1.5 s. Xem ADR-011. | _fixed_ |
| A-BUG-08 | **MEDIUM** | `AGV/carry_final/stm32_slave/src/motor_control.cpp:111-119` (`motor_brake`) | Every checkpoint hits `motor_brake()` which includes an 80 ms blocking delay (called in `auto_runner.cpp` on CP detect). Adds up across a 10-CP route. | Make brake duration advisory; hand control back to main loop immediately. |
| A-BUG-09 | **MEDIUM** | `AGV/carry_final/stm32_slave/src/main.cpp` (route parser) | `parseRouteFrame` validates total size but does not range-check individual action bytes. Invalid enum values (e.g. action byte = 0x42) reach `actionToTurnDir` and silently map to "straight" or "unknown". | Reject the frame and NACK on out-of-range action bytes. |
| A-BUG-10 | **MEDIUM** | `AGV/carry_final/esp32_master/src/follow_mode.cpp:51,93` | Low-battery and tag-lost countdowns (10 s / 30 s) use `millis()` deltas without overflow guard. 49.7 days uptime causes spurious single trigger. | Use `int32_t(now - t0) >= T` pattern (already used in some places — propagate). |
| A-BUG-11 | **MEDIUM** | `AGV/carry_final/esp32_master/src/mqtt_client.cpp:35-48` (`uidStringToId`) | Parser truncates UIDs silently to 16 bit. Two physically different tags whose low 2 bytes collide map to the same checkpoint. | Increase ID field to 32 bit end-to-end, or SHA-truncate UID with collision detection at backend `MapGraph.nodes.rfidUid`. |
| A-BUG-12 | **LOW** | `AGV/carry_final/esp32_master/src/button_handler.cpp` | Long-press discards pending single-click count. Not a bug, just undocumented UX. | Document in `AGV/docs/Button.txt`. |

### 1.3 Architecture / fragility

| # | Sev | File:Line | Description | Remediation |
|---|-----|-----------|-------------|-------------|
| A-ARCH-01 | **MEDIUM** (downgraded) | `esp32_master/src/main.cpp` (no `xTaskCreate*`) | **🟡 ACCEPTED 2026-04** — Quyết định KHÔNG dùng FreeRTOS tasks (xem ADR-007). Thay vào đó: (1) WDT 30 s feed mỗi loop iteration, (2) tất cả handler chunked < 100 ms, (3) blocking delays cắt từ 5 s → 200-1500 ms. Race condition risk thấp vì single-loop. Sẽ chỉ revisit khi feature mới đòi (vd. dual-Huskylens). | _N/A — accepted_ |
| ~~A-ARCH-02~~ | ~~**HIGH**~~ | `esp32_master/src/main.cpp` | **✅ FIXED 2026-04** — `esp_task_wdt_init(WDT_TIMEOUT_S=30, true)` trong `setup()`; `esp_task_wdt_reset()` ở đầu mỗi `loop()` + trong các handler dài (MQTT reconnect, OLED render). Panic = true → reset board nếu treo. Xem ADR-008. | _fixed_ |
| A-ARCH-03 | **MEDIUM** (downgraded) | `esp32_master/src/main.cpp` setup, `config.h` `WM_PORTAL_TIMEOUT=0` | **✅ PARTIAL FIX 2026-04** — Boot delay 5 s → 1.5 s (WDT-fed loop). Relay settle 5 s → 800 ms (`RELAY_SETTLE_MS`). **Còn lại:** `WM_PORTAL_TIMEOUT=0` (infinite) — đây là chủ ý cho robot tại factory mới boot lần đầu cần config; nên giữ. Production có thể override = 180 trong build flag. | Tùy site: thêm `-DWM_PORTAL_TIMEOUT=180` cho hospital deployment. |
| A-ARCH-04 | **MEDIUM** | `AGV/carry_final/esp32_master/src/globals.h`, `stm32_slave/src/globals.h` | Shared flags use `volatile` only (no mutex/atomic). That's correct only because today everything runs in `loop()`. Moment tasks are added (A-ARCH-01) these become race conditions (e.g. `g_velUpdatedAt`, `g_newCheckpoint`, `g_mqttCancel`). | Switch to `std::atomic` or FreeRTOS queues before tasking. |
| A-ARCH-05 | **MEDIUM** | `AGV/carry_final/stm32_slave/src/follow_runner.cpp` (FOLLOW_VEL_TIMEOUT_MS=500) | Follow-mode safety: motors stop only after 500 ms of velocity silence. During WiFi glitches or Huskylens lag this causes jerky stop/start. | Reduce to 200 ms and smooth via ramp-down; add explicit `CMD_FOLLOW_PAUSE` to distinguish "no update" from "intentional hold". |
| A-ARCH-06 | **MEDIUM** | `AGV/carry_final/stm32_slave/src/tof_sensor.cpp` (`Wire.setClock(100000)`) | 100 kHz I²C is a deliberate choice for long wires — not a concern — but it is **undocumented**. Anyone raising it to 400 kHz later will get mysterious VL53L0X timeouts. | Add inline comment + note in `AGV/docs/Pinout.txt`. |
| A-ARCH-07 | **LOW** | `AGV/carry_final/esp32_master/src/mqtt_client.cpp` (field normalization) | `MQTT_STRICT_SCHEMA` is OFF by default; legacy `prevNodeId`/`nodeId` fields accepted silently. OK for now but drifts toward hidden compatibility debt. | Flip strict on; bump all publishers to new schema. |

### 1.4 Incomplete features (from `AGV/docs/Checklist.txt`)

| # | Sev | File:Line | Description | Remediation |
|---|-----|-----------|-------------|-------------|
| ~~A-TD-01~~ | ~~**HIGH**~~ | `AGV/docs/Checklist.txt` | **✅ FIXED 2026-04** — Checklist.txt được rewrite hoàn toàn với 3-state legend (`[x]/[~]/[ ]`), reflect trạng thái thực, kèm "LOCKED decisions" L1-L7 và "Known Gaps" G1-G5. FreeRTOS đánh dấu intentional skip (ADR-007) chứ không phải TODO. | _fixed_ |
| ~~A-TD-02~~ | ~~**MEDIUM**~~ | `esp32_master/src/main.cpp` | **✅ FIXED 2026-04** — Comment "tạm tắt" thay bằng giải thích rõ ràng: STM32 không sở hữu battery telemetry (xem ADR-005). | _fixed_ |
| A-TD-03 | **LOW** | `AGV/docs/Auto_mode.txt` §Mismatch / §Return-when-cancelled | Behaviour described in docs: on ID mismatch, enter `AUTO_BLIND_FOLLOW` and report any CP reached. This *is* implemented (`stm32_slave/src/auto_runner.cpp` has the state), but no MQTT event distinguishes "arrived at correct CP" vs "arrived at fallback CP after mismatch". | Add a `mismatch_resolved` event field so backend can flag disturbed missions. |

### 1.5 Đã fix trong đợt 2026-04 (safety alignment wave)

| ID | What | Files chạm | Verify |
|----|------|------------|--------|
| FIX-01 | Non-blocking turn (A-BUG-01) | `stm32_slave/src/motor_control.{h,cpp}`, `auto_runner.cpp` | PlatformIO build STM32 PASS · UART CANCEL trong khi quay được xử lý ngay |
| FIX-02 | Line-lost safe brake (A-BUG-07) | `stm32_slave/src/auto_runner.cpp` | Mất line > 1.5 s → hard stop, không drift |
| FIX-03 | Bỏ dummy battery 100 % từ STM32 (A-BUG-02 partial) | `stm32_slave/src/main.cpp`, `uart_protocol.h` (cả 2 phía) | STM32 không spam CMD_BATTERY nữa; ESP32 ADC vẫn là single source |
| FIX-04 | Task watchdog ESP32 (A-ARCH-02) | `esp32_master/src/main.cpp`, `config.h` | `esp_task_wdt_init(30, true)` + reset mỗi loop & boot phase |
| FIX-05 | MED-gate cho mode switch (A-BUG-06) | `esp32_master/src/main.cpp` (`checkModeSwitch`) | Chỉ AUTO_IDLE ∧ at MED mới cho long press AUTO→FOLLOW |
| FIX-06 | Emergency stop MQTT `{"action":"stop"}` (mới) | `esp32_master/src/main.cpp` (`checkEmergencyStop`) | OLED hiện "EMERGENCY STOP", motor zero, freeze tới khi `{"action":"resume"}` |
| FIX-07 | Boot/relay delay 5 s → 200-1500 ms (A-ARCH-03 partial) | `esp32_master/src/main.cpp`, `relay_control.cpp`, `config.h` | `RELAY_SETTLE_MS=800` |
| FIX-08 | Comment "tạm tắt" battery → giải thích rõ (A-TD-02) | `esp32_master/src/main.cpp` | Không còn misleading wording |
| FIX-09 | Checklist.txt rewrite + ADRs (A-TD-01) | `AGV/docs/Checklist.txt`, `.planning/intel/decisions.md` (12 ADRs) | Doc ↔ code thống nhất |

Build verification: PlatformIO `pio run -e esp32_master` ✅ và `pio run -e bluepill_f103c8` ✅ đều SUCCESS sau loạt fix.

---

## 2. Hospital Dashboard (Backend + Frontend)

### 2.1 Security

| # | Sev | File:Line | Description | Remediation |
|---|-----|-----------|-------------|-------------|
| H-SEC-01 | **CRITICAL** | `Hospital Dashboard/Backend/.env` *(file is **tracked in git history**: commits `50550088`, `d0f9fb65`, `af94fa41` — despite `.gitignore:13` listing `.env`)* | Live secrets are in the repository and its git history: `MONGO_URI`, `MQTT_USER=hospital_backend`, `MQTT_PASS=123456`. Anyone with read access to the repo (or its mirror) has the broker password. | (1) Rotate every credential **now**. (2) `git filter-repo --path "Hospital Dashboard/Backend/.env" --invert-paths`, force-push, inform all clones. (3) `git rm --cached "Hospital Dashboard/Backend/.env"` and commit. (4) Confirm CI secret-scanning (gitleaks/trufflehog) is wired. |
| H-SEC-02 | **CRITICAL** | `Hospital Dashboard/Backend/.env.example` | `.env.example` contains real-looking credentials (`MQTT_PASS=123456`). Developers copy it to `.env` verbatim and re-leak it. | Replace sample values with placeholders (`<set-me>`) and add a preflight check that refuses to boot if any env var matches the example placeholders. |
| H-SEC-03 | **CRITICAL** | `Hospital Dashboard/Backend/src/routes/users.js`, `patients.js`, `robots.js`, `missions.js`, `maps.js` (all `router.get/post/put/delete` handlers) | **No authentication and no authorization middleware anywhere.** All routes — including `DELETE /api/patients/:id`, `DELETE /api/users/:uid`, `POST /api/missions/delivery`, and SSE `/api/robots/live` — are open to unauthenticated clients on the network. PII / PHI (patient names, beds, notes, prescriptions, timeline photos) is world-readable/writable. | Add `requireAuth` middleware (JWT or session), wire RBAC per role, protect writes with CSRF if using cookies. This is a full-stack effort, not a patch. Also see H-SEC-07. |
| H-SEC-04 | **CRITICAL** | `Hospital Dashboard/Backend/src/services/mqttService.js:17-19` | Same weak defaults compiled into the Node service: `MQTT_BROKER`, `MQTT_USER`, `MQTT_PASS` fall back to hardcoded values if env missing. Service will happily boot against an unauthenticated broker. | Fail-fast on missing env (`if (!process.env.MQTT_PASS) throw …`). Move broker to TLS (mTLS between backend & robot). |
| H-SEC-05 | **HIGH** | `Hospital Dashboard/Backend/src/index.js:42` | `app.use(cors())` — wide-open CORS (`*`) with `credentials` default. With no auth (H-SEC-03) this magnifies the leak: any attacker page on any origin can call the API from a victim's browser. | Explicit allowlist via `CORS_ORIGIN` env; set `credentials: true` only when the frontend origin matches. |
| H-SEC-06 | **HIGH** | `Hospital Dashboard/Backend/src/routes/patients.js` (multer config) | `fileFilter` validates only `mimetype` and `extname` — both trivially spoofable. No magic-byte check, no re-encode, no size cap documented in the route. Uploads land in `UPLOAD_DIR` resolved from `process.cwd()`. | Use `file-type` (magic-byte) to verify; re-encode through `sharp` to strip EXIF + exploit payloads; enforce `limits.fileSize`; resolve the upload directory once at boot with `path.resolve`, and store final filenames as random UUIDs (no user-controlled path segments). |
| H-SEC-07 | **HIGH** | `Hospital Dashboard/Backend/src/routes/robots.js` (`/live` SSE), `Hospital Dashboard/Backend/src/services/mqttService.js:122` (`handleMessage`) | SSE broadcast of full robot telemetry is unauthenticated. Incoming MQTT messages are dispatched by topic prefix only — no per-robotId ownership check, no payload schema guard for `/alert`, `/mission/progress`, `/telemetry` before mutating DB. A rogue publisher forges missions/telemetry. | Auth on `/live`. On the MQTT side, enforce broker ACLs so only the robot owning `<robotId>` can publish to its subtopics, and validate payloads with `zod`/`joi` before any DB write. |
| H-SEC-08 | **MEDIUM** | `Hospital Dashboard/Backend/package.json` (no helmet / rate-limit / csrf) | No `helmet`, no `express-rate-limit`, no CSRF protection. API is trivially DoS-able and missing baseline security headers. | Add `helmet()`, `express-rate-limit` on write routes, `csurf` (if cookie auth) or double-submit token. |
| H-SEC-09 | **MEDIUM** | `Hospital Dashboard/Backend/src/models/User.js` | `User` schema has no `passwordHash` / `passwordSalt` / role verification. Whatever "user" means here, it is not a credential-bearing entity. | Design real auth model (argon2 / bcrypt; roles: nurse, doctor, admin). |
| H-SEC-10 | **LOW** | `Hospital Dashboard/Frontend/.env.example` | Contains only `VITE_API_URL=` placeholder — no secret leak, but the frontend bundles this at build time; any future secret here would leak to clients. | Add a comment: "Never put secrets in VITE_*. These are embedded in the browser bundle." |

### 2.2 Technical debt / fragility

| # | Sev | File:Line | Description | Remediation |
|---|-----|-----------|-------------|-------------|
| H-TD-01 | **MEDIUM** | `Hospital Dashboard/Backend/src/services/mqttService.js:678-798` | `calculateReturnRouteFromNode` / `buildReturnPath` encode corridor/room layout as hardcoded JS constants. Changing the hospital map requires editing code. | Move topology into `MapGraph` and compute routes from the graph only. |
| H-TD-02 | **MEDIUM** | `Hospital Dashboard/Backend/src/services/mqttService.js:53-74` (`normalizeNodeFields`) | Non-strict schema path kept "for legacy producers" but no deprecation plan. | Log a metric when the legacy path triggers; flip strict on after a grace period. |
| H-TD-03 | **LOW** | `Hospital Dashboard/Backend/package.json`, `Hospital Dashboard/Frontend/package.json` | Dependencies are recent but never audited automatically. `npm audit --production` was not run in CI (no workflow file present). | Add a scheduled `npm audit` + `npm outdated` GitHub Action (or equivalent). |
| H-TD-04 | **LOW** | `Hospital Dashboard/Backend/src/db.js:8-11` | Falls back to local mongo with a console warning — ok for dev, but the warning is easy to miss in prod logs if stdout is noisy. | Refuse to boot in `NODE_ENV=production` without `MONGO_URI`. |

---

## 3. Repo hygiene / orphan files

| # | Sev | File | Description | Remediation |
|---|-----|------|-------------|-------------|
| R-HYG-01 | **HIGH** | `Hospital Dashboard/Backend/.env` | Tracked in git despite `.gitignore:13`. See **H-SEC-01** for the rewrite steps; listed again here because the repo-hygiene fix (untrack) is separate from credential rotation (operational). | `git rm --cached`; keep a real `.env` only locally. |
| R-HYG-02 | **MEDIUM** | `flash_stm32.cfg` | OpenOCD config references `C:/Users/ironc/Desktop/Hospital/AGV/carry_final/carry_final_stm32/.pio/build/bluepill_f103c8/firmware.bin` — that directory does not exist. Correct path is `AGV/carry_final/stm32_slave/.pio/build/bluepill_f103c8/firmware.bin`. | Fix the path; also make it relative (`./AGV/...`) so it works on any checkout. |
| R-HYG-03 | **MEDIUM** | `stm32_slave.bin` (repo root, ~46 KB) | Currently untracked (ignored by `*.bin`) but lives in the workspace root with no README explaining its provenance or purpose. Looks like a one-off flashed binary someone forgot. | Either move to `AGV/firmware-releases/stm32_slave-vX.Y.bin` with a version tag + checksum, or delete. |
| R-HYG-04 | **LOW** | `test_cmd.txt` (contents: `{"action":"cancel"}`) | Root-level throwaway file. | Delete; if you need command samples, put them in `AGV/docs/examples/`. |
| R-HYG-05 | **LOW** | `start-hospital-stack.bat` | Hardcodes `C:\Program Files\mosquitto\mosquitto.exe`. Breaks on any machine that didn't install Mosquitto to the default path. | Look up via `where mosquitto` or allow `%MOSQUITTO_HOME%` override. |
| R-HYG-06 | **LOW** | `.venv/`, `AGV/.venv/` (present in workspace, **not in `.gitignore`**) | Python virtualenvs are not ignored. If anyone commits dependencies, the repo balloons. | Add `.venv/` and `**/.venv/` to `.gitignore`. |
| R-HYG-07 | **LOW** | `CarryRobot/` (staged as deleted per `git status`) | Old subsystem architecture is partially deleted — mixed state could re-appear on a merge. | Finalise the deletion (`git add -u` + commit) or restore and document. |
| R-HYG-08 | **LOW** | `AGV/carry_final/` directory name | Literally named `carry_final` — classic technical-debt tell. Implies there was also `carry_v1`, `carry_v2`, … | Rename to `AGV/firmware/` once the OTA/release strategy (A-SEC-03) is locked. |

---

## 4. Priority remediation order (revised 2026-04)

✅ Đã làm đợt này (xem §1.5):
- A-BUG-01, A-BUG-07, A-BUG-02 (partial), A-BUG-06
- A-ARCH-02, A-ARCH-03 (partial)
- A-TD-01, A-TD-02
- + Emergency stop (mới, không có trong list gốc)

🔥 Còn lại — ưu tiên trước khi pilot với dữ liệu bệnh nhân thật:
1. **H-SEC-01 + A-SEC-01 + A-SEC-02** — rotate credentials, `git filter-repo` để purge `.env`, đổi password OTA per-device. Đây là **blocker đầu tiên**.
2. **H-SEC-03** — gắn auth middleware (JWT/session) vào tất cả Backend routes + SSE `/api/robots/live`.
3. **A-SEC-03** — Ed25519 sign STM32 OTA + HTTPS pin trước khi enable remote firmware push.
4. **A-SEC-05** — HMAC/nonce cho MQTT `relay`, `direct_vel`, `wheel_set` payloads (motion-related commands).
5. **A-BUG-04 + A-BUG-05** — Implement real Recovery Mode 2 giai đoạn (Huskylens scan → NFC) thay stub hiện tại; chuyển `relaySetAuto()` → `relaySetRecovery()` ở entry.
6. **H-SEC-06 + H-SEC-08** — magic-byte validate file upload, helmet + rate-limit baseline.

🟡 Optional/đã accepted (có thể bỏ qua nếu scope hẹp):
- A-ARCH-01 (FreeRTOS) — accepted via ADR-007.
- A-BUG-02 floor 10 % — cosmetic.
- A-ARCH-03 portal timeout — chủ ý cho factory boot.

---

_Last updated: 2026-04-20._
_Wave 1 fix: 2026-04-20 (xem §1.5 + ADRs)._
