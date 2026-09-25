/* Pattern generators that write a hand-made-sounding line into the sequencer instead of the random arpeggio styles. */

import { STEPS, SCALES, rng } from './theory.js';

const BLUES = SCALES.find(s => s.name === 'blues');

/* Degrees index the six-note blues scale: 0 root, 1 b3, 2 4th, 3 b5 (the blue note), 4 5th, 5 b7, 6 root an octave up, 7 b3 up.
   Weights favour the chord tones and keep the blue note as a colour rather than a home. */
const DEGREES = [[0, 3], [1, 2], [2, 2], [3, 1.5], [4, 3], [5, 2], [6, 1.5], [7, 1]];

/* Chance of a note on each sixteenth of a bar: the one is certain, the "e" and "a" of the beats carry the funk,
   and the quiet steps in between are the ghost-note gaps. */
const ONSET = [1, 0.15, 0.5, 0.65, 0.45, 0.2, 0.65, 0.4, 0.5, 0.2, 0.7, 0.45, 0.4, 0.25, 0.6, 0.55];
const SYNCOPATED = [3, 6, 10, 14];

function pickDegree(r) {
  let x = r() * DEGREES.reduce((a, [, w]) => a + w, 0);
  for (const [deg, w] of DEGREES) if ((x -= w) < 0) return deg;
  return 0;
}

function attempt(r, density) {
  const p = [];
  let prev = 0;
  for (let k = 0; k < STEPS; k++) {
    const rest = k > 0 && r() >= Math.min(1, ONSET[k] * density);
    if (rest) { p.push({ rest: true, deg: prev, accent: false, slide: false }); continue; }
    let deg;
    if (k === 0) deg = 0;
    else if (prev === 3) deg = r() < 0.7 ? 4 : 2;
    else if (k % 8 === 0 && r() < 0.5) deg = 0;
    else if (r() < 0.55) deg = Math.max(0, Math.min(7, prev + (r() < 0.5 ? -1 : 1) * (r() < 0.7 ? 1 : 2)));
    else deg = pickDegree(r);
    const before = p[k - 1];
    const stepwise = before && !before.rest && Math.abs(deg - before.deg) <= 2 && deg !== before.deg;
    p.push({
      rest: false, deg,
      accent: k === 0 || r() < (SYNCOPATED.includes(k) ? 0.6 : 0.1),
      slide: !!stepwise && r() < (deg === 3 ? 0.65 : 0.3),
    });
    prev = deg;
  }
  return p;
}

/**
 * Generates a one-bar funky blues line as sequencer steps.
 *
 * The line is built for the six-note blues scale: degrees 0..7 walk up through root, b3, 4, b5, 5, b7, root, b3.
 * It lands on the root on the one, syncopates the accents onto the "e" and "a" of the beats, leaves ghost-note gaps,
 * and slides into the blue note (b5), which then resolves to the 5th or the 4th.
 *
 * @param {object} [opts]
 * @param {number} [opts.seed=1] Seed for the deterministic random generator: the same seed gives the same riff.
 * @param {number} [opts.density=1] Scales how busy the riff is; 0.6 is sparse, 1.4 is dense.
 * @returns {{rest: boolean, deg: number, accent: boolean, slide: boolean}[]} STEPS steps, ready for `Sequencer.setState`.
 */
export function funkyBluesPattern({ seed = 1, density = 1 } = {}) {
  const r = rng(seed);
  let p = attempt(r, density);
  for (let i = 0; i < 20 && (p.filter(s => !s.rest).length < 6 || p.filter(s => s.rest).length < 3); i++) p = attempt(r, density);
  return p;
}

/**
 * Fills the sequencer with a funky blues improv and locks the harmony to the blues scale so the line sounds right.
 * The pattern is marked as hand-edited, so it survives chord changes until a style is picked.
 *
 * @param {import('./sequencer.js').Sequencer} seq The sequencer to fill.
 * @param {import('./harmony.js').Harmony} harmony Locked to the blues scale on `root`.
 * @param {object} [opts]
 * @param {number} [opts.root] Pitch class of the blues scale; defaults to the root of the current selection.
 * @param {number} [opts.seed=1] Riff seed, see {@link funkyBluesPattern}.
 * @param {number} [opts.density=1] Riff density, see {@link funkyBluesPattern}.
 * @param {number} [opts.bpm=104] Tempo; funk sits a little slower than the default acid tempo.
 * @returns {object[]} The generated pattern.
 */
export function fillBluesImprov(seq, harmony, { root = harmony.sel.root, seed = 1, density = 1, bpm = 104 } = {}) {
  const pattern = funkyBluesPattern({ seed, density });
  harmony.setLock(root, BLUES.iv);
  seq.setState({ pattern, edited: true, bpm });
  return pattern;
}
