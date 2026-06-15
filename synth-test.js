// TB-303-style acid bassline. Wraps the patch in a reusable Synth class.
// Runs unchanged in qjs and browsers (load as <script type="module">).

import { ADSR, VCF, mtof } from './synth.js';

const isBrowser = typeof globalThis.window !== 'undefined';

// One monosynth voice: VCO -> VCF -> VCA -> master -> destination.
// The VCO runs continuously; noteOn() reschedules pitch + envelopes.
class Synth {
  constructor(ctx, env, opts = {}) {
    this.ctx = ctx;

    this.master = new env.GainNode(ctx, { gain: opts.gain ?? 0.85 });
    this.master.connect(ctx.destination);

    this.vca = new env.GainNode(ctx, { gain: 0.0 });
    this.vca.connect(this.master);

    this.vcf = new VCF(ctx, {
      BiquadFilterNode: env.BiquadFilterNode,
      type: 'lowpass',
      frequency: opts.cutoff ?? 800,
      Q: opts.Q ?? 5,
      poles: opts.poles ?? 4,
    });
    this.vcf.connect(this.vca);

    this.vco = new env.OscillatorNode(ctx, {
      type: opts.type ?? 'sawtooth',
      frequency: 55,
    });
    this.vco.connect(this.vcf.input);
    this.vco.start();

    this.ampEnv    = new ADSR({ attack: 0.003, decay: 0.10, sustain: 0.4, release: 0.06 });
    this.filterEnv = new ADSR({ attack: 0.002, decay: 0.20, sustain: 0.0, release: 0.06 });
  }

  noteOn({ t, midi, gateLen, accent = false, slide = false }) {
    const freq = mtof(midi);

    // VCO pitch — slide = portamento from previous note
    this.vco.frequency.cancelScheduledValues(t);
    if(slide) this.vco.frequency.exponentialRampToValueAtTime(freq, t + 0.05);
    else      this.vco.frequency.setValueAtTime(freq, t);

    // VCF cutoff envelope (the acid squelch)
    const base = 300;
    const peak = base + (accent ? 3500 : 2000);
    this.filterEnv.trigger(this.vcf.frequency, t, gateLen, { peak, base });

    // Resonance bump on accents (single AudioParam, not broadcast)
    this.vcf.Q.setValueAtTime(accent ? 10 : 5, t);

    // VCA envelope — skip on slide so the note ties to the previous one
    if(!slide) {
      const peakGain = accent ? 0.85 : 0.55;
      this.ampEnv.trigger(this.vca.gain, t, gateLen, { peak: peakGain, base: 0 });
    }
  }

  stop(t = 0) { this.vco.stop(t); }
}

async function main() {
  const env = isBrowser ? globalThis : await import('labsound');
  const setTimeout = isBrowser ? globalThis.setTimeout : (await import('os')).setTimeout;

  const ctx = new env.AudioContext();
  const synth = new Synth(ctx, env);

  // Belt-and-braces keepalive. The binding already anchors nodes against
  // their context, but pinning here as well makes the lifetime obvious:
  // as long as the global exists, the whole audio graph stays live.
  globalThis.__synth_keepalive = { ctx, synth };

  // 16-step pattern: [midiNote, accent, slide]
  const pattern = [
    [33, false, false], [45, true,  false], [33, false, true ], [40, false, false],
    [33, false, false], [52, true,  false], [33, false, true ], [40, false, false],
    [33, false, false], [33, true,  false], [45, false, true ], [33, false, false],
    [37, false, false], [49, true,  false], [33, false, true ], [40, false, false],
  ];

  const bpm = 130;
  const stepDur = 60 / bpm / 4;          // 16th notes
  const repeats = 2;
  const totalSteps = pattern.length * repeats;
  const t0 = ctx.currentTime + 0.1;

  for(let i = 0; i < totalSteps; i++) {
    const [midi, accent, slide] = pattern[i % pattern.length];
    synth.noteOn({
      t: t0 + i * stepDur,
      midi,
      gateLen: stepDur * (slide ? 0.95 : 0.55),
      accent,
      slide,
    });
  }

  const totalMs = Math.ceil((totalSteps * stepDur + 0.6) * 1000);
  setTimeout(() => synth.stop(), totalMs);
}

main();
