// Demo for TwinTDrum: an analog Twin-T oscillator style drum resonator.
//
// A Twin-T RC notch network wired into an inverting feedback loop
// oscillates at its notch frequency; struck, it rings down like a tom,
// conga or woodblock -- that's exactly what TwinTDrum models (a
// stk::TwoPole resonator pinged with an impulse). Beyond the plain ring it
// adds three controls no analog Twin-T circuit had: a "secondary" detuned
// resonator for cowbell/agogo-style clusters, a noise "click" transient for
// attack punch, and a fast post-strike pitch drop for that analog-tom
// "boing". This script builds a small tuned kit out of one class and
// renders a solo groove to a WAV file.

import * as std from 'std';
import * as stk from 'stk';

const SR = 44100;

function writeWav(path, samples, sampleRate) {
  const nFrames = samples.length;
  const dataSize = nFrames * 2; // mono, 16-bit
  const buf = new ArrayBuffer(44 + dataSize);
  const dv = new DataView(buf);
  const str = (off, s) => { for(let i = 0; i < s.length; i++) dv.setUint8(off + i, s.charCodeAt(i)); };

  str(0, 'RIFF');
  dv.setUint32(4, 36 + dataSize, true);
  str(8, 'WAVE');
  str(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);
  dv.setUint16(22, 1, true); // mono
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * 2, true);
  dv.setUint16(32, 2, true);
  dv.setUint16(34, 16, true);
  str(36, 'data');
  dv.setUint32(40, dataSize, true);

  let off = 44;
  for(let i = 0; i < nFrames; i++) {
    const v = Math.max(-1, Math.min(1, samples[i]));
    dv.setInt16(off, Math.round(v * 32767), true); off += 2;
  }

  const f = std.open(path, 'wb');
  f.write(buf, 0, buf.byteLength);
  f.close();
}

// StkFrames.buffer aliases the underlying native Float64 storage, so this
// is a zero-copy view onto whatever render() produced.
function toFloat64(frames) {
  return new Float64Array(frames.buffer);
}

/* ---------- build a small tuned kit ---------- */
// Same class, five voices tuned/voiced differently -- this is the kind of
// kit you'd get from a rack of twin-T oscillator drum modules.

// Each voice carries its own tailSeconds so the render loop below can pull
// enough tail to hear the resonator all the way down, instead of chopping
// every hit off at the same fixed window regardless of its decay.

// Long decay + a wide, slow pitch drop (an octave-ish, settling over
// ~100ms) so the "analog tom" pitch bend is unmistakable instead of hiding
// inside the attack transient. Click is kept low so it accents the strike
// without swamping the resonant tail.
const lowTom = new stk.TwinTDrum(100);
lowTom.setDecay(0.9);
lowTom.setDrive(0.12);
lowTom.setPitchDrop(8, 0.09);
lowTom.setClick(0.08);
lowTom.tailSeconds = 0.9 * 3;

const midTom = new stk.TwinTDrum(165);
midTom.setDecay(0.7);
midTom.setDrive(0.16);
midTom.setPitchDrop(9, 0.075);
midTom.setClick(0.1);
midTom.tailSeconds = 0.7 * 3;

const hiTom = new stk.TwinTDrum(240);
hiTom.setDecay(0.55);
hiTom.setDrive(0.2);
hiTom.setPitchDrop(10, 0.06);
hiTom.setClick(0.12);
hiTom.tailSeconds = 0.55 * 3;

// Cowbell-ish voice: two closely-tuned resonators (the "secondary" control)
// beating against each other, more click, faster decay, hotter drive.
const cowbell = new stk.TwinTDrum(560);
cowbell.setDecay(0.4);
cowbell.setDrive(0.6);
cowbell.setSecondary(1.48, 0.8);
cowbell.setClick(0.4);
cowbell.tailSeconds = 0.4 * 3;

// Woodblock-ish voice: very short decay, high secondary ratio, no drive.
const woodblock = new stk.TwinTDrum(900);
woodblock.setDecay(0.08);
woodblock.setSecondary(2.0, 0.5);
woodblock.setClick(0.5);
woodblock.tailSeconds = 0.4;

/* ---------- a solo groove exercising the whole kit ---------- */

const bpm = 100;
const step = 60 / bpm / 4; // 16th notes
const totalSteps = 32;
const longestTail = Math.max(lowTom.tailSeconds, midTom.tailSeconds, hiTom.tailSeconds, cowbell.tailSeconds, woodblock.tailSeconds);
const totalFrames = Math.ceil((totalSteps * step + longestTail) * SR);
const out = new Float64Array(totalFrames);

// [voice, velocity] per 16th, '.' = rest.
const pattern = [
  [lowTom, 1.0], 0, [woodblock, 0.6], 0,
  [midTom, 0.9], [woodblock, 0.4], 0, [hiTom, 0.7],
  [cowbell, 0.8], 0, [woodblock, 0.6], [midTom, 0.5],
  0, [hiTom, 0.9], [woodblock, 0.4], [lowTom, 1.0],
  [midTom, 0.8], 0, [woodblock, 0.6], 0,
  [hiTom, 0.6], [cowbell, 0.9], 0, [woodblock, 0.4],
  [lowTom, 0.9], [midTom, 0.6], 0, [hiTom, 0.8],
  [woodblock, 0.5], [cowbell, 1.0], [lowTom, 0.7], [hiTom, 1.0],
];

for(let i = 0; i < totalSteps; i++) {
  const hit = pattern[i % pattern.length];
  if(!hit)
    continue;

  const [voice, velocity] = hit;
  const startFrame = Math.round(i * step * SR);

  // render(n, velocity) strikes the voice and renders its tail in one call
  // -- no per-sample tick() loop needed for one-shot use. Each voice's own
  // tailSeconds (set above, proportional to its decay) keeps long-release
  // toms from getting truncated by a fixed window.
  const tailFrames = Math.min(Math.round(SR * voice.tailSeconds), totalFrames - startFrame);
  if(tailFrames <= 0)
    continue;

  const tail = toFloat64(voice.render(tailFrames, velocity));
  for(let n = 0; n < tailFrames; n++)
    out[startFrame + n] += tail[n] * 0.8;
}

writeWav('twintdrum-test.wav', out, SR);
console.log(`Rendered ${(totalFrames / SR).toFixed(2)}s -> twintdrum-test.wav`);
