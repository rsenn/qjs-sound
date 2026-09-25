/* The invisible arpeggiator: a 16-step generated pattern per chord, scheduled a little ahead of the audio clock. */

import { STEPS, STYLES, makePattern, clamp } from './theory.js';

export class Sequencer {
  constructor(harmony, rack) {
    this.h = harmony;
    this.rack = rack;
    this.bpm = 116;
    this.running = false;
    this.step = 0;
    this.nextT = 0;
    this.seed = 303;
    this.styleSel = 'auto';
    this.chordBars = 2;
    this.barsInChord = 0;
    this.stepQueue = [];
    this.pattern = makePattern('acid', this.seed);
    this.edited = false;
  }

  newSeed() { return (this.seed = (this.seed * 1103515245 + 12345) >>> 0); }

  /* Each phrase position gets its own family of patterns so the arpeggio breathes with the harmony. */
  pickStyle() {
    if (this.styleSel !== 'auto') return this.styleSel;
    const pools = [['up', 'rolling', 'skip'], ['skip', 'updown', 'acid'], ['acid', 'updown', 'rolling'], ['octave', 'pedal', 'up']];
    const pool = pools[this.h.phraseCount % 4];
    return pool[Math.floor(Math.random() * pool.length)];
  }

  /* A hand-edited pattern survives chord changes and relatching; only choosing a style discards it. */
  regenerate() {
    if (!this.edited) this.pattern = makePattern(this.pickStyle(), this.newSeed());
  }

  setStyle(style) {
    this.styleSel = style;
    this.edited = false;
    this.regenerate();
  }

  getState() {
    return {
      pattern: this.pattern.map(s => ({ ...s })), edited: this.edited,
      styleSel: this.styleSel, chordBars: this.chordBars, bpm: this.bpm,
    };
  }

  /* Presets and stored sessions may come from another version, so every field is checked and rebuilt. */
  setState(st) {
    if (!st || typeof st !== 'object') return;
    if (Array.isArray(st.pattern) && st.pattern.length === STEPS) {
      this.pattern = st.pattern.map(s => {
        s = s || {};
        return { rest: !!s.rest, deg: clamp(Math.round(+s.deg) || 0, 0, 7), accent: !!s.accent, slide: !!s.slide };
      });
      this.edited = !!st.edited;
    }
    if (st.styleSel === 'auto' || STYLES.includes(st.styleSel)) this.styleSel = st.styleSel;
    if ([1, 2, 4].includes(st.chordBars)) this.chordBars = st.chordBars;
    if (Number.isFinite(st.bpm)) this.bpm = clamp(Math.round(st.bpm), 60, 200);
  }

  edit(k, patch) {
    this.pattern[k] = { ...this.pattern[k], ...patch };
    this.edited = true;
  }

  relatch() {
    this.h.relatch();
    this.barsInChord = 0;
    this.regenerate();
  }

  playStep(k, t) {
    const { h, rack } = this;
    if (k === 0) {
      if (this.barsInChord >= this.chordBars) {
        if (h.advance()) this.regenerate();
        this.barsInChord = 0;
      }
      this.barsInChord++;
      rack.chordHit(h.pianoVoicing(), t, this.barsInChord === 1 ? 0.9 : 0.6);
    } else if (k === 10 && Math.random() < 0.5) {
      rack.chordHit(h.pianoVoicing(), t, 0.45);
    }
    const s = this.pattern[k];
    const dur = 60 / this.bpm / 4, midi = s.rest ? null : h.pitchOf(s.deg);
    this.stepQueue.push({ k, t, note: !s.rest, midi, dur });
    if (s.rest) { rack.vcoRest(t); return; }
    const prev = this.pattern[(k + STEPS - 1) % STEPS], next = this.pattern[(k + 1) % STEPS];
    const eff = { accent: s.accent, slide: s.slide && !prev.rest };
    const tie = !next.rest && next.slide;
    for (const m of rack.mods) {
      if (m.type === 'vco') rack.vcoNote(m, t, midi, eff, tie, dur);
      else if (m.type === 'fmp' && (k % 2 === 0 || s.accent)) rack.fmNote(m, midi + 12, t, s.accent ? 0.85 : 0.55);
    }
  }

  /* The 100 ms lookahead keeps notes sample-accurate despite timer jitter. */
  schedule() {
    if (!this.running) return;
    while (this.nextT < this.rack.ctx.currentTime + 0.1) {
      this.playStep(this.step, this.nextT);
      this.nextT += 60 / this.bpm / 4;
      this.step = (this.step + 1) % STEPS;
    }
  }

  setRunning(on) {
    this.running = on;
    if (on) {
      this.nextT = this.rack.ctx.currentTime + 0.05;
      this.rack.padsSet(this.h.padVoicing());
    } else {
      this.stepQueue.length = 0;
      this.rack.silence();
    }
  }
}
