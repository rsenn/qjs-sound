#!/usr/bin/env qjsm
/*
 * Build a FastTracker II / MilkyTracker .XI instrument from a WAV sample.
 *
 * Detects the recording's fundamental with AubioPitch (confirmed against a
 * zero-crossing-rate estimate), decides the nearest equal-tempered note, and
 * -- if it's off by more than CENTS_TOLERANCE -- retunes it by resampling
 * with libsamplerate's SRC_SINC_BEST_QUALITY. Per design: pitch correction is
 * done *only* by resampling the waveform itself, never via the .XI sample's
 * finetune field, so the written sample is always finetune=0 and its relnote
 * alone encodes which note it now sounds like relative to C-4.
 *
 * The .XI format itself has no sample-rate field at all -- relnote/finetune
 * only ever encode a pitch *offset* from C-4, assumed at whatever clock the
 * player already uses for that note. That's exactly why resampling (which
 * changes the frame count, i.e. how many samples make up one waveform cycle)
 * is the only way to change the pitch the *data itself* implies.
 *
 * Also builds multi-sample instruments with no pitch correction at all --
 * for material (drum hits, one-shots) where "detect and retune to a note"
 * doesn't apply. Each input sample becomes its own XMSample, mapped to one
 * successive semitone starting at --start-note, so a tracker can play back
 * different hits chromatically:
 *
 *   qjsm xi-build.js <input.wav> [output.xi]
 *       single sample, auto-detected & retuned to the nearest note,
 *       mapped across the whole keyboard (the original mode above).
 *
 *   qjsm xi-build.js --onset <loop.wav> [-o output.xi] [--start-note NOTE]
 *       cut <loop.wav> into hits via AubioOnset, one hit per semitone
 *       starting at NOTE (default C3), unmodified (no pitch correction).
 *
 *   qjsm xi-build.js --multi <file1> <file2> ... [-o output.xi] [--start-note NOTE]
 *       same chromatic mapping, using the given sample files directly
 *       instead of cutting a loop.
 *
 *   qjsm xi-build.js --dir <directory> [-o output.xi] [--start-note NOTE]
 *       same as --multi, using every supported sample file in <directory>
 *       (sorted by filename).
 */

import * as std from 'std';
import * as os from 'os';
import { SndFile, SFM_READ } from 'sndfile';
import { SampleRateConverter, SRC_SINC_BEST_QUALITY } from 'samplerate';
import { AubioPitch, AubioOnset } from 'aubio';

const CENTS_TOLERANCE = 5;
/* A larger analysis window materially improves yinfft's frequency
 * resolution (measured error against a known tone: ~11 cents at 1024/512,
 * ~0.7 cents at 4096/2048) -- needed since CENTS_TOLERANCE is itself only 5. */
const PITCH_BUF = 4096;
const PITCH_HOP = 2048;

/* Finer time resolution than pitch detection needs, since onset boundaries
 * become hard sample-accurate slice cuts. */
const ONSET_BUF = 1024;
const ONSET_HOP = 256;
const MIN_SLICE_SECONDS = 0.03; /* reject spurious double-triggers */

const DEFAULT_START_NOTE = 'C3';
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/* Every extension libsndfile (this project's `sndfile` module) can open --
 * matches xi-build-batch.sh's own EXT_PATTERN. */
const SAMPLE_EXT_RE = /\.(wav|wave|aif|aiff|au|snd|flac|ogg|oga|caf|w64)$/i;

function baseName(path) {
  return path.replace(/^.*\//, '').replace(/\.[^./]+$/, '');
}

/* Accepts a bare MIDI number, or a note name -- tracker-style "C-3" (hyphen
 * as a natural-note filler, not a minus sign) as well as "C3"/"C#3"/"Db3". */
function noteNameToMidi(s) {
  if(/^-?\d+$/.test(s))
    return parseInt(s, 10);
  const m = /^([A-Ga-g])(#|s|b)?-?(\d+)$/.exec(s);
  if(!m)
    throw new Error(`bad note name: ${s}`);
  const base = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[m[1].toUpperCase()];
  const acc = (m[2] === '#' || m[2] === 's') ? 1 : m[2] === 'b' ? -1 : 0;
  const octave = parseInt(m[3], 10);
  return base + acc + (octave + 1) * 12; /* MIDI 60 == C4, matches the relnote reference */
}

function midiToNoteName(note) {
  return `${NOTE_NAMES[((note % 12) + 12) % 12]}${Math.floor(note / 12) - 1}`;
}

function listSampleFiles(dir) {
  const [entries, err] = os.readdir(dir);
  if(err)
    throw new Error(`cannot read directory ${dir}: errno ${err}`);
  const clean = dir.replace(/\/$/, '');
  return entries
    .filter((e) => e !== '.' && e !== '..' && SAMPLE_EXT_RE.test(e))
    .sort()
    .map((e) => `${clean}/${e}`);
}

function readMono(path) {
  const info = SndFile.info(path);
  const sf = new SndFile(path, SFM_READ);
  const interleaved = new Float32Array(info.frames * info.channels);
  const got = sf.read(interleaved, info.frames);
  sf.close();

  if(info.channels === 1)
    return { mono: interleaved.subarray(0, got), sampleRate: info.samplerate };

  console.log(`${path}: ${info.channels} channels, downmixing to mono`);
  const mono = new Float32Array(got);
  for(let i = 0; i < got; i++) {
    let sum = 0;
    for(let c = 0; c < info.channels; c++)
      sum += interleaved[i * info.channels + c];
    mono[i] = sum / info.channels;
  }
  return { mono, sampleRate: info.samplerate };
}

/* Median of voiced hops (hz > 0, aubio's own "no pitch found" sentinel) --
 * robust against silence/attack noise at the sample's edges without needing
 * to locate a "sustain region". `.confidence` is intentionally not used as a
 * gate here: it reads back ~0 for `yin`/`yinfft` even on a clean synthetic
 * sine in this build (see BUGS), so it isn't a reliable per-hop filter --
 * the median over many hops already discards the occasional outlier. */
function detectPitchAubio(mono, sampleRate) {
  const pitch = new AubioPitch('yinfft', PITCH_BUF, PITCH_HOP, sampleRate);
  pitch.setUnit('Hz');

  const hop = new Float32Array(PITCH_HOP);
  const readings = [];
  for(let offset = 0; offset < mono.length; offset += PITCH_HOP) {
    hop.fill(0);
    hop.set(mono.subarray(offset, Math.min(offset + PITCH_HOP, mono.length)));
    const [hz] = pitch.process(hop);
    if(hz > 0)
      readings.push(hz);
  }

  if(!readings.length)
    throw new Error('AubioPitch found no voiced pitch in this sample');

  readings.sort((a, b) => a - b);
  return readings[readings.length >> 1];
}

function zeroCrossingHz(mono, sampleRate) {
  let crossings = 0;
  for(let i = 1; i < mono.length; i++)
    if((mono[i - 1] < 0) !== (mono[i] < 0))
      crossings++;
  return (crossings / 2) * sampleRate / mono.length;
}

/* Onset positions mark the START of each hit, so slices run
 * [onset_i, onset_{i+1}), with the last slice extending to end of file --
 * any audio before the first onset (pre-roll silence) is dropped. */
function cutOnsets(mono, sampleRate) {
  const onset = new AubioOnset('default', ONSET_BUF, ONSET_HOP, sampleRate);
  const hop = new Float32Array(ONSET_HOP);
  const positions = [];
  for(let offset = 0; offset < mono.length; offset += ONSET_HOP) {
    hop.fill(0);
    hop.set(mono.subarray(offset, Math.min(offset + ONSET_HOP, mono.length)));
    const [v] = onset.process(hop);
    if(v !== 0)
      positions.push(onset.last);
  }
  if(!positions.length)
    throw new Error('AubioOnset found no onsets in this loop');

  const minSlice = Math.round(sampleRate * MIN_SLICE_SECONDS);
  const slices = [];
  for(let i = 0; i < positions.length; i++) {
    const start = positions[i];
    const end = i + 1 < positions.length ? positions[i + 1] : mono.length;
    if(end - start >= minSlice)
      slices.push(mono.subarray(start, end));
  }
  if(!slices.length)
    throw new Error('all detected onset slices were shorter than the minimum hit length');
  return slices;
}

function hzToMidi(hz) {
  return 69 + 12 * Math.log2(hz / 440);
}

function midiToHz(note) {
  return 440 * Math.pow(2, (note - 69) / 12);
}

function floatToInt16(buf) {
  const out = new Int16Array(buf.length);
  for(let i = 0; i < buf.length; i++)
    out[i] = Math.round(Math.max(-1, Math.min(1, buf[i])) * 32767);
  return out;
}

/* Classic MOD/XM/S3M sample compression: each stored value is the
 * difference from the previous *decoded* sample, wrapping mod 65536 --
 * TypedArray element assignment already performs that wraparound (ToInt16),
 * so no explicit masking is needed here. */
function deltaEncode16(int16) {
  const out = new Int16Array(int16.length);
  let prev = 0;
  for(let i = 0; i < int16.length; i++) {
    out[i] = int16[i] - prev;
    prev = int16[i];
  }
  return out;
}

function padString(str, len) {
  const out = new Uint8Array(len);
  for(let i = 0; i < Math.min(str.length, len); i++)
    out[i] = str.charCodeAt(i);
  return out;
}

/* `samples`: array of { pcm16 (Int16Array), relnote (int8), name }.
 * `sampleMap`: Uint8Array(96), sampleMap[midiNote - 12] = sample index --
 * left all-zero (every key -> sample 0) for the single-sample tuned mode,
 * populated per-key for the chromatic multi-sample modes. */
function buildXiMulti(name, samples, sampleMap) {
  const HEADER_SIZE = 21 + 22 + 1 + 20 + 2 + 230 + 2;
  const SAMPLE_HEADER_SIZE = 40;
  const totalDataBytes = samples.reduce((sum, s) => sum + s.pcm16.length * 2, 0);
  const buf = new ArrayBuffer(HEADER_SIZE + SAMPLE_HEADER_SIZE * samples.length + totalDataBytes);
  const dv = new DataView(buf);
  const bytes = new Uint8Array(buf);
  let off = 0;

  const putBytes = (arr) => { bytes.set(arr, off); off += arr.length; };
  const putStr = (s, len) => putBytes(padString(s, len));
  const putU8 = (v) => { dv.setUint8(off, v); off += 1; };
  const putU16 = (v) => { dv.setUint16(off, v, true); off += 2; };
  const putU32 = (v) => { dv.setUint32(off, v, true); off += 4; };
  const putI8 = (v) => { dv.setInt8(off, v); off += 1; };

  /* -- XIInstrumentHeader preamble (66 bytes) -- */
  putStr('Extended Instrument: ', 21);
  putStr(name, 22);
  putU8(0x1a); /* eof */
  putStr('qjs-sound', 20); /* trackerName */
  putU16(0x0102); /* version 1.02 */

  /* -- embedded XMInstrument (230 bytes) -- */
  putBytes(sampleMap); /* sampleMap[96] */
  off += 24 * 2; /* volEnv[24] u16, unused (no envelope) */
  off += 24 * 2; /* panEnv[24] u16, unused */
  putU8(0); /* volPoints */
  putU8(0); /* panPoints */
  putU8(0); /* volSustain */
  putU8(0); /* volLoopStart */
  putU8(0); /* volLoopEnd */
  putU8(0); /* panSustain */
  putU8(0); /* panLoopStart */
  putU8(0); /* panLoopEnd */
  putU8(0); /* volFlags: envelope off */
  putU8(0); /* panFlags: envelope off */
  putU8(0); /* vibType */
  putU8(0); /* vibSweep */
  putU8(0); /* vibDepth */
  putU8(0); /* vibRate */
  putU16(0); /* volFade: no fadeout */
  putU8(0); /* midiEnabled */
  putU8(0); /* midiChannel */
  putU16(0); /* midiProgram */
  putU16(0); /* pitchWheelRange */
  putU8(0); /* muteComputer */
  off += 15; /* reserved[15] */

  putU16(samples.length); /* numSamples */

  /* -- XMSample headers (40 bytes each), all headers before any sample
   * data, per OpenMPT's own ReadXIInstrument loop order -- */
  for(const s of samples) {
    putU32(s.pcm16.length * 2); /* length, in bytes */
    putU32(0); /* loopStart */
    putU32(0); /* loopLength */
    putU8(64); /* vol: max */
    putI8(0); /* finetune: always 0, tuning is done by resampling only */
    putU8(0x10); /* flags: sample16Bit, no loop */
    putU8(128); /* pan: center */
    putI8(s.relnote);
    putU8(0); /* reserved */
    putStr(s.name, 22);
  }

  /* -- sample data: 16-bit mono, delta-encoded, little-endian -- */
  for(const s of samples) {
    const delta = deltaEncode16(s.pcm16);
    for(let i = 0; i < delta.length; i++)
      putU16(delta[i] & 0xffff);
  }

  return buf;
}

function writeXi(outPath, buf) {
  const f = std.open(outPath, 'wb');
  f.write(buf, 0, buf.byteLength);
  f.close();
}

function parseArgs() {
  const opts = { mode: 'single', startNote: DEFAULT_START_NOTE, out: null, positional: [] };
  const args = scriptArgs.slice(1);
  for(let i = 0; i < args.length; i++) {
    const a = args[i];
    if(a === '--onset')
      opts.mode = 'onset';
    else if(a === '--multi')
      opts.mode = 'multi';
    else if(a === '--dir')
      opts.mode = 'dir';
    else if(a === '-o' || a === '--out')
      opts.out = args[++i];
    else if(a === '--start-note')
      opts.startNote = args[++i];
    else
      opts.positional.push(a);
  }
  return opts;
}

/* Original single-sample mode: detect the fundamental, retune by
 * resampling if it's off by more than CENTS_TOLERANCE, map across the
 * whole keyboard (sampleMap left all-zero). */
function buildTunedSingleSample(inPath) {
  const name = baseName(inPath);
  const { mono, sampleRate } = readMono(inPath);
  console.log(`${inPath}: ${mono.length} frames @ ${sampleRate}Hz (${(mono.length / sampleRate).toFixed(2)}s)`);

  const hzAubio = detectPitchAubio(mono, sampleRate);
  const hzZeroCross = zeroCrossingHz(mono, sampleRate);
  const disagreementCents = 1200 * Math.log2(hzZeroCross / hzAubio);
  if(Math.abs(disagreementCents) > 50)
    console.log(`warning: zero-crossing estimate (${hzZeroCross.toFixed(1)}Hz) disagrees with aubio ` +
      `(${hzAubio.toFixed(1)}Hz) by ${disagreementCents.toFixed(0)} cents -- possible octave error, using aubio's value`);

  const detectedHz = hzAubio;
  const targetNote = Math.round(hzToMidi(detectedHz));
  const targetHz = midiToHz(targetNote);
  const cents = 1200 * Math.log2(detectedHz / targetHz);

  console.log(`detected ~${detectedHz.toFixed(2)}Hz (zero-cross ~${hzZeroCross.toFixed(2)}Hz), ` +
    `nearest note MIDI ${targetNote} (${targetHz.toFixed(2)}Hz), ${cents.toFixed(1)} cents off`);

  let tuned = mono;
  if(Math.abs(cents) >= CENTS_TOLERANCE) {
    const ratio = detectedHz / targetHz;
    tuned = SampleRateConverter.simple(mono, ratio, SRC_SINC_BEST_QUALITY, 1);
    console.log(`retuning: resampling by ratio ${ratio.toFixed(6)} (${mono.length} -> ${tuned.length} frames)`);
  } else {
    console.log('already in tune within tolerance, no resampling needed');
  }

  const relnote = targetNote - 60; /* 60 = MIDI middle C = XM's C-4 reference */
  return { name, samples: [{ pcm16: floatToInt16(tuned), relnote, name }], sampleMap: new Uint8Array(96) };
}

/* Chromatic multi-sample mapping, no pitch correction -- appropriate for
 * unpitched material (drum hits, one-shots) where "detect and retune to a
 * note" doesn't apply. sampleMap index = midiNote - 12 (index 48 == C4,
 * confirmed against OpenMPT's own ReadXIInstrument reference to
 * sampleMap[48] as its "middle C" slot). */
function buildChromaticMap(monoBuffers, names, startNote) {
  const lastIndex = startNote - 12 + monoBuffers.length - 1;
  if(startNote - 12 < 0 || lastIndex > 95)
    throw new Error(`${monoBuffers.length} samples starting at ${midiToNoteName(startNote)} ` +
      `don't fit in the 96-note range -- pick a lower --start-note or fewer samples`);

  const sampleMap = new Uint8Array(96);
  const samples = monoBuffers.map((mono, i) => {
    const note = startNote + i;
    sampleMap[note - 12] = i;
    return { pcm16: floatToInt16(mono), relnote: note - 60, name: names[i] };
  });

  console.log(`mapped ${samples.length} sample(s) chromatically from ${midiToNoteName(startNote)} ` +
    `to ${midiToNoteName(startNote + samples.length - 1)}`);
  return { samples, sampleMap };
}

function main() {
  const opts = parseArgs();
  if(!opts.positional.length) {
    console.log('usage: xi-build.js <input.wav> [output.xi]');
    console.log('       xi-build.js --onset <loop.wav> [-o output.xi] [--start-note NOTE]');
    console.log('       xi-build.js --multi <file1> <file2> ... [-o output.xi] [--start-note NOTE]');
    console.log('       xi-build.js --dir <directory> [-o output.xi] [--start-note NOTE]');
    std.exit(1);
  }

  const startNote = noteNameToMidi(opts.startNote);

  if(opts.mode === 'onset') {
    const loopPath = opts.positional[0];
    const outPath = opts.out || `${baseName(loopPath)}.xi`;
    const { mono, sampleRate } = readMono(loopPath);
    console.log(`${loopPath}: ${mono.length} frames @ ${sampleRate}Hz, detecting onsets...`);

    const slices = cutOnsets(mono, sampleRate);
    console.log(`found ${slices.length} hit(s)`);

    const names = slices.map((_, i) => `hit${i + 1}`);
    const { samples, sampleMap } = buildChromaticMap(slices, names, startNote);
    writeXi(outPath, buildXiMulti(baseName(loopPath), samples, sampleMap));
    console.log(`wrote ${outPath}`);
    return;
  }

  if(opts.mode === 'multi' || opts.mode === 'dir') {
    const files = opts.mode === 'dir' ? listSampleFiles(opts.positional[0]) : opts.positional;
    if(!files.length)
      throw new Error('no sample files to build from');

    const instrumentName = opts.mode === 'dir' ? baseName(opts.positional[0].replace(/\/$/, '')) : 'multi-instrument';
    const outPath = opts.out || `${instrumentName}.xi`;

    const loaded = files.map((f) => ({ ...readMono(f), name: baseName(f) }));
    const rates = new Set(loaded.map((l) => l.sampleRate));
    if(rates.size > 1)
      console.log(`warning: input files have differing sample rates (${[...rates].join(', ')}Hz) -- ` +
        `not resampled in multi-sample mode, relative pitch/speed between samples may be off`);

    const { samples, sampleMap } = buildChromaticMap(loaded.map((l) => l.mono), loaded.map((l) => l.name), startNote);
    writeXi(outPath, buildXiMulti(instrumentName, samples, sampleMap));
    console.log(`wrote ${outPath}`);
    return;
  }

  /* default: single-sample pitch-corrected mode */
  const inPath = opts.positional[0];
  const outPath = opts.out || opts.positional[1] || `${baseName(inPath)}.xi`;
  const { name, samples, sampleMap } = buildTunedSingleSample(inPath);
  writeXi(outPath, buildXiMulti(name, samples, sampleMap));
  console.log(`wrote ${outPath} (relnote ${samples[0].relnote}, finetune 0)`);
}

main();
