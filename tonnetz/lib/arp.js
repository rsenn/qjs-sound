/* Arpeggiator runs: sequencer steps whose `deg` indexes the ladder of chord notes (see Harmony.ladder), so a run climbs
   and falls through a 3-note chord stacked over several octaves instead of walking a scale. */

import { STEPS, clamp, rng } from './theory.js';

export const ARP_STYLES = ['runs', 'up', 'down', 'updown', 'skips'];

/* Steps between rests, accents and slides, per style: the plain patterns stay busy, the runs breathe a little. */
const REST_P = { runs: 0.12, up: 0.04, down: 0.04, updown: 0.05, skips: 0.06 };

/* The runs style walks the ladder in stretches: it keeps one direction for a few notes, sometimes skips a note or
   repeats one, and turns at either end, so it sweeps through the whole octave range without ever being periodic. */
function runIndices(r, n) {
  const idx = [];
  let i = 0, dir = 1, len = 0;
  for (let k = 0; k < STEPS; k++) {
    idx.push(i);
    len++;
    if ((i === n - 1 && dir > 0) || (i === 0 && dir < 0) || (len >= 3 && r() < 0.35)) { dir = -dir; len = 0; }
    const roll = r();
    i = clamp(i + dir * (roll < 0.22 ? 2 : roll < 0.3 ? 0 : 1), 0, n - 1);
  }
  return idx;
}

function plainIndex(style, k, n) {
  switch (style) {
    case 'down': return n - 1 - (k % n);
    case 'updown': { const period = Math.max(1, 2 * n - 2), q = k % period; return q < n ? q : period - q; }
    case 'skips': return ((k >> 1) + (k & 1 ? 2 : 0)) % n;
    default: return k % n;
  }
}

/**
 * Generates one bar of arpeggio as sequencer steps.
 *
 * @param {string} style One of {@link ARP_STYLES}.
 * @param {number} seed Seed for the deterministic random generator: the same seed gives the same run.
 * @param {number} n Number of notes on the ladder (3 chord notes times the octave range).
 * @returns {{rest: boolean, deg: number, accent: boolean, slide: boolean}[]} STEPS steps whose `deg` is a ladder index in 0..n-1.
 */
export function arpPattern(style, seed, n) {
  n = Math.max(1, n);
  const r = rng(seed), idx = style === 'runs' ? runIndices(r, n) : null, p = [];
  for (let k = 0; k < STEPS; k++) {
    const deg = idx ? idx[k] : plainIndex(style, k, n);
    const rest = k > 0 && r() < (REST_P[style] ?? 0.05);
    const prev = p[k - 1], extreme = deg === 0 || deg === n - 1;
    p.push({
      rest, deg: rest && prev ? prev.deg : deg,
      accent: !rest && (k % 4 === 0 ? r() < 0.6 : r() < (extreme ? 0.4 : 0.12)),
      slide: !rest && !!prev && !prev.rest && Math.abs(deg - prev.deg) === 1 && r() < 0.3,
    });
  }
  return p;
}
