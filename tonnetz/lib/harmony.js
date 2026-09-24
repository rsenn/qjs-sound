/* Selection state, key analysis and the voice-led chord walk. Independent of audio and screen layout. */

import { KEYS, ROOT_PRIOR, mod12, cid, triadPcs, vlCost, triSel, defaultLocate } from './theory.js';

export class Harmony {
  /* `locate(id, from)` maps a chord id to a concrete lattice selection; the geometry supplies the copy nearest `from`.
     `onChange` fires after every recompute, but never from the constructor. */
  constructor({ locate = defaultLocate, onChange = () => {} } = {}) {
    this.locate = locate;
    this.onChange = onChange;
    this.octave = 2;
    this.sel = triSel(-1, 0, false);
    this.hist = [];
    this.hints = [];
    this.lastKey = 0;
    this.home = null;
    this.phraseCount = 0;
    this.trail = [];
    this.remember();
    this.compute();
  }

  remember() {
    const s = this.sel, e = { id: s.id, pcs: s.pcs, tri: s.kind === 'tri' };
    if (!this.hist.length || this.hist[0].id !== e.id) this.hist.unshift(e);
    this.hist.length = Math.min(this.hist.length, 12);
  }

  compute() {
    this.analysis = this.analyse();
    this.hints = this.scoreCands().slice(0, 3);
  }

  recompute() {
    this.compute();
    this.onChange();
  }

  commit() {
    this.remember();
    this.recompute();
  }

  setSel(s) {
    this.sel = s;
    this.recompute();
  }

  analyse() {
    const sel = this.sel;
    let best = -1e9, bk = 0;
    for (let k = 0; k < 12; k++) {
      const set = KEYS[k];
      let sc = sel.pcs.filter(pc => set.includes(pc)).length * 10;
      this.hist.forEach((h, i) => { sc += Math.pow(0.85, i) * h.pcs.filter(pc => set.includes(pc)).length / h.pcs.length * 2; });
      if (k === this.lastKey) sc += 1;
      const di = set.indexOf(sel.root);
      if (di >= 0) sc += ROOT_PRIOR[di];
      if (sc > best) { best = sc; bk = k; }
    }
    this.lastKey = bk;
    const set = KEYS[bk];
    const offs = set.map(pc => mod12(pc - sel.root)).sort((a, b) => a - b);
    if (offs[0] !== 0) offs.unshift(0);
    return { key: bk, mode: set.indexOf(sel.root), offs };
  }

  homeId() {
    if (this.home !== null) return this.home;
    const h = [...this.hist].reverse().find(e => e.tri);
    return h ? h.id : null;
  }

  /* Smooth voice leading and diatonic fit make a move likely; the phrase position bends the walk so that every
     fourth chord cadences back home, and recently played chords are penalised so it keeps travelling. */
  scoreCands() {
    const sel = this.sel, curId = sel.kind === 'tri' ? sel.id : null, set = KEYS[this.analysis.key], hm = this.homeId();
    const seen = [];
    for (const h of this.hist) if (h.tri && h.id !== curId && !seen.includes(h.id)) seen.push(h.id);
    const pos = this.phraseCount % 4, out = [];
    for (let r = 0; r < 12; r++) for (const minor of [false, true]) {
      const id = cid(r, minor);
      if (id === curId) continue;
      const pcs = triadPcs(r, minor);
      if (curId === null && !sel.pcs.every(pc => pcs.includes(pc))) continue;
      let sc = (6 - Math.min(vlCost(sel.pcs, pcs), 6)) * 0.22;
      if (pcs.every(pc => set.includes(pc))) sc += 0.5;
      const idx = seen.indexOf(id);
      if (idx >= 0) sc += 0.7 * Math.pow(0.7, idx);
      if (hm !== null) {
        const hr = hm >> 1;
        if (pos === 3 && id === hm) sc += 2;
        if (pos === 3 && r === mod12(hr + 7)) sc += 0.5;
        if (pos === 2 && (r === mod12(hr + 5) || r === mod12(hr + 7))) sc += 0.4;
      }
      if (this.trail.includes(id) && !(pos === 3 && id === hm)) sc -= 0.9;
      out.push({ id, score: sc, back: idx === 0 });
    }
    return out.sort((a, b) => b.score - a.score);
  }

  /* Weighted pick among the four best moves keeps the walk varied without wandering off the map. Returns false when no move was made. */
  advance() {
    const all = this.scoreCands(), cs = all.slice(0, 4);
    if (!cs.length) return false;
    const floor = cs[cs.length - 1].score;
    const w = cs.map(c => Math.pow(c.score - floor + 0.2, 2));
    let x = Math.random() * w.reduce((a, b) => a + b, 0);
    const cadence = this.phraseCount % 4 === 3 ? all.find(c => c.id === this.homeId()) : null;
    const pick = cadence || cs[Math.max(0, w.findIndex(v => (x -= v) <= 0))];
    const next = this.locate(pick.id, this.sel);
    if (!next) return false;
    this.trail.push(this.sel.id);
    if (this.trail.length > 3) this.trail.shift();
    this.setSel(next);
    this.commit();
    this.phraseCount++;
    return true;
  }

  /* A touch restarts the piece: the latched chord becomes home and the walk begins again from it. */
  relatch() {
    this.hist.length = 0;
    this.commit();
    this.home = this.sel.kind === 'tri' ? this.sel.id : null;
    this.trail.length = 0;
    this.phraseCount = 0;
  }

  pitchOf(deg) {
    const offs = this.analysis.offs, n = offs.length;
    return 12 * (this.octave + 1) + this.sel.root + offs[deg % n] + 12 * (Math.floor(deg / n) % 3);
  }

  /* The selected notes plus colour from the generated scale: a triad gains its diatonic 7th,
     a lone node or edge is filled up to a triad. */
  chordPcs() {
    const sel = this.sel, offs = this.analysis.offs;
    const tones = [...new Set(sel.pcs.map(pc => mod12(pc - sel.root)))];
    if (sel.kind === 'tri') tones.push(offs[6]);
    else for (const d of [2, 4, 6]) if (tones.length < 3 && !tones.includes(offs[d])) tones.push(offs[d]);
    return [...new Set(tones.map(t => mod12(sel.root + t)))].sort((a, b) => a - b);
  }

  /* Upper voices are placed by pitch class in a fixed octave window so notes shared by two chords keep the same pitch. */
  pianoVoicing() {
    const lo = 12 * (this.octave + 3);
    return [lo - 12 + this.sel.root, ...this.chordPcs().map(pc => lo + pc)];
  }

  padVoicing() {
    const lo = 12 * (this.octave + 2);
    return [{ midi: lo - 12 + this.sel.root, bass: true }, ...this.chordPcs().map(pc => ({ midi: lo + pc }))];
  }

  /* Direct play sounds exactly the touched notes: one for a node, two for an edge, three for a triangle. */
  directPiano() {
    const lo = 12 * (this.octave + 3);
    return this.sel.pcs.map(pc => lo + pc);
  }

  directPad() {
    const lo = 12 * (this.octave + 2);
    return this.sel.pcs.map(pc => ({ midi: lo + pc }));
  }

  directLead() {
    return 12 * (this.octave + 1) + this.sel.root;
  }
}
