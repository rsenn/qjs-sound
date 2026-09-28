import { HarmTable, Oscili, Buzz, Randi, ADSR, Reson, Comb, Allpass, Mixer, Gain, VDelay } from 'sndobj';

function assert(cond, msg) {
  if(!cond) throw new Error('assertion failed: ' + msg);
}

function testBasicChain() {
  const sine = new HarmTable(4096, 1, HarmTable.SINE);
  assert(sine.len === 4096, 'HarmTable.len');
  assert(sine.toArray() instanceof Float32Array, 'HarmTable.toArray()');

  const lfo = new Randi(6, 0.15);
  const osc = new Oscili(sine, 220, 0.5, null, lfo);
  const env = new ADSR(0.01, 1, 0.1, 0.7, 0.3, 1.0, osc);
  const filt = new Reson(800, 150, env);
  const gain = new Gain(-6, filt);

  const numBlocks = 20;
  let lastBlock = null;
  for(let i = 0; i < numBlocks; i++) {
    lfo.process();
    osc.process();
    env.process();
    filt.process();
    gain.process();
    lastBlock = gain.block();
  }

  assert(lastBlock instanceof Float32Array, 'block() returns a Float32Array');
  assert(lastBlock.length === gain.vecsize, 'block() length matches vecsize');
  console.log('basic chain OK,', numBlocks, 'blocks, vecsize', gain.vecsize);
}

function testFxChain() {
  const sine = new HarmTable(4096, 1, HarmTable.SINE);
  const osc = new Oscili(sine, 110, 0.3);
  const c1 = new Comb(0.7, 0.0297, osc);
  const c2 = new Comb(0.7, 0.0371, osc);
  const c3 = new Comb(0.7, 0.0411, osc);
  const c4 = new Comb(0.7, 0.0437, osc);
  const mix = new Mixer();
  mix.addObj(c1);
  mix.addObj(c2);
  mix.addObj(c3);
  mix.addObj(c4);
  assert(mix.objNo === 4, 'mixer.objNo after 4 addObj calls');

  const ap1 = new Allpass(0.7, 0.01, mix);
  const ap2 = new Allpass(0.7, 0.0017, ap1);
  const out = new Gain(-6, ap2);

  for(let i = 0; i < 10; i++) {
    osc.process();
    c1.process();
    c2.process();
    c3.process();
    c4.process();
    mix.process();
    ap1.process();
    ap2.process();
    out.process();
  }
  const block = out.block();
  assert(block instanceof Float32Array, 'fx chain block() returns Float32Array');
  console.log('fx chain OK, block length', block.length);
}

function testMixerAddDelete() {
  const sine = new HarmTable(1024, 1, HarmTable.SINE);
  const a = new Oscili(sine, 100, 0.1);
  const b = new Oscili(sine, 200, 0.1);
  const mixer = new Mixer();
  mixer.addObj(a);
  mixer.addObj(b);
  assert(mixer.objNo === 2, 'objNo after two addObj calls');
  mixer.deleteObj(a);
  assert(mixer.objNo === 1, 'objNo after deleteObj');
  console.log('mixer add/delete OK');
}

function testVDelayAndBuzz() {
  const buzz = new Buzz(150, 0.4, 8);
  const vd = new VDelay(0.5, 0.1, 0.3, 1.0, 0.0, buzz);
  for(let i = 0; i < 5; i++) {
    buzz.process();
    vd.process();
  }
  assert(vd.block() instanceof Float32Array, 'VDelay block()');
  console.log('VDelay/Buzz OK');
}

testBasicChain();
testFxChain();
testMixerAddDelete();
testVDelayAndBuzz();

console.log('all sndobj tests passed');
