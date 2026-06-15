// TB-303-style acid bassline. Wraps the patch in a reusable Synth class.
// Runs unchanged in qjs and browsers (load as <script type="module">).

import { Synth } from './synth.js';

const isBrowser = typeof globalThis.window !== 'undefined';

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
