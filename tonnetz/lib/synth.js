/* The modular synth rack. Talks to any WebAudio-shaped context passed in; owns no timers of its own except through `timers`. */

import { midiHz } from './theory.js';

const pct = v => Math.round(v * 100) + '%';
export const glideOf = v => 0.01 + v * 0.14;
const padCutoff = v => 250 * Math.pow(10, v);
const swellOf = v => 0.2 + v * 3.8;
const driftCents = v => 2 + v * 9;
const cutoffOf = v => 50 * Math.pow(80, v);
const decayOf = v => 0.08 + v * 0.9;
const dtime = v => 0.05 + v * v * 0.85;
export const rdecay = v => 0.4 + v * 4.6;
const fmIndex = v => 0.4 + v * 3.6;
const fmDecay = v => 0.4 + v * 3.6;
const driveOf = v => 1 + v * 11;
const distTone = v => 800 * Math.pow(16, v);
const fmtHz = f => (f < 1000 ? Math.round(f) + ' Hz' : (f / 1000).toFixed(1) + ' kHz');
const fmtMs = s => Math.round(s * 1000) + ' ms';

export const TYPES = {
  vco: {
    name: 'VCO', color: '#39d5ff',
    params: [
      { label: 'wave', def: 0, fmt: v => (v < 0.15 ? 'saw' : v > 0.85 ? 'square' : pct(v) + ' sq') },
      { label: 'glide', def: 0.4, fmt: v => fmtMs(glideOf(v)) },
      { label: 'level', def: 0.7, fmt: pct },
    ],
  },
  vcf: {
    name: 'VCF', color: '#ff4fd8',
    params: [
      { label: 'cutoff', def: 0.35, fmt: v => fmtHz(cutoffOf(v)) },
      { label: 'reso', def: 0.7, fmt: pct },
      { label: 'env mod', def: 0.6, fmt: pct },
      { label: 'decay', def: 0.35, fmt: v => fmtMs(decayOf(v)) },
    ],
  },
  fmp: {
    name: 'PIANO', color: '#ff6b6b',
    params: [
      { label: 'level', def: 0.7, fmt: pct },
      { label: 'bright', def: 0.55, fmt: v => 'index ' + fmIndex(v).toFixed(1) },
      { label: 'decay', def: 0.5, fmt: v => fmDecay(v).toFixed(1) + ' s' },
    ],
  },
  dist: {
    name: 'DIST', color: '#ffe14d',
    params: [
      { label: 'drive', def: 0.55, fmt: v => 'x' + driveOf(v).toFixed(1) },
      { label: 'tone', def: 0.65, fmt: v => fmtHz(distTone(v)) },
      { label: 'level', def: 0.7, fmt: pct },
    ],
  },
  pad: {
    name: 'PAD', color: '#7dffb0',
    params: [
      { label: 'level', def: 0.6, fmt: pct },
      { label: 'bright', def: 0.45, fmt: v => Math.round(padCutoff(v)) + ' Hz' },
      { label: 'swell', def: 0.5, fmt: v => swellOf(v).toFixed(1) + ' s' },
      { label: 'drift', def: 0.35, fmt: v => '±' + Math.round(driftCents(v)) + ' ct' },
    ],
  },
  delay: {
    name: 'DELAY', color: '#ffb340',
    params: [
      { label: 'time', def: 0.59, fmt: v => fmtMs(dtime(v)) },
      { label: 'feedback', def: 0.4, fmt: v => Math.round(v * 88) + '%' },
      { label: 'mix', def: 0.35, fmt: pct },
    ],
  },
  reverb: {
    name: 'REVERB', color: '#9b7bff',
    params: [
      { label: 'mix', def: 0.4, fmt: pct },
      { label: 'decay', def: 0.4, fmt: v => rdecay(v).toFixed(1) + ' s' },
    ],
  },
};

export class Rack {
  constructor(ctx, { timers = globalThis } = {}) {
    this.ctx = ctx;
    this.timers = timers;
    this.mods = [];

    const comp = ctx.createDynamicsCompressor();
    const master = this.master = { type: 'master', in: ctx.createGain(), x: 0, y: 0 };
    master.in.gain.value = 0.6;
    master.in.connect(comp); comp.connect(ctx.destination);
    master.an = ctx.createAnalyser(); master.an.fftSize = 256;
    master.fr = new Uint8Array(master.an.frequencyBinCount);
    master.tb = new Float32Array(master.an.fftSize);
    master.level = 0; master.hold = 0; master.holdT = 0;
    master.in.connect(master.an);
  }

  ofType(type) { return this.mods.filter(m => m.type === type); }

  /* ---------- module construction ---------- */

  build = {
    vco: m => {
      const ctx = this.ctx;
      m.saw = ctx.createOscillator(); m.saw.type = 'sawtooth';
      m.sqr = ctx.createOscillator(); m.sqr.type = 'square';
      m.gs = ctx.createGain(); m.gq = ctx.createGain();
      m.vca = ctx.createGain(); m.vca.gain.value = 0;
      m.out = ctx.createGain();
      m.saw.connect(m.gs); m.sqr.connect(m.gq);
      m.gs.connect(m.vca); m.gq.connect(m.vca); m.vca.connect(m.out);
      m.saw.frequency.value = m.sqr.frequency.value = 110;
      m.saw.start(); m.sqr.start();
    },
    vcf: m => {
      const ctx = this.ctx;
      m.in = ctx.createGain();
      m.f1 = ctx.createBiquadFilter(); m.f2 = ctx.createBiquadFilter();
      m.f1.type = m.f2.type = 'lowpass';
      m.f2.Q.value = 1;
      const shaper = ctx.createWaveShaper();
      const curve = new Float32Array(1024), k = 2.5;
      for (let i = 0; i < 1024; i++) curve[i] = Math.tanh(k * (i / 1023 * 2 - 1)) / Math.tanh(k);
      shaper.curve = curve; shaper.oversample = 'none';
      m.out = ctx.createGain();
      m.in.connect(m.f1); m.f1.connect(m.f2); m.f2.connect(shaper); shaper.connect(m.out);
      m.env = ctx.createConstantSource(); m.env.offset.value = 0;
      m.envGain = ctx.createGain();
      m.env.connect(m.envGain); m.envGain.connect(m.f1.detune); m.envGain.connect(m.f2.detune);
      m.env.start();
    },
    fmp: m => {
      m.out = this.ctx.createGain();
      m.live = [];
    },
    /* The bias before the saturator makes it asymmetric, adding even harmonics next to the odd ones. */
    dist: m => {
      const ctx = this.ctx;
      m.in = ctx.createGain();
      const shaper = ctx.createWaveShaper();
      const curve = new Float32Array(2048);
      let peak = 0;
      for (let i = 0; i < 2048; i++) {
        curve[i] = Math.tanh(3 * (i / 2047 * 2 - 1 + 0.1)) - Math.tanh(0.3);
        peak = Math.max(peak, Math.abs(curve[i]));
      }
      for (let i = 0; i < 2048; i++) curve[i] /= peak;
      shaper.curve = curve; shaper.oversample = 'none';
      m.tone = ctx.createBiquadFilter(); m.tone.type = 'lowpass';
      m.out = ctx.createGain();
      m.in.connect(shaper); shaper.connect(m.tone); m.tone.connect(m.out);
    },
    pad: m => {
      const ctx = this.ctx;
      m.out = ctx.createGain(); m.bus = ctx.createGain();
      m.bus.connect(m.out);
      m.voices = new Map(); m.dying = 0;
      m.lfo = ctx.createOscillator(); m.lfo.frequency.value = 0.11;
      m.lfoDepth = ctx.createGain();
      m.lfo.connect(m.lfoDepth); m.lfo.start();
    },
    delay: m => {
      const ctx = this.ctx;
      m.in = ctx.createGain(); m.out = ctx.createGain();
      m.d = ctx.createDelay(1); m.fb = ctx.createGain(); m.wet = ctx.createGain();
      const tone = ctx.createBiquadFilter(); tone.type = 'lowpass'; tone.frequency.value = 3000;
      m.in.connect(m.out); m.in.connect(m.d);
      m.d.connect(tone); tone.connect(m.fb); m.fb.connect(m.d);
      m.d.connect(m.wet); m.wet.connect(m.out);
    },
    reverb: m => {
      const ctx = this.ctx;
      m.in = ctx.createGain(); m.out = ctx.createGain();
      m.dry = ctx.createGain(); m.wet = ctx.createGain(); m.conv = ctx.createConvolver();
      m.in.connect(m.dry); m.dry.connect(m.out);
      m.in.connect(m.conv); m.conv.connect(m.wet); m.wet.connect(m.out);
    },
  };

  scheduleImpulse(m) {
    const ctx = this.ctx;
    this.timers.clearTimeout(m.irTimer);
    m.irTimer = this.timers.setTimeout(() => {
      const len = Math.floor(ctx.sampleRate * rdecay(m.p[1]));
      const buf = ctx.createBuffer(2, len, ctx.sampleRate);
      for (let c = 0; c < 2; c++) {
        const d = new Float32Array(len);
        for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 2.5);
        /* getChannelData is a copy in some engines, so the noise goes in through copyToChannel. */
        buf.copyToChannel(d, c);
      }
      m.conv.buffer = buf;
    }, m.irQueued ? 150 : 0);
    m.irQueued = true;
  }

  apply(m) {
    const t = this.ctx.currentTime;
    const set = (p, v) => p.setTargetAtTime(v, t, 0.02);
    const [a, b, c, d] = m.p;
    switch (m.type) {
      case 'vco':
        set(m.gs.gain, Math.cos(a * Math.PI / 2));
        set(m.gq.gain, Math.sin(a * Math.PI / 2) * 0.8);
        set(m.out.gain, c * 0.35);
        break;
      case 'fmp':
        set(m.out.gain, a * a);
        break;
      case 'dist':
        set(m.in.gain, driveOf(a));
        set(m.tone.frequency, distTone(b));
        set(m.out.gain, c * 1.1 / (1 + a * 3));
        break;
      case 'pad':
        set(m.out.gain, a * a);
        set(m.lfoDepth.gain, 60 + d * 250);
        for (const v of m.voices.values()) {
          if (v.f) set(v.f.frequency, padCutoff(b));
          v.pair.forEach((o, i) => set(o.detune, (i ? 1 : -1) * driftCents(d)));
        }
        break;
      case 'vcf':
        set(m.f1.frequency, cutoffOf(a)); set(m.f2.frequency, cutoffOf(a));
        set(m.f1.Q, b * 22);
        set(m.envGain.gain, c * 4800);
        set(m.out.gain, 0.9 / (1 + b * 0.6));
        break;
      case 'delay':
        set(m.d.delayTime, dtime(a));
        set(m.fb.gain, b * 0.88);
        set(m.wet.gain, c);
        break;
      case 'reverb':
        set(m.dry.gain, Math.cos(a * Math.PI / 2));
        set(m.wet.gain, Math.sin(a * Math.PI / 2));
        this.scheduleImpulse(m);
        break;
    }
  }

  makeModule(type, x, y) {
    const ctx = this.ctx;
    const m = { type, x, y, p: TYPES[type].params.map(q => q.def), target: null, rms: 0, pk: 0 };
    m.an = ctx.createAnalyser(); m.an.fftSize = 1024; m.buf = new Float32Array(1024);
    this.build[type](m);
    m.out.connect(m.an);
    this.apply(m);
    this.mods.push(m);
    return m;
  }

  /* ---------- patching ---------- */

  static reaches(o, m) {
    for (let c = o; c; c = c.target) if (c === m) return true;
    return false;
  }

  canConnect(s, t) {
    return t !== s && (t === this.master || (t.in && !Rack.reaches(t, s)));
  }

  disconnect(m) {
    m.out.disconnect();
    m.out.connect(m.an);
    m.target = null;
  }

  connect(s, t) {
    this.disconnect(s);
    s.out.connect(t.in);
    s.target = t;
  }

  removeModule(m) {
    for (const o of this.mods) if (o.target === m) this.disconnect(o);
    m.out.disconnect();
    if (m.saw) { m.saw.stop(); m.sqr.stop(); }
    if (m.env) m.env.stop();
    if (m.lfo) {
      for (const v of m.voices.values()) for (const o of v.oscs) o.stop();
      m.lfo.stop();
    }
    if (m.live) for (const v of m.live) { v.car.stop(); v.mod.stop(); }
    this.mods.splice(this.mods.indexOf(m), 1);
  }

  /* ---------- voices ---------- */

  vcfTrigger(m, t, accent) {
    const e = m.env.offset;
    const tau = decayOf(m.p[3]) / 3 * (accent ? 0.6 : 1);
    e.cancelScheduledValues(t);
    e.setTargetAtTime(accent ? 1 : 0.65, t, 0.002);
    e.setTargetAtTime(0, t + 0.01, tau);
  }

  vcoNote(m, t, midi, s, tie, dur) {
    const f = midiHz(midi);
    for (const o of [m.saw, m.sqr]) {
      if (s.slide) o.frequency.setTargetAtTime(f, t, glideOf(m.p[1]) / 3);
      else o.frequency.setValueAtTime(f, t);
    }
    const gn = m.vca.gain, peak = s.accent ? 1 : 0.6;
    if (s.slide) {
      gn.setTargetAtTime(peak, t, 0.005);
    } else {
      gn.cancelScheduledValues(t);
      gn.setTargetAtTime(peak, t, 0.002);
      for (let c = m.target; c; c = c.target) if (c.type === 'vcf') this.vcfTrigger(c, t, s.accent);
    }
    if (!tie) gn.setTargetAtTime(0, t + dur * 0.6, 0.015);
  }

  vcoRest(t) {
    for (const m of this.ofType('vco')) m.vca.gain.setTargetAtTime(0, t, 0.015);
  }

  /* Opens the gate and leaves it open until vcoStop; a slide keeps the filter envelope from retriggering. */
  vcoHold(midi, t, slide) {
    for (const m of this.ofType('vco')) this.vcoNote(m, t, midi, { accent: true, slide }, true, 0);
  }

  vcoStop() {
    const t = this.ctx.currentTime;
    for (const m of this.ofType('vco')) {
      m.vca.gain.cancelScheduledValues(t);
      m.vca.gain.setTargetAtTime(0, t, 0.02);
    }
  }

  /* Two-operator FM at a 1:1 ratio: a triangle modulator swings the sine carrier's frequency, and the modulation index
     collapses shortly after the strike, so each note starts bright and bell-like and mellows into an electric-piano body. */
  fmNote(m, midi, t, vel) {
    const ctx = this.ctx;
    const [, bright, dec] = m.p, f = midiHz(midi);
    const car = ctx.createOscillator(), mod = ctx.createOscillator();
    const idxG = ctx.createGain(), amp = ctx.createGain();
    car.type = 'sine'; mod.type = 'triangle';
    car.frequency.value = f; mod.frequency.value = f;
    const idx = fmIndex(bright) * (0.4 + 0.6 * vel) * f;
    idxG.gain.setValueAtTime(idx, t);
    idxG.gain.setTargetAtTime(idx * 0.15, t, 0.06 + dec * 0.5);
    const tau = fmDecay(dec) / 3.5;
    amp.gain.setValueAtTime(0, t);
    amp.gain.linearRampToValueAtTime(vel * 0.3, t + 0.004);
    amp.gain.setTargetAtTime(0, t + 0.004, tau);
    mod.connect(idxG); idxG.connect(car.frequency);
    car.connect(amp); amp.connect(m.out);
    const end = t + 0.004 + tau * 7;
    car.start(t); mod.start(t); car.stop(end); mod.stop(end);

    const now = ctx.currentTime;
    m.live = m.live.filter(v => v.end > now);
    m.live.push({ car, mod, amp, end });
    while (m.live.length > 12) {
      const old = m.live.shift();
      old.amp.gain.cancelScheduledValues(now);
      old.amp.gain.setTargetAtTime(0, now, 0.02);
      old.car.stop(now + 0.2); old.mod.stop(now + 0.2);
    }
  }

  chordHit(notes, t, vel) {
    for (const m of this.ofType('fmp')) notes.forEach((n, i) => this.fmNote(m, n, t + i * 0.014, vel));
  }

  padOn(m, n) {
    const ctx = this.ctx;
    const t = ctx.currentTime, swell = swellOf(m.p[2]), f0 = midiHz(n.midi);
    const gn = ctx.createGain(); gn.gain.value = 0; gn.connect(m.bus);
    const oscs = [], pair = [];
    let f = null;
    if (n.bass) {
      const o = ctx.createOscillator(); o.type = 'sine'; o.frequency.value = f0;
      o.connect(gn); o.start(); oscs.push(o);
    } else {
      /* Triangles and a sine an octave up keep every partial on the harmonic series, so the only beating is the slow
         chorus between the detuned pair; saws here made the chord dense and clashing. */
      f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.Q.value = 0.3; f.frequency.value = padCutoff(m.p[1]);
      m.lfoDepth.connect(f.frequency); f.connect(gn);
      for (const side of [-1, 1]) {
        const o = ctx.createOscillator(), pan = ctx.createStereoPanner();
        o.type = 'triangle'; o.frequency.value = f0; o.detune.value = side * driftCents(m.p[3]);
        pan.pan.value = side * 0.5;
        o.connect(pan); pan.connect(f); o.start(); oscs.push(o); pair.push(o);
      }
      const air = ctx.createOscillator(), airG = ctx.createGain();
      air.type = 'sine'; air.frequency.value = f0 * 2; airG.gain.value = 0.3;
      air.connect(airG); airG.connect(f); air.start(); oscs.push(air);
    }
    gn.gain.setTargetAtTime(n.bass ? 0.16 : 0.15, t, swell / 3);
    m.voices.set(n.midi, { oscs, pair, g: gn, f });
  }

  /* Rapid chord changes could pile up long releases and choke a phone, so releases shorten once many are still fading. */
  padOff(m, midi) {
    const v = m.voices.get(midi);
    m.voices.delete(midi);
    const t = this.ctx.currentTime, tau = m.dying > 10 ? 0.15 : Math.min(1.2, swellOf(m.p[2]) / 2.5);
    v.g.gain.cancelScheduledValues(t);
    v.g.gain.setValueAtTime(v.g.gain.value, t);
    v.g.gain.setTargetAtTime(0, t, tau);
    for (const o of v.oscs) o.stop(t + tau * 6);
    m.dying++;
    this.timers.setTimeout(() => {
      m.dying--;
      if (v.f) m.lfoDepth.disconnect(v.f.frequency);
    }, tau * 6000 + 200);
  }

  padSet(m, notes) {
    const want = new Map(notes.map(n => [n.midi, n]));
    for (const k of [...m.voices.keys()]) if (!want.has(k)) this.padOff(m, k);
    for (const [k, n] of want) if (!m.voices.has(k)) this.padOn(m, n);
  }

  padsSet(notes) {
    for (const m of this.ofType('pad')) this.padSet(m, notes);
  }

  silence() {
    this.padsSet([]);
    this.vcoStop();
  }
}
