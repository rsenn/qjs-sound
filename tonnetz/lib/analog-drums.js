/* Analog drum voice models in plain JS: a port of ../../analog-drums.hpp (STK based), for the browser and for node.
 *
 * ```js
 * const kick = new Tr909BassDrum(44100);
 * kick.setPitchEnvelope(380, 56, 0.055); kick.setAmpEnvelope(0.55);
 * const samples = render(kick, 0.9);                 // Float32Array, one hit, ready for an AudioBuffer
 * const hat = renderPreset('hihatClosed', 44100);    // the same, from the named patches below
 * ```
 *
 * Voices: TwinTDrum (Twin-T resonator: toms, congas, bongos, woodblock, cowbell), Tr909BassDrum (the 909 kick designer),
 * Tr909Percussion (noise and six-square-oscillator core: snare, clap, hats, cymbals). Each has the same controls as the
 * C++ class. Noise is a seeded generator, so a render is the same every time.
 */

const TWO_PI = 2 * Math.PI;

/* uniform noise in [-1, 1) from a seeded generator (xorshift32) */
function noiseSource(seed = 0x9e3779b9) {
  let s = seed >>> 0 || 1;
  return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 2147483648 - 1; };
}

/* drive: 'tanh' (default), 'cubic' or 'fold' */
export function analogDrive(x, amount, type = 'tanh') {
  if (amount <= 0) return x;
  let d = x * (1 + amount * 8);
  if (type === 'cubic') { d = Math.max(-1, Math.min(1, d)); return d - (d * d * d) / 3; }
  if (type === 'fold') {
    for (let i = 0; i < 16 && (d > 1 || d < -1); i++) d = d > 1 ? 2 - d : -2 - d;
    return d;
  }
  return Math.tanh(d);
}

/* STK TwoPole: y = b0 x - a1 y1 - a2 y2 */
class TwoPole {
  constructor() { this.b0 = 1; this.a1 = 0; this.a2 = 0; this.y1 = 0; this.y2 = 0; }
  setResonance(freq, radius, sr, normalize = false) {
    this.a2 = radius * radius; this.a1 = -2 * radius * Math.cos(TWO_PI * freq / sr);
    if (normalize) {
      const re = 1 - radius + (this.a2 - radius) * Math.cos(TWO_PI * 2 * freq / sr), im = (this.a2 - radius) * Math.sin(TWO_PI * 2 * freq / sr);
      this.b0 = Math.hypot(re, im);
    }
  }
  tick(x) { const y = this.b0 * x - this.a1 * this.y1 - this.a2 * this.y2; this.y2 = this.y1; this.y1 = y; return y; }
}

/* STK OnePole: y = b0 x - a1 y1 */
class OnePole {
  constructor() { this.b0 = 1; this.a1 = 0; this.y1 = 0; }
  setPole(p) { this.b0 = p > 0 ? 1 - p : 1 + p; this.a1 = -p; }
  tick(x) { this.y1 = this.b0 * x - this.a1 * this.y1; return this.y1; }
}

/* STK BiQuad low-, high- and band-pass: a transposed direct form with the STK coefficients */
class BiQuad {
  constructor(sr) { this.sr = sr; this.b = [1, 0, 0]; this.a = [1, 0, 0]; this.x1 = 0; this.x2 = 0; this.y1 = 0; this.y2 = 0; }
  common(fc, q) {
    const K = Math.tan(Math.PI * fc / this.sr), k2 = K * K, den = 1 / (1 + K / q + k2);
    this.a = [1, 2 * (k2 - 1) * den, (1 - K / q + k2) * den];
    return { K, k2, den };
  }
  setLowPass(fc, q) { const { k2, den } = this.common(fc, q); this.b[0] = k2 * q * den; this.b[1] = 2 * this.b[0]; this.b[2] = this.b[0]; }
  setHighPass(fc, q) { const { den } = this.common(fc, q); this.b[0] = q * den; this.b[1] = -2 * this.b[0]; this.b[2] = this.b[0]; }
  setBandPass(fc, q) { const { K, den } = this.common(fc, q); this.b[0] = K * den; this.b[1] = 0; this.b[2] = -this.b[0]; }
  tick(x) {
    const y = this.b[0] * x + this.b[1] * this.x1 + this.b[2] * this.x2 - this.a[1] * this.y1 - this.a[2] * this.y2;
    this.x2 = this.x1; this.x1 = x; this.y2 = this.y1; this.y1 = y;
    return y;
  }
}

const decayRadius = (t60, sr) => Math.pow(10, -3 / Math.max(1, t60 * sr));
const envCoeff = (t, sr) => Math.pow(1e-3, 1 / Math.max(1, t * sr));

/* Twin-T drum: a struck 2-pole resonator, with an optional pitch drop, a second resonator and a noise click */
export class TwinTDrum {
  constructor(sr = 44100, frequency = 200, seed) {
    this.sr = sr; this.frequency = frequency; this.decay = 0.25; this.drive = 0; this.driveType = 'tanh';
    this.pitchDropAmt = 0; this.pitchDropTime = 0.03; this.secondaryRatio = 1.5; this.secondaryMix = 0; this.clickAmount = 0;
    this.clickEnv = 0; this.age = 0;
    this.res = new TwoPole(); this.sec = new TwoPole(); this.noise = noiseSource(seed);
    this.refresh();
  }
  setFrequency(f) { this.frequency = f; this.refresh(); }
  setDecay(t60) { this.decay = Math.max(0.001, t60); this.refresh(); }
  setDrive(amount, type = 'tanh') { this.drive = amount; this.driveType = type; }
  setPitchDrop(semitones, time) { this.pitchDropAmt = semitones; this.pitchDropTime = Math.max(0.0005, time); }
  setSecondary(ratio, mix) { this.secondaryRatio = ratio; this.secondaryMix = mix; this.refresh(); }
  setClick(amount) { this.clickAmount = amount; }
  refresh() {
    this.res.setResonance(this.frequency, decayRadius(this.decay, this.sr), this.sr);
    this.sec.setResonance(this.frequency * this.secondaryRatio, decayRadius(this.decay, this.sr), this.sr);
  }
  /* an impulse scaled by sin(w0), so the hit peaks near `amplitude` over the whole tuning range */
  strike(amplitude = 1) {
    this.age = 0;
    this.res.tick(amplitude * Math.sin(TWO_PI * this.frequency / this.sr));
    if (this.secondaryMix > 0) this.sec.tick(amplitude * Math.sin(TWO_PI * this.frequency * this.secondaryRatio / this.sr));
    if (this.clickAmount > 0) this.clickEnv = 1;
  }
  tick() {
    if (this.pitchDropAmt) {
      const f = this.frequency * Math.pow(2, this.pitchDropAmt * Math.exp(-this.age / this.pitchDropTime) / 12);
      this.res.setResonance(f, decayRadius(this.decay, this.sr), this.sr);
      if (this.secondaryMix > 0) this.sec.setResonance(f * this.secondaryRatio, decayRadius(this.decay, this.sr), this.sr);
    }
    let y = this.res.tick(0);
    if (this.secondaryMix > 0) y += this.secondaryMix * this.sec.tick(0);
    if (this.clickEnv > 1e-4) { y += this.clickAmount * this.clickEnv * this.noise(); this.clickEnv *= 0.9; }
    this.age += 1 / this.sr;
    return analogDrive(y, this.drive, this.driveType);
  }
  trigger(v = 1) { this.strike(v); }
}

/* the 909 bass drum designer: a driven sine or triangle core, a pitch envelope with an extra fast spike, an amplitude
   decay, a punch boost, a tone filter (optionally resonant, tracking the pitch), a click and a sub oscillator */
export class Tr909BassDrum {
  constructor(sr = 44100, seed) {
    this.sr = sr;
    Object.assign(this, { pitchStart: 400, pitchEnd: 55, pitchDecay: 0.06, pitchLinear: false, pitchSpikeAmt: 0, pitchSpikeTime: 0.005,
      ampDecay: 0.45, ampLinear: false, punchAmount: 0, punchTime: 0.01, drive: 0.35, driveType: 'tanh', waveform: 'sine',
      tone: 6000, toneResonance: 0, clickLevel: 0.5, clickDecay: 0.04, subMix: 0, subOctave: 1, tune: 1 });
    this.toneFilter = new OnePole(); this.resFilter = new TwoPole(); this.noise = noiseSource(seed);
    this.setTone(this.tone);
    this.phase = 0; this.subPhase = 0; this.velocity = 0;
    this.pitchEnv = 0; this.spikeEnv = 0; this.ampEnv = 0; this.punchEnv = 0; this.clickEnv = 0;
  }
  setPitchEnvelope(start, end, decay, type = 'exp') { this.pitchStart = start; this.pitchEnd = end > 0 ? end : 1; this.pitchDecay = Math.max(0.001, decay); this.pitchLinear = type === 'linear'; }
  setPitchSpike(semitones, time) { this.pitchSpikeAmt = semitones; this.pitchSpikeTime = Math.max(0.0005, time); }
  setAmpEnvelope(decay, type = 'exp') { this.ampDecay = Math.max(0.001, decay); this.ampLinear = type === 'linear'; }
  setPunch(amount, time) { this.punchAmount = amount; this.punchTime = Math.max(0.0005, time); }
  setDrive(amount, type = 'tanh') { this.drive = amount; this.driveType = type; }
  setWaveform(type) { this.waveform = type; }
  setTone(hz) { this.tone = hz; this.toneFilter.setPole(Math.exp(-TWO_PI * Math.min(Math.max(hz, 20), this.sr * 0.45) / this.sr)); }
  setToneResonance(a) { this.toneResonance = Math.max(0, Math.min(0.95, a)); if (this.toneResonance <= 0) { this.resFilter.b0 = 1; this.resFilter.a1 = 0; this.resFilter.a2 = 0; } }
  setClick(level, decay) { this.clickLevel = level; this.clickDecay = Math.max(0.001, decay); }
  setSub(mix, octave = 1) { this.subMix = mix; this.subOctave = octave <= 0 ? 1 : octave; }
  setTune(m) { this.tune = m; }

  trigger(velocity = 1) {
    const sr = this.sr;
    this.phase = 0; this.subPhase = 0; this.velocity = velocity;
    this.pitchEnv = 1; this.pitchCoeff = envCoeff(this.pitchDecay, sr); this.pitchStep = 1 / Math.max(1, this.pitchDecay * sr);
    this.spikeEnv = 1; this.spikeCoeff = envCoeff(this.pitchSpikeTime, sr);
    this.ampEnv = 1; this.ampCoeff = envCoeff(this.ampDecay, sr); this.ampStep = 1 / Math.max(1, this.ampDecay * sr);
    this.punchEnv = 1; this.punchCoeff = envCoeff(this.punchTime, sr);
    this.clickEnv = 1; this.clickCoeff = envCoeff(this.clickDecay, sr);
  }
  osc(phase) { return this.waveform === 'triangle' ? (2 / Math.PI) * Math.asin(Math.sin(phase)) : Math.sin(phase); }
  tick() {
    const sr = this.sr, base = this.pitchEnd + (this.pitchStart - this.pitchEnd) * this.pitchEnv;
    const spike = this.pitchSpikeAmt ? Math.pow(2, this.pitchSpikeAmt * this.spikeEnv / 12) : 1, freq = base * spike * this.tune;
    this.phase += TWO_PI * freq / sr; if (this.phase > TWO_PI) this.phase -= TWO_PI;
    let s = this.toneFilter.tick(analogDrive(this.osc(this.phase), this.drive, this.driveType));
    if (this.toneResonance > 0) this.resFilter.setResonance(Math.min(this.tone, Math.max(20, base * 3)), this.toneResonance, sr, true);
    s = this.resFilter.tick(s);
    if (this.subMix > 0) { this.subPhase += TWO_PI * (freq / this.subOctave) / sr; if (this.subPhase > TWO_PI) this.subPhase -= TWO_PI; s += this.subMix * this.osc(this.subPhase); }
    if (this.clickEnv > 1e-4) { s += this.clickLevel * this.clickEnv * this.noise(); this.clickEnv *= this.clickCoeff; }
    s *= this.ampEnv * this.velocity * (1 + this.punchAmount * this.punchEnv);
    this.pitchEnv = this.pitchLinear ? Math.max(0, this.pitchEnv - this.pitchStep) : this.pitchEnv * this.pitchCoeff;
    this.spikeEnv *= this.spikeCoeff;
    this.ampEnv = this.ampLinear ? Math.max(0, this.ampEnv - this.ampStep) : this.ampEnv * this.ampCoeff;
    this.punchEnv *= this.punchCoeff;
    return s;
  }
}

/* the six inharmonic square oscillators of the 808/909 hat bank, relative to 540.4 Hz */
const METAL = [0.3799, 0.5633, 0.6839, 0.9673, 1.0, 1.4815];

/* noise and metal percussion: snare, clap, hats and cymbals from the same few blocks */
export class Tr909Percussion {
  constructor(sr = 44100, seed) {
    this.sr = sr;
    Object.assign(this, { tone1: 180, tone2: 330, toneMix: 0, toneDecay: 0.1, metalBase: 540, metalMix: 0, metalDecay: 0.3, noiseMix: 1,
      noiseDecay: 0.2, crunch: 0.25, crunchType: 'tanh', clapHits: 1, clapSpacing: 0.01, tune: 1 });
    this.metalFilter = new BiQuad(sr); this.metalFilter.setHighPass(7000, 0.7);
    this.noiseFilter = new BiQuad(sr); this.noiseFilter.setBandPass(2000, 1);
    this.noise = noiseSource(seed);
    this.metalPhase = Array(6).fill(0);
  }
  setTone(f1, f2, mix, decay) { Object.assign(this, { tone1: f1, tone2: f2, toneMix: mix, toneDecay: Math.max(0.001, decay) }); }
  setMetallic(base, mix, decay, brightness) { Object.assign(this, { metalBase: base, metalMix: mix, metalDecay: Math.max(0.001, decay) }); this.metalFilter.setHighPass(Math.max(20, brightness), 0.7); }
  setNoise(mix, decay) { this.noiseMix = mix; this.noiseDecay = Math.max(0.001, decay); }
  setNoiseFilter(hz, q, type = 'bandpass') {
    const c = Math.max(20, hz), qq = Math.max(0.1, q);
    if (type === 'highpass') this.noiseFilter.setHighPass(c, qq); else if (type === 'lowpass') this.noiseFilter.setLowPass(c, qq); else this.noiseFilter.setBandPass(c, qq);
  }
  setCrunch(amount, type = 'tanh') { this.crunch = amount; this.crunchType = type; }
  setClap(hits, spacing) { this.clapHits = Math.max(1, hits); this.clapSpacing = Math.max(0.001, spacing); }
  setTune(m) { this.tune = m; }

  trigger(velocity = 1) {
    const sr = this.sr;
    this.p1 = 0; this.p2 = 0; this.metalPhase.fill(0); this.velocity = velocity; this.n = 0;
    this.toneEnv = 1; this.toneCoeff = envCoeff(this.toneDecay, sr);
    this.metalEnv = 1; this.metalCoeff = envCoeff(this.metalDecay, sr);
    this.noiseEnv = 1; this.noiseCoeff = envCoeff(this.noiseDecay, sr); this.burstCoeff = envCoeff(0.006, sr);
    this.hit = 1; this.nextHit = Math.floor(this.clapSpacing * sr);
  }
  tick() {
    const sr = this.sr;
    if (this.clapHits > 1 && this.hit < this.clapHits && this.n >= this.nextHit) { this.noiseEnv = 1; this.hit++; this.nextHit += Math.floor(this.clapSpacing * sr); }
    this.n++;
    let s = 0;
    if (this.toneMix > 0) {
      this.p1 += TWO_PI * this.tone1 * this.tune / sr; if (this.p1 > TWO_PI) this.p1 -= TWO_PI;
      this.p2 += TWO_PI * this.tone2 * this.tune / sr; if (this.p2 > TWO_PI) this.p2 -= TWO_PI;
      s += this.toneMix * this.toneEnv * 0.5 * (Math.sin(this.p1) + Math.sin(this.p2));
    }
    if (this.metalMix > 0) {
      let m = 0;
      for (let i = 0; i < 6; i++) {
        this.metalPhase[i] += TWO_PI * this.metalBase * METAL[i] * this.tune / sr; if (this.metalPhase[i] > TWO_PI) this.metalPhase[i] -= TWO_PI;
        m += this.metalPhase[i] < Math.PI ? 1 : -1;
      }
      s += this.metalMix * this.metalEnv * this.metalFilter.tick(m / 6);
    }
    if (this.noiseMix > 0) s += this.noiseMix * this.noiseEnv * this.noiseFilter.tick(this.noise());
    s = analogDrive(s, this.crunch, this.crunchType) * this.velocity;
    this.toneEnv *= this.toneCoeff; this.metalEnv *= this.metalCoeff;
    this.noiseEnv *= this.hit >= this.clapHits ? this.noiseCoeff : this.burstCoeff;
    return s;
  }
}

/* one hit of a voice as samples: the voice is triggered, then ticked for `seconds`; the end fades out over 10 ms */
export function render(voice, seconds, velocity = 1) {
  const n = Math.max(1, Math.round(seconds * voice.sr)), out = new Float32Array(n), fade = Math.min(n, Math.round(0.01 * voice.sr));
  voice.trigger(velocity);
  for (let i = 0; i < n; i++) out[i] = voice.tick() * (i >= n - fade ? (n - i) / fade : 1);
  return out;
}

/* named patches, from the demo scripts of the C++ classes: [make(sr) -> voice, length in seconds] */
export const PRESETS = {
  kick: [sr => { const v = new Tr909BassDrum(sr); v.setPitchEnvelope(380, 56, 0.055); v.setAmpEnvelope(0.55); v.setDrive(0.35); v.setTone(5500); v.setClick(0.55, 0.03); return v; }, 0.8],
  kickDeep: [sr => { const v = new Tr909BassDrum(sr); v.setPitchEnvelope(320, 46, 0.07); v.setAmpEnvelope(0.75); v.setDrive(0.3); v.setTone(3000); v.setSub(0.25, 1); v.setClick(0.35, 0.025); return v; }, 1.0],
  kickPunch: [sr => { const v = new Tr909BassDrum(sr); v.setPitchEnvelope(300, 55, 0.045); v.setAmpEnvelope(0.35); v.setPunch(0.6, 0.008); v.setDrive(0.4); v.setTone(4000); v.setClick(0.4, 0.02); return v; }, 0.6],
  snare: [sr => { const v = new Tr909Percussion(sr); v.setTone(180, 330, 0.35, 0.12); v.setNoise(0.75, 0.18); v.setNoiseFilter(2200, 1.4, 'bandpass'); v.setCrunch(0.4); return v; }, 0.4],
  rim: [sr => { const v = new Tr909Percussion(sr); v.setTone(350, 520, 0.7, 0.04); v.setNoise(0.2, 0.05); v.setNoiseFilter(3000, 2.0, 'bandpass'); v.setCrunch(0.3); return v; }, 0.2],
  clap: [sr => { const v = new Tr909Percussion(sr); v.setNoise(1.0, 0.22); v.setNoiseFilter(1500, 1.2, 'bandpass'); v.setClap(4, 0.012); v.setCrunch(0.35); return v; }, 0.5],
  hihatClosed: [sr => { const v = new Tr909Percussion(sr); v.setMetallic(540, 1.0, 0.05, 8500); v.setNoise(0.15, 0.05); v.setCrunch(0.15); return v; }, 0.15],
  hihatOpen: [sr => { const v = new Tr909Percussion(sr); v.setMetallic(540, 1.0, 0.6, 7500); v.setNoise(0.25, 0.5); v.setCrunch(0.15); return v; }, 0.9],
  tomLow: [sr => { const v = new TwinTDrum(sr, 100); v.setDecay(0.9); v.setDrive(0.12); v.setPitchDrop(8, 0.09); v.setClick(0.08); return v; }, 1.2],
  tom: [sr => { const v = new TwinTDrum(sr, 165); v.setDecay(0.7); v.setDrive(0.16); v.setPitchDrop(9, 0.075); v.setClick(0.1); return v; }, 1.0],
  tomHigh: [sr => { const v = new TwinTDrum(sr, 240); v.setDecay(0.55); v.setDrive(0.2); v.setPitchDrop(10, 0.06); v.setClick(0.12); return v; }, 0.8],
  /* a muffled, rattling cowbell: two low partials, a quiet metallic layer, and a few quick bursts of band-passed noise */
  cowbell: [sr => { const v = new Tr909Percussion(sr); v.setTone(470, 700, 0.8, 0.2); v.setMetallic(330, 0.25, 0.14, 500); v.setNoise(0.5, 0.14); v.setNoiseFilter(1100, 2.5, 'bandpass'); v.setClap(5, 0.014); v.setCrunch(0.2); return v; }, 0.5],
  woodblock: [sr => { const v = new TwinTDrum(sr, 900); v.setDecay(0.08); v.setSecondary(2.0, 0.5); v.setClick(0.5); return v; }, 0.3],

  /* more kicks, snares, claps and hats */
  kickSub: [sr => { const v = new Tr909BassDrum(sr); v.setPitchEnvelope(200, 40, 0.09); v.setAmpEnvelope(1.2); v.setDrive(0.15); v.setTone(900); v.setSub(0.35, 1); v.setClick(0.1, 0.02); return v; }, 1.4],
  snareFat: [sr => { const v = new Tr909Percussion(sr); v.setTone(150, 260, 0.3, 0.2); v.setNoise(0.7, 0.3); v.setNoiseFilter(1400, 1.0, 'bandpass'); v.setCrunch(0.3); return v; }, 0.5],
  snareTight: [sr => { const v = new Tr909Percussion(sr); v.setTone(200, 350, 0.5, 0.06); v.setNoise(0.7, 0.1); v.setNoiseFilter(2600, 1.4, 'bandpass'); v.setCrunch(0.35); return v; }, 0.25],
  clapTight: [sr => { const v = new Tr909Percussion(sr); v.setNoise(1.0, 0.12); v.setNoiseFilter(1800, 1.4, 'bandpass'); v.setClap(3, 0.009); v.setCrunch(0.35); return v; }, 0.3],
  shaker: [sr => { const v = new Tr909Percussion(sr); v.setNoise(1.0, 0.07); v.setNoiseFilter(7000, 0.8, 'highpass'); v.setCrunch(0.1); return v; }, 0.14],
  hihatPedal: [sr => { const v = new Tr909Percussion(sr); v.setMetallic(480, 0.8, 0.09, 5000); v.setNoise(0.1, 0.07); v.setCrunch(0.05); return v; }, 0.2],
  hihatHalf: [sr => { const v = new Tr909Percussion(sr); v.setMetallic(540, 1.0, 0.25, 8000); v.setNoise(0.2, 0.2); v.setCrunch(0.15); return v; }, 0.5],
  crash: [sr => { const v = new Tr909Percussion(sr); v.setMetallic(460, 0.85, 1.8, 6000); v.setNoise(0.35, 1.6); v.setCrunch(0.2); return v; }, 2.2],
  ride: [sr => { const v = new Tr909Percussion(sr); v.setMetallic(380, 0.7, 1.3, 4000); v.setNoise(0.2, 1.1); v.setCrunch(0.15); return v; }, 1.6],

  /* the lesser-known 909 side: Twin-T drums, tuned and shaped. subTom and tomDeep sit in the bass range and can carry a line
     together with the kick; congas and bongos are the same circuit, tuned higher with a shorter ring and more click */
  subTom: [sr => { const v = new TwinTDrum(sr, 52); v.setDecay(0.6); v.setDrive(0.25); v.setPitchDrop(14, 0.05); v.setClick(0.15); return v; }, 1.0],
  tomDeep: [sr => { const v = new TwinTDrum(sr, 78); v.setDecay(0.5); v.setDrive(0.2); v.setPitchDrop(10, 0.06); v.setClick(0.12); return v; }, 0.9],
  congaLo: [sr => { const v = new TwinTDrum(sr, 230); v.setDecay(0.3); v.setDrive(0.12); v.setPitchDrop(3, 0.02); v.setSecondary(1.6, 0.12); v.setClick(0.28); return v; }, 0.5],
  congaHi: [sr => { const v = new TwinTDrum(sr, 340); v.setDecay(0.22); v.setDrive(0.12); v.setPitchDrop(3, 0.02); v.setSecondary(1.6, 0.12); v.setClick(0.3); return v; }, 0.4],
  bongoLo: [sr => { const v = new TwinTDrum(sr, 370); v.setDecay(0.16); v.setDrive(0.1); v.setPitchDrop(2, 0.015); v.setClick(0.3); return v; }, 0.3],
  bongoHi: [sr => { const v = new TwinTDrum(sr, 500); v.setDecay(0.12); v.setDrive(0.1); v.setPitchDrop(2, 0.015); v.setClick(0.32); return v; }, 0.25],
  claves: [sr => { const v = new TwinTDrum(sr, 1800); v.setDecay(0.05); v.setSecondary(1.0, 0); v.setClick(0.6); return v; }, 0.15],
  agogo: [sr => { const v = new TwinTDrum(sr, 880); v.setDecay(0.25); v.setDrive(0.3); v.setSecondary(1.5, 0.7); v.setClick(0.3); return v; }, 0.6],
};

/* a named patch rendered at sample rate sr, scaled so its peak is 0.9 */
export function renderPreset(name, sr = 44100) {
  const [make, seconds] = PRESETS[name], out = render(make(sr), seconds);
  let peak = 0;
  for (const x of out) peak = Math.max(peak, Math.abs(x));
  if (peak > 0) for (let i = 0; i < out.length; i++) out[i] *= 0.9 / peak;
  return out;
}

/* the sounds each row of a drum sequencer can take, the default first: kick, snare, clap, hat, open hat, tom, perc, bell */
export const BANKS = [
  ['kickDeep', 'kick', 'kickPunch', 'kickSub'],
  ['snare', 'snareFat', 'snareTight', 'rim'],
  ['clap', 'clapTight', 'shaker'],
  ['hihatClosed', 'hihatPedal', 'shaker'],
  ['hihatOpen', 'hihatHalf', 'ride', 'crash'],
  ['tom', 'tomLow', 'tomHigh', 'tomDeep', 'subTom', 'congaLo', 'congaHi', 'bongoLo', 'bongoHi'],
  ['rim', 'woodblock', 'claves'],
  ['cowbell', 'agogo'],
];

/* one hit of a Twin-T voice at a given pitch (MIDI note number), for a tuned line: subTom as the model, ring and drop kept */
export function twinTNote(midi, sr = 44100, decay = 0.45) {
  const v = new TwinTDrum(sr, 440 * Math.pow(2, (midi - 69) / 12));
  v.setDecay(decay); v.setDrive(0.25); v.setPitchDrop(6, 0.04); v.setClick(0.12);
  return render(v, Math.min(1.2, decay * 2));
}
