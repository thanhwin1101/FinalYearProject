# Conflict Detection Report

Synthesis of 5 ingested documents from `AGV/docs/` (1 PRD, 2 SPEC, 2 DOC).
Precedence: SPEC > PRD > DOC (no ADRs present).

Summary: 0 BLOCKERS, 0 WARNINGS, 4 INFO (auto-resolved).

---

## BLOCKERS (0)

None. No LOCKED-vs-LOCKED contradictions, no cross-ref cycles, no
UNKNOWN-low-confidence docs, no locked-CONTEXT contradictions (new mode —
no pre-existing `.planning/` context to conflict with).

---

## WARNINGS (0)

None. No competing acceptance variants across PRDs; only one PRD is present
(`agent_skill.txt`), and behavioral SPECs do not introduce competing
acceptance criteria for the same requirement — they refine the PRD.

---

## INFO (4)

[INFO] Auto-resolved: SPEC > PRD on Auto-mode boot relay state

  Found: `AGV/docs/agent_skill.txt` (PRD, precedence 2) states in prose:
    "Mặc định: Auto mode, Relay 1 ON (PN532 + line sensor), Relay 2 OFF"
    — this describes a legacy two-relay design.

  Conflicts with: `AGV/docs/Auto_mode.txt` (SPEC, precedence 1) declares:
    "Tắt R1 (nếu đang bật), Bật R2 và R3 (cấp nguồn line sensor + PN532)"
    — and `AGV/docs/setup_wifi_MQTT.txt` (DOC) agrees: "digitalWrite(R2,HIGH);
    digitalWrite(R3,HIGH)".

  Also conflicts with: the PRD's own relay table which defines R1=Vision,
    R2=Line, R3=NFC — making "Relay 1 ON for PN532+line" internally
    inconsistent.

  Resolution: SPEC wins. Canonical Auto-mode relay state is
    R1=OFF, R2=ON, R3=ON, settle 200 ms. Recorded in
    `constraints.md#CON-relay-power-domains`.

  source: AGV/docs/agent_skill.txt
  source: AGV/docs/Auto_mode.txt
  source: AGV/docs/setup_wifi_MQTT.txt

  → No action required. If the PRD prose is later edited, this entry can be
    removed.

---

[INFO] Auto-resolved: SPEC > PRD on Follow-mode relay state

  Found: `AGV/docs/agent_skill.txt` (PRD, precedence 2) states in prose:
    "Relay 1 OFF, Relay 2 ON (Huskylens + SR05)" for Follow mode — again
    using legacy two-relay wording.

  Conflicts with: `AGV/docs/Follow_mode.txt` (SPEC, precedence 1):
    "Tắt R2 và R3 (line, PN532), Bật R1 (Huskylens, SR05, servo)".

  Also conflicts with: PRD's own relay table (R1=Vision, not R2).

  Resolution: SPEC wins. Canonical Follow-mode relay state is
    R1=ON, R2=OFF, R3=OFF, settle 300 ms. Recorded in
    `constraints.md#CON-relay-power-domains`.

  source: AGV/docs/agent_skill.txt
  source: AGV/docs/Follow_mode.txt

  → No action required.

---

[INFO] Auto-resolved: SPEC > PRD on Recovery-mode entry location

  Found: `AGV/docs/agent_skill.txt` (PRD, precedence 2) states a **universal**
    mode-switch constraint: "Chuyển mode chỉ được phép khi robot ở checkpoint
    MED và đang IDLE" — and specifically for Recovery:
    "Khi double click để chuyển từ Follow về Auto (tại MED)".

  Conflicts with: `AGV/docs/Follow_mode.txt` (SPEC, precedence 1) which
    explicitly refines this: "Chuyển từ Follow (hoặc Find) về Auto một cách
    an toàn, **không cần ở MED**" and "Khi người dùng double click từ Follow
    hoặc Find (**bất kỳ đâu**, không cần MED)".

  Also aligned with: `AGV/docs/Checklist.txt` (DOC) item 1.7:
    "button_handler... gọi callback chuyển mode (khi ở Auto+MED hoặc
    Follow/Find bất kỳ)" — independent corroboration that the universal PRD
    rule has a Follow/Find exception.

  Resolution: SPEC wins. Mode-switch gating:
    - Auto → Follow: MED + IDLE (PRD rule preserved)
    - Follow/Find → Recovery: anywhere (SPEC refinement)
  Recorded in `constraints.md#CON-mode-switch-gate` and
  `requirements.md#REQ-mode-switch-constraint`.

  **This is a behavioral refinement, not a contradiction** — the PRD's
  universal statement was over-general and the SPECs carve out the Recovery
  exception explicitly. Downstream planners should treat the per-transition
  table in `constraints.md` as authoritative and ignore the PRD's universal
  phrasing in isolation.

  source: AGV/docs/agent_skill.txt
  source: AGV/docs/Follow_mode.txt
  source: AGV/docs/Checklist.txt

  → No action required, but worth surfacing in a roadmap-time design review
    if the team wants to reconsider (e.g., is anywhere-recovery safe if the
    robot is far from the line?).

---

[INFO] Auto-resolved: SPEC > PRD on battery telemetry source

  Found: `AGV/docs/agent_skill.txt` (PRD, precedence 2) and
    `AGV/docs/Checklist.txt` (DOC, precedence 3) 1.13 both describe the
    ESP32 as the battery ADC owner ("Đo pin GPIO35", ESP32 publishes MQTT).

  Also: the UART protocol table in the PRD defines `0x81 Battery` from
    STM32 → ESP32 with `uint8 percent` — suggesting STM32 is the source.

  Conflicts with: Checklist.txt 2.11 (DOC) explicitly flags STM32 battery
    reporting as optional ("Thực tế ESP32 đo pin rồi gửi MQTT, STM32 không
    cần. Tuy nhiên vẫn có thể có pin riêng cho motor. Tùy chọn.").

  Resolution: ESP32 is the authoritative battery-percent source. `0x81` is
    retained in the UART command table as an optional STM32-side channel
    (e.g., separate motor battery) but is not required for the ≥30% gate.
    Recorded in `constraints.md#CON-uart-command-table` and
    `requirements.md#REQ-battery-guard`.

  source: AGV/docs/agent_skill.txt
  source: AGV/docs/Checklist.txt (1.13, 2.11)

  → Planner should explicitly decide whether to keep, repurpose (e.g., for
    a separate motor battery), or retire `0x81` during the firmware plan.
