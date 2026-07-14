// Demo for the quickjs-stk bindings: a StkShakers instrument driven through
// a small effects pedalboard, rendered offline to a stereo WAV file.
//
// Signal chain (chosen in this order on purpose — it's the classic
// "stompbox" order: grind first, then shape, then place in space):
//
//   StkShakers --> StkCubic --> StkIir --> ping-pong delay --> StkFreeVerb --> WAV
//     (source)      (fuzz)    (analog-     (two cross-fed        (room)
//                              style        StkDelayL lines,
//                              resonant     bouncing L/R)
//                              lowpass,
//                              swept)
//
// Distortion first while the signal is still hot and mono turns the papery
// shaker hits into a gritty, almost percussive-synth texture. Running that
// through a resonant lowpass sweep next reshapes the harmonics distortion
// just added (like a fuzz pedal into a filter/wah), giving the noise a
// vocal, "talking" quality. The ping-pong delay then throws that mono voice
// across the stereo field, and the reverb glues the bounces into a room.
//
// STK is compiled with a default sample rate of 44100 Hz (see SRATE in
// Stk.h); the "stk" module's exported Stk.sampleRate setter is currently a
// no-op placeholder (not a real binding to Stk::setSampleRate), so we just
// rely on that built-in default rather than pretend to change it.

import * as std from 'std';
import * as stk from 'stk';

const SR = 44100;
const DURATION = 8.0;
const totalFrames = Math.floor(SR * DURATION);

const mtof = (n) => 440 * Math.pow(2, (n - 69) / 12);

/* ---------- source: StkShakers ---------- */
// Instrument select numbers (noteOn's "frequency" arg, decoded back to an
// instrument index by Shakers::noteOn -- see Shakers.h doc comment).
const MARACA = 0, CABASA = 1, SEKERE = 2, GUIRO = 19;
const shakers = new stk.StkShakers(MARACA);

/* ---------- distortion: StkCubic ---------- */
const cubic = new stk.StkCubic();
cubic.setA1(0.4);
cubic.setA2(0.0);
cubic.setA3(0.9);   // odd-harmonic-heavy, fuzzy
cubic.setGain(3.5);
cubic.setThreshold(0.85);

/* ---------- analog-style filter: StkIir resonant lowpass ---------- */
// RBJ biquad lowpass, recomputed every block to sweep the cutoff -- gives
// the fuzzed shaker a "wah" -like analog filter movement.
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
const FILTER_Q = 4.5;
let iir = new stk.StkIir(...lowpassCoeffs(600, FILTER_Q));
const BLOCK = 512;

/* ---------- ping-pong delay: two StkDelayL, cross-fed ---------- */
const bpm = 96;
const eighthNoteSamples = Math.round((60 / bpm / 2) * SR);
const delayA = new stk.StkDelayL(eighthNoteSamples, eighthNoteSamples + 8);
const delayB = new stk.StkDelayL(eighthNoteSamples, eighthNoteSamples + 8);
const PING_PONG_FEEDBACK = 0.52;
let lastA = 0, lastB = 0;

/* ---------- reverb: StkFreeVerb ---------- */
const verb = new stk.StkFreeVerb();
verb.setEffectMix(0.35);

/* ---------- shaker hit pattern ---------- */
// A loose 16th-note groove that cycles through a few PhISEM instruments,
// with accented and ghost hits for dynamics.
const stepDur = 60 / bpm / 4; // 16th notes
const pattern = [
  { inst: MARACA, amp: 0.9 }, { inst: MARACA, amp: 0.25 },
  { inst: MARACA, amp: 0.5 }, { inst: MARACA, amp: 0.25 },
  { inst: CABASA, amp: 0.8 }, { inst: MARACA, amp: 0.2 },
  { inst: MARACA, amp: 0.5 }, { inst: MARACA, amp: 0.3 },
  { inst: SEKERE, amp: 0.85 }, { inst: MARACA, amp: 0.2 },
  { inst: MARACA, amp: 0.5 }, { inst: MARACA, amp: 0.25 },
  { inst: CABASA, amp: 0.7 }, { inst: GUIRO, amp: 0.6 },
  { inst: MARACA, amp: 0.5 }, { inst: MARACA, amp: 0.35 },
];
let nextHitFrame = 0, patternIndex = 0;

/* ---------- render ---------- */
const outL = new Float64Array(totalFrames);
const outR = new Float64Array(totalFrames);

for(let n = 0; n < totalFrames; n++) {
  if(n >= nextHitFrame) {
    const step = pattern[patternIndex % pattern.length];
    shakers.noteOn(mtof(step.inst), step.amp);
    patternIndex++;
    nextHitFrame += Math.round(stepDur * SR);
  }

  if(n % BLOCK === 0) {
    // Slow cutoff sweep: 400 Hz .. 2800 Hz over a ~5s cycle.
    const t = n / SR;
    const cutoff = 1600 + 1200 * Math.sin((2 * Math.PI * t) / 5);
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
  outL[n] = l;
  outR[n] = r;
}

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
console.log(`Rendered ${DURATION}s (${totalFrames} frames) -> ${outPath}`);
