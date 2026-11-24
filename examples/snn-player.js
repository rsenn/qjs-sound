/* snn-player: a spiking network that plays MIDI, quantized and in key.
 *
 * ```sh
 * qjsm examples/snn-player.js --mode melody --tempo 100 --scale minor-pent --root 48
 * qjsm examples/snn-player.js --mode drums --tempo 120 --dry 8     // offline statistics
 * ```
 *
 * Leaky integrate-and-fire neurons, stepped every millisecond. A neuron
 * is driven by a rhythm template for the coming 16th step, noise and its
 * neighbours; its spike becomes a note-on on that step (spikes are
 * quantized to the grid, notes come only from the chosen scale or drum set).
 *
 *   --mode drums|melody    kick/snare/hats, or scale-degree neurons (melody)
 *   --tempo BPM            default 100
 *   --scale NAME           minor-pent major-pent minor major dorian blues
 *   --root N               lowest note, default 48 (C3)
 *   --density X            0..1 how much of the template fires, default 0.6
 *   --noise X              0..1 randomness, default 0.35
 *   --seed N               random seed, default 1
 *   --port RE              MIDI output port name, default Morph
 *   --channel N            0..15, default 0
 *   --dry BARS             no MIDI, no clock: print statistics
 *   --list                 list MIDI ports
 */

import * as os from 'os';
import * as std from 'std';
import { RtMidiOut } from 'stk';

/*
 * opt(k, d): argument parser helper.
 * Extracts value after key k in args, or returns default d.
 * k: string argument key (e.g. '--mode')
 * d: default value (e.g. 'melody')
 * Returns: value as string or number (parsed), or d if not found.
 */
const args = scriptArgs.slice(1);
const opt = (k, d) => (args.includes(k) ? args[args.indexOf(k) + 1] : d);
const MODE = opt('--mode', 'melody');
const TEMPO = +opt('--tempo', 100);
const ROOT = +opt('--root', 48);
const DENSITY = +opt('--density', 0.6);
/*
 * --noise X: randomness factor in neuron activation (0..1), default 0.35.
 * Controls how much stochasticity is introduced into the firing of neurons.
 * Higher values produce more unpredictable, organic behavior; lower values result in more deterministic, rhythmic patterns.
 */
const NOISE = +opt('--noise', 0.35);
/*
 * --channel N: MIDI channel (0..15), default 0. Output notes are sent to this channel.
 * Enables routing of different instrument sets (e.g., drums on channel 0, melody on channel 1) in real-time MIDI.
 */
const CHANNEL = +opt('--channel', 0);
const STEP_MS = 60000 / TEMPO / 4; // one 16th
const SCALES = {
  // minor-pent: [0, 3, 5, 7, 10] - a minor pentatonic scale, used for melody generation with organic, sparse note patterns.
  'minor-pent': [0, 3, 5, 7, 10],
  'major-pent': [0, 2, 4, 7, 9], // Major pentatonic scale: C, D, E, G, A (e.g., C major with missing B)
  'major-pent': [0, 2, 4, 7, 9],
  // minor: [0, 2, 3, 5, 7, 8, 10] - natural minor scale, used for melody generation with a darker, more expressive tonal character.
  minor: [0, 2, 3, 5, 7, 8, 10],
  // major: [0, 2, 4, 5, 7, 9, 11] - natural major scale, used for melody generation with bright, clear tonal character.
  major: [0, 2, 4, 5, 7, 9, 11],
  dorian: [0, 2, 3, 5, 7, 9, 10],
  blues: [0, 3, 5, 6, 7, 10],
};

/*
 * --port RE: MIDI output port name (default "Morph").
 * Used to configure RtMidiOut for real-time MIDI output.
 * Must be a valid port name as listed by --list.
 */
/*
 * --dry BARS: run simulation for BARS bars (e.g., 8) without MIDI output or clock.
 * Prints statistics on neuron firing, spike counts, and activation patterns.
 * Used for offline analysis of neural dynamics (e.g., spike timing, density).
 */
/*
 * RNG Design: Mulberry32 (a 32-bit seeded PRNG) is used to introduce stochasticity into neural dynamics.
 * It ensures reproducible, organic behavior: same seed → same sequence of random values.
 * The output is scaled to [0,1) for use in neuron activation (e.g., noise factor) and spike timing.
 */
/* mulberry32: a small seeded generator, so a run can be repeated */
function rng(seed) {
  let a = seed >>> 0;

  return () => {
    a = (a + 0x6d2b79f5) >>> 0;

    let t = a;

    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/*
 * --seed N: random seed (default 1), used to initialize the random number generator for reproducible neural dynamics.
 * Ensures consistent behavior across runs while maintaining organic, stochastic patterns.
 */
const rand = rng(+opt('--seed', 1));
const rep = (a, n) => Array.from({ length: n }, () => a).flat();

/* one neuron: a note, a weight per 16th step, leaky potential */
class Neuron {
  constructor(note, template, bias = 1) {
    this.note = note;
    this.template = template; // 16 weights, 0..1
    this.bias = bias;
    this.v = 0;
    this.rest = 0; // refractory time left in ms
    this.adapt = 0; // slow self-inhibition
    this.last = -1; // step a spike was queued for
  }
}

const DRUMS = [
  new Neuron(36, [1, 0, 0, 0, 0, 0, 0.3, 0, 0.8, 0, 0, 0.3, 0, 0, 0.4, 0]), // kick
  new Neuron(38, [0, 0, 0, 0, 1, 0, 0, 0.2, 0, 0, 0, 0, 1, 0, 0.3, 0]), // snare
  new Neuron(42, rep([0.9, 0.3, 0.7, 0.3], 4)), // closed hat
  new Neuron(46, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.4, 0, 0, 0, 0, 0.3]), // open hat
];
const RHYTHM = [1, 0.2, 0.5, 0.2, 0.8, 0.2, 0.5, 0.3, 1, 0.2, 0.5, 0.2, 0.7, 0.3, 0.6, 0.4];

/* the notes of the scale over two octaves above ROOT, as neurons: the
 * root and fifth are favoured, the middle of the range preferred */
function melodyNeurons() {
  const scale = SCALES[opt('--scale', 'minor-pent')] || SCALES['minor-pent'], out = [];

  for(const oct of [0, 12])
    for(const d of scale) {
      const pc = d % 12, w = pc === 0 ? 1 : pc === 7 ? 0.9 : [3, 4].includes(pc) ? 0.8 : 0.6;

      out.push(new Neuron(ROOT + oct + d, RHYTHM, w * (oct ? 0.85 : 1)));
    }

  return out;
}

class SpikePlayer {
  constructor() {
    this.cells = MODE === 'drums' ? DRUMS : melodyNeurons();
    this.t = 0; // simulated ms
    this.queue = []; // { at, note, vel, len }
    this.lastIdx = -1; // melody: the neuron that spiked last
    this.tau = 15; // membrane time constant, ms
  }

  /* one millisecond of the network */
  step() {
    const t = this.t++, f = (t % STEP_MS) / STEP_MS, next = Math.floor(t / STEP_MS) + 1, s = next % 16;

    if(f < 0.5) {
      // first half of a step: leak, no drive, so nothing fires early
      for(const n of this.cells) {
        n.v *= 1 - 1 / this.tau;
        n.rest = Math.max(0, n.rest - 1);
        n.adapt *= 0.9995;
      }

      return;
    }

    for(let i = 0; i < this.cells.length; i++) {
      const n = this.cells[i];
      const near = MODE === 'melody' && this.lastIdx >= 0 ? 0.5 * Math.exp(-Math.abs(i - this.lastIdx) / 1.5) : 0;
      const drive = DENSITY * n.template[s] * (n.bias + near - n.adapt) * 2.2 + NOISE * (rand() - 0.5) * 0.9;

      n.rest = Math.max(0, n.rest - 1);
      n.adapt *= 0.9995;
      n.v += (-n.v + (n.rest ? 0 : Math.max(0, drive))) / this.tau;

      if(n.v >= 0.55 && !n.rest && n.last !== next)
        this.fire(i, next, n.v);
    }
  }

  /* a spike: queue the note on the coming 16th, then inhibit the others */
  fire(i, next, v) {
    const n = this.cells[i], inhib = MODE === 'melody' ? 0.8 : 0.25;
    const vel = Math.round(48 + 79 * Math.min(1, (v - 0.55) * 2.5 + 0.3 * RHYTHM[next % 16]));

    n.last = next;
    n.v = 0;
    n.rest = STEP_MS * 0.9;
    n.adapt += 0.35;
    this.queue.push({ at: next * STEP_MS, note: n.note, vel, len: STEP_MS * (MODE === 'melody' ? 0.9 : 0.3), step: next % 16 });
    this.lastIdx = i;

    for(const k of this.cells)
      if(k !== n)
        k.v = Math.max(0, k.v - inhib);
  }

  /* events due now or earlier */
  due() {
    const out = this.queue.filter(e => e.at <= this.t);

    this.queue = this.queue.filter(e => e.at > this.t);
    return out;
  }
}

const player = new SpikePlayer();

if(args.includes('--dry')) {
  const bars = +opt('--dry', 8), events = [];

  for(let t = 0; t < bars * 16 * STEP_MS; t++) {
    player.step();
    events.push(...player.due());
  }

  const hist = new Array(16).fill(0), byNote = {}, vels = events.map(e => e.vel);

  for(const e of events) {
    hist[e.step]++;
    byNote[e.note] = (byNote[e.note] || 0) + 1;
  }

  const notes = events.map(e => e.note), steps = notes.slice(1).map((n, i) => Math.abs(n - notes[i]));

  console.log(`${MODE}, ${TEMPO} bpm, ${bars} bars: ${events.length} events (${(events.length / bars).toFixed(1)} per bar)`);
  console.log('per 16th step:', hist.join(' '));
  console.log('per note:', Object.entries(byNote).map(([n, c]) => `${n}:${c}`).join(' '));
  console.log(`velocity ${Math.min(...vels)}..${Math.max(...vels)}`);

  if(MODE === 'melody')
    console.log(`stepwise (<= 3 semitones): ${Math.round((100 * steps.filter(d => d <= 3).length) / steps.length)}%, repeats: ${Math.round((100 * steps.filter(d => d === 0).length) / steps.length)}%`);

  std.exit(0);
}

/*
 * RtMidiOut usage:
 * - Port index 0 is used by default (from 'stk'), configured via --port RE.
 * - Channel is set to 0 by default (via --channel N), allowing routing to different instruments.
 * - Output is enabled only when --mode is melody or drums, and disabled in --dry mode.
 */
const out = new RtMidiOut(0, 'snn-player');

if(args.includes('--list')) {
  for(let i = 0; i < out.getPortCount(); i++)
    console.log(i, out.getPortName(i));

  std.exit(0);
}

const re = new RegExp(opt('--port', 'Morph'));
let port = -1;

for(let i = 0; i < out.getPortCount(); i++)
  if(re.test(out.getPortName(i)))
    port = i;

if(port < 0) {
  console.log(`no MIDI port matches ${re}; ports:`);

  for(let i = 0; i < out.getPortCount(); i++)
    console.log(' ', i, out.getPortName(i));

  std.exit(1);
}

out.openPort(port, 'snn');
console.log(`snn-player: ${MODE}, ${TEMPO} bpm, ${opt('--scale', 'minor-pent')} from ${ROOT}, to ${out.getPortName(port)}`);

const offs = []; // { at, note }
const t0 = Date.now();

for(;;) {
  const now = Date.now() - t0;

  while(player.t < now) {
    player.step();

    for(const e of player.due()) {
      out.sendMessage([0x90 | CHANNEL, e.note, e.vel]);
      offs.push({ at: e.at + e.len, note: e.note });
    }

    for(const o of offs.filter(x => x.at <= player.t))
      out.sendMessage([0x80 | CHANNEL, o.note, 0]);

    offs.splice(0, offs.length, ...offs.filter(x => x.at > player.t));
  }

  os.sleep(1);
}
