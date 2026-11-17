// labsound-waveshapernode-offline-silent: a WaveShaperNode curve given from
// JS was dropped (read_float_array returned -1 on success), so the node
// rendered silence. Checks the curve reaches the node on all three paths,
// and that AudioBuffer.copyToChannel accepts a Float32Array.

import * as ls from 'labsound';
import * as std from 'std';

const curve = new Float32Array(1024);
for(let i = 0; i < 1024; i++) curve[i] = Math.tanh((i / 512) * 4 - 2);

let allPass = true;

async function peakOf(build) {
  const ctx = new ls.OfflineAudioContext(1, 2048, 44100);
  build(ctx);
  const r = await ctx.startRendering();
  let pk = 0;
  for(const v of r.getChannelData(0)) pk = Math.max(pk, Math.abs(v));
  return pk;
}

function chain(ctx, shaper) {
  const gain = ctx.createGain();
  gain.gain.value = 0.5;
  const osc = ctx.createOscillator();
  osc.connect(shaper);
  shaper.connect(gain);
  gain.connect(ctx.destination);
  osc.start();
}

const cases = {
  'createWaveShaper({curve})': ctx => chain(ctx, ctx.createWaveShaper({ curve })),
  'curve setter': ctx => { const s = ctx.createWaveShaper(); s.curve = curve; chain(ctx, s); },
  'new WaveShaperNode': ctx => chain(ctx, new ls.WaveShaperNode(ctx, { curve })),
};

for(const [name, build] of Object.entries(cases)) {
  const pk = await peakOf(build);
  const ok = pk > 0.1;
  console.log(`${ok ? 'PASS' : 'FAIL'}: ${name} peak=${pk}`);
  if(!ok) allPass = false;
}

{
  const buf = new ls.AudioBuffer({ numberOfChannels: 1, length: 16, sampleRate: 44100 });
  try {
    buf.copyToChannel(new Float32Array(16).fill(0.25), 0);
    const ok = buf.getChannelData(0)[0] === 0.25;
    console.log(`${ok ? 'PASS' : 'FAIL'}: copyToChannel`);
    if(!ok) allPass = false;
  } catch(e) {
    console.log(`FAIL: copyToChannel threw ${e.name}: ${e.message}`);
    allPass = false;
  }
}

std.exit(allPass ? 0 : 1);
