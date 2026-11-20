// portaudio-test: PaStream construction, info/hostApiType getters, read/write
// buffer format validation, and the closed-guard. The sandbox has no real
// audio device, but opening a default stream still works for testing the
// binding's getters and the closed-guard.
import * as pa from 'portaudio';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

const DEFAULT_IN = 0;
const DEFAULT_OUT = 2;

tests({
  'Pa_Initialize / Pa_Terminate are idempotent'() {
    pa.Pa_Initialize();
    pa.Pa_Initialize();
    pa.Pa_Terminate();
    pa.Pa_Terminate();
  },

  'Pa_GetVersion returns a number'() {
    pa.Pa_Initialize();
    assert(typeof pa.Pa_GetVersion() === 'number');
    pa.Pa_Terminate();
  },

  'Pa_GetVersionText returns a string'() {
    pa.Pa_Initialize();
    assert(typeof pa.Pa_GetVersionText() === 'string');
    pa.Pa_Terminate();
  },

  'Pa_GetSampleSize returns a positive number for known formats'() {
    pa.Pa_Initialize();
    assert(pa.Pa_GetSampleSize(pa.paFloat32) === 4);
    assert(pa.Pa_GetSampleSize(pa.paInt16) === 2);
    assert(pa.Pa_GetSampleSize(pa.paInt32) === 4);
    pa.Pa_Terminate();
  },

  'Pa_Sleep sleeps without throwing'() {
    pa.Pa_Initialize();
    eq(pa.Pa_Sleep(10), undefined);
    pa.Pa_Terminate();
  },

  'Pa_GetLastHostErrorInfo returns an object'() {
    pa.Pa_Initialize();
    const e = pa.Pa_GetLastHostErrorInfo();
    assert(typeof e === 'object');
    pa.Pa_Terminate();
  },

  'devices is an exotic array view'() {
    pa.Pa_Initialize();
    assert(typeof pa.devices === 'object');
    assert(typeof pa.devices.length === 'number');
    if (pa.devices.length) {
      assert(typeof pa.devices[0].name === 'string');
      assert(typeof pa.devices[0].maxInputChannels === 'number');
      assert(typeof pa.devices[0].maxOutputChannels === 'number');
    }
    pa.Pa_Terminate();
  },

  'hostApis is an exotic array view'() {
    pa.Pa_Initialize();
    assert(typeof pa.hostApis === 'object');
    assert(typeof pa.hostApis.length === 'number');
    if (pa.hostApis.length) {
      assert(typeof pa.hostApis[0].name === 'string');
      assert(typeof pa.hostApis[0].type === 'number');
    }
    pa.Pa_Terminate();
  },

  'PaStream construction and toStringTag'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    eq(Object.prototype.toString.call(s), '[object PaStream]');
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream getters work on an open stream'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    assert(typeof s.active === 'boolean');
    assert(typeof s.stopped === 'boolean');
    assert(typeof s.sampleRate === 'number');
    assert(typeof s.inputLatency === 'number');
    assert(typeof s.outputLatency === 'number');
    // hostApiType is the host API's name, present only when PortAudio has Pa_GetStreamHostApiType
    assert(s.hostApiType === undefined || typeof s.hostApiType === 'string');
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream.write rejects a mismatched Float64Array on paFloat32'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    let threw = false;
    try {
      s.write(new Float64Array(256));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'write(Float64Array on paFloat32) should throw');
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream.read rejects a mismatched Float64Array on paFloat32'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    let threw = false;
    try {
      s.read(new Float64Array(256));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'read(Float64Array on paFloat32) should throw');
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream.write rejects a mismatched Int16Array on paFloat32'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    let threw = false;
    try {
      s.write(new Int16Array(256));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'write(Int16Array on paFloat32) should throw');
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream.write accepts an ArrayBuffer (any-bytes form)'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    // The write may fail at the PortAudio layer (no real device) but the
    // binding's format check must accept it. We only test that it doesn't
    // throw a TypeError from the binding.
    try {
      s.write(new ArrayBuffer(256 * 4));
    } catch (e) {
      assert(!(e instanceof TypeError), 'ArrayBuffer should not throw TypeError');
    }
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream.read accepts an ArrayBuffer (any-bytes form)'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    try {
      s.read(new ArrayBuffer(256 * 4));
    } catch (e) {
      assert(!(e instanceof TypeError), 'ArrayBuffer should not throw TypeError');
    }
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream.close is idempotent'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    s.close();
    s.close();
    pa.Pa_Terminate();
  },

  'PaStream methods after close throw'() {
    pa.Pa_Initialize();
    const s = new pa.PaStream(DEFAULT_IN, DEFAULT_OUT);
    s.close();
    let threw = false;
    try {
      s.write(new Float32Array(256));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'write after close should throw');
    pa.Pa_Terminate();
  },
});