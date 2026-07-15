// Demo for the quickjs-stk bindings: StkShakers, STK's PhISEM (Physically
// Informed Stochastic Event Modeling) instrument, run through a light
// effects chain and rendered offline to a stereo WAV file.
//
// This has two sections:
//
//   1. A showcase that solos every one of Shakers' 23 built-in instrument
//      types in turn (Maraca through Tuned Bamboo Chimes), each given a
//      "shake-shake-shake" gesture and a moment to decay, run through only
//      a light touch of StkCubic waveshaping -- no filter, delay or
//      reverb -- so each instrument's own papery/crunchy character stays
//      clearly audible instead of getting buried.
//   2. A short groove afterward that cycles through ten of those
//      instruments in a 16th-note pattern, through a tamed version of the
//      original signal chain (distortion -> gentle sweep filter ->
//      ping-pong delay -> reverb) to show them working together
//      musically, without the earlier version's heavy resonance/feedback/
//      reverb wash swamping the transients.
//
// STK is compiled with a default sample rate of 44100 Hz (see SRATE in
// Stk.h); the "stk" module's exported Stk.sampleRate setter is currently a
// no-op placeholder (not a real binding to Stk::setSampleRate), so we just
// rely on that built-in default rather than pretend to change it.

import * as std from 'std';
import * as stk from 'stk';

const SR = 44100;
const mtof = (n) => 440 * Math.pow(2, (n - 69) / 12);

/* ---------- all 23 PhISEM instrument types (Shakers.h doc comment) ---------- */
const ALL_INSTRUMENTS = [
  [0, 'Maraca'], [1, 'Cabasa'], [2, 'Sekere'], [3, 'Tambourine'], [4, 'Sleigh Bells'],
  [5, 'Bamboo Chimes'], [6, 'Sand Paper'], [7, 'Coke Can'], [8, 'Sticks'], [9, 'Crunch'],
  [10, 'Big Rocks'], [11, 'Little Rocks'], [12, 'Next Mug'], [13, 'Penny + Mug'],
  [14, 'Nickle + Mug'], [15, 'Dime + Mug'], [16, 'Quarter + Mug'], [17, 'Franc + Mug'],
  [18, 'Peso + Mug'], [19, 'Guiro'], [20, 'Wrench'], [21, 'Water Drops'],
  [22, 'Tuned Bamboo Chimes'],
];

const shakers = new stk.StkShakers(0);
const cubic = new stk.StkCubic();

/* ---------- section 1: showcase every instrument, lightly touched ---------- */
// A continuous "shake" gesture (noteOn retriggered every 50ms, like
// actually shaking the instrument rather than a couple of taps), then a
// moment to let it decay. Guiro/Wrench (types 19/20) are ratchet-driven --
// each noteOn is one scrape -- so continuous retriggering also gives them
// a proper multi-scrape sound. Shake energy in this model decays to
// inaudible within tens of milliseconds of the last noteOn, and a few
// instruments (Water Drops in particular) only produce a sound on a
// per-sample random chance while energy is up -- a couple of taps isn't a
// long enough window for that chance to land reliably, so the shake phase
// needs to be sustained, not just a triggered decay.
const SHOWCASE_SHAKE_SPACING = 0.05;
const SHOWCASE_SHAKE_DURATION = 0.6;
const SHOWCASE_TAIL = 0.4;
const SHOWCASE_GAP = 0.25;
const SHOWCASE_ITEM_SECONDS = SHOWCASE_SHAKE_DURATION + SHOWCASE_TAIL;
const SHOWCASE_ITEM_FRAMES = Math.round(SHOWCASE_ITEM_SECONDS * SR);
const SHOWCASE_GAP_FRAMES = Math.round(SHOWCASE_GAP * SR);

// Just enough waveshaping to add a little crunch to the transients, not
// enough to smear the instrument's own character.
cubic.setA1(0.2);
cubic.setA2(0.0);
cubic.setA3(0.35);
cubic.setGain(1.3);
cubic.setThreshold(0.95);

const showcaseFrames = ALL_INSTRUMENTS.length * (SHOWCASE_ITEM_FRAMES + SHOWCASE_GAP_FRAMES);
const showcaseL = new Float64Array(showcaseFrames);

{
  let pos = 0;
  for(const [index, name] of ALL_INSTRUMENTS) {
    console.log(`showcase: ${name} (type ${index})`);
    let nextHit = 0;
    for(let n = 0; n < SHOWCASE_ITEM_FRAMES; n++) {
      if(n >= nextHit && n < SHOWCASE_SHAKE_DURATION * SR) {
        shakers.noteOn(mtof(index), 0.85);
        nextHit += Math.round(SHOWCASE_SHAKE_SPACING * SR);
      }
      showcaseL[pos + n] = cubic.tick(shakers.tick());
    }
    shakers.noteOff(0);
    pos += SHOWCASE_ITEM_FRAMES + SHOWCASE_GAP_FRAMES;
  }
}

/* ---------- section 2: a short groove, tamed effects chain ---------- */

/* -- distortion: StkCubic, still crunchy but far less extreme than before -- */
cubic.setA1(0.3);
cubic.setA2(0.0);
cubic.setA3(0.55);
cubic.setGain(1.7);
cubic.setThreshold(0.9);

/* -- gentle sweep filter: brighter range and much lower Q than before, so -- */
/* -- it shapes the tone without ringing or dulling the high-frequency    -- */
/* -- crunch that makes these instruments sound crisp.                   -- */
function lowpassCoeffs(freq, q) {
  const w0 = (2 * Math.PI * freq) / SR;
  const alpha = Math.sin(w0) / (2 * q);
  const cosw0 = Math.cos(w0);
  const a0 = 1 + alpha;
  const b0 = (1 - cosw0) / 2 / a0;
  const b1 = (1 - cosw0) / a0;
  const b2 = b0;
  const a1 = (-2 * cosw0) / a0;
  const a2 = (1 - alpha) / a0;
  return [[b0, b1, b2], [1, a1, a2]];
}
const FILTER_Q = 1.1;
let iir = new stk.StkIir(...lowpassCoeffs(4500, FILTER_Q));
const BLOCK = 512;

/* -- ping-pong delay: much lower feedback than before, so repeats fade -- */
/* -- out cleanly instead of building into a wash.                     -- */
const bpm = 96;
const eighthNoteSamples = Math.round((60 / bpm / 2) * SR);
const delayA = new stk.StkDelayL(eighthNoteSamples, eighthNoteSamples + 8);
const delayB = new stk.StkDelayL(eighthNoteSamples, eighthNoteSamples + 8);
const PING_PONG_FEEDBACK = 0.28;
let lastA = 0, lastB = 0;

/* -- reverb: subtler mix, just a touch of space -- */
const verb = new stk.StkFreeVerb();
verb.setEffectMix(0.18);

const MARACA = 0, CABASA = 1, SEKERE = 2, TAMBOURINE = 3, SLEIGHBELLS = 4, BAMBOO = 5, COKECAN = 7, STICKS = 8, GUIRO = 19,
    WATERDROPS = 21;
const stepDur = 60 / bpm / 4; // 16th notes
const groovePattern = [
  { inst: MARACA, amp: 0.9 }, { inst: MARACA, amp: 0.25 },
  { inst: CABASA, amp: 0.7 }, { inst: STICKS, amp: 0.4 },
  { inst: SEKERE, amp: 0.85 }, { inst: MARACA, amp: 0.2 },
  { inst: TAMBOURINE, amp: 0.6 }, { inst: MARACA, amp: 0.3 },
  { inst: CABASA, amp: 0.7 }, { inst: GUIRO, amp: 0.6 },
  { inst: MARACA, amp: 0.5 }, { inst: SLEIGHBELLS, amp: 0.5 },
  { inst: COKECAN, amp: 0.5 }, { inst: WATERDROPS, amp: 0.4 },
  { inst: BAMBOO, amp: 0.45 }, { inst: MARACA, amp: 0.35 },
];
const grooveSteps = groovePattern.length;
const grooveTail = 1.5;
const grooveFrames = Math.round((grooveSteps * stepDur + grooveTail) * SR);
const grooveL = new Float64Array(grooveFrames);
const grooveR = new Float64Array(grooveFrames);

{
  let nextHitFrame = 0, patternIndex = 0;
  for(let n = 0; n < grooveFrames; n++) {
    if(patternIndex < grooveSteps && n >= nextHitFrame) {
      const step = groovePattern[patternIndex % grooveSteps];
      shakers.noteOn(mtof(step.inst), step.amp);
      patternIndex++;
      nextHitFrame += Math.round(stepDur * SR);
    }

    if(n % BLOCK === 0) {
      // Slow, bright cutoff sweep: 3200 Hz .. 5800 Hz over a ~5s cycle.
      const t = n / SR;
      const cutoff = 4500 + 1300 * Math.sin((2 * Math.PI * t) / 5);
      iir = new stk.StkIir(...lowpassCoeffs(cutoff, FILTER_Q));
    }

    let x = shakers.tick();
    x = cubic.tick(x);
    x = iir.tick(x);

    const a = delayA.tick(x + PING_PONG_FEEDBACK * lastB);
    const b = delayB.tick(PING_PONG_FEEDBACK * lastA);
    lastA = a;
    lastB = b;

    const [l, r] = verb.tick(a, b);
    grooveL[n] = l;
    grooveR[n] = r;
  }
}

/* ---------- concatenate showcase + groove ---------- */

// PhISEM's shake-energy model is inherently quiet (peaks well under 0.15
// even at full velocity) -- normalize each section to a healthy target
// peak independently, since the showcase (dry) and groove (through delay
// and reverb) build up very differently and a single global normalization
// would leave one of them too quiet.
function normalize(...channels) {
  const TARGET_PEAK = 0.9;
  let peak = 0;
  for(const ch of channels)
    for(let i = 0; i < ch.length; i++)
      peak = Math.max(peak, Math.abs(ch[i]));
  const gain = peak > 0 ? TARGET_PEAK / peak : 1.0;
  for(const ch of channels)
    for(let i = 0; i < ch.length; i++)
      ch[i] *= gain;
}
normalize(showcaseL);
normalize(grooveL, grooveR);

const sectionGap = new Float64Array(Math.round(SR * 0.5));
const outL = new Float64Array(showcaseL.length + sectionGap.length + grooveL.length);
const outR = new Float64Array(outL.length);
outL.set(showcaseL, 0);
outR.set(showcaseL, 0); // showcase is dry/mono -- duplicate to both channels
outL.set(grooveL, showcaseL.length + sectionGap.length);
outR.set(grooveR, showcaseL.length + sectionGap.length);

/* ---------- write out a 16-bit stereo WAV ---------- */
function writeWav(path, left, right, sampleRate) {
  const nFrames = left.length;
  const dataSize = nFrames * 2 * 2; // stereo, 16-bit
  const buf = new ArrayBuffer(44 + dataSize);
  const dv = new DataView(buf);
  const str = (off, s) => { for(let i = 0; i < s.length; i++) dv.setUint8(off + i, s.charCodeAt(i)); };

  str(0, 'RIFF');
  dv.setUint32(4, 36 + dataSize, true);
  str(8, 'WAVE');
  str(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);  // PCM
  dv.setUint16(22, 2, true);  // channels
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * 2 * 2, true); // byte rate
  dv.setUint16(32, 4, true);  // block align
  dv.setUint16(34, 16, true); // bits per sample
  str(36, 'data');
  dv.setUint32(40, dataSize, true);

  let off = 44;
  for(let i = 0; i < nFrames; i++) {
    const l = Math.max(-1, Math.min(1, left[i]));
    const r = Math.max(-1, Math.min(1, right[i]));
    dv.setInt16(off, Math.round(l * 32767), true); off += 2;
    dv.setInt16(off, Math.round(r * 32767), true); off += 2;
  }

  const f = std.open(path, 'wb');
  f.write(buf, 0, buf.byteLength);
  f.close();
}

const outPath = 'shakers-test.wav';
writeWav(outPath, outL, outR, SR);
console.log(`Rendered showcase (${ALL_INSTRUMENTS.length} instruments, ${(showcaseL.length / SR).toFixed(2)}s) + groove (${(grooveL.length / SR).toFixed(2)}s) = ${(outL.length / SR).toFixed(2)}s -> ${outPath}`);
