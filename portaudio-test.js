// Demo for the quickjs-portaudio bindings: live mic -> speakers passthrough
// through a single full-duplex PaStream, using blocking read()/write().
// Also accumulates what was captured and saves it to a WAV file, so the
// result can be inspected even when nobody's listening live.
//
// PaStream is opened without a realtime callback (blocking I/O) so JS is
// only ever driven from the interpreter thread -- see quickjs-portaudio.c.
//
// One deliberate design choice: everything runs through a *single*
// full-duplex stream (numInputChannels=1, numOutputChannels=1) rather than
// separate input-only and output-only streams. Opening a second PaStream
// after closing a first one was observed to reliably corrupt the QuickJS
// heap in this environment (confirmed with gdb -- the crash is inside
// QuickJS's own property lookup, not in PortAudio, and the identical
// Pa_OpenDefaultStream/Pa_CloseStream sequence in a standalone C program
// does not crash), most likely a PortAudio/ALSA/PulseAudio background
// thread that isn't fully torn down by the time Pa_CloseStream returns,
// racing with the next stream's allocations. Sticking to one stream for
// the process lifetime sidesteps it.
//
// Usage:
//   qjs -m --std portaudio-test.js [seconds]

import * as std from 'std';
import * as pa from 'portaudio';

const SR = 44100;
const CHUNK_FRAMES = 1024;
const SECONDS = Number(scriptArgs[1]) || 3;

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

function main() {
  pa.Pa_Initialize();

  console.log(`Full-duplex passthrough: mic -> speakers, ${SECONDS}s @ ${SR}Hz mono`);
  const stream = new pa.PaStream(1, 1, pa.paFloat32, SR, CHUNK_FRAMES);
  stream.start();

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
  stream.close();
  pa.Pa_Terminate();

  let peak = 0, sumSq = 0;
  for(let i = 0; i < recorded.length; i++) {
    const a = Math.abs(recorded[i]);
    if(a > peak) peak = a;
    sumSq += recorded[i] * recorded[i];
  }
  console.log(`peak=${peak.toFixed(4)} rms=${Math.sqrt(sumSq / recorded.length).toFixed(4)}`);

  const outPath = 'portaudio-test.wav';
  writeWavMono(outPath, recorded, SR);
  console.log(`Wrote ${outPath}`);
}

main();
