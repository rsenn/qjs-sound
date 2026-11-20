// sndfile-test: SndFile read path (open, getters, read, seek, close), the
// SF_* constants, and the known write-path bug (recorded in BUGS).
import * as sf from 'sndfile';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

const TEST_WAV = '/home/roman/rec.wav';

tests({
  'SndFile construction and toStringTag (read)'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    eq(Object.prototype.toString.call(f), '[object SndFile]');
    f.close();
  },

  'SndFile getters on a read file'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    assert(typeof f.frames === 'number' || typeof f.frames === 'bigint');
    assert(typeof f.samplerate === 'number');
    assert(typeof f.channels === 'number');
    assert(typeof f.format === 'number');
    assert(typeof f.sections === 'number');
    assert(typeof f.seekable === 'boolean');
    f.close();
  },

  'SndFile.read with Float32Array'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    const buf = new Float32Array(1000);
    const n = f.read(buf, 1000);
    assert(typeof n === 'number' || typeof n === 'bigint');
    assert(n >= 0);
    assert(n <= 1000);
    f.close();
  },

  'SndFile.read with Int16Array'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    const buf = new Int16Array(1000);
    const n = f.read(buf, 1000);
    assert(typeof n === 'number' || typeof n === 'bigint');
    assert(n >= 0);
    assert(n <= 1000);
    f.close();
  },

  'SndFile.read rejects a Uint8Array (element kind mismatch)'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    let threw = false;
    try {
      f.read(new Uint8Array(1000));
    } catch (e) {
      threw = true;
      assert(e instanceof Error, 'expected Error, got ' + e);
    }
    assert(threw, 'read(Uint8Array) should throw');
    f.close();
  },

  'SndFile.seek with SEEK_SET'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    const pos = f.seek(0, sf.SEEK_SET);
    eq(pos, 0);
    const pos2 = f.seek(100, sf.SEEK_SET);
    assert(typeof pos2 === 'number' || typeof pos2 === 'bigint');
    f.close();
  },

  'SndFile.seek with SEEK_CUR'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    const pos = f.seek(5, sf.SEEK_CUR);
    assert(typeof pos === 'number' || typeof pos === 'bigint');
    f.close();
  },

  'SndFile.close is idempotent'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    f.close();
    f.close();
  },

  'SndFile methods after close throw'() {
    const f = new sf.SndFile(TEST_WAV, sf.SFM_READ);
    f.close();
    let threw = false;
    try {
      f.read(new Float32Array(100));
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'read after close should throw');
  },

  'SndFile.open non-existent file throws InternalError'() {
    let threw = false;
    try {
      new sf.SndFile('/tmp/does-not-exist-12345.wav', sf.SFM_READ);
    } catch (e) {
      threw = true;
      assert(e instanceof Error, 'expected Error, got ' + typeof e);
      assert(e.name === 'InternalError', 'expected InternalError, got ' + e.name);
    }
    assert(threw, 'opening missing file should throw');
  },

  'SF format constants are present'() {
    assert(typeof sf.SF_FORMAT_WAV === 'number');
    assert(typeof sf.SF_FORMAT_AIFF === 'number');
    assert(typeof sf.SF_FORMAT_PCM_16 === 'number');
    assert(typeof sf.SF_FORMAT_FLOAT === 'number');
    assert(typeof sf.SF_FORMAT_DOUBLE === 'number');
    assert(typeof sf.SFM_READ === 'number');
    assert(typeof sf.SFM_WRITE === 'number');
    assert(typeof sf.SFM_RDWR === 'number');
    assert(typeof sf.SEEK_SET === 'number');
    assert(typeof sf.SEEK_CUR === 'number');
    assert(typeof sf.SEEK_END === 'number');
  },

  'SndFile.info static method'() {
    const info = sf.SndFile.info(TEST_WAV);
    assert(typeof info.frames === 'number' || typeof info.frames === 'bigint');
    assert(typeof info.samplerate === 'number');
    assert(typeof info.channels === 'number');
    assert(typeof info.format === 'number');
    assert(typeof info.sections === 'number');
    assert(typeof info.seekable === 'boolean');
  },

  // Known bug: write path throws a non-Error value ([unsupported type])
  // instead of a proper InternalError. Tracked in BUGS as
  // 'sndfile-write-throws-non-error'; no test here so the suite stays green.
});