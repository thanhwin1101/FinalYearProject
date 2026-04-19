/**
 * Bed ID utilities — nguồn duy nhất cho việc parse/normalize bed ID.
 *
 * Định dạng hỗ trợ:
 *   canonical : R{room}{M|O}{idx}  e.g. "R1M2", "R3O1"
 *   legacy    : R{room}-Bed{n}     e.g. "R1-Bed3"
 *
 * Mapping legacy → canonical:
 *   Bed1→M1, Bed2→O1, Bed3→M2, Bed4→O2, Bed5→M3, Bed6→O3
 */

const LEGACY_TO_SIDE = { 1: 'M1', 2: 'O1', 3: 'M2', 4: 'O2', 5: 'M3', 6: 'O3' };
const CANONICAL_TO_BED_NUM = { M1: 1, O1: 2, M2: 3, O2: 4, M3: 5, O3: 6 };

/**
 * Chuẩn hoá bed ID về dạng canonical (R{room}{side}{idx}).
 * Trả về null nếu không điều khiển được.
 * @param {string} bedId
 * @returns {string|null}
 */
export function normalizeBedToCanonical(bedId) {
  const s = String(bedId || '').trim();

  const canonical = /^R(\d+)([MO])(\d+)$/i.exec(s);
  if (canonical) {
    const room = Number(canonical[1]);
    const side = canonical[2].toUpperCase();
    const idx  = Number(canonical[3]);
    if (room >= 1 && room <= 4 && idx >= 1 && idx <= 3) return `R${room}${side}${idx}`;
    return null;
  }

  const legacy = /^R(\d+)-Bed(\d+)$/i.exec(s);
  if (legacy) {
    const room = Number(legacy[1]);
    const bed  = Number(legacy[2]);
    if (!(room >= 1 && room <= 4) || !(bed >= 1 && bed <= 6)) return null;
    const part = LEGACY_TO_SIDE[bed];
    return `R${room}${part}`;
  }

  return null;
}

/**
 * Parse canonical bed ID thành các thành phần.
 * @param {string} bedId  canonical format: R{room}{M|O}{idx}
 * @returns {{ room: number, side: string, idx: number }|null}
 */
export function parseBedId(bedId) {
  const m = /^R(\d)([MO])(\d)$/i.exec(String(bedId || ''));
  if (!m) return null;
  const room = Number(m[1]);
  const side = m[2].toUpperCase();
  const idx  = Number(m[3]);
  if (room < 1 || room > 4 || idx < 1 || idx > 3) return null;
  return { room, side, idx };
}

/**
 * Chuyển canonical → legacy (R1M1 → R1-Bed1).
 * @param {string} canonicalId
 * @returns {string|null}
 */
export function canonicalToLegacy(canonicalId) {
  const m = /^R(\d+)([MO])(\d+)$/i.exec(String(canonicalId || ''));
  if (!m) return null;
  const room = Number(m[1]);
  const side = m[2].toUpperCase();
  const idx  = Number(m[3]);
  const bedNum = CANONICAL_TO_BED_NUM[`${side}${idx}`];
  return bedNum ? `R${room}-Bed${bedNum}` : null;
}

/**
 * Trả về mảng tất cả alias (canonical + legacy) của một bed ID.
 * Dùng cho truy vấn MongoDB với $in.
 * @param {string} bedId
 * @returns {string[]}
 */
export function getBedAliases(bedId) {
  const canonical = normalizeBedToCanonical(bedId);
  if (!canonical) return [String(bedId).toUpperCase()];
  const legacy = canonicalToLegacy(canonical);
  return legacy ? [canonical.toUpperCase(), legacy.toUpperCase()] : [canonical.toUpperCase()];
}

/**
 * Parse bed ID (cả 2 format) để lấy {room, bed} phục vụ việc sort.
 * @param {string} bedId
 * @returns {{ room: number, bed: number }}
 */
export function parseBedForSort(bedId) {
  const canonical = /^R(\d+)([MO])(\d+)$/i.exec(String(bedId));
  if (canonical) {
    const side = canonical[2].toUpperCase();
    const idx  = Number(canonical[3]);
    const bedNum = CANONICAL_TO_BED_NUM[`${side}${idx}`] ?? 9999;
    return { room: Number(canonical[1]), bed: bedNum };
  }
  const legacy = /^R(\d+)-Bed(\d+)$/i.exec(String(bedId));
  if (legacy) return { room: Number(legacy[1]), bed: Number(legacy[2]) };
  return { room: 9999, bed: 9999 };
}

/**
 * Danh sách fallback 24 bed canonical khi map chưa load được.
 * @returns {string[]}
 */
export function fallbackBedIds() {
  const out = [];
  for (let r = 1; r <= 4; r++) {
    for (const part of Object.values(LEGACY_TO_SIDE)) out.push(`R${r}${part}`);
  }
  return out;
}
