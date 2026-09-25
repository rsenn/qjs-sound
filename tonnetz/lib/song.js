/* Song arrangements as scale degrees relative to the tonic (1 = tonic, 4 = subdominant, 5 = dominant ...), so the
   same arrangement works in any key: [1, 1, 4, 4, 6, 6, 5, 5, 1, 1, ...] is C C F F Am Am G G C C in C major. */

import { rng, cid, mod12 } from './theory.js';

/* Weighted next-degree choices: mostly the moves common in popular music (I-IV-V-vi and friends). */
const NEXT = {
  1: [[4, 4], [5, 3], [6, 3], [2, 1], [3, 1]],
  2: [[5, 5], [4, 2], [1, 1]],
  3: [[6, 4], [4, 3], [2, 1]],
  4: [[1, 4], [5, 4], [2, 2], [6, 2]],
  5: [[1, 5], [6, 3], [4, 2]],
  6: [[4, 4], [2, 3], [5, 3], [1, 1]],
};

function pick(next, r) {
  let x = r() * next.reduce((a, [, w]) => a + w, 0);
  for (const [deg, w] of next) if ((x -= w) < 0) return deg;
  return next[0][0];
}

/**
 * Generates a chord progression as one scale degree per bar.
 *
 * The song starts on the tonic and ends on it, approached by the dominant (5) or subdominant (4) when there is room.
 * Every chord lasts a multiple of `hold` bars, so a chord may repeat across several slots (1 1 1 1) the way a
 * hand-written arrangement holds one for a longer stretch.
 *
 * @param {object} [opts]
 * @param {number} [opts.bars=22] Length in bars; rounded down to a multiple of `hold`, at least `hold` * 2.
 * @param {number} [opts.hold=2] Bars per chord slot.
 * @param {number} [opts.seed=1] Seed for the deterministic random generator: the same seed gives the same song.
 * @param {number} [opts.stay=0.2] Chance in 0..1 that a slot repeats the previous chord instead of moving on.
 * @returns {number[]} One degree (1..6) per bar; feed it to {@link songToChords} to hear it in a key.
 */
export function generateSong({ bars = 22, hold = 2, seed = 1, stay = 0.2 } = {}) {
  const slots = Math.max(2, Math.floor(bars / hold)), r = rng(seed), chords = [1];
  for (let i = 1; i < slots; i++) {
    const prev = chords[i - 1], last = i === slots - 1;
    if (last) chords.push(1);
    else if (i === slots - 2 && slots > 3) chords.push(r() < 0.7 ? 5 : 4);
    else chords.push(r() < stay ? prev : pick(NEXT[prev], r));
  }
  return chords.flatMap(d => Array(hold).fill(d));
}

const MAJOR = [0, 2, 4, 5, 7, 9, 11];

/**
 * Turns scale degrees into concrete triads of a major key. Degrees 2, 3 and 6 are minor chords, the rest major;
 * degree 7 would be diminished and is voiced as minor, since the rack only knows major and minor triads.
 *
 * @param {number[]} degrees Degrees 1..7, as returned by {@link generateSong}.
 * @param {number} [tonic=0] Pitch class of the key (0 = C, 7 = G ...).
 * @returns {number[]} Chord ids as used by `cid` and `cname` in theory.js, one per entry of `degrees`.
 */
export function songToChords(degrees, tonic = 0) {
  return degrees.map(d => {
    const i = ((d - 1) % 7 + 7) % 7;
    return cid(mod12(tonic + MAJOR[i]), [1, 2, 5, 6].includes(i));
  });
}
