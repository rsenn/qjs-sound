// Reusable effect helpers built on the new WebAudio nodes.
// Runtime-agnostic — pass the env in.

// Build a tanh-style soft saturation curve. `drive` controls how aggressive
// the clipping is; values 2–10 give warm, 20+ give grindy.
export function makeDistortionCurve(drive = 6, size = 1024) {
  const curve = new Float32Array(size);
  for(let i = 0; i < size; i++) {
    const x = (i / (size - 1)) * 2 - 1;
    curve[i] = Math.tanh(x * drive);
  }
  return curve;
}

// Bit-crusher-ish curve: hard clip + quantization steps.
export function makeFuzzCurve(drive = 30, steps = 16, size = 1024) {
  const curve = new Float32Array(size);
  for(let i = 0; i < size; i++) {
    const x = (i / (size - 1)) * 2 - 1;
    const driven = Math.max(-1, Math.min(1, x * drive));
    curve[i] = Math.round(driven * steps) / steps;
  }
  return curve;
}

// Mono delay with feedback loop. Returns an effect block exposing `input`
// (where source signal goes) and `output` (the delayed signal). The dry
// signal is NOT mixed in here — connect both the dry source and `output` to
// the destination for the classic dry+wet send.
//
//   source ──┬──> dest
//            └──> delay.input;  delay.output ──> dest
export function createDelay(ctx, env, { time = 0.3, feedback = 0.4, maxDelayTime = 2.0 } = {}) {
  const delay = new env.DelayNode(ctx, { delayTime: time, maxDelayTime });
  const fb = new env.GainNode(ctx, { gain: feedback });
  delay.connect(fb);
  fb.connect(delay);
  return { input: delay, output: delay, delay, feedback: fb };
}

// Ping-pong delay: alternating L/R taps with shared feedback. Input is mono,
// output is a stereo signal already panned. Connect the source to `input`
// and connect `output` to wherever (e.g. master).
export function createPingPongDelay(ctx, env, { time = 0.375, feedback = 0.45 } = {}) {
  const split  = new env.GainNode(ctx, { gain: 1.0 });
  const dL     = new env.DelayNode(ctx, { delayTime: time, maxDelayTime: 4.0 });
  const dR     = new env.DelayNode(ctx, { delayTime: time, maxDelayTime: 4.0 });
  const fb     = new env.GainNode(ctx, { gain: feedback });
  const panL   = new env.StereoPannerNode(ctx, { pan: -1 });
  const panR   = new env.StereoPannerNode(ctx, { pan:  1 });
  const out    = new env.GainNode(ctx, { gain: 1.0 });

  // Cross-feed: input -> dL -> panL -> out, also dL -> dR -> panR -> out,
  // and dR -> fb -> dL closes the loop.
  split.connect(dL);
  dL.connect(panL); panL.connect(out);
  dL.connect(dR);
  dR.connect(panR); panR.connect(out);
  dR.connect(fb);
  fb.connect(dL);

  return { input: split, output: out, left: dL, right: dR, feedback: fb };
}
