// aubio-test: AubioNotes / AubioOnset / AubioPitch construction, getters,
// process() output shape, and the toStringTag each instance carries.
import * as aubio from 'aubio';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

const SAMPLE_RATE = 44100;
const HOP = 256;

function newPitch(method = 'default') {
  return new aubio.AubioPitch(method, 256, HOP, SAMPLE_RATE);
}
function newOnset(method = 'default') {
  return new aubio.AubioOnset(method, 256, HOP, SAMPLE_RATE);
}
function newNotes(method = 'default') {
  return new aubio.AubioNotes(method, 256, HOP, SAMPLE_RATE);
}

const zero = new Float32Array(HOP);

tests({
  'AubioPitch construction and toStringTag'() {
    const p = newPitch();
    eq(Object.prototype.toString.call(p), '[object AubioPitch]');
  },

  'AubioPitch.process returns a Float32Array'() {
    const p = newPitch();
    const r = p.process(zero);
    assertStrictEquals(Object.prototype.toString.call(r), '[object Float32Array]');
    eq(r.length, 1);
  },

  'AubioPitch.process on silence reads zero'() {
    const p = newPitch();
    const r = p.process(zero);
    eq(r[0], 0);
  },

  'AubioPitch.process rejects a wrong-length buffer'() {
    const p = newPitch();
    let threw = false;
    try {
      p.process(new Float32Array(4));
    } catch (e) {
      threw = true;
      assert(e instanceof RangeError, 'expected RangeError, got ' + e);
    }
    assert(threw, 'process(4) should throw');
  },

  'AubioOnset construction and toStringTag'() {
    const o = newOnset();
    eq(Object.prototype.toString.call(o), '[object AubioOnset]');
  },

  'AubioOnset.process returns a Float32Array of length 1'() {
    const o = newOnset();
    const r = o.process(zero);
    assertStrictEquals(Object.prototype.toString.call(r), '[object Float32Array]');
    eq(r.length, 1);
  },

  'AubioOnset.process on silence reads zero'() {
    const o = newOnset();
    eq(o.process(zero)[0], 0);
  },

  'AubioOnset getters/setters round-trip'() {
    const o = newOnset();
    o.threshold = 0.2;
    o.silence = -50;
    o.minioiMs = 30;
    assert(Math.abs(o.threshold - 0.2) < 1e-5, 'threshold: ' + o.threshold);
    assert(Math.abs(o.silence - (-50)) < 1e-5, 'silence: ' + o.silence);
    assert(Math.abs(o.minioiMs - 30) < 1e-5, 'minioiMs: ' + o.minioiMs);
  },

  'AubioNotes construction and toStringTag'() {
    const n = newNotes();
    eq(Object.prototype.toString.call(n), '[object AubioNotes]');
  },

  'AubioNotes.process returns a Float32Array of length 3'() {
    const n = newNotes();
    const r = n.process(zero);
    assertStrictEquals(Object.prototype.toString.call(r), '[object Float32Array]');
    eq(r.length, 3);
  },

  'AubioNotes.process on silence is silent'() {
    const n = newNotes();
    const r = n.process(zero);
    eq(r[0], 0);
    eq(r[1], 0);
    eq(r[2], 0);
  },

  'AubioNotes setters exist and round-trip'() {
    const n = newNotes();
    n.silence = -40;
    n.minioiMs = 40;
    n.releaseDrop = 0.1;
    eq(n.silence, -40);
    eq(n.minioiMs, 40);
    assert(Math.abs(n.releaseDrop - 0.1) < 1e-5, 'releaseDrop: ' + n.releaseDrop);
  },

  'AubioPitch.setUnit and confidence getter'() {
    const p = newPitch();
    eq(p.setUnit('Hz'), 0);
    eq(p.setUnit('midi'), 0);
    eq(p.setUnit('cent'), 0);
    eq(p.setUnit('bin'), 0);
    const buf = new Float32Array(HOP);
    for (let i = 0; i < HOP; i++)
      buf[i] = Math.sin(2 * Math.PI * i * 440 / SAMPLE_RATE);
    const r = p.process(buf);
    assert(typeof p.confidence === 'number');
  },

  'AubioPitch with a pitched input detects a note'() {
    const p = newPitch();
    const buf = new Float32Array(HOP);
    for (let i = 0; i < HOP; i++)
      buf[i] = Math.sin(2 * Math.PI * i * 440 / SAMPLE_RATE);
    const r = p.process(buf);
    assert(r[0] > 0, 'expected a detected pitch, got ' + r[0]);
  },

  'AubioOnset.reset and last getters'() {
    const o = newOnset();
    o.process(new Float32Array(HOP).fill(0.5));
    o.reset();
    assert(typeof o.last === 'number');
    assert(typeof o.lastS === 'number');
    assert(typeof o.lastMs === 'number');
  },
});