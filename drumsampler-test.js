// Drum sampler demo: 16-step pattern across kick/snare/hihat (sample-based)
// and tom/cymbal/conga/djembe (procedural).
//
// Lab ships kick.wav / snare.wav / hihat.wav under third_party/LabSound/
// assets/samples; the test reads them straight off disk via the qjs-only
// ctx.createBufferFromFile() helper. In a browser, the same script uses
// fetch + decodeAudioData on the same paths.

import { DrumSampler, tom, cymbal, conga, djembe } from './drumsampler.js';

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
  const drums = new DrumSampler(ctx, env, { gain: 0.7 });
  drums.connect(ctx.destination);

  drums.loadSample('kick',  await loadBuffer(ctx, `${SAMPLES_DIR}/kick.wav`));
  drums.loadSample('snare', await loadBuffer(ctx, `${SAMPLES_DIR}/snare.wav`));
  drums.loadSample('hihat', await loadBuffer(ctx, `${SAMPLES_DIR}/hihat.wav`));

  drums.defineVoice('tom',    tom);
  drums.defineVoice('cymbal', cymbal);
  drums.defineVoice('conga',  conga);
  drums.defineVoice('djembe', djembe);

  globalThis.__drum_keepalive = { ctx, drums };

  // 16-step grid per row. 'X' = hit, '-' = rest.
  const pattern = {
    kick:   'X---X-------X---',
    snare:  '----X-------X---',
    hihat:  'X-X-X-X-X-X-X-X-',
    cymbal: 'X---------------',
    tom:    '------X---------',
    conga:  '----------X-----',
    djembe: '--------------X-',
  };

  const bpm = 124;
  const stepDur = 60 / bpm / 4;
  const bars = 2;
  const t0 = ctx.currentTime + 0.15;

  for(let bar = 0; bar < bars; bar++) {
    for(const [name, row] of Object.entries(pattern)) {
      for(let i = 0; i < row.length; i++) {
        if(row[i] !== '-') {
          const t = t0 + (bar * 16 + i) * stepDur;
          drums.trigger(name, t, { gain: name === 'hihat' ? 0.45 : 1.0 });
        }
      }
    }
  }

  const totalMs = Math.ceil((bars * 16 * stepDur + 1.0) * 1000);
  setTimeout(() => {}, totalMs);
}

main();
