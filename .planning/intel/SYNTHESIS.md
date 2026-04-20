# Synthesis Summary

Ingest of `AGV/docs/` completed. Single entry point for downstream consumers
(`gsd-roadmapper`, planners, discuss/plan workflows).

---

## Inputs

- 5 documents, all under `AGV/docs/`
- Mode: `new` (no pre-existing `.planning/PROJECT.md`, `REQUIREMENTS.md`, or
  `ROADMAP.md`)
- Precedence applied: `SPEC > PRD > DOC` (no ADRs present)

### Breakdown by type

| Type | Count | Files |
|------|-------|-------|
| ADR  | 0     | —     |
| SPEC | 2     | `Auto_mode.txt`, `Follow_mode.txt` |
| PRD  | 1     | `agent_skill.txt`                  |
| DOC  | 2     | `Checklist.txt`, `setup_wifi_MQTT.txt` |

All classifications were high-confidence and manifest-overridden.

---

## Outputs

| Artifact                                    | Purpose                                    |
|---------------------------------------------|--------------------------------------------|
| `.planning/intel/decisions.md`              | ADR entries (empty — ADR candidates noted) |
| `.planning/intel/requirements.md`           | 12 requirements (`REQ-*`) from PRD + SPECs |
| `.planning/intel/constraints.md`            | 11 constraints (protocol / schema / NFR)   |
| `.planning/intel/context.md`                | 10 topic notes + open questions            |
| `.planning/INGEST-CONFLICTS.md`             | Conflict report (0 BLOCKER / 0 WARNING / 4 INFO) |
| `.planning/intel/SYNTHESIS.md` (this file)  | Entry point                                |

---

## Counts

- **Decisions locked:** 0 (no ADRs; 10 candidate-ADRs listed in `decisions.md`
  for downstream consideration)
- **Requirements extracted:** 12
  - REQ-wifi-mqtt-provisioning
  - REQ-mqtt-command-channel
  - REQ-uart-protocol
  - REQ-auto-mode
  - REQ-follow-mode
  - REQ-find-mode
  - REQ-recovery-mode
  - REQ-return-to-base
  - REQ-button-ux
  - REQ-battery-guard
  - REQ-oled-ux
  - REQ-mode-switch-constraint
  - REQ-freertos-tasking  *(13 total including tasking)*
- **Constraints:** 11 (2 api-contract, 2 schema, 7 nfr/protocol)
- **Context topics:** 10

---

## Conflicts

| Bucket              | Count |
|---------------------|-------|
| BLOCKERS            | 0     |
| WARNINGS (variants) | 0     |
| INFO (auto-resolved)| 4     |

All 4 auto-resolved entries are SPEC > PRD wins where the PRD's informal
Vietnamese prose used legacy two-relay wording or an over-general
"MED IDLE only" mode-switch rule. The SPECs (`Auto_mode.txt`,
`Follow_mode.txt`) align with the PRD's own relay table and the Checklist's
button-gating language, so all 4 resolve cleanly. See `INGEST-CONFLICTS.md`
for detail.

**Cycle check:** ran. All `cross_refs` in `Checklist.txt` point to code
files (not other docs), so no document-to-document cycles are possible.

**UNKNOWN docs:** 0.

---

## Status

**STATUS: READY — safe to route.**

No BLOCKERs and no competing variants, so the roadmapper can proceed
directly. Downstream consumers should:

1. Read `requirements.md` for the source of truth on behavior.
2. Treat `constraints.md` as the authoritative contract table (especially
   CON-relay-power-domains, CON-uart-command-table, CON-mode-switch-gate).
3. Consult `context.md#Known gaps` for the 7 open questions the planner must
   answer before implementation (OLED mockups, MQTT publish topic names,
   pin assignments, battery telemetry ownership, Find mode fallback,
   MQTT reconnect backoff, Recovery entry condition — the last already
   resolved in `INGEST-CONFLICTS.md`).
4. Consider promoting the 10 candidate ADRs listed in `decisions.md` if any
   should be locked against future silent change (two-MCU split, UART
   protocol, three-relay partition, mecanum drive, etc.).
