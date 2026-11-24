/* morph-instrument: two STK physical models morphed in the spectral domain.
 *
 * ```
 * voice A (bowed string) --> PVA --\
 *                                   PVMorph --> PVTransp --> PVBlur --> PVS --> echo --> out
 * voice B (pan flute)    --> PVA --/
 * ```
 *
 * CC 3 slides the sound from A to B, spectrum and all: the partials of
 * the string turn into the breathy pipe while you play. The mod wheel
 * (CC 1) is the space macro (see --wheel). Voices are chosen at start
 * (--a, --b) or live by program change. Play it from the glassloom web
 * interface (or any MIDI source); doc/morph-instrument.md has the rest:
 *
 * ```sh
 * qjsm examples/morph-instrument.js            # virtual MIDI input "Morph"
 * qjsm examples/morph-instrument.js --midi 1   # an existing MIDI input port
 * qjsm examples/morph-instrument.js --list     # the input ports
 * qjsm examples/morph-instrument.js --test     # offline check, no devices
 * ```
 *
 * glassloom: "Add MIDI device..." lists the WebMIDI output named Morph
 * (ALSA: "RtMidi Input Client:Morph"). Choose it, send notes on any
 * channel; lanes on CC 1 (space) and CC 3 (morph) steer it.
 *
 *   --a NAME / --b NAME   voices. STK: Bowed Clarinet Flute BlowBotl Brass
 *                         Saxofony, marimba vibes agogo (ModalBar),
 *                         Plucked Mandolin StifKarp. glassloom: glass:flute
 *                         glass:bottle glass:guitar glass:harp
 *                         e.g. --a glass:guitar --b glass:harp
 *                              --a vibes --b marimba   --a vibes --b glass:bottle
 *   --winds PATH          glassloom's app/acid/winds.js (default
 *                         ../../../glassloom/app/acid/winds.js)
 *   --port NAME           name of the virtual port (default Morph)
 *   --debug               print every MIDI message (with its block number)
 *                         and record BASE.wav, BASE-a.wav, BASE-b.wav
 *   --record BASE         the same recording without the MIDI log; BASE
 *                         defaults to morph-debug. BASE.wav is the output,
 *                         -a and -b the two voices before the spectral chain
 *   --fft N               FFT size (default 2048; 1024 is 23 ms faster, but
 *                         blurs notes below about 110 Hz)
 *   --hold MS             shortest note, default 300: a tap is held until then
 *   --wheel MODE          what CC 1 does: space (default), the
 *                         macro from a clean voice to outer space: motor
 *                         tremolo, a growing spectral smear, a long feedback
 *                         echo and a slow warble of the spectrum's pitch,
 *                         or morph (then the morph is on CC 1, not CC 3)
 *   --low N               lowest note the voices play as they are, default 55;
 *                         lower notes are folded up octaves and shifted back
 *   --morph X             morph position at the start, 0 (A) to 1 (B)
 *   --morphs LIST         morph positions of the offline check, e.g. 0,1
 *   --pre N               silent blocks before the note of the offline check
 *   --len N               note length in blocks of the offline check (a tap)
 *   --blocks N            blocks the offline check renders (default 400)
 *   --note N              note of the offline check (default 57)
 *   --hop N               frames per block (default 256, latency)
 *   --rawwaves DIR        STK rawwaves (default third_party/stk/rawwaves/)
 */
import * as std from 'std';
import { Stk, StkFrames, RtMidiIn, RtWvOut, Bowed, Clarinet, Flute, BlowBotl, Brass, Saxofony, ModalBar, Plucked, Mandolin, StifKarp } from 'stk';
import { HammingTable, SndObj, PVA, PVS, PVMorph, PVTransp, PVBlur, VDelay } from 'sndobj';

const SR = 44100;
/* below LOW the voices are played whole octaves up and shifted back down by
 * the spectral transposer: the models sound wrong or not at all that deep */
/* a physical model needs time to speak: a note shorter than this is held on
 * until it has lasted HOLD blocks (taps of 80 ms would stay inaudible) */
const HOLD = scriptArgs.includes('--hold') ? +scriptArgs[scriptArgs.indexOf('--hold') + 1] : 300;
/* what the mod wheel (CC 1) does: `morph` slides A to B, `space` sweeps the
 * output chain from clean to outer space (the morph is then CC 3) */
const WHEEL = scriptArgs.includes('--wheel') ? scriptArgs[scriptArgs.indexOf('--wheel') + 1] : 'space';
const LOW = scriptArgs.includes('--low') ? +scriptArgs[scriptArgs.indexOf('--low') + 1] : 55;
const FFT = scriptArgs.includes('--fft') ? +scriptArgs[scriptArgs.indexOf('--fft') + 1] : 2048;
const PVSCALE = scriptArgs.includes('--pvscale') ? +scriptArgs[scriptArgs.indexOf('--pvscale') + 1] : 826 / FFT; // measured: gain = 0.00121 * FFT * scale

const GLASS_WINDS = scriptArgs.includes('--winds')
  ? scriptArgs[scriptArgs.indexOf('--winds') + 1]
  : '../../../glassloom/app/acid/winds.js';
const GLASS_STRINGS = GLASS_WINDS.replace(/winds\.js$/, 'strings.js');
const { Winds } = await import(GLASS_WINDS).catch(() => ({}));
const { Strings } = await import(GLASS_STRINGS).catch(() => ({}));

/* glassloom's blown pipes behind the STK instrument interface: noteOn, noteOff,
 * setFrequency, controlChange, tick, clear. Breath noise, vibrato and tone are
 * read at the next noteOn; CC 2 moves the pressure live. */
class GlassPipe {
  constructor(model, hop) {
    this.model = model;
    this.w = new Winds(SR);
    this.buf = new Float32Array(hop);
    this.n = 0;
    this.p = { breath: 0.4, vib: 0.5, tone: 0.5, level: 1 };
  }

  midi(hz) {
    return 69 + 12 * Math.log2(hz / 440);
  }

  noteOn(hz, vel) {
    this.n++;
    this.w.blow({ id: 1, n: this.n, model: this.model, midi: this.midi(hz), vel, ...this.p, vibDelay: 0.25, at: this.w.n });
  }

  noteOff() {
    this.w.stop({ id: 1, n: this.n, release: 0.08, at: this.w.n });
  }

  setFrequency(hz) {
    this.w.glide({ id: 1, midi: this.midi(hz), time: 0.03, at: this.w.n });
  }

  controlChange(num, v) {
    const x = v / 127;

    if(num === 2) {
      this.p.level = 0.3 + 0.7 * x;
      this.w.breath({ id: 1, level: this.p.level, time: 0.05, at: this.w.n });
    } else if(num === 4)
      this.p.tone = x;
    else if(num === 1)
      this.p.vib = x;
    else if(num === 128)
      this.p.breath = x;
  }

  tick() {
    this.buf.fill(0);
    this.w.render(this.buf, 0, this.buf.length);
    return { toFloat32: () => this.buf };
  }

  /* silences at once; a queued hush would also drop the next blow */
  clear() {
    this.w.q.length = 0;

    for(const v of this.w.v)
      v.on = false;
  }
}

/* glassloom's plucked strings (Karplus-Strong) behind the same interface.
 * They ring after the key is up, like a guitar or a harp does. */
const STRING_KINDS = {
  guitar: { t60: 1.8, bright: 0.55, pick: 0.12 }, // nylon, picked near the bridge
  harp: { t60: 4.5, bright: 0.3, pick: 0.3 }, // long, soft, plucked mid-string
};

class GlassStrings {
  constructor(kind, hop) {
    this.w = new Strings(SR);
    this.buf = new Float32Array(hop);
    this.p = { ...STRING_KINDS[kind] };
  }

  midi(hz) {
    return 69 + 12 * Math.log2(hz / 440);
  }

  noteOn(hz, vel) {
    this.w.pluck({ id: 1, midi: this.midi(hz), vel, ...this.p, at: this.w.n });
  }

  noteOff() {}

  setFrequency(hz) {
    this.w.glide({ id: 1, midi: this.midi(hz), time: 0.03, at: this.w.n });
  }

  /* CC 2: how long it rings, CC 4: where the string is plucked */
  controlChange(num, v) {
    const x = v / 127;

    if(num === 2)
      this.p.t60 = 0.3 + 6 * x;
    else if(num === 4)
      this.p.pick = 0.02 + 0.48 * x;
  }

  tick() {
    this.buf.fill(0);
    this.w.render(this.buf, 0, this.buf.length);
    return { toFloat32: () => this.buf };
  }

  clear() {
    this.w.q.length = 0;

    for(const v of this.w.v)
      v.on = false;
  }
}

/* make: builds the instrument; exc/pos/vibGain/vibRate: the controller numbers;
 * ring: struck or plucked, it rings on when the key is released */
const stk = (ctor, args, exc, pos, x = {}) => ({
  make: () => {
    const i = new ctor(...args);

    x.setup?.(i);
    return i;
  },
  exc,
  pos,
  vibGain: 1,
  vibRate: 11,
  ...x,
});
const glass = model => ({ make: hop => new GlassPipe(model, hop), exc: 2, pos: 4, vibGain: 1, vibRate: 11 });
const strings = kind => ({ make: hop => new GlassStrings(kind, hop), exc: 2, pos: 4, vibGain: -1, vibRate: -1, ring: true });
const modal = preset => stk(ModalBar, [], 2, 4, { setup: i => i.setPreset(preset), ring: true });
const plucked = (ctor, args) => stk(ctor, args, -1, -1, { vibGain: -1, vibRate: -1, ring: true });

const MODELS = {
  Bowed: stk(Bowed, [20], 2, 4),
  Clarinet: stk(Clarinet, [20], 2, 4),
  Flute: stk(Flute, [20], 2, 4),
  BlowBotl: stk(BlowBotl, [], 4, -1),
  Brass: stk(Brass, [20], 2, 4),
  Saxofony: stk(Saxofony, [20], 2, 4),
  'glass:flute': glass('flute'),
  'glass:bottle': glass('bottle'),
  'glass:guitar': strings('guitar'),
  'glass:harp': strings('harp'),
  marimba: modal(0),
  vibes: modal(1),
  agogo: modal(2),
  Plucked: plucked(Plucked, [20]),
  Mandolin: plucked(Mandolin, [20]),
  StifKarp: plucked(StifKarp, [20]),
};

/* program change n selects PROGRAMS[n]: channel 1 for voice A, channel 2
 * for voice B (0 Bowed, 1 Clarinet, ... in the order of MODELS) */
const PROGRAMS = Object.keys(MODELS);

/* linear to 0.7, then a smooth knee towards 1: a peak never wraps or hard-clips */
const soft = x => {
  const a = Math.abs(x);

  return a < 0.7 ? x : Math.sign(x) * (0.7 + 0.3 * Math.tanh((a - 0.7) / 0.3));
};
/* the phase vocoder chain leaves the voices about 12 dB down; this brings the
 * output to a normal level, and soft() catches what is still too loud */
const MAKEUP = 4;
const clamp = (x, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, x));
const mtof = n => 440 * Math.pow(2, (n - 69) / 12);

/* a 16 bit mono wav, its header kept valid while recording (a killed process
 * still leaves a playable file) */
class WavWriter {
  constructor(path, sr = SR) {
    this.f = std.open(path, 'wb');

    if(!this.f)
      throw new Error(`cannot write ${path}`);

    this.sr = sr;
    this.bytes = 0;
    this.blocks = 0;
    this.header();
  }

  header() {
    const b = new DataView(new ArrayBuffer(44)),
      tag = (o, t) => [...t].forEach((c, i) => b.setUint8(o + i, c.charCodeAt(0)));

    tag(0, 'RIFF');
    b.setUint32(4, 36 + this.bytes, true);
    tag(8, 'WAVEfmt ');
    b.setUint32(16, 16, true);
    b.setUint16(20, 1, true);
    b.setUint16(22, 1, true);
    b.setUint32(24, this.sr, true);
    b.setUint32(28, this.sr * 2, true);
    b.setUint16(32, 2, true);
    b.setUint16(34, 16, true);
    tag(36, 'data');
    b.setUint32(40, this.bytes, true);
    this.f.seek(0, std.SEEK_SET);
    this.f.write(b.buffer, 0, 44);
    this.f.seek(0, std.SEEK_END);
  }

  write(x) {
    const pcm = new Int16Array(x.length);

    for(let i = 0; i < x.length; i++)
      pcm[i] = Math.round(clamp(x[i], -1, 1) * 32767);

    this.f.write(pcm.buffer, 0, pcm.byteLength);
    this.bytes += pcm.byteLength;

    if(++this.blocks % 64 === 0) {
      this.header();
      this.f.flush();
    }
  }

  close() {
    this.header();
    this.f.close();
  }
}

/* one physical model: noteOn/off, controllers, a block of samples */
class Voice {
  constructor(name, hop) {
    const m = MODELS[name];

    if(!m)
      throw new Error(`unknown voice ${name}, one of ${Object.keys(MODELS).join(' ')}`);

    this.name = name;
    this.m = m;
    this.inst = m.make(hop);
    this.frames = new StkFrames(hop, 1);
    this.level = 1;
    this.base = 1; // set by calibrate(): equal loudness at level 1
    this.detune = 0; // cents
    this.octave = 0;
    this.hop = hop;
    this.ccs = {}; // last controller values, kept across a change of model
    this.calibrate();
  }

  /* program change: another model in the same voice slot, same settings */
  setModel(name) {
    const m = MODELS[name];

    if(!m || name === this.name)
      return false;

    this.name = name;
    this.m = m;
    this.inst = m.make(this.hop);
    this.calibrate();

    for(const [k, v] of Object.entries(this.ccs))
      this.cc(k, v);

    return true;
  }

  freq(hz) {
    return hz * Math.pow(2, this.octave + this.detune / 1200);
  }

  /* one block from the model; Guitar has only the one sample tick() */
  pull() {
    const r = this.inst.tick(this.frames);

    if(r?.toFloat32)
      return r.toFloat32();

    const out = new Float32Array(this.frames.size);

    out[0] = r;

    for(let i = 1; i < out.length; i++)
      out[i] = this.inst.tick();

    return out;
  }

  /* a struck or plucked voice is left to ring */
  off() {
    if(!this.m.ring)
      this.inst.noteOff(0.5);
  }

  cc(which, v01) {
    const n = this.m[which];

    this.ccs[which] = v01;

    if(n >= 0)
      this.inst.controlChange(n, clamp(v01) * 127);
  }

  block() {
    const out = this.pull();
    const g = this.level * this.base;

    /* a model that went NaN stays NaN: reset it, and say so */
    if(out.some(v => !Number.isFinite(v))) {
      console.log('voice went non-finite, reset:', this.name);
      this.inst.clear();
      out.fill(0);
    }

    for(let i = 0; i < out.length; i++)
      out[i] *= g;

    return out;
  }

  /* plays a note and sets base so every model sounds equally loud */
  calibrate(target = 0.25) {
    /* a sustained voice is measured once it has settled; a struck or
     * plucked one only has its first moments, so measure those */
    const from = this.m.ring ? 2 : 100, to = this.m.ring ? 40 : 150;
    let e = 0, peak = 0;

    this.inst.noteOn(220, 0.8);

    for(let i = 0; i < to; i++) {
      const blk = this.pull();

      for(const v of blk) {
        if(i >= from)
          e += v * v;

        peak = Math.max(peak, Math.abs(v));
      }
    }

    this.inst.noteOff(0.5);
    this.inst.clear();
    /* the loudness target, but no peak above 1: a pluck's attack is many times its body */
    this.base = e > 0 ? clamp(Math.min(target / Math.sqrt(e / ((to - from) * this.frames.size)), 0.5 / peak), 0.05, 40) : 1;
  }
}

/* the spectral chain; every setter is a controller below */
class Engine {
  constructor(a, b, hop) {
    this.a = a;
    this.b = b;
    this.hop = hop;
    this.win = new HammingTable(FFT, 0.5);
    this.inA = new SndObj(null, hop, SR);
    this.inB = new SndObj(null, hop, SR);
    this.pvaA = new PVA(this.win, this.inA, PVSCALE, FFT, hop, SR);
    this.pvaB = new PVA(this.win, this.inB, PVSCALE, FFT, hop, SR);
    this.morph = new PVMorph(0, 0, this.pvaA, this.pvaB, undefined, undefined, FFT, SR);
    this.transp = new PVTransp(this.morph, 1, 0, undefined, FFT, SR);
    this.blur = new PVBlur(this.transp, 2 * hop / SR, hop, FFT, SR);
    this.synth = new PVS(this.win, this.blur, FFT, hop, SR);
    this.echo = new VDelay(1, 0.25, 0, 0, 0, this.synth, undefined, undefined, undefined, undefined, hop, SR);
  }

  /* one hop of audio; both voices advance every hop, morph or not */
  render() {
    this.dryA = this.a.block();
    this.dryB = this.b.block();
    this.inA.pushIn(this.dryA);
    this.inB.pushIn(this.dryB);
    this.pvaA.process();
    this.pvaB.process();
    this.morph.process();
    this.transp.process();
    this.blur.process();
    this.synth.process();
    this.echo.process();
    return this.echo.block();
  }
}

/* the player state: held notes (last note wins), bend, controllers */
class Player {
  constructor(a, b, hop) {
    this.e = new Engine(a, b, hop);
    this.a = a;
    this.b = b;
    this.held = [];
    this.sustain = false;
    this.sustained = new Set();
    this.blockNo = 0; // blocks rendered, the clock of the minimum note length
    this.onAt = new Map(); // note -> block it started on
    this.pending = []; // note offs waiting for the minimum length
    this.bend = 0;
    this.bendRange = 2;
    this.drift = 1; // the slow warble of the space macro, a pitch ratio
    this.space = 0; // the space macro now, 0 to 1
    this.spaceTarget = 0; // where the wheel put it
    this.blurIdx = 0;
    this.fold = 0; // octaves the voices are played above the note
    this.pitch = 1; // the spectral shift ratio set by the controller
    this.note = -1;
    this.vel = 0;
    this.master = 0.8;
    this.pos = { morph: 0, curve: 0, ampOff: 0, freqOff: 0 };
    this.setMorph();
    for(const p of PARAMS)
      p.set(this, p.def);
  }

  hz() {
    return mtof(this.note + 12 * this.fold + this.bend * this.bendRange);
  }

  /* the spectral shift is the controller's, plus the octaves folded up */
  applyPitch() {
    this.e.transp.setPitch(this.pitch * this.drift * Math.pow(2, -this.fold));
  }

  noteOn(n, vel) {
    const first = this.held.length === 0;

    this.held = this.held.filter(x => x !== n);
    this.held.push(n);
    this.sustained.delete(n);
    this.pending = this.pending.filter(x => x !== n);
    this.onAt.set(n, this.blockNo);
    this.note = n;
    this.vel = vel;
    this.fold = Math.max(0, Math.ceil((LOW - n) / 12));
    this.applyPitch();

    /* a sustained voice glides on a legato note; a struck or plucked
     * one only sounds when struck, so it is struck on every note */
    for(const v of [this.a, this.b])
      if(first || v.m.ring)
        v.inst.noteOn(v.freq(this.hz()), vel);
      else
        this.retuneVoice(v);
  }

  noteOff(n, due = false) {
    const wait = this.onAt.get(n) + Math.ceil((HOLD * SR) / (1000 * this.e.hop)) - this.blockNo;

    if(!due && wait > 0 && !(this.a.m.ring && this.b.m.ring)) {
      if(!this.pending.includes(n))
        this.pending.push(n);

      return;
    }

    if(this.sustain) {
      this.sustained.add(n);
      return;
    }

    this.held = this.held.filter(x => x !== n);

    if(this.held.length) {
      this.note = this.held[this.held.length - 1];
      this.retune();
    } else {
      this.a.off();
      this.b.off();
    }
  }

  retune() {
    if(this.note < 0)
      return;

    this.fold = Math.max(0, Math.ceil((LOW - this.note) / 12));
    this.applyPitch();
    this.retuneVoice(this.a);
    this.retuneVoice(this.b);
  }

  /* a struck voice keeps ringing at its own pitch */
  retuneVoice(v) {
    if(!v.m.ring)
      v.inst.setFrequency(v.freq(this.hz()));
  }

  /* called once per rendered block: releases the notes held for HOLD */
  step() {
    this.blockNo++;

    if(Math.abs(this.spaceTarget - this.space) > 0.0005) {
      this.space += (this.spaceTarget - this.space) * 0.02;
      this.applySpace();
    }

    if(this.space > 0) {
      const t = (this.blockNo * this.e.hop) / SR;

      this.drift = 1 + this.space * (0.05 * Math.sin(2 * Math.PI * 0.23 * t) + 0.03 * Math.sin(2 * Math.PI * 0.51 * t + 1));
      this.applyPitch();
    }

    for(const n of this.pending.filter(x => this.blockNo - this.onAt.get(x) >= Math.ceil((HOLD * SR) / (1000 * this.e.hop)))) {
      this.pending = this.pending.filter(x => x !== n);
      this.noteOff(n, true);
    }
  }

  pedal(down) {
    this.sustain = down;

    if(!down) {
      for(const n of this.sustained)
        this.noteOff(n);

      this.sustained.clear();
    }
  }

  allOff() {
    this.held = [];
    this.sustained.clear();
    this.a.off();
    this.b.off();
  }

  /* the space macro: one wheel, everything that makes a note leave the
   * room. The wheel only sets a target; step() glides to it, because an
   * instant change of the echo or the smear is a click. */
  setSpace(x) {
    this.spaceTarget = x;
  }

  applySpace() {
    const blur = [0.012, 0.05, 0.12, 0.25, 0.5, 0.9, 1.5], x = this.space, i = Math.min(6, Math.floor(x * 7)), e = this.e;

    for(const v of [this.a, this.b]) {
      v.cc('vibGain', x * 0.9);
      v.cc('vibRate', 0.3 + 0.5 * x);
    }

    e.echo.setFwdgain(0.75 * x);
    e.echo.setFdbgain(0.3 + 0.55 * x);

    if(i !== this.blurIdx) {
      this.blurIdx = i;
      e.blur.setBlurTime(Math.max((2 * e.hop) / SR, blur[i]));
    }
  }

  /* the morph position through the curve; amp and freq offsets
   * pull the two halves of the morph apart */
  setMorph() {
    const { morph, curve, ampOff, freqOff } = this.pos,
      s = x => 0.5 + 0.5 * Math.tanh((x - 0.5) * (1 + curve * 9)) / Math.tanh(0.5 * (1 + curve * 9)),
      m = s(morph);

    this.e.morph.setFreqMorph(clamp(m + freqOff));
    this.e.morph.setAmpMorph(clamp(m + ampOff));
  }

  message(d) {
    const st = d[0] & 0xf0;

    if(st === 0x90 && d[2] > 0)
      this.noteOn(d[1], d[2] / 127);
    else if(st === 0x80 || (st === 0x90 && d[2] === 0))
      this.noteOff(d[1]);
    else if(st === 0xe0) {
      this.bend = (((d[2] << 7) | d[1]) - 8192) / 8192;
      this.retune();
    } else if(st === 0xc0) {
      /* program change: channel 1 voice A, channel 2 voice B */
      const v = d[0] & 15 ? this.b : this.a, name = PROGRAMS[d[1]];

      if(name && v.setModel(name)) {
        this.allOff();
        console.log(`program ${d[1]}: voice ${v === this.a ? 'A' : 'B'} = ${name}`);
      }
    } else if(st === 0xd0) {
      /* channel pressure leans on both vibratos */
      this.a.cc('vibGain', d[1] / 127);
      this.b.cc('vibGain', d[1] / 127);
    } else if(st === 0xb0) {
      const p = PARAMS.find(x => x.cc === d[1]);

      if(p)
        p.set(this, d[2] / 127);
      else if(d[1] === 64)
        this.pedal(d[2] >= 64);
      else if(d[1] === 120 || d[1] === 123)
        this.allOff();
    }
  }
}

/* cc, name, default 0..1, effect; the table is the manual */
const PARAMS = [
  WHEEL === 'space'
    ? { cc: 1, name: 'SPACE: voice to outer space (mod wheel)', def: 0, set: (p, v) => p.setSpace(v) }
    : { cc: 1, name: 'morph A to B (mod wheel)', def: scriptArgs.includes('--morph') ? +scriptArgs[scriptArgs.indexOf('--morph') + 1] : 0, set: (p, v) => { p.pos.morph = v; p.setMorph(); } },
  { cc: 3, name: 'morph A to B', def: scriptArgs.includes('--morph') ? +scriptArgs[scriptArgs.indexOf('--morph') + 1] : 0, set: (p, v) => { p.pos.morph = v; p.setMorph(); } },
  { cc: 70, name: 'morph curve: linear to snap', def: 0, set: (p, v) => { p.pos.curve = v; p.setMorph(); } },
  { cc: 71, name: 'amplitude morph offset', def: 0.5, set: (p, v) => { p.pos.ampOff = v - 0.5; p.setMorph(); } },
  { cc: 74, name: 'frequency morph offset', def: 0.5, set: (p, v) => { p.pos.freqOff = v - 0.5; p.setMorph(); } },
  { cc: 72, name: 'spectral pitch shift +-12 st', def: 0.5, set: (p, v) => { p.pitch = Math.pow(2, (v - 0.5) * 2); p.applyPitch(); } },
  { cc: 73, name: 'keep formants (on above 64)', def: 0, set: (p, v) => p.e.transp.setMode(v >= 0.5 ? 1 : 0) },
  { cc: 75, name: 'spectral blur 0..1 s', def: 0, set: (p, v) => p.e.blur.setBlurTime(Math.max(2 * p.e.hop / SR, v)) },
  { cc: 76, name: 'bend range 1..12 st', def: 1 / 11, set: (p, v) => { p.bendRange = 1 + Math.round(v * 11); } },
  { cc: 77, name: 'B detune +-50 cents', def: 0.5, set: (p, v) => { p.b.detune = (v - 0.5) * 100; p.retune(); } },
  { cc: 78, name: 'B octave -1 0 +1', def: 0.5, set: (p, v) => { p.b.octave = v < 0.33 ? -1 : v > 0.66 ? 1 : 0; p.retune(); } },
  { cc: 79, name: 'A level', def: 1, set: (p, v) => { p.a.level = v; } },
  { cc: 80, name: 'B level', def: 1, set: (p, v) => { p.b.level = v; } },
  { cc: 14, name: 'A excitation (bow pressure)', def: 0.6, set: (p, v) => p.a.cc('exc', v) },
  { cc: 15, name: 'A position (bow position)', def: 0.5, set: (p, v) => p.a.cc('pos', v) },
  { cc: 16, name: 'A vibrato depth', def: 0.1, set: (p, v) => p.a.cc('vibGain', v) },
  { cc: 17, name: 'A vibrato rate', def: 0.4, set: (p, v) => p.a.cc('vibRate', v) },
  { cc: 18, name: 'B excitation (breath/jet)', def: 0.6, set: (p, v) => p.b.cc('exc', v) },
  { cc: 19, name: 'B position / noise', def: 0.3, set: (p, v) => p.b.cc('pos', v) },
  { cc: 20, name: 'B vibrato depth', def: 0.1, set: (p, v) => p.b.cc('vibGain', v) },
  { cc: 21, name: 'B vibrato rate', def: 0.4, set: (p, v) => p.b.cc('vibRate', v) },
  { cc: 7, name: 'master volume', def: 0.8, set: (p, v) => { p.master = v; } },
  { cc: 91, name: 'echo wet', def: 0, set: (p, v) => p.e.echo.setFwdgain(v * 0.8) },
  { cc: 92, name: 'echo time 20..800 ms', def: 0.3, set: (p, v) => p.e.echo.setDelayTime(0.02 + v * 0.78) },
  { cc: 93, name: 'echo feedback', def: 0.3, set: (p, v) => p.e.echo.setFdbgain(v * 0.85) },
];

/* ---------------- offline check ---------------- */

/* the spectral centroid of 1024 samples, in Hz */
function centroid(x) {
  const N = x.length;
  let num = 0, den = 0;

  for(let k = 1; k < N / 2; k++) {
    let re = 0, im = 0;

    for(let n = 0; n < N; n++) {
      const a = (2 * Math.PI * k * n) / N;

      re += x[n] * Math.cos(a);
      im -= x[n] * Math.sin(a);
    }

    const mag = Math.hypot(re, im);

    num += mag * k * (SR / N);
    den += mag;
  }

  return den ? num / den : 0;
}

function selftest(a, b, hop) {
  const note = +opt('--note', 57);

  console.log(`morph check: ${a.name} -> ${b.name}, note ${note} (${mtof(note).toFixed(0)} Hz), fft ${FFT}, hop ${hop}`);

  for(const m of opt('--morphs', '0,0.25,0.5,0.75,1').split(',').map(Number)) {
    const p = new Player(new Voice(a.name, hop), new Voice(b.name, hop), hop);
    let tail = [], energy = 0, early = 0, peak = 0;

    const rec = recordBase ? new WavWriter(`${recordBase}-morph-${m.toFixed(2)}.wav`) : null,
      dry = recordBase && m === 0 ? [new WavWriter(`${recordBase}-a.wav`), new WavWriter(`${recordBase}-b.wav`)] : null;

    p.message([0xb0, 1, Math.round(m * 127)]);

    /* silent blocks first, as when the instrument waits for a player */
    for(let i = 0, pre = +opt('--pre', 0); i < pre; i++) {
      const blk = p.e.render();

      p.step();
      rec?.write(blk);

      if(blk.some(v => !Number.isFinite(v)))
        console.log(`  non-finite output after ${i} silent blocks`);
    }

    p.message([0x90, note, 110]);

    for(let i = 0, n = +opt('--blocks', 400); i < n; i++) {
      if(i === +opt('--len', 1e9))
        p.message([0x80, note, 0]);

      const blk = p.e.render();

      p.step();
      rec?.write(blk);
      dry?.[0].write(p.e.dryA);
      dry?.[1].write(p.e.dryB);

      if(i >= +opt('--blocks', 400) - FFT / hop) {
        tail = tail.concat(Array.from(blk));
      }

      if(i >= 200)
        for(const v of blk)
          energy += v * v;

      if(i >= 2 && i < 62)
        for(const v of blk) {
          early += v * v;
          peak = Math.max(peak, Math.abs(v));
        }
    }

    rec?.close();
    dry?.forEach(w => w.close());

    const bad = tail.some(v => !Number.isFinite(v));

    console.log(
      `  morph ${m.toFixed(2)}  first 350 ms: rms ${Math.sqrt(early / (60 * hop)).toFixed(4)} peak ${peak.toFixed(3)}  later rms ${Math.sqrt(energy / (200 * hop)).toFixed(4)}  centroid ${centroid(tail).toFixed(0)} Hz${bad ? '  NaN!' : ''}`,
    );

    if(bad)
      std.exit(1);
  }
}

/* ---------------- main ---------------- */

const args = scriptArgs.slice(1);
const opt = (k, d) => (args.includes(k) ? args[args.indexOf(k) + 1] : d);
const hop = +opt('--hop', 256);

Stk.setRawwavePath(opt('--rawwaves', 'third_party/stk/rawwaves/'));

const midi = new RtMidiIn(0, 'RtMidi Input Client');

if(args.includes('--list')) {
  for(let i = 0; i < midi.getPortCount(); i++)
    console.log(i, midi.getPortName(i));

  std.exit(0);
}

const recordBase = args.includes('--record') ? opt('--record', 'morph-debug') : args.includes('--debug') ? 'morph-debug' : null;
const A = new Voice(opt('--a', 'Bowed'), hop);
const B = new Voice(opt('--b', 'BlowBotl'), hop);

if(args.includes('--test')) {
  selftest(A, B, hop);
  std.exit(0);
}

const player = new Player(A, B, hop);

/* --replay FILE: feeds the "midi <block> <bytes>" lines of a --debug log
 * to the instrument offline, and reports where the output goes silent
 * or non-finite while notes are held */
if(args.includes('--replay')) {
  const ev = std.loadFile(opt('--replay', '')).split('\n').map(l => l.match(/^midi (\d+) ([\d ]+)$/)).filter(Boolean).map(m => [+m[1], m[2].split(' ').map(Number)]);
  const last = ev.length ? ev[ev.length - 1][0] + 600 : 0;
  let k = 0, silent = 0, clicks = 0, prev1 = 0, prev2 = 0;

  for(let b = 0; b < last; b++) {
    for(; k < ev.length && ev[k][0] <= b; k++)
      player.message(ev[k][1]);

    const blk = player.e.render();

    player.step();

    if(b >= +opt('--from', 1e9) && b < +opt('--from', 1e9) + 700 && b % 10 === 0) {
      const r = x => Math.sqrt(x.reduce((m, v) => m + v * v, 0) / x.length).toFixed(4);

      console.log('blk', b, 'out', r(blk), 'A', r(player.e.dryA), 'B', r(player.e.dryB), 'held', player.held.join());
    }

    for(let i = 0; i < blk.length; i++) {
      const y = blk[i];

      clicks += i < 2 && Math.abs(y - 2 * prev1 + prev2) > 0.05 ? 1 : 0;
      prev2 = prev1;
      prev1 = y;
    }

    const bad = blk.some(v => !Number.isFinite(v)), peak = blk.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

    if(bad)
      console.log('non-finite output at block', b, 'dry A', player.e.dryA.every(Number.isFinite), 'dry B', player.e.dryB.every(Number.isFinite));

    if(player.held.length && peak === 0) {
      if(++silent === 200)
        console.log('held note but silent output for 200 blocks, at block', b);
    } else
      silent = 0;
  }

  console.log('clicks (|d2| > 0.05):', clicks);
  console.log('replayed', ev.length, 'events,', last, 'blocks');
  std.exit(0);
}

if(args.includes('--midi'))
  midi.openPort(+opt('--midi', 0), 'Morph');
else
  midi.openVirtualPort(opt('--port', 'Morph'));

midi.ignoreTypes(true, true, true);

console.log(`morph-instrument: ${A.name} <-> ${B.name}, hop ${hop} (${((1000 * hop) / SR).toFixed(1)} ms)`);
console.log(`MIDI input: ${args.includes('--midi') ? midi.getPortName(+opt('--midi', 0)) : 'virtual port ' + opt('--port', 'Morph')}`);
console.log('controllers:');

for(const p of PARAMS)
  console.log(`  CC ${String(p.cc).padStart(3)}  ${p.name}`);

console.log('  CC  64  sustain pedal, pitch bend, channel pressure = vibrato');
console.log('  program change: channel 1 = voice A, channel 2 = voice B, one of:');
console.log('   ', PROGRAMS.map((n, i) => `${i} ${n}`).join(', '));

const rec = recordBase ? { out: new WavWriter(`${recordBase}.wav`), a: new WavWriter(`${recordBase}-a.wav`), b: new WavWriter(`${recordBase}-b.wav`) } : null;

if(rec)
  console.log(`recording ${recordBase}.wav, ${recordBase}-a.wav, ${recordBase}-b.wav`);

let blockNo = 0;
const out = new RtWvOut(1, SR, 0, 512, 4);
const frames = new StkFrames(hop, 1);

/* the stream starts on the first write; fill the device buffers first so
 * it does not run dry while the first blocks are rendered */
for(let i = 0; i < 4; i++)
  out.tick(frames);

try {
  for(;;) {
    for(let m = midi.getMessage(); m.data.length; m = midi.getMessage()) {
      if(args.includes('--debug'))
        console.log('midi', blockNo, Array.from(m.data).join(' '));

      player.message(m.data);
    }

    const block = player.e.render();

    player.step();

    if(!Number.isFinite(block[0]) || block.some(v => !Number.isFinite(v)))
      console.log('output chain non-finite at block', blockNo, 'dry A/B finite:',
        player.e.dryA.every(Number.isFinite), player.e.dryB.every(Number.isFinite));

    for(let i = 0; i < block.length; i++)
      block[i] = soft(block[i] * player.master * MAKEUP);

    if(rec) {
      rec.out.write(block);
      rec.a.write(player.e.dryA);
      rec.b.write(player.e.dryB);
    }

    blockNo++;

    out.tick(frames.fromFloat32(block));
  }
} finally {
  out.stop();
  midi.closePort();

  if(rec)
    for(const w of Object.values(rec))
      w.close();
}
