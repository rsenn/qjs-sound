/* sizes, angles, scale tables and small pure helpers shared by drawing and input. */

import { TYPES } from '../lib/synth.js';
import { NOTES, SCALES, clamp, cname, mod12 } from '../lib/theory.js';

export const al = (hex, a) => hex + Math.round(clamp(a, 0, 1) * 255).toString(16).padStart(2, '0');

export const pcHue = pc => mod12(pc * 7) * 30;

/* Module size in world units, and the zoom limits of the table view. */
export const U = 34, Z_MIN = 0.3, Z_MAX = 3;

export const stripH = 22, SPL = 10;

export const ringInner = i => U * (1 + 0.5 * i);

export const outerR = m => ringInner(TYPES[m.type].params.length) + 3;

export const nameOf = s => (s.kind === 'tri' ? cname(s.id) : s.kind === 'node' ? NOTES[s.root] : `${NOTES[s.pcs[0]]}+${NOTES[s.pcs[1]]}`);

export const WHITE_OFFS = [0, 2, 4, 5, 7, 9, 11];

/* Black keys sit on the boundary after the white key with this index. */
export const BLACK_KEYS = [[1, 1], [3, 2], [6, 4], [8, 5], [10, 6]];

export const A0 = 0.75 * Math.PI, SPAN = 1.5 * Math.PI;

export const ROOT_NAMES = NOTES, SCALE_OPTS = [null, ...SCALES];
