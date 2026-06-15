// Demo for the three new WebAudio nodes — DelayNode, WaveShaperNode,
// StereoPannerNode — wrapped in reusable effect blocks from effects.js.
//
// Signal chain:
//   Oscillator --> WaveShaper (distortion) --+--> StereoPanner (auto-pan) -> master
//                                            |
//                                            +--> PingPongDelay ---------> master
//
// We modulate the pan with setValueAtTime ramps so the dry signal swings L/R
// while the delay taps ping-pong around it. The WaveShaper makes the saw
// edges grindy. Runs unchanged in qjs and browser.

import { makeDistortionCurve, createPingPongDelay } from './effects.js';

const isBrowser = typeof globalThis.window !== 'undefined';

async function main() {
  const env = isBrowser ? globalThis : await import('labsound');
  const setTimeout = isBrowser ? globalThis.setTimeout : (await import('os')).setTimeout;

  const ctx = new env.AudioContext();
  const master = new env.GainNode(ctx, { gain: 0.5 });
  master.connect(ctx.destination);

  // Distortion: tanh saturation, 2x oversampled to soften aliasing.
  const dist = new env.WaveShaperNode(ctx, {
    curve: makeDistortionCurve(7),
    oversample: '2x',
  });

  // Auto-pan for the dry signal.
  const pan = new env.StereoPannerNode(ctx, { pan: 0 });
  pan.connect(master);

  // Wet send: ping-pong delay, mixed back at -6 dB.
  const ppd = createPingPongDelay(ctx, env, { time: 0.27, feedback: 0.4 });
  const wetGain = new env.GainNode(ctx, { gain: 0.5 });
  ppd.output.connect(wetGain);
  wetGain.connect(master);

  // Source -> distortion fan-out
  const vca = new env.GainNode(ctx, { gain: 0.0 });
  vca.connect(dist);
  dist.connect(pan);          // dry path
  dist.connect(ppd.input);    // wet path

  const vco = new env.OscillatorNode(ctx, { type: 'sawtooth', frequency: 110 });
  vco.connect(vca);
  vco.start();

  globalThis.__fx_keepalive = { ctx, vco, vca, dist, pan, ppd, master };

  // Note pattern — riff using a pentatonic minor scale, plus pan + envelope
  // sweep so all three effects are obvious.
  const mtof = (m) => 440 * Math.pow(2, (m - 69) / 12);
  const notes = [45, 52, 48, 55, 50, 57, 53, 60];   // A2 minor pentatonic-ish
  const bpm = 110;
  const stepDur = 60 / bpm / 2;       // 8th notes
  const totalSteps = 32;
  const t0 = ctx.currentTime + 0.1;

  for(let i = 0; i < totalSteps; i++) {
    const t = t0 + i * stepDur;
    const freq = mtof(notes[i % notes.length]);
    vco.frequency.setValueAtTime(freq, t);

    // VCA gate envelope
    vca.gain.cancelScheduledValues(t);
    vca.gain.setValueAtTime(0, t);
    vca.gain.linearRampToValueAtTime(0.5, t + 0.005);
    vca.gain.setTargetAtTime(0, t + stepDur * 0.6, 0.02);

    // Auto-pan — swing across stereo over 4-step cycle
    const phase = ((i % 4) / 4) * 2 * Math.PI;
    pan.pan.setValueAtTime(Math.sin(phase), t);
  }

  const totalMs = Math.ceil((totalSteps * stepDur + 1.5) * 1000);
  setTimeout(() => vco.stop(), totalMs);
}

main();
