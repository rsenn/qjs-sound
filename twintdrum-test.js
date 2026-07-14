// Demo for StkTwinTDrum: an analog Twin-T oscillator style drum resonator.
//
// A Twin-T RC notch network wired into an inverting feedback loop
// oscillates at its notch frequency; struck, it rings down like a tom,
// conga or woodblock -- that's exactly what StkTwinTDrum models (a
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

const lowTom = new stk.StkTwinTDrum(110);
lowTom.setDecay(0.35);
lowTom.setDrive(0.15);
lowTom.setPitchDrop(5, 0.05);
lowTom.setClick(0.25);

const midTom = new stk.StkTwinTDrum(175);
midTom.setDecay(0.28);
midTom.setDrive(0.2);
midTom.setPitchDrop(6, 0.04);
midTom.setClick(0.3);

const hiTom = new stk.StkTwinTDrum(260);
hiTom.setDecay(0.22);
hiTom.setDrive(0.25);
hiTom.setPitchDrop(7, 0.03);
hiTom.setClick(0.35);

// Cowbell-ish voice: two closely-tuned resonators (the "secondary" control)
// beating against each other, more click, faster decay, hotter drive.
const cowbell = new stk.StkTwinTDrum(560);
cowbell.setDecay(0.18);
cowbell.setDrive(0.6);
cowbell.setSecondary(1.48, 0.8);
cowbell.setClick(0.5);

// Woodblock-ish voice: very short decay, high secondary ratio, no drive.
const woodblock = new stk.StkTwinTDrum(900);
woodblock.setDecay(0.05);
woodblock.setSecondary(2.0, 0.5);
woodblock.setClick(0.6);

/* ---------- a solo groove exercising the whole kit ---------- */

const bpm = 100;
const step = 60 / bpm / 4; // 16th notes
const totalSteps = 32;
const totalFrames = Math.ceil((totalSteps * step + 1.0) * SR);
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
  // -- no per-sample tick() loop needed for one-shot use.
  const tailFrames = Math.min(Math.round(SR * 0.6), totalFrames - startFrame);
  if(tailFrames <= 0)
    continue;

  const tail = toFloat64(voice.render(tailFrames, velocity));
  for(let n = 0; n < tailFrames; n++)
    out[startFrame + n] += tail[n] * 0.8;
}

writeWav('twintdrum-test.wav', out, SR);
console.log(`Rendered ${(totalFrames / SR).toFixed(2)}s -> twintdrum-test.wav`);
