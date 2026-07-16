// Demo for Tr909Percussion: a noise/metallic-core percussion designer
// covering snares, claps, hihats and cymbals.
//
// Real analog drum machines build these four voices out of the same few
// circuit blocks in different proportions, not four separate synthesis
// engines: a noise source (the "snap"/"hiss"/"sizzle" common to all four),
// a filter to carve its character, an envelope (single-shot for snare/
// hihat/cymbal, multi-triggered for a clap's characteristic "burst-burst-
// burst-tail"), and -- for snare and hihat/cymbal specifically -- a tonal
// or metallic core: setTone() gives two detuned oscillators for a snare's
// "shell" thump (the classic 808/909 snare mixes ~180Hz/330Hz tones under
// its noise, not noise alone); setMetallic() gives a bank of six square
// oscillators at fixed inharmonic ratios (the classic 808/909 hi-hat
// "six-oscillator" trick) for hihat/cymbal shimmer. setNoiseFilter()
// shapes the noise layer (bandpass/highpass/lowpass), setCrunch() adds
// waveshaping distortion for the snap, and setClap() retriggers the noise
// envelope for the multi-burst clap circuit.
//
// This script renders 18 patches -- five snares, four claps, four hihats,
// three cymbals, plus two that lean hard on setCrunch()/the metallic+noise
// blend -- to one WAV file.

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
  const p = new stk.Tr909Percussion();
  configure(p);
  return toFloat64(p.render(Math.round(duration * SR), velocity));
}

function main() {
  /* ---------- 18 patches ---------- */

  const patches = [
    /* ---- snares: tone "shell" + bandpassed, crunched noise ---- */
    ["snare909", 0.35, (p) => {
      p.setTone(180, 330, 0.35, 0.12);
      p.setNoise(0.75, 0.18);
      p.setNoiseFilter(2200, 1.4, 'bandpass');
      p.setCrunch(0.4, 'tanh');
    }],
    ["snare808", 0.4, (p) => {
      p.setTone(160, 250, 0.4, 0.15);
      p.setNoise(0.6, 0.22);
      p.setNoiseFilter(1600, 1.1, 'bandpass');
      p.setCrunch(0.25, 'tanh');
    }],
    ["snareGabber", 0.3, (p) => {
      // Hyper-distorted, hardcore-style snare: heavy crunch dominates.
      p.setTone(200, 340, 0.5, 0.08);
      p.setNoise(0.9, 0.15);
      p.setNoiseFilter(2500, 1.8, 'bandpass');
      p.setCrunch(0.85, 'cubic');
    }],
    ["snareRimshot", 0.15, (p) => {
      // Very short, tone-forward, barely any noise -- a rimshot/side-stick.
      p.setTone(350, 520, 0.7, 0.04);
      p.setNoise(0.2, 0.05);
      p.setNoiseFilter(3000, 2.0, 'bandpass');
      p.setCrunch(0.3, 'tanh');
    }],
    ["snareFat", 0.5, (p) => {
      // Longer body, warmer noise -- a fatter, less clicky snare.
      p.setTone(150, 260, 0.3, 0.2);
      p.setNoise(0.7, 0.3);
      p.setNoiseFilter(1400, 1.0, 'bandpass');
      p.setCrunch(0.3, 'tanh');
    }],

    /* ---- claps: pure noise, multi-burst envelope ---- */
    ["clap909", 0.45, (p) => {
      p.setNoise(1.0, 0.22);
      p.setNoiseFilter(1500, 1.2, 'bandpass');
      p.setClap(4, 0.012);
      p.setCrunch(0.35, 'tanh');
    }],
    ["clapTight", 0.25, (p) => {
      p.setNoise(1.0, 0.1);
      p.setNoiseFilter(1800, 1.4, 'bandpass');
      p.setClap(3, 0.008);
      p.setCrunch(0.3, 'tanh');
    }],
    ["clapReverby", 0.9, (p) => {
      // More hits, longer tail -- a roomier, "wetter" clap.
      p.setNoise(0.9, 0.55);
      p.setNoiseFilter(1300, 1.0, 'bandpass');
      p.setClap(5, 0.014);
      p.setCrunch(0.2, 'tanh');
    }],
    ["clapLofi", 0.4, (p) => {
      // Dull, heavily distorted -- a crushed, lo-fi clap.
      p.setNoise(1.0, 0.2);
      p.setNoiseFilter(900, 0.8, 'lowpass');
      p.setClap(4, 0.011);
      p.setCrunch(0.7, 'fold');
    }],

    /* ---- hihats: six-oscillator metallic core, highpassed ---- */
    ["hihatClosed909", 0.12, (p) => {
      p.setMetallic(540, 1.0, 0.05, 8500);
      p.setNoise(0.15, 0.05);
      p.setCrunch(0.15, 'tanh');
    }],
    ["hihatClosed808", 0.18, (p) => {
      p.setMetallic(430, 1.0, 0.08, 6500);
      p.setNoise(0.2, 0.08);
      p.setCrunch(0.1, 'tanh');
    }],
    ["hihatOpen", 0.9, (p) => {
      p.setMetallic(540, 1.0, 0.6, 7500);
      p.setNoise(0.25, 0.5);
      p.setCrunch(0.15, 'tanh');
    }],
    ["hihatPedal", 0.2, (p) => {
      // Softer, less bright -- a foot-closed hihat "chick".
      p.setMetallic(480, 0.8, 0.09, 5000);
      p.setNoise(0.1, 0.07);
      p.setCrunch(0.05, 'tanh');
    }],

    /* ---- cymbals: metallic core + noise, long decay ---- */
    ["crashCymbal", 2.2, (p) => {
      p.setMetallic(460, 0.85, 1.8, 6000);
      p.setNoise(0.35, 1.6);
      p.setCrunch(0.2, 'tanh');
    }],
    ["rideCymbal", 1.6, (p) => {
      // Lower brightness cutoff keeps more body/"ping" than a crash.
      p.setMetallic(380, 0.7, 1.3, 4000);
      p.setNoise(0.2, 1.1);
      p.setCrunch(0.15, 'tanh');
    }],
    ["splashCymbal", 0.5, (p) => {
      p.setMetallic(620, 0.9, 0.35, 8000);
      p.setNoise(0.3, 0.3);
      p.setCrunch(0.2, 'tanh');
    }],

    /* ---- two that lean hard on one specific control ---- */
    ["snareCrunchFx", 0.35, (p) => {
      // setCrunch() pushed to the extreme -- almost a distorted noise stab.
      p.setTone(190, 300, 0.25, 0.1);
      p.setNoise(0.85, 0.16);
      p.setNoiseFilter(2000, 1.3, 'bandpass');
      p.setCrunch(0.95, 'fold');
    }],
    ["industrialMetalHit", 0.7, (p) => {
      // Metallic core + noise blended clap-style -- not a real analog
      // voice, but the same building blocks recombined into something new.
      p.setMetallic(300, 0.6, 0.3, 3500);
      p.setNoise(0.7, 0.35);
      p.setNoiseFilter(1200, 1.5, 'bandpass');
      p.setClap(3, 0.015);
      p.setCrunch(0.6, 'cubic');
    }],
  ];

  /* ---------- concatenate with small gaps into one demo reel ---------- */

  const gap = new Float64Array(Math.round(SR * 0.25));
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

  // Normalize to a safe target peak before writing.
  let peak = 0;
  for(let i = 0; i < out.length; i++)
    peak = Math.max(peak, Math.abs(out[i]));
  const TARGET_PEAK = 0.95;
  const norm = peak > TARGET_PEAK ? TARGET_PEAK / peak : 1.0;
  for(let i = 0; i < out.length; i++)
    out[i] *= norm;

  writeWav('tr909percussion-test.wav', out, SR);
  console.log(`Rendered ${(total / SR).toFixed(2)}s -> tr909percussion-test.wav`);
}

main();
