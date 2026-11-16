/*
 * Advanced demo: PortMidi input driving multiple STK instruments,
 * one per MIDI channel. Each instrument exposes all its CC-controllable
 * parameters via standard MIDI Control Change messages.
 *
 * Instruments per channel:
 *   Ch 1 (0): Flute          - jet delay, end reflection, noise, vibrato, pressure
 *   Ch 2 (1): BlowBotl       - noise, vibrato, mod wheel (pressure), aftertouch
 *   Ch 3 (2): BlowHole       - reed stiffness, tonehole, vent, noise, vibrato, mod wheel, aftertouch
 *   Ch 4 (3): Whistle        - noise, fipple freq/gain mod, blow freq mod, tick size, can loss, mod wheel, sustain
 *   Ch 5 (4): Rhodey (FM)    - FM family CCs (via StkFM controlChange)
 *   Ch 6 (5): TubeBell (FM)  - FM family CCs (via StkFM controlChange)
 *   Ch 7 (6): Guitar         - coupling, pluck pos, loop gain/damping, mod wheel filter, aftertouch
 *   Ch 8 (7): Plucked        - generic plucked string (StkInstrmnt CCs)
 *   Ch 9 (8): Brass          - generic brass (StkInstrmnt CCs)
 *   Ch 10 (9): Clarinet      - generic clarinet (StkInstrmnt CCs)
 *   Ch 11 (10): Saxofony     - generic sax (StkInstrmnt CCs)
 *   Ch 12 (11): Bowed        - generic bowed string (StkInstrmnt CCs)
 *   Ch 13 (12): Recorder     - generic recorder (StkInstrmnt CCs)
 *   Ch 14 (13): Shakers      - generic shakers (StkInstrmnt CCs)
 *   Ch 15 (14): BandedWG     - generic banded waveguide (StkInstrmnt CCs)
 *   Ch 16 (15): Simple       - simple FM (StkInstrmnt CCs)
 *
 * Usage: qjsm tests/portmidi-stk-live-test.js
 * Connect a MIDI controller. Play notes on different channels.
 * Move CCs (mod wheel=CC1, CC2, CC4, CC11, CC64, CC128, etc.) to modulate.
 * Ctrl+C to stop.
 */

import * as std from 'std';
import { initialize, terminate, PortMidiStream, devices, PM_FILT_ACTIVE, PM_FILT_CLOCK } from 'portmidi';
import {
  Flute, BlowBotl, BlowHole, Whistle,
  Rhodey, TubeBell,
  Guitar, Plucked, Brass, Clarinet, Saxofony, Bowed, Recorder,
  Shakers, BandedWG, Simple,
  RtWvOut, Stk
} from 'stk';

const mtof = (n) => 440 * Math.pow(2, (n - 69) / 12);

const POLYPHONY_PER_CHANNEL = 4;
const N_CHANNELS = 16;
const LOWEST_FREQUENCY = 50;
const SAMPLE_RATE = 44100;
const BUFFER_FRAMES = 512;
const N_BUFFERS = 8;

function findInputDevice() {
  for(let i = 0; i < devices.length; i++)
    if(devices[i].input)
      return i;
  return -1;
}

/**
 * Voice pool for a monophonic STK instrument.
 * Round-robin/steal-oldest allocation.
 */
class VoicePool {
  constructor(InstrumentCtor, size, ...ctorArgs) {
    this.voices = Array.from({ length: size }, () => new InstrumentCtor(...ctorArgs));
    this.owner = new Array(size).fill(-1); // MIDI note currently held
    this.order = []; // voice indices, oldest-assigned first
  }

  noteOn(note, amplitude) {
    let slot = this.owner.indexOf(-1);
    if(slot < 0) {
      slot = this.order.shift();
    } else {
      this.order = this.order.filter((i) => i !== slot);
    }
    this.owner[slot] = note;
    this.order.push(slot);
    this.voices[slot].noteOn(mtof(note), amplitude);
  }

  noteOff(note, amplitude) {
    const slot = this.owner.indexOf(note);
    if(slot < 0)
      return;
    this.voices[slot].noteOff(amplitude);
    this.owner[slot] = -1;
    this.order = this.order.filter((i) => i !== slot);
  }

  controlChange(number, value) {
    for(const voice of this.voices)
      voice.controlChange(number, value);
  }

  tick() {
    let sample = 0;
    for(const voice of this.voices)
      sample += voice.tick();
    return sample / this.voices.length;
  }
}

/**
 * CC map for instruments that use stk::Instrmnt's controlChange
 * (Brass, Clarinet, Saxofony, Bowed, Recorder, Shakers, BandedWG, Simple, Plucked).
 * These inherit a common CC layout from STK's Instrmnt base:
 *   CC 2  = breath pressure / vibrato gain (varies)
 *   CC 4  = noise level / filter
 *   CC 11 = vibrato frequency / modulation
 *   CC 1  = mod wheel (pressure/filter)
 *   CC 128 = aftertouch
 */
const INSTRMNT_CC_MAP = {
  2: 'pressure',
  4: 'noise',
  11: 'vibratoFreq',
  1: 'modWheel',
  128: 'aftertouch'
};

/**
 * Instrument configuration: constructor, ctor args, and human-readable name.
 * For FM instruments (Rhodey, TubeBell), we use the StkFM class which has
 * its own controlChange implementation.
 */
const INSTRUMENT_CONFIGS = [
  // Ch 1: Flute (needs lowestFrequency arg)
  { ctor: Flute, args: [LOWEST_FREQUENCY], name: 'Flute' },
  // Ch 2: BlowBotl
  { ctor: BlowBotl, args: [], name: 'BlowBotl' },
  // Ch 3: BlowHole (needs lowestFrequency arg)
  { ctor: BlowHole, args: [LOWEST_FREQUENCY], name: 'BlowHole' },
  // Ch 4: Whistle
  { ctor: Whistle, args: [], name: 'Whistle' },
  // Ch 5: Rhodey (FM) - uses StkFM class, no lowestFrequency needed
  { ctor: Rhodey, args: [], name: 'Rhodey (FM)' },
  // Ch 6: TubeBell (FM) - uses StkFM class
  { ctor: TubeBell, args: [], name: 'TubeBell (FM)' },
  // Ch 7: Guitar (default 6 strings)
  { ctor: Guitar, args: [6], name: 'Guitar' },
  // Ch 8: Plucked
  { ctor: Plucked, args: [10.0], name: 'Plucked' },
  // Ch 9: Brass
  { ctor: Brass, args: [8.0], name: 'Brass' },
  // Ch 10: Clarinet
  { ctor: Clarinet, args: [8.0], name: 'Clarinet' },
  // Ch 11: Saxofony
  { ctor: Saxofony, args: [8.0], name: 'Saxofony' },
  // Ch 12: Bowed
  { ctor: Bowed, args: [8.0], name: 'Bowed' },
  // Ch 13: Recorder
  { ctor: Recorder, args: [], name: 'Recorder' },
  // Ch 14: Shakers
  { ctor: Shakers, args: [0], name: 'Shakers' },
  // Ch 15: BandedWG
  { ctor: BandedWG, args: [], name: 'BandedWG' },
  // Ch 16: Simple
  { ctor: Simple, args: [], name: 'Simple' },
];

function main() {
  // Point STK at the rawwaves directory before constructing instruments
  // that load wavetables (FM family, Whistle, etc.)
  try {
    Stk.setRawwavePath('../../../third_party/stk/rawwaves');
  } catch(e) {
    console.warn('Could not set rawwave path:', e.message);
  }

  initialize();

  const deviceId = findInputDevice();
  if(deviceId < 0) {
    terminate();
    throw new Error('No MIDI input device found -- connect a controller first');
  }

  console.log(`Using input device ${deviceId}: ${devices[deviceId].name}`);
  console.log(`Sample rate: ${SAMPLE_RATE} Hz, buffer: ${BUFFER_FRAMES} frames × ${N_BUFFERS}`);

  const input = PortMidiStream.openInput(deviceId);
  input.setFilter(PM_FILT_ACTIVE | PM_FILT_CLOCK);

  // Create voice pools for all 16 channels
  const pools = [];
  for(let ch = 0; ch < N_CHANNELS; ch++) {
    const cfg = INSTRUMENT_CONFIGS[ch];
    try {
      pools[ch] = new VoicePool(cfg.ctor, POLYPHONY_PER_CHANNEL, ...cfg.args);
      console.log(`Ch ${ch+1}: ${cfg.name} (${POLYPHONY_PER_CHANNEL} voices)`);
    } catch(e) {
      console.error(`Ch ${ch+1}: failed to create ${cfg.name}:`, e.message);
      pools[ch] = null;
    }
  }

  const out = new RtWvOut(1, SAMPLE_RATE, 0, BUFFER_FRAMES, N_BUFFERS);
  console.log('Audio output started. Playing... Ctrl+C to stop.');

  let frameCount = 0;
  const startTime = Date.now();

  try {
    for(;;) {
      // Process MIDI input
      if(input.poll()) {
        for(const ev of input.read(64)) {
          const status = ev.status & 0xf0;
          const channel = ev.status & 0x0f; // 0-15
          const pool = pools[channel];

          if(!pool)
            continue;

          if(status === 0x90 && ev.data2 > 0) {
            // Note On
            pool.noteOn(ev.data1, ev.data2 / 127);
          } else if(status === 0x80 || (status === 0x90 && ev.data2 === 0)) {
            // Note Off
            pool.noteOff(ev.data1, ev.data2 / 127);
          } else if(status === 0xB0) {
            // Control Change
            pool.controlChange(ev.data1, ev.data2);
          } else if(status === 0xE0) {
            // Pitch Bend (not directly mapped in STK, but could be added)
            // STK instruments don't have a standard pitchBend method.
            // Could implement via setFrequency offset if needed.
          }
        }
      }

      // Generate audio: mix all active channels
      let sample = 0;
      let activeCount = 0;
      for(const pool of pools) {
        if(pool) {
          sample += pool.tick();
          activeCount++;
        }
      }
      if(activeCount > 0)
        sample /= activeCount;

      // Output one frame per channel (mono output, same sample to all)
      for(let i = 0; i < BUFFER_FRAMES; i++)
        out.tick(sample);

      frameCount += BUFFER_FRAMES;

      // Periodic status (every ~5 seconds at 44100/512 ≈ 86 iterations/sec)
      if(frameCount % (SAMPLE_RATE * 5) < BUFFER_FRAMES) {
        const elapsed = (Date.now() - startTime) / 1000;
        const xruns = out.clipStatus() ? ' CLIP!' : '';
        console.log(`  ${elapsed.toFixed(1)}s frames=${frameCount} clip=${xruns}`);
      }
    }
  } catch(e) {
    if(e.name !== 'Error' || e.message !== 'Interrupted') {
      console.error('Error:', e.message);
    }
  } finally {
    out.stop();
    input.closePort();
    terminate();
    const elapsed = (Date.now() - startTime) / 1000;
    console.log(`\nStopped after ${elapsed.toFixed(1)}s, ${frameCount} frames.`);
  }
}

try {
  main();
} catch(e) {
  console.error(e.message);
  std.exit(1);
}