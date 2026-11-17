// labsound-graph-test.js: every node class can be created and connected
// in a graph that renders to a buffer. No sound card needed (offline ctx).

import * as labsound from 'labsound';

// A 1-D tanh saturation curve, like the one in fx-test.js.
function makeCurve() {
  const curve = new Float32Array(44100);
  for(let i = 0; i < 44100; i++) {
    const x = (i / 22050) * 4 - 2;
    curve[i] = Math.tanh(x);
  }
  return curve;
}

async function main() {
  const ctx = new labsound.OfflineAudioContext(1, 2048, 44100);

  function makeGraph() {
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    const filter = ctx.createBiquadFilter();
    const delay = ctx.createDelay();
    const shaper = ctx.createWaveShaper({ curve: makeCurve() });
    const panner = ctx.createStereoPanner();
    const conv = ctx.createConvolver();
    const analyser = ctx.createAnalyser();
    const comp = ctx.createDynamicsCompressor();
    const src = ctx.createBufferSource();
    const cs = ctx.createConstantSource();
    const dest = ctx.destination;

    gain.gain.value = 0.5;
    delay.delayTime.value = 0.0;
    panner.pan.value = 0;
    analyser.fftSize = 256;
    comp.threshold.value = -24;
    cs.offset.value = 1;

    osc.connect(gain);
    gain.connect(filter);
    filter.connect(delay);
    delay.connect(shaper);
    shaper.connect(panner);
    panner.connect(conv);
    conv.connect(analyser);
    analyser.connect(comp);
    comp.connect(dest);

    // ConvolverNode needs an impulse response to do anything.
    const ir = ctx.createBuffer(1, 256, 44100);
    const irData = ir.getChannelData(0);
    for(let i = 0; i < 256; i++) irData[i] = Math.exp(-i * 0.05) * (Math.random() * 2 - 1);
    conv.buffer = ir;

    // BufferSource also feeds the chain.
    const buf = ctx.createBuffer(1, 64, 44100);
    const ch = buf.getChannelData(0);
    for(let i = 0; i < 64; i++) ch[i] = Math.sin(2 * Math.PI * 440 * i / 44100);
    src.buffer = buf;
    src.connect(gain);
    src.loop = true;

    // ConstantSourceNode is a scheduled source: start it too.
    cs.start();
    osc.start();
    src.start();

    return [osc, gain, filter, delay, shaper, panner, conv, analyser, comp, src, cs, dest];
  }

  makeGraph();

  const rendered = await ctx.startRendering();
  if(!rendered || rendered.length === 0)
    throw new Error('rendered buffer is empty');

  const peak = Math.max(...Array.from(rendered.getChannelData(0)).map(Math.abs));
  if(peak <= 0)
    throw new Error('rendered signal is silent');

  console.log('labsound graph test passed: peak=' + peak.toFixed(4));
}

main();