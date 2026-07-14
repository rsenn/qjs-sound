// Demo for StkTr909BassDrum: an analog "909-style" bass drum.
//
// Real analog kick circuits (TR-909 included) are not an ADSR patch: a
// sine (or triangle) core VCO gets a fast pitch-drop envelope for the
// "punch" and a single decay stage for amplitude -- no attack/sustain/
// release stages, no multi-segment shaping. StkTr909BassDrum models
// exactly that: setPitchEnvelope()/setAmpEnvelope() are two independent
// decay-only envelopes, and the classic thump comes from the pitch
// settling much faster than the amplitude. On top of the classic circuit
// this adds a few things no analog 909 had: a choice of distortion
// character (tanh / cubic / a foldback wavefolder), a sub-oscillator layer
// for modern low-end weight, and a batch render() for one-shot samples.
//
// This script renders a single reference hit plus a small round-robin of
// "unprecedented" variations (the wavefolder kick, a sub-heavy 808-style
// hit, a clicky tight kick, and a long dubby boom) to one WAV file.

import * as std from 'std';
import * as stk from 'stk';

const SR = 44100;

function writeWav(path, samples, sampleRate) {
  const nFrames = samples.length;
  const dataSize = nFrames * 2;
  const buf = new ArrayBuffer(44 + dataSize);
  const dv = new DataView(buf);
  const str = (off, s) => { for(let i = 0; i < s.length; i++) dv.setUint8(off + i, s.charCodeAt(i)); };

  str(0, 'RIFF');
  dv.setUint32(4, 36 + dataSize, true);
  str(8, 'WAVE');
  str(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);
  dv.setUint16(22, 1, true);
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

function toFloat64(frames) {
  return new Float64Array(frames.buffer);
}

function renderHit(configure, duration, velocity = 1.0) {
  const bd = new stk.StkTr909BassDrum();
  configure(bd);
  return toFloat64(bd.render(Math.round(duration * SR), velocity));
}

/* ---------- the reference "classic 909" hit ---------- */
const classic = renderHit((bd) => {
  bd.setPitchEnvelope(380, 58, 0.055, 'exp'); // fast pitch snap down
  bd.setAmpEnvelope(0.5, 'exp');              // long-ish body decay
  bd.setDrive(0.35, 'tanh');
  bd.setTone(5500);
  bd.setClick(0.55, 0.03);
}, 1.0);

/* ---------- variations: same engine, unprecedented territory ---------- */

// A wavefolder kick -- no analog 909 ever had this distortion topology.
const foldKick = renderHit((bd) => {
  bd.setPitchEnvelope(320, 90, 0.04, 'exp');
  bd.setAmpEnvelope(0.35, 'exp');
  bd.setDrive(0.5, 'fold');
  bd.setTone(4000);
  bd.setClick(0.4, 0.02);
}, 0.8);

// Sub-heavy, 808-leaning hit: a full octave-down sub layered under the core.
const subKick = renderHit((bd) => {
  bd.setPitchEnvelope(300, 45, 0.08, 'exp');
  bd.setAmpEnvelope(0.9, 'exp');
  bd.setDrive(0.2, 'tanh');
  bd.setTone(3500);
  bd.setSub(0.6, 2);
  bd.setClick(0.3, 0.02);
}, 1.4);

// Tight, clicky kick: linear pitch snap, short amp decay, hot click.
const tightKick = renderHit((bd) => {
  bd.setPitchEnvelope(500, 90, 0.02, 'linear');
  bd.setAmpEnvelope(0.15, 'linear');
  bd.setDrive(0.5, 'cubic');
  bd.setTone(7000);
  bd.setClick(0.8, 0.015);
}, 0.5);

// Long dubby boom: slow pitch settle, very long amp decay, low tone.
const boomKick = renderHit((bd) => {
  bd.setPitchEnvelope(150, 40, 0.15, 'exp');
  bd.setAmpEnvelope(2.2, 'exp');
  bd.setDrive(0.15, 'tanh');
  bd.setTone(2200);
  bd.setSub(0.35, 1);
  bd.setClick(0.2, 0.04);
}, 2.6);

/* ---------- concatenate with small gaps into one demo reel ---------- */

const gap = new Float64Array(Math.round(SR * 0.3));
const parts = [classic, gap, foldKick, gap, subKick, gap, tightKick, gap, boomKick];
const total = parts.reduce((n, p) => n + p.length, 0);
const out = new Float64Array(total);
let pos = 0;
for(const p of parts) {
  out.set(p, pos);
  pos += p.length;
}

// A few of the hotter voices (click + sub + drive stacked) can clip against
// the sine core's own peak -- normalize to a safe target peak before writing.
let peak = 0;
for(let i = 0; i < out.length; i++)
  peak = Math.max(peak, Math.abs(out[i]));
const TARGET_PEAK = 0.95;
const norm = peak > TARGET_PEAK ? TARGET_PEAK / peak : 1.0;
for(let i = 0; i < out.length; i++)
  out[i] *= norm;

writeWav('tr909bassdrum-test.wav', out, SR);
console.log(`Rendered ${(total / SR).toFixed(2)}s -> tr909bassdrum-test.wav`);
