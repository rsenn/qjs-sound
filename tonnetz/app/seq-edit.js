/* touch editing of a step sequencer grid, independent of where the grid is drawn.
 *
 * ```js
 * const s = seqPress(S, seq, p);  // null when p is outside the grid
 * seqDrag(S, seq, s, p);          // while the finger moves
 * ```
 *
 *   object  S    grid geometry from seqGeo(rect)
 *   object  seq  the Sequencer whose pattern is edited
 *   object  p    pointer { x, y } in screen pixels
 */

import { STEPS, clamp } from '../lib/theory.js';

/* which strip of the grid y falls in: 'slide', 'accent', 'grid' or null */
export function seqArea(p, S) {
  if (p.y >= S.sy) return 'slide';
  if (p.y >= S.ay) return 'accent';
  if (p.y >= S.gy0 && p.y < S.gy0 + 8 * S.rh) return 'grid';
  return null;
}

export function paintStep(s, col, p, S, seq) {
  if (s.area !== 'grid') seq.edit(col, { [s.area]: s.val });
  else if (s.mode === 'erase') seq.edit(col, { rest: true });
  else seq.edit(col, { deg: clamp(7 - Math.floor((p.y - S.gy0) / S.rh), 0, 7), rest: false });
}

/* a finger lands: the gesture state, with the first cell already painted */
export function seqPress(S, seq, p) {
  const col = Math.floor((p.x - S.x0) / S.cw), area = seqArea(p, S);
  if (col < 0 || col >= STEPS || !area) return null;
  const st = seq.pattern[col], s = { kind: 'seq', area, col };
  if (area === 'grid') {
    const deg = clamp(7 - Math.floor((p.y - S.gy0) / S.rh), 0, 7);
    s.mode = !st.rest && st.deg === deg ? 'erase' : 'note';
  } else {
    s.val = !st[area];
  }
  return s;
}

/* the finger moved: paint every column it crossed */
export function seqDrag(S, seq, s, p) {
  const col = clamp(Math.floor((p.x - S.x0) / S.cw), 0, STEPS - 1), dir = col >= s.col ? 1 : -1;
  for (let c = s.col + dir; dir > 0 ? c <= col : c >= col; c += dir) paintStep(s, c, p, S, seq);
  if (col === s.col) paintStep(s, col, p, S, seq);
  s.col = col;
}
