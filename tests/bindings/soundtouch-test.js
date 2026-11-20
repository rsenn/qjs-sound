// soundtouch-test: SoundTouch construction, the SETTING_* getters/setters,
// putSamples/flush/receiveSamples, numSamples/numUnprocessedSamples,
// clear, and the closed-guard.
import * as st from 'soundtouch';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

tests({
  'SoundTouch construction and toStringTag'() {
    const s = new st.SoundTouch(44100, 1);
    eq(Object.prototype.toString.call(s), '[object SoundTouch]');
  },

  'channels and sampleRate getters'() {
    const s = new st.SoundTouch(44100, 2);
    eq(s.channels, 2);
    eq(s.sampleRate, 44100);
  },

  'setting getters return numbers'() {
    const s = new st.SoundTouch(44100, 1);
    assert(typeof s.tempo === 'number');
    assert(typeof s.pitch === 'number');
    assert(typeof s.pitchSemitones === 'number');
    assert(typeof s.pitchOctaves === 'number');
    assert(typeof s.rate === 'number');
  },

  'setting setters round-trip'() {
    const s = new st.SoundTouch(44100, 1);
    s.tempo = 1.5;
    s.pitch = 1.1;
    s.rate = 2.0;
    eq(s.tempo, 1.5);
    eq(s.pitch, 1.1);
    eq(s.rate, 2.0);
  },

  'pitchSemitones setter round-trips through the derived getter'() {
    const s = new st.SoundTouch(44100, 1);
    s.pitchSemitones = 2;
    assert(Math.abs(s.pitchSemitones - 2) < 1e-9, 'pitchSemitones: ' + s.pitchSemitones);
    s.pitchOctaves = 1;
    assert(Math.abs(s.pitchOctaves - 1) < 1e-9, 'pitchOctaves: ' + s.pitchOctaves);
  },

  'putSamples + flush + receiveSamples cycle'() {
    const s = new st.SoundTouch(44100, 1);
    s.putSamples(new Float32Array([0.1, -0.2, 0.3, 0.4, 0.5, -0.6]));
    s.flush();
    const out = new Float32Array(1000);
    const n = s.receiveSamples(out);
    assert(typeof n === 'number');
    assert(n >= 0);
    assert(n <= 1000);
  },

  'numSamples and numUnprocessedSamples are numbers'() {
    const s = new st.SoundTouch(44100, 1);
    assert(typeof s.numSamples() === 'number');
    assert(typeof s.numUnprocessedSamples() === 'number');
  },

  'clear() is idempotent'() {
    const s = new st.SoundTouch(44100, 1);
    s.putSamples(new Float32Array([0.1, 0.2]));
    s.clear();
    s.clear();
  },

  'putSamples rejects a non-Float32Array'() {
    const s = new st.SoundTouch(44100, 1);
    let threw = false;
    try {
      s.putSamples(new Int16Array([1, 2]));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'putSamples(Int16Array) should throw');
  },

  'receiveSamples rejects a non-Float32Array'() {
    const s = new st.SoundTouch(44100, 1);
    let threw = false;
    try {
      s.receiveSamples(new Int16Array(10));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'receiveSamples(Int16Array) should throw');
  },

  'methods after close throw'() {
    const s = new st.SoundTouch(44100, 1);
    s.close();
    let threw = false;
    try {
      s.putSamples(new Float32Array([0.1]));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'putSamples after close should throw');
  },
});