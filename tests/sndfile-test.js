import { SndFile, SFM_READ, SFM_WRITE, SF_FORMAT_WAV, SF_FORMAT_PCM_16 } from 'sndfile';
import { SampleRateConverter, SRC_SINC_BEST_QUALITY } from 'samplerate';
import { SoundTouch } from 'soundtouch';
import { AubioPitch } from 'aubio';

const TARGET_RATE = 48000;
const CHUNK = 4096; // frames per iteration

const src = new SndFile('in.wav', SFM_READ);
const { channels, samplerate, format } = src;
const ratio = TARGET_RATE / samplerate;

const dst = new SndFile('out.wav', SFM_WRITE, {
  samplerate: TARGET_RATE, channels,
  format: SF_FORMAT_WAV | format,
});

const conv = new SampleRateConverter(SRC_SINC_BEST_QUALITY, channels);
const st = new SoundTouch(TARGET_RATE, channels);
st.pitchSemitones = 2; // "do something interesting": shift up two semitones
const pitch = new AubioPitch('yinfft', CHUNK, CHUNK, TARGET_RATE); // bufSize == hopSize: one hop per chunk here

// Allocated once, reused for the whole file - this is the entire
// working-buffer footprint of the pipeline.
const inBuf = new Float32Array(CHUNK * channels);
const resampled = new Float32Array((Math.ceil(CHUNK * ratio) + 256) * channels); // headroom, see doc/native/samplerate.md
const outBuf = new Float32Array(CHUNK * channels);

let frames;
while ((frames = src.read(inBuf, CHUNK)) > 0) {
  const isLast = frames < CHUNK;
  const input = frames === CHUNK ? inBuf : inBuf.subarray(0, frames * channels);

  // 1. Resample straight into `resampled` - no allocation (doc/native/samplerate.md).
  const produced = conv.process(input, ratio, isLast, resampled);
  const resampledView = resampled.subarray(0, produced * channels);

  // 2. Tap `aubio` for analysis on the same buffer `soundtouch` is about
  //    to consume - process() only reads it, so this is free to do
  //    in-line without disturbing the pipeline (mono only; a real
  //    multi-channel script would mix down first).
  if (channels === 1 && produced === CHUNK) {
    const [hz] = pitch.process(resampledView);
    if (hz > 0) console.log(`~${hz.toFixed(1)}Hz`);
  }

  // 3. Push into SoundTouch, flush on the last chunk.
  st.putSamples(resampledView);
  if (isLast) st.flush();

  // 4. Pull whatever SoundTouch has ready straight into `outBuf`, write
  //    it straight out - both zero-copy.
  let got;
  while ((got = st.receiveSamples(outBuf)) > 0)
    dst.write(outBuf, got);
}

src.close();
dst.close();
conv.close();