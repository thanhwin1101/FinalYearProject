/**
 * ID checkpoint trên UART (uint16 từ 2 byte cuối UID MIFARE) — khớp STM32 slave_nfc_read_id.
 * Tên node khớp CarryRobot/carry_slave route_runner UID_MAP.
 */
import { CHECKPOINT_UID } from '../../seed/checkpointsF1.js';

/** @param {string} uidStr ví dụ "35:4D:9B:83" */
export function uidStringToCpId(uidStr) {
  const parts = String(uidStr || '')
    .split(':')
    .map((x) => parseInt(x.trim(), 16))
    .filter((n) => !Number.isNaN(n));
  if (parts.length < 2) return null;
  const hi = parts[parts.length - 2];
  const lo = parts[parts.length - 1];
  return (hi << 8) | lo;
}

/** @returns {Record<number, string>} */
export function buildIdToName() {
  /** @type {Record<number, string>} */
  const m = {};
  for (const [name, uid] of Object.entries(CHECKPOINT_UID)) {
    const id = uidStringToCpId(uid);
    if (id != null) m[id] = name;
  }
  return m;
}

const ID_TO_NAME = buildIdToName();

export function checkpointIdToName(id) {
  if (typeof id !== 'number' || !Number.isFinite(id)) return null;
  return ID_TO_NAME[id] ?? null;
}

const idMed = uidStringToCpId(CHECKPOINT_UID.MED);
const idR4M3 = uidStringToCpId(CHECKPOINT_UID.R4M3);

/** Route test: MED → R4M3 (2 điểm, đúng encoding firmware) */
export const ROUTE_TEST_MED_TO_R4M3 = [idMed, idR4M3].filter((x) => x != null);

export function routeReturnMedFrom(cpId) {
  if (cpId == null || idMed == null) return [];
  return [cpId, idMed];
}
