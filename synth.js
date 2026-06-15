// Reusable synth building blocks. Pure JS, no runtime imports — runs
// unchanged in qjs (with our labsound module) and in browsers.

// Wraps several AudioParams so automation broadcasts to all of them.
// Used by VCF when cascading multiple BiquadFilter stages.
export class BroadcastParam {
  constructor(params) { this.params = params; }
  get value() { return this.params[0].value; }
  set value(v) { for(const p of this.params) p.value = v; }
  setValueAtTime(v, t)              { for(const p of this.params) p.setValueAtTime(v, t);              return this; }
  linearRampToValueAtTime(v, t)     { for(const p of this.params) p.linearRampToValueAtTime(v, t);     return this; }
  exponentialRampToValueAtTime(v, t){ for(const p of this.params) p.exponentialRampToValueAtTime(v, t);return this; }
  setTargetAtTime(v, t, k)          { for(const p of this.params) p.setTargetAtTime(v, t, k);          return this; }
  cancelScheduledValues(t)          { for(const p of this.params) p.cancelScheduledValues(t);          return this; }
}

// ADSR envelope as AudioParam automation. Works on any AudioParam.
//   attack/decay/release in seconds; sustain is a level 0..1.
//
// trigger(param, t, gateLen, { peak, base })
//   Schedules the full envelope on `param`, ramping between `base` (rest)
//   and `peak` (attack target). Note runs from `t` to `t + gateLen`.
export class ADSR {
  constructor({ attack = 0.005, decay = 0.1, sustain = 0.3, release = 0.1 } = {}) {
    this.attack = attack;
    this.decay = decay;
    this.sustain = sustain;
    this.release = release;
  }

  trigger(param, t, gateLen, { peak = 1.0, base = 0.0 } = {}) {
    const aEnd = t + this.attack;
    const dEnd = aEnd + this.decay;
    const rStart = Math.max(t + gateLen, dEnd);
    const sustainLevel = base + (peak - base) * this.sustain;

    param.cancelScheduledValues(t);
    param.setValueAtTime(base, t);
    param.linearRampToValueAtTime(peak, aEnd);
    param.linearRampToValueAtTime(sustainLevel, dEnd);
    if(rStart > dEnd) param.setValueAtTime(sustainLevel, rStart);
    // Release: exponential approach toward base. timeConstant ≈ release/4.
    param.setTargetAtTime(base, rStart, this.release / 4);
  }
}

// Resonant low-pass voltage-controlled filter built from N cascaded
// BiquadFilters (each is 2-pole, so poles=4 gives a 24 dB/oct slope —
// closer to the TB-303 ladder character than a single biquad).
//
// Pass the runtime's BiquadFilterNode in via opts so the class stays
// environment-agnostic.
export class VCF {
  constructor(ctx, { BiquadFilterNode, type = 'lowpass', frequency = 800, Q = 8, poles = 4 } = {}) {
    if(!BiquadFilterNode)
      throw new TypeError('VCF needs { BiquadFilterNode } in its options');

    const nStages = Math.max(1, Math.ceil(poles / 2));
    this.stages = Array.from({ length: nStages }, (_, i) =>
      new BiquadFilterNode(ctx, {
        type,
        frequency,
        // Put resonance on the last stage; keep earlier stages flat to avoid
        // resonance compounding across the cascade.
        Q: i === nStages - 1 ? Q : 0.5,
      }));

    for(let i = 0; i < nStages - 1; i++)
      this.stages[i].connect(this.stages[i + 1]);

    this.input = this.stages[0];
    this.output = this.stages[nStages - 1];
    // Cutoff is broadcast across all stages so the envelope sweeps the whole
    // cascade. Resonance lives on the last stage only — broadcasting Q at
    // values like 14+ across 2 cascaded biquads compounds into an extremely
    // narrow filter that nukes the signal.
    this.frequency = new BroadcastParam(this.stages.map(s => s.frequency));
    this.Q = this.stages[nStages - 1].Q;
  }

  connect(dest) { return this.output.connect(dest); }
  disconnect(dest) { return dest === undefined ? this.output.disconnect() : this.output.disconnect(dest); }
}

export const mtof = (m) => 440 * Math.pow(2, (m - 69) / 12);

// One TB-303-style monosynth voice: VCO -> VCF -> VCA -> master -> output.
// The VCO runs continuously; noteOn() reschedules pitch + envelopes per step.
// opts.output (defaults to ctx.destination) lets the caller route the synth
// through an effects chain.
export class Synth {
  constructor(ctx, env, opts = {}) {
    this.ctx = ctx;
    this.env = env;

    this.master = new env.GainNode(ctx, { gain: opts.gain ?? 0.85 });
    this.master.connect(opts.output ?? ctx.destination);

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

    this.ampEnv    = new ADSR(opts.ampEnv    ?? { attack: 0.003, decay: 0.10, sustain: 0.4, release: 0.06 });
    this.filterEnv = new ADSR(opts.filterEnv ?? { attack: 0.002, decay: 0.20, sustain: 0.0, release: 0.06 });
  }

  noteOn({ t, midi, gateLen, accent = false, slide = false }) {
    const freq = mtof(midi);

    // VCO pitch — slide = portamento from previous note
    this.vco.frequency.cancelScheduledValues(t);
    if(slide) this.vco.frequency.exponentialRampToValueAtTime(freq, t + 0.05);
    else      this.vco.frequency.setValueAtTime(freq, t);

    // VCF cutoff envelope
    const base = 300;
    const peak = base + (accent ? 3500 : 2000);
    this.filterEnv.trigger(this.vcf.frequency, t, gateLen, { peak, base });

    // Resonance bump on accents
    this.vcf.Q.setValueAtTime(accent ? 10 : 5, t);

    // VCA envelope — skip on slide so the note ties to the previous one
    if(!slide) {
      const peakGain = accent ? 0.85 : 0.55;
      this.ampEnv.trigger(this.vca.gain, t, gateLen, { peak: peakGain, base: 0 });
    }
  }

  stop(t = 0) { this.vco.stop(t); }
}
