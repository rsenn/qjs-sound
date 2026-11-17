// portaudio-pastream-getters: getters lack closed-guard after .close()
// BUGS canonical name: pastream-getters-no-closed-guard
// Repro: open stream, close, then read .stopped/.active/.time -> returns wrong values instead of throwing

import * as pa from 'portaudio';
import * as std from 'std';

pa.Pa_Initialize();

const s = new pa.PaStream(0, 2);

s.close();

function throwsOrUndefined(name, fn) {
  let err;
  try {
    const val = fn();
    // If no throw, check if it's a meaningless value
    if (val === true || val === false || typeof val === 'number') {
      console.log(`FAIL: ${name} returned ${val} after close (should throw or be undefined)`);
      return false;
    }
    console.log(`FAIL: ${name} returned ${val} after close (should throw)`);
    return false;
  } catch (e) {
    if (e instanceof TypeError || e instanceof Error) {
      console.log(`PASS: ${name} threw ${e.name}`);
      return true;
    }
    console.log(`FAIL: ${name} threw unexpected ${e.name}`);
    return false;
  }
}

let allPass = true;
allPass &= throwsOrUndefined('stopped', () => s.stopped);
allPass &= throwsOrUndefined('active', () => s.active);
allPass &= throwsOrUndefined('time', () => s.time);
allPass &= throwsOrUndefined('cpuLoad', () => s.cpuLoad);
allPass &= throwsOrUndefined('readAvailable', () => s.readAvailable);
allPass &= throwsOrUndefined('writeAvailable', () => s.writeAvailable);

// inputLatency/outputLatency/sampleRate were previously "safe" only because
// Pa_GetStreamInfo(NULL) returns NULL; with the closed-guard they throw too.
allPass &= throwsOrUndefined('inputLatency', () => s.inputLatency);
allPass &= throwsOrUndefined('outputLatency', () => s.outputLatency);
allPass &= throwsOrUndefined('sampleRate', () => s.sampleRate);

pa.Pa_Terminate();

if (!allPass) {
  console.log('OVERALL: FAIL');
  std.exit(1);
} else {
  console.log('OVERALL: PASS');
  std.exit(0);
}