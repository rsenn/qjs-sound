/* audio-report: musical report of a 16-bit mono wav, from aubio.
 *
 * ```sh
 * QUICKJS_MODULE_PATH=build/x86_64-linux-gnu qjsm examples/audio-report.js REC.wav [--slice 10]
 * ```
 *
 * prints, as text a person or Claude can read: length and level, a level
 * and pitch line per slice, onsets, a tempo guess, and the pitch classes
 */

import * as std from 'std';
import { AubioOnset, AubioPitch } from 'aubio';

const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const path = scriptArgs[1];
const slice = scriptArgs.includes('--slice') ? +scriptArgs[scriptArgs.indexOf('--slice') + 1] : 10;
const SR = 44100, HOP = 256;

if(!path) {
  console.log('usage: audio-report.js REC.wav [--slice SECONDS]');
  std.exit(1);
}

/* the samples of a 16-bit mono wav as floats, header skipped */
function readWav(file) {
  const f = std.open(file, 'rb');

  f.seek(0, std.SEEK_END);

  const size = f.tell(), raw = new ArrayBuffer(size);

  f.seek(0, std.SEEK_SET);
  f.read(raw, 0, size);
  f.close();

  const s16 = new Int16Array(raw, 44, (size - 44) >> 1), out = new Float32Array(s16.length);

  for(let i = 0; i < out.length; i++)
    out[i] = s16[i] / 32768;

  return out;
}

const x = readWav(path);
const onset = new AubioOnset('default', 1024, HOP, SR);
const pitch = new AubioPitch('yinfft', 1024, HOP, SR);

pitch.setUnit('midi');
pitch.silence = -50;
onset.silence = -50;

const per = Math.round((slice * SR) / HOP), slices = [];
const classes = new Array(12).fill(0);
let cur = null, onsets = 0, peak = 0;
const at = []; // time of every onset in seconds

for(let h = 0; (h + 1) * HOP <= x.length; h++) {
  if(h % per === 0)
    slices.push((cur = { e: 0, n: 0, on: 0, midi: [] }));

  const hop = x.subarray(h * HOP, (h + 1) * HOP);

  for(const v of hop) {
    cur.e += v * v;
    peak = Math.max(peak, Math.abs(v));
  }

  cur.n += HOP;

  if(onset.process(hop)[0]) {
    cur.on++;
    onsets++;
    at.push(onset.lastS);
  }

  const [m] = pitch.process(hop);

  if(m > 20 && pitch.confidence > 0.7) {
    cur.midi.push(m);
    classes[Math.round(m) % 12]++;
  }
}

/* the median gap between onsets, as beats per minute */
const gaps = at.slice(1).map((t, i) => t - at[i]).filter(g => g > 0.1 && g < 2);
const bpm = gaps.length ? 60 / gaps.slice().sort((p, q) => p - q)[gaps.length >> 1] : 0;
const db = e => (e > 0 ? (10 * Math.log10(e)).toFixed(0) : '-inf');
const median = a => (a.length ? a.slice().sort((p, q) => p - q)[a.length >> 1] : null);
const name = m => (m === null ? '-' : NAMES[Math.round(m) % 12] + (Math.floor(Math.round(m) / 12) - 1));

console.log(`${path}: ${(x.length / SR).toFixed(1)} s, peak ${peak.toFixed(2)}, ${onsets} onsets, tempo about ${bpm ? bpm.toFixed(0) + ' bpm' : '?'} (median onset interval)`);
console.log('slice   level(dB)  onsets  pitch (median of confident frames)');

slices.forEach((s, i) => {
  const md = median(s.midi);

  console.log(`${String(i * slice).padStart(4)} s  ${db(s.e / s.n).padStart(6)}  ${String(s.on).padStart(6)}  ${name(md)}${md === null ? '' : ` (${s.midi.length} frames)`}`);
});

const total = classes.reduce((a, b) => a + b, 0);

console.log('pitch classes: ' + (total ? classes.map((c, i) => `${NAMES[i]} ${Math.round((100 * c) / total)}%`).filter((_, i) => classes[i]).join(', ') : 'none confident'));
