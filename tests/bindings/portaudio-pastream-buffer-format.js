// portaudio-pastream-buffer-format: .read()/.write() must reject a TypedArray
// whose element kind does not match the stream's negotiated sampleFormat.
// BUGS canonical name: pastream-buffer-format-not-validated
// Repro: a paFloat32 stream handed a Float64Array writes half as many frames
// as intended and reinterprets every sample's bytes as the wrong type.

import * as pa from 'portaudio';

pa.Pa_Initialize();

let allPass = true;

function expectThrow(name, fn) {
  try {
    fn();
    console.log(`FAIL: ${name} did not throw on a mismatched TypedArray`);
    allPass = false;
  } catch (e) {
    if (e instanceof TypeError) {
      console.log(`PASS: ${name} threw TypeError: ${e.message}`);
    } else {
      console.log(`FAIL: ${name} threw ${e.name} instead of TypeError`);
      allPass = false;
    }
  }
}

// Default stream: 0 input channels, 2 output channels, sampleFormat paFloat32.
// The default stream opens in this sandbox (no real audio device needed for
// the open itself - see BUGS pastream-reopen-after-close-segfault).
const s = new pa.PaStream(0, 2);

// The fix: a TypedArray whose element kind does not match the stream's
// sampleFormat is rejected before the pointer is handed to PortAudio, so
// the caller gets a TypeError instead of silently writing garbage.
expectThrow('write Float64Array on paFloat32 stream', () => s.write(new Float64Array(256)));
expectThrow('read Float64Array on paFloat32 stream', () => s.read(new Float64Array(256)));
expectThrow('write Int16Array on paFloat32 stream', () => s.write(new Int16Array(256)));

// A plain ArrayBuffer (no element kind) is the "any bytes" form and cannot
// be checked against the sample format, so it is accepted by the binding.
// Whether the underlying PortAudio write then succeeds depends on having a
// real device, which this sandbox does not - so only the rejection path is
// pinned here.
let threwOnArrayBuffer = false;
try {
  s.write(new ArrayBuffer(256 * 4));
} catch (e) {
  threwOnArrayBuffer = true;
  if (e instanceof TypeError) {
    console.log('FAIL: write ArrayBuffer threw TypeError (should be accepted)');
    allPass = false;
  } else {
    console.log(`NOTE: write ArrayBuffer threw ${e.name} - no audio device in this sandbox, expected`);
  }
}
if (!threwOnArrayBuffer)
  console.log('PASS: write ArrayBuffer on paFloat32 stream was accepted');

s.close();
pa.Pa_Terminate();

if (!allPass) {
  console.log('OVERALL: FAIL');
  process.exit(1);
} else {
  console.log('OVERALL: PASS');
  process.exit(0);
}