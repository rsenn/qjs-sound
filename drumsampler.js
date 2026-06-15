// Drum sampler with sample-based AND procedural voices.
//
// Sample-based: kick / snare / hihat etc. — load a WAV via AudioContext and
// register it with loadSample(name, buffer). Every trigger() spawns a fresh
// AudioBufferSourceNode (the WebAudio idiom for sample playback).
//
// Procedural: tom / cymbal / conga / djembe — synthesized from oscillators
// + noise + filters + envelopes. Each defineVoice(name, fn) registers a
// function that, on trigger, builds the per-hit graph and returns its
// output AudioNode.
//
// Runtime-agnostic: pass in the runtime env ({ GainNode, OscillatorNode,
// NoiseNode, BiquadFilterNode, AudioBufferSourceNode }) so the same file
// works in qjs and the browser.

export class DrumSampler {
  constructor(ctx, env, opts = {}) {
    this.ctx = ctx;
    this.env = env;
    this.master = new env.GainNode(ctx, { gain: opts.gain ?? 0.7 });
    this.voices = new Map();
  }

  connect(dest) { return this.master.connect(dest); }
  disconnect(dest) { return dest === undefined ? this.master.disconnect() : this.master.disconnect(dest); }

  loadSample(name, buffer) {
    this.voices.set(name, { kind: 'sample', buffer });
    return this;
  }

  // fn(ctx, env, t, opts) -> output AudioNode connected via internal graph.
  defineVoice(name, fn) {
    this.voices.set(name, { kind: 'proc', fn });
    return this;
  }

  trigger(name, t, opts = {}) {
    const v = this.voices.get(name);
    if(!v) throw new Error(`unknown drum voice: ${name}`);

    // Per-hit gain so each trigger can have its own velocity.
    const hitGain = new this.env.GainNode(this.ctx, { gain: opts.gain ?? 1.0 });

    // Optional per-hit stereo placement.
    let downstream = this.master;
    if(opts.pan !== undefined) {
      const panner = new this.env.StereoPannerNode(this.ctx, { pan: opts.pan });
      panner.connect(this.master);
      downstream = panner;
    }
    hitGain.connect(downstream);

    if(v.kind === 'sample') {
      const src = new this.env.AudioBufferSourceNode(this.ctx, {
        buffer: v.buffer,
        playbackRate: opts.rate ?? 1.0,
      });
      src.connect(hitGain);
      src.start(t, opts.offset ?? 0);
    } else {
      const out = v.fn(this.ctx, this.env, t, opts);
      out.connect(hitGain);
    }
  }
}

/* ---------- procedural drum voices ---------- */

// Tom: low sine with a fast downward pitch sweep and exponential decay.
export function tom(ctx, env, t, { freq = 110, decay = 0.4 } = {}) {
  const osc = new env.OscillatorNode(ctx, { type: 'sine', frequency: freq * 2 });
  const g   = new env.GainNode(ctx, { gain: 0 });
  osc.connect(g);

  osc.frequency.setValueAtTime(freq * 2, t);
  osc.frequency.exponentialRampToValueAtTime(freq, t + 0.06);

  g.gain.setValueAtTime(0, t);
  g.gain.linearRampToValueAtTime(0.9, t + 0.005);
  g.gain.exponentialRampToValueAtTime(0.001, t + decay);

  osc.start(t);
  osc.stop(t + decay + 0.05);
  return g;
}

// Cymbal: highpassed white noise with a long exponential tail.
export function cymbal(ctx, env, t, { decay = 0.8 } = {}) {
  const n  = new env.NoiseNode(ctx, { type: 'white' });
  const hp = new env.BiquadFilterNode(ctx, { type: 'highpass', frequency: 6000, Q: 0.7 });
  const g  = new env.GainNode(ctx, { gain: 0 });
  n.connect(hp);
  hp.connect(g);

  g.gain.setValueAtTime(0, t);
  g.gain.linearRampToValueAtTime(0.4, t + 0.005);
  g.gain.exponentialRampToValueAtTime(0.001, t + decay);

  n.start(t);
  n.stop(t + decay + 0.05);
  return g;
}

// Conga: short sine with a quick pitch sweep, higher than tom.
export function conga(ctx, env, t, { freq = 240, decay = 0.25 } = {}) {
  const osc = new env.OscillatorNode(ctx, { type: 'sine', frequency: freq * 1.5 });
  const g   = new env.GainNode(ctx, { gain: 0 });
  osc.connect(g);

  osc.frequency.setValueAtTime(freq * 1.5, t);
  osc.frequency.exponentialRampToValueAtTime(freq, t + 0.04);

  g.gain.setValueAtTime(0, t);
  g.gain.linearRampToValueAtTime(0.75, t + 0.003);
  g.gain.exponentialRampToValueAtTime(0.001, t + decay);

  osc.start(t);
  osc.stop(t + decay + 0.05);
  return g;
}

// Djembe: pitched sine + highpassed noise. Sine is the body, noise is the slap.
export function djembe(ctx, env, t, { freq = 180, decay = 0.3 } = {}) {
  const osc    = new env.OscillatorNode(ctx, { type: 'sine', frequency: freq * 1.8 });
  const noise  = new env.NoiseNode(ctx, { type: 'white' });
  const hp     = new env.BiquadFilterNode(ctx, { type: 'highpass', frequency: 900, Q: 0.7 });
  const oscG   = new env.GainNode(ctx, { gain: 0 });
  const noiseG = new env.GainNode(ctx, { gain: 0 });
  const out    = new env.GainNode(ctx, { gain: 1.0 });

  osc.connect(oscG); oscG.connect(out);
  noise.connect(hp); hp.connect(noiseG); noiseG.connect(out);

  osc.frequency.setValueAtTime(freq * 1.8, t);
  osc.frequency.exponentialRampToValueAtTime(freq, t + 0.04);

  oscG.gain.setValueAtTime(0, t);
  oscG.gain.linearRampToValueAtTime(0.7, t + 0.003);
  oscG.gain.exponentialRampToValueAtTime(0.001, t + decay);

  noiseG.gain.setValueAtTime(0, t);
  noiseG.gain.linearRampToValueAtTime(0.3, t + 0.003);
  noiseG.gain.exponentialRampToValueAtTime(0.001, t + decay * 0.4);

  osc.start(t);   osc.stop(t + decay + 0.05);
  noise.start(t); noise.stop(t + decay + 0.05);
  return out;
}
