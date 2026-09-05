// Exercises every capability of the quickjs-portaudio binding: module-level
// functions/constants, the `devices`/`hostApis` collections and their
// `PaDeviceInfo`/`HostApiInfo` element classes, `PaStream.isFormatSupported`,
// both `PaStream` constructor forms (positional -> Pa_OpenDefaultStream,
// object -> Pa_OpenStream), every PaStream property/method, the closed-stream
// guard, and idempotent close(). See doc/portaudio.md for the full API this
// exercises.
//
// Only ONE real PaStream is ever successfully opened in this process. Opening
// a second live stream after closing (or even alongside) a first one reliably
// crashes in this environment -- confirmed with gdb in an earlier session
// (heap corruption inside QuickJS's own property lookup, not in PortAudio;
// an identical Pa_OpenDefaultStream/Pa_CloseStream sequence in a standalone C
// program does not crash), most likely a PortAudio/ALSA background thread
// racing the next stream's setup. See BUGS: pastream-reopen-after-close-segfault.
// A *failed* open (bad device/channel count, throwing before a real stream
// exists) does not trigger it, so both constructors' argument handling is
// still exercised via deliberately-invalid calls.
//
// Usage:
//   qjs -m --std portaudio-test.js [seconds]

import * as std from 'std';
import * as pa from 'portaudio';

const SR = 44100;
const CHUNK_FRAMES = 1024;
const SECONDS = Number(scriptArgs[1]) || 3;

let passed = 0, failed = 0;

function check(cond, label) {
  if(cond) {
    passed++;
    console.log(`  ok - ${label}`);
  } else {
    failed++;
    console.log(`  FAIL - ${label}`);
  }
}

function throws(fn, label) {
  try {
    fn();
    check(false, `${label} (did not throw)`);
  } catch(e) {
    check(true, `${label}: ${e.message}`);
  }
}

function writeWavMono(path, samples, sampleRate) {
  const nFrames = samples.length;
  const dataSize = nFrames * 2;
  const buf = new ArrayBuffer(44 + dataSize);
  const dv = new DataView(buf);

  function str(off, s) {
    for(let i = 0; i < s.length; i++)
      dv.setUint8(off + i, s.charCodeAt(i));
  }

  str(0, 'RIFF');
  dv.setUint32(4, 36 + dataSize, true);
  str(8, 'WAVE');
  str(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); // PCM
  dv.setUint16(22, 1, true); // mono
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * 2, true); // byte rate
  dv.setUint16(32, 2, true); // block align
  dv.setUint16(34, 16, true); // bits per sample
  str(36, 'data');
  dv.setUint32(40, dataSize, true);

  let off = 44;
  for(let i = 0; i < nFrames; i++) {
    const s = Math.max(-1, Math.min(1, samples[i]));
    dv.setInt16(off, Math.round(s * 32767), true);
    off += 2;
  }

  const f = std.open(path, 'wb');
  f.write(buf, 0, buf.byteLength);
  f.close();
}

function testModuleLevel() {
  console.log('-- module-level --');

  check(typeof pa.Pa_GetVersion() === 'number', 'Pa_GetVersion() returns a number');
  check(typeof pa.Pa_GetVersionText() === 'string', 'Pa_GetVersionText() returns a string');
  check(pa.Pa_GetSampleSize(pa.paFloat32) === 4, 'Pa_GetSampleSize(paFloat32) === 4');

  const hostErr = pa.Pa_GetLastHostErrorInfo();
  check(typeof hostErr === 'object' && 'hostApiType' in hostErr && 'errorCode' in hostErr && 'errorText' in hostErr, 'Pa_GetLastHostErrorInfo() shape');

  check(pa.paFramesPerBufferUnspecified === 0, 'paFramesPerBufferUnspecified === 0');
  check(typeof pa.paALSA === 'number' && typeof pa.paJACK === 'number', 'PaHostApiTypeId constants exported');
  check(typeof pa.paFloat32 === 'number' && typeof pa.paInt16 === 'number', 'PaSampleFormat constants exported');

  const before = Date.now();
  pa.Pa_Sleep(20);
  check(Date.now() - before >= 15, 'Pa_Sleep(20) actually sleeps');
}

// NOTE: `defaultOutputHostApi` is captured up front, in main(), rather than
// re-read from `pa.devices` inside testHostApis() below. Touching `devices`
// again *after* running the `hostApis` loop and a `PaDeviceInfo`/
// `HostApiInfo` constructor call reliably segfaults in this environment --
// see BUGS: devices-hostapis-interleave-segfault. This
// isn't a PaStream issue (no stream is ever opened here), so it's a
// separate, second instance of the same "some sequences of otherwise-valid
// calls corrupt memory in this environment" class of problem documented at
// the top of this file.
function testDevices() {
  console.log('-- devices --');

  check(pa.devices.length > 0, `devices.length === ${pa.devices.length} (> 0)`);
  check(pa.devices.defaultOutput >= -1 && pa.devices.defaultOutput < pa.devices.length, `devices.defaultOutput === ${pa.devices.defaultOutput}`);
  check(pa.devices.defaultInput >= -1 && pa.devices.defaultInput < pa.devices.length, `devices.defaultInput === ${pa.devices.defaultInput}`);

  for(let i = 0; i < pa.devices.length; i++) {
    const d = pa.devices[i];
    check(typeof d.name === 'string' && d.maxInputChannels >= 0 && d.maxOutputChannels >= 0 && d.hostApi >= 0 && d.hostApi < pa.hostApis.length, `devices[${i}] (${d.name}) has sane fields`);
  }
}

function testHostApis(defaultOutputHostApi) {
  console.log('-- hostApis --');

  check(pa.hostApis.length > 0, `hostApis.length === ${pa.hostApis.length} (> 0)`);
  check(pa.hostApis.default >= 0 && pa.hostApis.default < pa.hostApis.length, `hostApis.default === ${pa.hostApis.default}`);

  for(let i = 0; i < pa.hostApis.length; i++) {
    const h = pa.hostApis[i];
    check(typeof h.name === 'string' && typeof h.typeName === 'string' && h.deviceCount >= 0, `hostApis[${i}] (${h.name}/${h.typeName}) has sane fields`);
  }

  const defaultApi = pa.hostApis[defaultOutputHostApi];
  check(defaultApi.defaultOutputDevice === pa.devices.defaultOutput, 'hostApis[devices[defaultOutput].hostApi].defaultOutputDevice round-trips to devices.defaultOutput');
}

// Split out from testDevices()/testHostApis() above and run last, once both
// collections have already been fully exercised - see the note above.
function testDeviceInfoAndHostApiInfoConstructors() {
  console.log('-- PaDeviceInfo / HostApiInfo constructors --');

  const outDev = pa.devices.defaultOutput;
  const viaDeviceCtor = new pa.PaDeviceInfo(outDev);
  check(viaDeviceCtor.name === pa.devices[outDev].name, 'new PaDeviceInfo(index) matches devices[index]');

  const viaHostApiCtor = new pa.HostApiInfo(pa.hostApis.default);
  check(viaHostApiCtor.name === pa.hostApis[pa.hostApis.default].name, 'new HostApiInfo(index) matches hostApis[index]');
}

function testIsFormatSupported() {
  console.log('-- PaStream.isFormatSupported --');

  const goodOutput = { device: pa.devices.defaultOutput, channelCount: 2, sampleFormat: pa.paFloat32, suggestedLatency: 0.02 };
  let threw = false;
  try {
    pa.PaStream.isFormatSupported(null, goodOutput, SR);
  } catch(e) {
    threw = true;
  }
  check(!threw, `isFormatSupported(null, {device: defaultOutput, channelCount: 2}, ${SR}) does not throw`);

  throws(() => pa.PaStream.isFormatSupported(null, { device: pa.devices.defaultOutput, channelCount: 1024 }, SR), 'isFormatSupported rejects an absurd channel count');
}

function testConstructorArgumentHandling() {
  console.log('-- PaStream constructor argument handling (failing calls only - see header) --');

  throws(() => new pa.PaStream(0, 1024, pa.paFloat32, SR), 'positional form: absurd channel count throws');
  throws(() => new pa.PaStream({ output: { device: -2, channelCount: 2 }, sampleRate: SR }), 'object form: invalid device throws');
  throws(() => new pa.PaStream({ output: { device: pa.devices.defaultOutput, channelCount: 1024 }, sampleRate: SR }), 'object form: absurd channel count throws');
}

function testRealStream() {
  console.log(`-- one real full-duplex PaStream (object-form constructor), ${SECONDS}s @ ${SR}Hz mono --`);

  const params = (device) => ({ device, channelCount: 1, sampleFormat: pa.paFloat32, suggestedLatency: 0.02 });
  const stream = new pa.PaStream({
    input: params(pa.devices.defaultInput),
    output: params(pa.devices.defaultOutput),
    sampleRate: SR,
    framesPerBuffer: CHUNK_FRAMES,
  });
  check(true, 'object-form new PaStream({input, output, sampleRate, framesPerBuffer}) succeeded');

  check(stream.stopped === true, 'stream.stopped === true before start()');
  check(stream.active === false, 'stream.active === false before start()');
  check(typeof stream.inputLatency === 'number', 'stream.inputLatency is a number');
  check(typeof stream.outputLatency === 'number', 'stream.outputLatency is a number');
  check(typeof stream.sampleRate === 'number', 'stream.sampleRate is a number');
  if(stream.hostApiType !== undefined)
    check(typeof stream.hostApiType === 'string', `stream.hostApiType === ${JSON.stringify(stream.hostApiType)}`);

  stream.start();
  check(stream.active === true, 'stream.active === true after start()');
  check(stream.stopped === false, 'stream.stopped === false after start()');
  check(typeof stream.time === 'number', 'stream.time is a number');
  check(typeof stream.cpuLoad === 'number', 'stream.cpuLoad is a number');

  check(typeof stream.readAvailable === 'number', 'stream.readAvailable is a number');
  check(typeof stream.writeAvailable === 'number', 'stream.writeAvailable is a number');

  const total = SR * SECONDS;
  const recorded = new Float32Array(total);
  const chunk = new Float32Array(CHUNK_FRAMES);

  let written = 0;
  while(written < total) {
    const frames = Math.min(CHUNK_FRAMES, total - written);
    stream.read(chunk, frames); // capture from mic
    stream.write(chunk, frames); // play back live
    recorded.set(chunk.subarray(0, frames), written);
    written += frames;
  }

  stream.stop();
  check(stream.stopped === true, 'stream.stopped === true after stop()');

  stream.close();
  stream.close(); // idempotent, must not throw
  check(true, 'close() twice is a no-op, not an error');

  throws(() => stream.start(), 'start() after close() throws');
  throws(() => stream.read(chunk), 'read() after close() throws');
  throws(() => stream.write(chunk), 'write() after close() throws');

  let peak = 0, sumSq = 0;
  for(let i = 0; i < recorded.length; i++) {
    const a = Math.abs(recorded[i]);
    if(a > peak) peak = a;
    sumSq += recorded[i] * recorded[i];
  }
  console.log(`  peak=${peak.toFixed(4)} rms=${Math.sqrt(sumSq / recorded.length).toFixed(4)}`);

  const outPath = 'portaudio-test.wav';
  writeWavMono(outPath, recorded, SR);
  console.log(`  wrote ${outPath}`);
}

function main() {
  pa.Pa_Initialize();

  const defaultOutputHostApi = pa.devices[pa.devices.defaultOutput].hostApi;

  testModuleLevel();
  testDevices();
  testHostApis(defaultOutputHostApi);
  testDeviceInfoAndHostApiInfoConstructors();
  testIsFormatSupported();
  testConstructorArgumentHandling();
  testRealStream();

  pa.Pa_Terminate();

  console.log(`\n${passed} passed, ${failed} failed`);
  if(failed > 0)
    std.exit(1);
}

main();
