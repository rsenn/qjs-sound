// TB-303-style acid bassline using reusable ADSR + VCF building blocks.
// Runs unchanged in qjs and in browsers (load as <script type="module">).

import { ADSR, VCF, mtof } from './synth.js';

const isBrowser = typeof globalThis.window !== 'undefined';

async function main() {
  const env = isBrowser ? globalThis : await import('labsound');
  const { AudioContext, OscillatorNode, GainNode, BiquadFilterNode } = env;
  const setTimeout = isBrowser ? globalThis.setTimeout : (await import('os')).setTimeout;

  const ctx = new AudioContext();

  // VCO -> VCF -> VCA -> master -> destination
  const master = new GainNode(ctx, { gain: 0.6 });
  master.connect(ctx.destination);

  const vca = new GainNode(ctx, { gain: 0.0 });
  vca.connect(master);

  const vcf = new VCF(ctx, { BiquadFilterNode, type: 'lowpass', frequency: 200, Q: 14, poles: 4 });
  vcf.connect(vca);

  const vco = new OscillatorNode(ctx, { type: 'sawtooth', frequency: 55 });
  vco.connect(vcf.input);
  vco.start();

  const ampEnv    = new ADSR({ attack: 0.003, decay: 0.05, sustain: 0.6, release: 0.05 });
  const filterEnv = new ADSR({ attack: 0.001, decay: 0.15, sustain: 0.0, release: 0.05 });

  // 16-step pattern: [midi, accent, slide]
  const pattern = [
    [33, false, false], [45, true,  false], [33, false, true ], [40, false, false],
    [33, false, false], [52, true,  false], [33, false, true ], [40, false, false],
    [33, false, false], [33, true,  false], [45, false, true ], [33, false, false],
    [37, false, false], [49, true,  false], [33, false, true ], [40, false, false],
  ];

  const bpm = 130;
  const stepDur = 60 / bpm / 4;            // 16th notes
  const repeats = 2;
  const totalSteps = pattern.length * repeats;
  const t0 = ctx.currentTime + 0.1;

  for(let i = 0; i < totalSteps; i++) {
    const [midi, accent, slide] = pattern[i % pattern.length];
    const t = t0 + i * stepDur;
    const freq = mtof(midi);
    const gateLen = stepDur * (slide ? 0.95 : 0.5);

    // VCO pitch — slide = portamento from previous note
    vco.frequency.cancelScheduledValues(t);
    if(slide) vco.frequency.exponentialRampToValueAtTime(freq, t + stepDur * 0.5);
    else      vco.frequency.setValueAtTime(freq, t);

    // VCF envelope — the acid squelch
    const cutoffBase = 200;
    const cutoffPeak = cutoffBase + (accent ? 4000 : 1600);
    filterEnv.trigger(vcf.frequency, t, gateLen, { peak: cutoffPeak, base: cutoffBase });
    vcf.Q.setValueAtTime(accent ? 22 : 14, t);

    // VCA envelope — skip retrigger on slide so the note ties to previous
    if(!slide) {
      const peak = accent ? 0.9 : 0.55;
      ampEnv.trigger(vca.gain, t, gateLen, { peak, base: 0 });
    }
  }

  const totalMs = Math.ceil((totalSteps * stepDur + 0.6) * 1000);
  setTimeout(() => vco.stop(), totalMs);
}

main();
