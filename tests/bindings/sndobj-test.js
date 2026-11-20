// sndobj-test: SndObj classes (HarmTable, generators, effects), their
// process()/output()/block() cycle, sr/vecsize/error getters, and toStringTag.
import * as so from 'sndobj';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

tests({
  'HarmTable construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    eq(Object.prototype.toString.call(ht), '[object HarmTable]');
  },

  'HarmTable.len getter'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    eq(ht.len, 4096);
  },

  'Oscili construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    eq(Object.prototype.toString.call(o), '[object Oscili]');
  },

  'Oscili sr/vecsize/error getters'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    eq(o.sr, 44100);
    eq(o.vecsize, 256);
    eq(o.error, 0);
  },

  'Oscili setFreq/setAmp'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    o.setFreq(440);
    o.setAmp(0.8);
    o.process();
    const block = o.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Oscili.process + output + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    o.process();
    assert(typeof o.output(0) === 'number');
    const block = o.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Buzz construction and toStringTag'() {
    const b = new so.Buzz(440, 0.5);
    eq(Object.prototype.toString.call(b), '[object Buzz]');
  },

  'Buzz sr getter'() {
    const b = new so.Buzz(440, 0.5);
    eq(b.sr, 44100);
  },

  'Buzz.process + block cycle'() {
    const b = new so.Buzz(440, 0.5);
    b.process();
    const block = b.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Randi construction and toStringTag'() {
    const r = new so.Randi(6, 0.15);
    eq(Object.prototype.toString.call(r), '[object Randi]');
  },

  'Randi sr getter'() {
    const r = new so.Randi(6, 0.15);
    eq(r.sr, 44100);
  },

  'Randi.process + block cycle'() {
    const r = new so.Randi(6, 0.15);
    r.process();
    const block = r.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Randi construction and toStringTag'() {
    const r = new so.Randi(6, 0.15);
    eq(Object.prototype.toString.call(r), '[object Randi]');
  },

  'ADSR construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const ad = new so.ADSR(0.01, 1, 0.1, 0.7, 0.3, 1.0, o);
    eq(Object.prototype.toString.call(ad), '[object ADSR]');
  },

  'ADSR sr getter'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const ad = new so.ADSR(0.01, 1, 0.1, 0.7, 0.3, 1.0, o);
    eq(ad.sr, 44100);
  },

  'ADSR.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const ad = new so.ADSR(0.01, 1, 0.1, 0.7, 0.3, 1.0, o);
    ad.process();
    const block = ad.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Gain construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const g = new so.Gain(-6, o);
    eq(Object.prototype.toString.call(g), '[object Gain]');
  },

  'Gain sr getter and setGain/setGainM'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const g = new so.Gain(-6, o);
    eq(g.sr, 44100);
    g.setGain(-12);
    g.setGainM(0.5);
  },

  'Gain.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const g = new so.Gain(-6, o);
    o.process();
    g.process();
    const block = g.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Mixer construction and toStringTag'() {
    const mix = new so.Mixer(256, 44100);
    eq(Object.prototype.toString.call(mix), '[object Mixer]');
  },

  'Mixer objNo, addObj, deleteObj'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const mix = new so.Mixer(256, 44100);
    eq(mix.objNo, 0);
    mix.addObj(o);
    eq(mix.objNo, 1);
    mix.deleteObj(o);
    eq(mix.objNo, 0);
  },

  'Mixer.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const mix = new so.Mixer(256, 44100);
    mix.addObj(o);
    o.process();
    mix.process();
    const block = mix.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Allpass construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const all = new so.Allpass(44100, 0.1, o);
    eq(Object.prototype.toString.call(all), '[object Allpass]');
  },

  'Allpass.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const all = new so.Allpass(44100, 0.1, o);
    o.process();
    all.process();
    const block = all.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Comb construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const comb = new so.Comb(44100, 0.1, o);
    eq(Object.prototype.toString.call(comb), '[object Comb]');
  },

  'Comb.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const comb = new so.Comb(44100, 0.1, o);
    o.process();
    comb.process();
    const block = comb.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'Reson construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const res = new so.Reson(800, 150, o);
    eq(Object.prototype.toString.call(res), '[object Reson]');
  },

  'Reson.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const res = new so.Reson(800, 150, o);
    o.process();
    res.process();
    const block = res.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },

  'VDelay construction and toStringTag'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const vd = new so.VDelay(1.0, 0.1, 0.1, 1.0, 0.0, o);
    eq(Object.prototype.toString.call(vd), '[object VDelay]');
  },

  'VDelay.process + block cycle'() {
    const ht = new so.HarmTable(4096, 1, so.HarmTable.SINE);
    const o = new so.Oscili(ht, 220, 0.5);
    const vd = new so.VDelay(1.0, 0.1, 0.1, 1.0, 0.0, o);
    o.process();
    vd.process();
    const block = vd.block();
    assertStrictEquals(Object.prototype.toString.call(block), '[object Float32Array]');
    eq(block.length, 256);
  },
});