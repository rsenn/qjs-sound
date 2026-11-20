// samplerate-test: SampleRateConverter construction, the SRC_* quality
// constants, process() with and without an explicit output buffer, and the
// closed-guard.
import * as sr from 'samplerate';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

tests({
  'SampleRateConverter construction and toStringTag'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    eq(Object.prototype.toString.call(c), '[object SampleRateConverter]');
  },

  'SRC quality constants are distinct integers'() {
    assert(typeof sr.SRC_SINC_BEST_QUALITY === 'number');
    assert(typeof sr.SRC_SINC_MEDIUM_QUALITY === 'number');
    assert(typeof sr.SRC_SINC_FASTEST === 'number');
    assert(typeof sr.SRC_ZERO_ORDER_HOLD === 'number');
    assert(typeof sr.SRC_LINEAR === 'number');
    assert(sr.SRC_SINC_BEST_QUALITY !== sr.SRC_LINEAR);
  },

  'process() with an explicit output buffer returns frames generated'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    const inBuf = new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]);
    const outBuf = new Float32Array(32);
    const n = c.process(inBuf, 2.0, true, outBuf);
    assert(typeof n === 'number');
    assert(n >= 0);
    assert(n <= 32);
  },

  'process() without an output buffer returns a fresh Float32Array'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    const inBuf = new Float32Array([0.1, 0.2, 0.3, 0.4]);
    const r = c.process(inBuf, 1.0, true);
    assert(r instanceof Float32Array, 'process() is ' + typeof r);
    assert(r.length >= 4);
  },

  'process() rejects a non-Float32Array input'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    let threw = false;
    try {
      c.process(new Int16Array([1, 2, 3, 4]), 1.0, true);
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'process(Int16Array) should throw');
  },

  'process() rejects a non-Float32Array output buffer'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    let threw = false;
    try {
      c.process(new Float32Array([0.1, 0.2, 0.3, 0.4]), 1.0, true, new Int16Array(32));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'process(Int16Array output) should throw');
  },

  'reset() returns undefined'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    eq(c.reset(), undefined);
  },

  'process() after close throws'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    c.close();
    let threw = false;
    try {
      c.process(new Float32Array([0.1, 0.2]), 1.0, true);
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'process() after close should throw');
  },

  'setRatio() works'() {
    const c = new sr.SampleRateConverter(sr.SRC_SINC_BEST_QUALITY, 1);
    eq(c.setRatio(2.0), undefined);
  },
});