/* Pure music theory and pattern generation: no DOM, no audio, so browsers and qjsm can share it. */

export const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const MODES = ['ionian', 'dorian', 'phrygian', 'lydian', 'mixolydian', 'aeolian', 'locrian'];

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
export const mod12 = n => ((n % 12) + 12) % 12;
export const midiHz = m => 440 * Math.pow(2, (m - 69) / 12);

/* Scales a player can be locked to for improvisation; intervals are semitones above the root. */
export const SCALES = [
  { name: 'major', iv: [0, 2, 4, 5, 7, 9, 11] },
  { name: 'minor', iv: [0, 2, 3, 5, 7, 8, 10] },
  { name: 'major pent', iv: [0, 2, 4, 7, 9] },
  { name: 'minor pent', iv: [0, 3, 5, 7, 10] },
  { name: 'blues', iv: [0, 3, 5, 6, 7, 10] },
  { name: 'dorian', iv: [0, 2, 3, 5, 7, 9, 10] },
  { name: 'mixolydian', iv: [0, 2, 4, 5, 7, 9, 10] },
  { name: 'harmonic minor', iv: [0, 2, 3, 5, 7, 8, 11] },
  { name: 'whole tone', iv: [0, 2, 4, 6, 8, 10] },
];

export const KEYS = [...Array(12)].map((_, k) => [0, 2, 4, 5, 7, 9, 11].map(o => mod12(k + o)));
export const ROOT_PRIOR = [0.6, 0.25, 0.1, 0.25, 0.35, 0.5, 0];

export const cid = (root, minor) => root * 2 + (minor ? 1 : 0);
export const cname = id => NOTES[id >> 1] + (id & 1 ? 'm' : '');
export const triadPcs = (root, minor) => [root, mod12(root + (minor ? 3 : 4)), mod12(root + 7)];

export function triNodes(i, j, up) {
  return up ? [[i, j], [i + 1, j], [i, j + 1]] : [[i + 1, j], [i, j + 1], [i + 1, j + 1]];
}

export function triSel(i, j, up) {
  const root = mod12(7 * i + 4 * j + (up ? 0 : 4)), minor = !up;
  return {
    kind: 'tri', key: `t${i},${j},${up ? 'u' : 'd'}`, root, minor, id: cid(root, minor),
    pcs: triadPcs(root, minor), nodes: triNodes(i, j, up).map(([a, b]) => `n${a},${b}`), tri: { i, j, up },
  };
}

export function nodeSel(i, j) {
  const pc = mod12(7 * i + 4 * j);
  return { kind: 'node', key: `n${i},${j}`, root: pc, id: 'n' + pc, pcs: [pc], nodes: [`n${i},${j}`], at: [i, j] };
}

/* The root of a tonnetz edge is its lower note: the fifth, major third or minor third is measured upward from it. */
export function edgeSel(a, b) {
  const pa = mod12(7 * a[0] + 4 * a[1]), pb = mod12(7 * b[0] + 4 * b[1]);
  const d = mod12(pb - pa), aRoot = d === 7 || d === 4 || d === 3;
  const root = aRoot ? pa : pb, other = aRoot ? pb : pa;
  return {
    kind: 'edge', key: `e${a},${b}`, root, id: `e${root}-${other}`, pcs: [root, other],
    nodes: [`n${a[0]},${a[1]}`, `n${b[0]},${b[1]}`], edge: [a, b],
  };
}

/* Any lattice copy of a triad has the same pitch classes; 7 is its own inverse mod 12, so i solves 7i = root (major) or 7i + 4 = root (minor). */
export function defaultLocate(id) {
  const root = id >> 1, minor = !!(id & 1);
  return triSel(mod12((root - (minor ? 4 : 0)) * 7), 0, !minor);
}

export const circ = (a, b) => { const d = mod12(a - b); return Math.min(d, 12 - d); };
const PERMS = [[0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0]];

/* Cheapest way to move each note of the current chord onto a note of the next one, in semitones. */
export function vlCost(from, to) {
  if (from.length < 3) return from.reduce((s, a) => s + Math.min(...to.map(b => circ(a, b))), 0);
  return Math.min(...PERMS.map(p => from.reduce((s, a, i) => s + circ(a, to[p[i]]), 0)));
}

export const STEPS = 16;
export const STYLES = ['acid', 'up', 'updown', 'pedal', 'skip', 'rolling', 'octave'];
const REST_P = { acid: 0.2, up: 0.05, updown: 0.05, pedal: 0.1, skip: 0.08, rolling: 0.05, octave: 0.15 };

export function rng(a) {
  return () => {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function makePattern(style, sd) {
  const r = rng(sd), p = [];
  const degAt = k => {
    switch (style) {
      case 'up': return k % 8;
      case 'updown': { const q = k % 14; return q < 8 ? q : 14 - q; }
      case 'pedal': return k % 2 === 0 ? 0 : 1 + Math.floor(r() * 7);
      case 'skip': return [0, 2, 4, 2, 1, 3, 5, 3][k % 8];
      case 'rolling': return [0, 1, 2, 1][k % 4] + 2 * (Math.floor(k / 4) % 2);
      case 'octave': return [0, 7, 0, 4, 0, 7, 2, 4][k % 8];
      default: return k === 0 ? 0 : Math.floor(Math.pow(r(), 1.6) * 8);
    }
  };
  for (let k = 0; k < STEPS; k++) {
    const deg = degAt(k);
    const rest = k > 0 && r() < REST_P[style];
    p.push({
      rest, deg,
      accent: r() < (k % 4 === 0 ? 0.6 : 0.15),
      slide: k > 0 && !rest && !p[k - 1].rest && r() < (style === 'acid' ? 0.25 : 0.12),
    });
  }
  return p;
}
