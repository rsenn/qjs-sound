/* Factory presets, so a fresh install starts with a few playable setups. Each is a full state as the page stores it. */

import { makePattern } from './theory.js';

/* `chain` lists [type, params] in signal order; modules are spread evenly over [x0, x1] and each feeds the next, the last feeds OUT. */
function preset(name, chain, { style, seed, bpm, chordBars, arp }, { x0, x1, y }) {
  const mods = chain.map(([type, p], k) => ({
    type, p, x: x0 + (x1 - x0) * (k + 0.5) / chain.length, y, to: k + 1 < chain.length ? k + 1 : 'out',
  }));
  /* An arpeggiator preset carries no pattern: the runs are dealt fresh from the chord when it loads. */
  const seq = arp
    ? { mode: 'arp', arp, styleSel: 'auto', chordBars, bpm }
    : { pattern: makePattern(style, seed), edited: false, styleSel: style, chordBars, bpm };
  return {
    name,
    state: {
      v: 2, fit: true, mods, seq,
      scale: { idx: 0, root: 0 }, octave: 2,
    },
  };
}

export function stockPresets(area) {
  return [
    preset('Acid Liner', [
      ['vco', [0, 0.55, 0.75]],
      ['dist', [0.5, 0.7, 0.6]],
      ['vcf', [0.3, 0.8, 0.75, 0.3]],
      ['delay', [0.6, 0.45, 0.3]],
    ], { arp: { octaves: 2, style: 'runs' }, bpm: 128, chordBars: 2 }, area),
    preset('Piano Room', [
      ['fmp', [0.75, 0.5, 0.65]],
      ['delay', [0.55, 0.35, 0.3]],
      ['reverb', [0.4, 0.55]],
    ], { style: 'rolling', seed: 7, bpm: 100, chordBars: 2 }, area),
    preset('Pad Wash', [
      ['pad', [0.6, 0.5, 0.7, 0.5]],
      ['dist', [0.25, 0.5, 0.6]],
      ['vcf', [0.55, 0.3, 0.2, 0.6]],
      ['reverb', [0.55, 0.7]],
    ], { style: 'up', seed: 21, bpm: 90, chordBars: 4 }, area),
    preset('Hexachord FM', [
      ['fm', [0, 0.8, 0.5, 0.5]],
    ], { style: 'rolling', seed: 3, bpm: 96, chordBars: 2 }, area),
  ];
}
