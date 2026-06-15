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
