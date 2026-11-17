#!/usr/bin/env qjsm
/* StkFrames.sampleType / toFloat32 / fromFloat32.
 *
 * Run: QUICKJS_MODULE_PATH=<build-dir> qjsm tests/stkframes-test.js */
import * as std from 'std';
import { StkFrames, SineWave } from 'stk';

let failed = 0;
const check = (name, ok) => {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
};
const throws = (fn, type) => {
  try {
    fn();
  } catch (e) {
    return e.name === type;
  }
  return false;
};

const f = new StkFrames(64, 2);
const sine = new SineWave();
sine.frequency = 440;
sine.tick(f);

check('sampleType is f32 or f64', ['f32', 'f64'].includes(f.sampleType));

const view = f.sampleType == 'f64' ? new Float64Array(f.buffer) : new Float32Array(f.buffer);
const a = f.toFloat32();
check('toFloat32() returns a Float32Array of size', a instanceof Float32Array && a.length == f.size);
check('toFloat32() matches Math.fround of buffer', a.every((v, i) => v === Math.fround(view[i])));
check('signal is non-silent', a.some(v => v != 0));

const out = new Float32Array(f.size);
check('toFloat32(out) returns out', f.toFloat32(out) === out);
check('toFloat32(short) throws RangeError', throws(() => f.toFloat32(new Float32Array(3)), 'RangeError'));
check('toFloat32(Float64Array) throws TypeError', throws(() => f.toFloat32(new Float64Array(f.size)), 'TypeError'));

const g = new StkFrames(4, 1);
check('fromFloat32 returns this', g.fromFloat32(Float32Array.of(0.5, -0.25, 1, 2)) === g);
check('fromFloat32 widens values', Array.from(g.toFloat32()).join() == '0.5,-0.25,1,2');
check('fromFloat32(too long) throws RangeError', throws(() => g.fromFloat32(new Float32Array(9)), 'RangeError'));
check('empty frames convert', new StkFrames(0, 1).toFloat32().length == 0);

std.exit(failed ? 1 : 0);
