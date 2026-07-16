// Demo for Tr909BassDrum: a bass drum designer, 909-core with mBase-11-
// style reach.
//
// Real analog kick circuits (TR-909 included) are not an ADSR patch: a
// sine (or triangle) core VCO gets a fast pitch-drop envelope for the
// "punch" and a single decay stage for amplitude -- no attack/sustain/
// release stages, no multi-segment shaping. Tr909BassDrum models
// exactly that: setPitchEnvelope()/setAmpEnvelope() are two independent
// decay-only envelopes, and the classic thump comes from the pitch
// settling much faster than the amplitude. On top of the classic circuit
// this adds the controls that turn a one-knob "kick" into a real designer
// (Jomox mBase 11 territory): setPitchSpike() layers a second, much faster
// pitch overshoot on top of the main drop (the near-instant transient
// analog VCOs get when slammed hard); setPunch() adds a dedicated
// transient gain boost independent of the amp envelope; setToneResonance()
// adds a resonant peak that *tracks the kick's own settling pitch* rather
// than sitting at a fixed Hz value -- an untracked resonance reads as an
// unrelated second pitched element as soon as the pitch envelope sweeps
// past it (the same mechanism that makes a struck resonant filter sound
// like a bell elsewhere in this file), so tracking it is what keeps a
// resonant kick sounding like a kick instead of a kick-plus-cowbell;
// setWaveform() switches the core between sine and triangle; plus a choice
// of distortion character (tanh / cubic / a foldback wavefolder), a
// sub-oscillator layer for modern low-end weight, and a batch render() for
// one-shot samples.
//
// This script renders a bank of distinct bass drum patches -- 909/808
// references, techno/house/gabber/dub/trap/lo-fi/boom-bap flavors, and a
// few patches that each lean on one mBase-11-style control in isolation
// (pitch spike, punch, tone resonance) -- to one WAV file.

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
  const bd = new stk.Tr909BassDrum();
  configure(bd);
  return toFloat64(bd.render(Math.round(duration * SR), velocity));
}

function main() {
  /* ---------- a bank of 18 distinct bass drum patches ---------- */
  // Kept deliberately realistic: pitch spikes stay in the few-semitones-to-
  // an-octave range (a multi-octave spike just sounds like a synth zap, not
  // a kick), and tone resonance stays moderate -- now that it tracks the
  // settling pitch instead of ringing at a fixed Hz, it reads as analog
  // "growl" character rather than a bell even pushed fairly high (see
  // industrialGrowl/formantGrowl below), but restraint still reads more like
  // a drum and less like a special effect.

  const patches = [
    ["classic909", 1.0, (bd) => {
      // The reference hit: fast pitch snap, long-ish body, hot click.
      bd.setPitchEnvelope(380, 58, 0.055, 'exp');
      bd.setAmpEnvelope(0.5, 'exp');
      bd.setDrive(0.35, 'tanh');
      bd.setTone(5500);
      bd.setClick(0.55, 0.03);
    }],
    ["deepSub808", 2.2, (bd) => {
      // Slow 808-style pitch glide over a long, deep sub body.
      bd.setPitchEnvelope(220, 40, 0.35, 'exp');
      bd.setAmpEnvelope(1.8, 'exp');
      bd.setDrive(0.1, 'tanh');
      bd.setTone(1200);
      bd.setSub(0.5, 1);
      bd.setClick(0.15, 0.02);
    }],
    ["gabberHardcore", 0.8, (bd) => {
      // Hard-clipped, distorted hardcore kick with a small transient snap.
      bd.setPitchEnvelope(250, 65, 0.03, 'exp');
      bd.setPitchSpike(10, 0.006);
      bd.setAmpEnvelope(0.4, 'exp');
      bd.setPunch(1.0, 0.005);
      bd.setDrive(0.9, 'cubic');
      bd.setTone(3000);
      bd.setToneResonance(0.3);
      bd.setClick(0.6, 0.01);
    }],
    ["technoPunch", 0.7, (bd) => {
      // Tight, punchy 4/4 techno kick.
      bd.setPitchEnvelope(300, 55, 0.045, 'exp');
      bd.setAmpEnvelope(0.35, 'exp');
      bd.setPunch(0.6, 0.008);
      bd.setDrive(0.4, 'tanh');
      bd.setTone(4000);
      bd.setClick(0.4, 0.02);
    }],
    ["dubTechnoWarm", 2.0, (bd) => {
      // Warm, round, long-tailed dub techno kick.
      bd.setPitchEnvelope(140, 45, 0.12, 'exp');
      bd.setAmpEnvelope(1.6, 'exp');
      bd.setDrive(0.12, 'tanh');
      bd.setTone(1500);
      bd.setToneResonance(0.25);
      bd.setSub(0.3, 1);
      bd.setClick(0.15, 0.03);
    }],
    ["industrialGrowl", 1.4, (bd) => {
      // Foldback-distorted, resonance-heavy industrial kick -- the growl
      // stays tied to the pitch instead of turning into a bell.
      bd.setPitchEnvelope(180, 60, 0.06, 'exp');
      bd.setAmpEnvelope(0.9, 'exp');
      bd.setDrive(0.7, 'fold');
      bd.setTone(2500);
      bd.setToneResonance(0.65);
      bd.setClick(0.4, 0.02);
    }],
    ["lofiDull", 0.9, (bd) => {
      // Soft, dull, low-cutoff lo-fi kick with a linear amp/pitch curve.
      bd.setPitchEnvelope(200, 60, 0.08, 'linear');
      bd.setAmpEnvelope(0.5, 'linear');
      bd.setDrive(0.1, 'tanh');
      bd.setTone(800);
      bd.setClick(0.1, 0.02);
    }],
    ["trapSubGlide", 2.8, (bd) => {
      // Very slow trap/808 pitch glide, minimal click, huge tail.
      bd.setPitchEnvelope(180, 35, 0.5, 'exp');
      bd.setAmpEnvelope(2.5, 'exp');
      bd.setDrive(0.05, 'tanh');
      bd.setTone(900);
      bd.setClick(0.05, 0.015);
    }],
    ["housePunch", 0.8, (bd) => {
      // Bright, punchy house kick, a hair detuned via setTune().
      bd.setPitchEnvelope(320, 60, 0.05, 'exp');
      bd.setAmpEnvelope(0.45, 'exp');
      bd.setPunch(0.5, 0.006);
      bd.setDrive(0.3, 'tanh');
      bd.setTone(5000);
      bd.setTune(1.03);
      bd.setClick(0.6, 0.025);
    }],
    ["electroSnap", 0.5, (bd) => {
      // Tight, snappy electro kick with a modest pitch spike.
      bd.setPitchEnvelope(400, 90, 0.025, 'linear');
      bd.setPitchSpike(8, 0.004);
      bd.setAmpEnvelope(0.2, 'linear');
      bd.setDrive(0.45, 'cubic');
      bd.setTone(6000);
      bd.setClick(0.7, 0.012);
    }],
    ["triangleMellow", 1.4, (bd) => {
      // Triangle core instead of sine -- softer, rounder harmonic content.
      bd.setWaveform('triangle');
      bd.setPitchEnvelope(160, 50, 0.1, 'exp');
      bd.setAmpEnvelope(1.0, 'exp');
      bd.setDrive(0.08, 'tanh');
      bd.setTone(1800);
      bd.setClick(0.15, 0.02);
    }],
    ["minimalClick", 0.4, (bd) => {
      // Very short, minimal-techno click kick.
      bd.setPitchEnvelope(280, 80, 0.015, 'exp');
      bd.setAmpEnvelope(0.12, 'exp');
      bd.setDrive(0.2, 'tanh');
      bd.setTone(5000);
      bd.setClick(0.5, 0.008);
    }],
    ["boomBapHipHop", 1.2, (bd) => {
      // Mid-length, sub-supported boom-bap kick.
      bd.setPitchEnvelope(210, 50, 0.09, 'exp');
      bd.setAmpEnvelope(0.7, 'exp');
      bd.setDrive(0.25, 'tanh');
      bd.setTone(2800);
      bd.setSub(0.25, 1);
      bd.setClick(0.3, 0.025);
    }],
    ["formantGrowl", 1.8, (bd) => {
      // setToneResonance() pushed further (0.7) in isolation, triangle core
      // -- an analog-VCF growl that still tracks the settling pitch.
      bd.setWaveform('triangle');
      bd.setPitchEnvelope(150, 45, 0.15, 'exp');
      bd.setAmpEnvelope(1.3, 'exp');
      bd.setDrive(0.3, 'tanh');
      bd.setTone(2200);
      bd.setToneResonance(0.7);
      bd.setClick(0.2, 0.02);
    }],
    ["punchySnap", 0.9, (bd) => {
      // setPunch() carrying the transient more or less on its own.
      bd.setPitchEnvelope(260, 55, 0.04, 'exp');
      bd.setAmpEnvelope(0.5, 'exp');
      bd.setPunch(1.8, 0.007);
      bd.setDrive(0.3, 'tanh');
      bd.setTone(4200);
      bd.setClick(0.35, 0.018);
    }],
    ["spikeTransient", 2.0, (bd) => {
      // setPitchSpike() in relative isolation, over a long dubby body.
      bd.setPitchEnvelope(130, 42, 0.18, 'exp');
      bd.setPitchSpike(12, 0.006);
      bd.setAmpEnvelope(1.5, 'exp');
      bd.setDrive(0.2, 'tanh');
      bd.setTone(2000);
      bd.setClick(0.3, 0.02);
    }],
    ["subFoundation", 2.8, (bd) => {
      // Clean, near-pure sine sub -- almost no click, no drive, just body.
      bd.setPitchEnvelope(90, 35, 0.25, 'exp');
      bd.setAmpEnvelope(2.4, 'exp');
      bd.setDrive(0.05, 'tanh');
      bd.setTone(700);
      bd.setClick(0.05, 0.02);
    }],
    ["foldbackDistorted", 0.9, (bd) => {
      // The wavefolder distortion character on its own, moderate settings.
      bd.setPitchEnvelope(240, 60, 0.05, 'exp');
      bd.setAmpEnvelope(0.5, 'exp');
      bd.setDrive(0.55, 'fold');
      bd.setTone(3200);
      bd.setClick(0.4, 0.02);
    }],
  ];

  /* ---------- concatenate with small gaps into one demo reel ---------- */

  const gap = new Float64Array(Math.round(SR * 0.3));
  const parts = [];
  for(const [name, duration, configure] of patches) {
    if(parts.length > 0)
      parts.push(gap);
    parts.push(renderHit(configure, duration));
  }
  console.log(`${patches.length} patches: ${patches.map(([name]) => name).join(', ')}`);

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
}

main();
