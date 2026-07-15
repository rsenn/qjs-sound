// Demo for StkTr909BassDrum: a bass drum designer, 909-core with mBase-11-
// style reach.
//
// Real analog kick circuits (TR-909 included) are not an ADSR patch: a
// sine (or triangle) core VCO gets a fast pitch-drop envelope for the
// "punch" and a single decay stage for amplitude -- no attack/sustain/
// release stages, no multi-segment shaping. StkTr909BassDrum models
// exactly that: setPitchEnvelope()/setAmpEnvelope() are two independent
// decay-only envelopes, and the classic thump comes from the pitch
// settling much faster than the amplitude. On top of the classic circuit
// this adds the controls that turn a one-knob "kick" into a real designer
// (Jomox mBase 11 territory): setPitchSpike() layers a second, much faster
// pitch overshoot on top of the main drop (the near-instant transient
// analog VCOs get when slammed hard); setPunch() adds a dedicated
// transient gain boost independent of the amp envelope; setToneResonance()
// pushes the tone stage from a plain rolloff into a resonant, near-self-
// ringing peak; setWaveform() switches the core between sine and triangle;
// plus a choice of distortion character (tanh / cubic / a foldback
// wavefolder), a sub-oscillator layer for modern low-end weight, and a
// batch render() for one-shot samples.
//
// This script renders a single reference hit, a small round-robin of
// "unprecedented" variations (the wavefolder kick, a sub-heavy 808-style
// hit, a clicky tight kick, and a long dubby boom), and a couple of
// designer patches that lean on the new mBase-11-style controls, to one
// WAV file.

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

/* ---------- designer patches: leaning on the mBase-11-style controls ---------- */

// mBase-11-style "long kick": a fast pitch spike stacked on top of the main
// drop for that instant transient snap, a dedicated punch boost, and a
// resonant tone stage pushed almost to self-ringing -- while still
// resolving into a long, tuned sub-bass tail.
const designerLong = renderHit((bd) => {
  bd.setPitchEnvelope(120, 42, 0.2, 'exp');
  bd.setPitchSpike(30, 0.004);        // +2.5 octaves of instant snap, gone in ~4ms
  bd.setAmpEnvelope(2.0, 'exp');
  bd.setPunch(1.2, 0.006);            // sharp transient on top of the amp envelope
  bd.setDrive(0.25, 'tanh');
  bd.setWaveform('triangle');
  bd.setTone(1800);
  bd.setToneResonance(0.7);           // near-ringing VCF character
  bd.setSub(0.4, 1);
  bd.setClick(0.35, 0.02);
}, 2.4);

// mBase-11-style "click monster": triangle core, huge pitch spike, hard
// punch, hot resonance right at the click transient -- short body, all
// attack, the kind of aggressive click a one-knob sine kick can't reach.
const designerClick = renderHit((bd) => {
  bd.setPitchEnvelope(300, 70, 0.03, 'exp');
  bd.setPitchSpike(48, 0.0025);       // 4 octaves, ~2.5ms
  bd.setAmpEnvelope(0.25, 'exp');
  bd.setPunch(2.0, 0.003);
  bd.setDrive(0.4, 'cubic');
  bd.setWaveform('triangle');
  bd.setTone(4500);
  bd.setToneResonance(0.85);
  bd.setClick(0.5, 0.012);
}, 0.6);

/* ---------- concatenate with small gaps into one demo reel ---------- */

const gap = new Float64Array(Math.round(SR * 0.3));
const parts = [classic, gap, foldKick, gap, subKick, gap, tightKick, gap, boomKick, gap, designerLong, gap, designerClick];
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
