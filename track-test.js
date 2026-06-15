// Full track: drums + TB-303 synth + effects, all in one mix.
//
// Bus layout:
//
//   Synth ─► WaveShaper (dist) ─┬─► synthBus ─► master ─► destination
//                               └─► PingPongDelay ─► wetSend ─► synthBus
//
//   Drums (kick/snare/hihat samples + procedural tom/cymbal) ─► drumBus ─► master
//
// 4 bars at 124 BPM. Drums per-hit panned across the stereo field; synth runs
// through distortion + an 8th-dotted ping-pong delay.

import { Synth } from './synth.js';
import { DrumSampler, tom, cymbal } from './drumsampler.js';
import { makeDistortionCurve, createPingPongDelay } from './effects.js';

const isBrowser = typeof globalThis.window !== 'undefined';

const SAMPLES_DIR = isBrowser
  ? './third_party/LabSound/assets/samples'
  : '/mnt/data/Projects/plot-cv/quickjs/qjs-sound/third_party/LabSound/assets/samples';

async function loadBuffer(ctx, path) {
  if(isBrowser) {
    const r = await fetch(path);
    const ab = await r.arrayBuffer();
    return await ctx.decodeAudioData(ab);
  }
  return ctx.createBufferFromFile(path);
}

async function main() {
  const env = isBrowser ? globalThis : await import('labsound');
  const setTimeout = isBrowser ? globalThis.setTimeout : (await import('os')).setTimeout;

  const ctx = new env.AudioContext();

  /* ---------- mixer ---------- */

  const master = new env.GainNode(ctx, { gain: 0.7 });
  master.connect(ctx.destination);

  /* ---------- drums ---------- */

  const drumBus = new env.GainNode(ctx, { gain: 0.9 });
  drumBus.connect(master);

  const drums = new DrumSampler(ctx, env, { gain: 1.0 });
  drums.connect(drumBus);

  drums.loadSample('kick',  await loadBuffer(ctx, `${SAMPLES_DIR}/kick.wav`));
  drums.loadSample('snare', await loadBuffer(ctx, `${SAMPLES_DIR}/snare.wav`));
  drums.loadSample('hihat', await loadBuffer(ctx, `${SAMPLES_DIR}/hihat.wav`));
  drums.defineVoice('tom',    tom);
  drums.defineVoice('cymbal', cymbal);

  /* ---------- synth bus: distortion + ping-pong delay ---------- */

  const synthBus = new env.GainNode(ctx, { gain: 0.55 });
  synthBus.connect(master);

  const dist = new env.WaveShaperNode(ctx, {
    curve: makeDistortionCurve(4.5),
    oversample: '2x',
  });
  dist.connect(synthBus);

  // 8th-dotted delay at 124 BPM = (60/124) * 0.75 = ~0.363 s
  const bpm = 124;
  const stepDur = 60 / bpm / 4;          // 16th notes
  const ppd = createPingPongDelay(ctx, env, { time: stepDur * 3, feedback: 0.45 });
  const wetSend = new env.GainNode(ctx, { gain: 0.35 });
  ppd.output.connect(wetSend);
  wetSend.connect(synthBus);
  dist.connect(ppd.input);

  // Synth feeds the distortion (it then fans out to dry + delay)
  const synth = new Synth(ctx, env, {
    gain: 0.7,
    cutoff: 600,
    Q: 6,
    poles: 4,
    output: dist,
  });

  globalThis.__track_keepalive = { ctx, drums, synth, master, drumBus, synthBus, dist, ppd, wetSend };

  /* ---------- patterns ---------- */

  // 16-step drum pattern. X = hit, '-' = rest.
  const drumPattern = {
    kick:   'X---X---X---X---',
    snare:  '----X-------X---',
    hihat:  '--X---X---X---X-',
    cymbal: 'X---------------',
    tom:    '--------------X-',
  };

  // Per-voice stereo placement (-1 = hard left, +1 = hard right).
  const drumPan = {
    kick:   0.0,
    snare: -0.15,
    hihat:  0.3,
    cymbal: 0.5,
    tom:   -0.5,
  };

  // 16-step bassline. [midi, accent, slide]
  const synthPattern = [
    [33, false, false], [45, true,  false], [33, false, true ], [40, false, false],
    [33, false, false], [52, true,  false], [33, false, true ], [40, false, false],
    [33, false, false], [33, true,  false], [45, false, true ], [33, false, false],
    [37, false, false], [49, true,  false], [33, false, true ], [40, false, false],
  ];

  /* ---------- schedule ---------- */

  const bars = 4;
  const stepsPerBar = 16;
  const totalSteps = bars * stepsPerBar;
  const t0 = ctx.currentTime + 0.2;

  for(let i = 0; i < totalSteps; i++) {
    const t = t0 + i * stepDur;
    const stepInBar = i % stepsPerBar;

    // Drums
    for(const [name, row] of Object.entries(drumPattern)) {
      if(row[stepInBar] !== '-') {
        drums.trigger(name, t, {
          gain: name === 'hihat' ? 0.4 : 1.0,
          pan: drumPan[name],
        });
      }
    }

    // Synth — start the bassline from bar 2 so the drums establish first
    if(i >= stepsPerBar) {
      const [midi, accent, slide] = synthPattern[stepInBar];
      synth.noteOn({
        t,
        midi,
        gateLen: stepDur * (slide ? 0.95 : 0.55),
        accent,
        slide,
      });
    }
  }

  const totalMs = Math.ceil((totalSteps * stepDur + 1.5) * 1000);
  setTimeout(() => synth.stop(), totalMs);
}

main();
