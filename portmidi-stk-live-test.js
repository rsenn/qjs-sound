/*
 * Demo for the quickjs-portmidi + quickjs-stk bindings together: reads note
 * on/off and control-change events from a real MIDI controller via
 * PortMidi, and drives a small polyphonic pool of STK Flute physical
 * models (breath noise through a jet delay, no reed) through RtWvOut for
 * realtime audio output.
 *
 * Both bindings deliberately avoid ever calling into JS from a native
 * callback thread (see doc/portmidi.md and doc/stk-io.md, "Scope
 * limitations") -- everything here runs on the one JS/interpreter thread:
 * PortMidi's read()/poll() never block, and RtWvOut.tick() is STK's own
 * blocking wrapper around its realtime audio callback, so calling it
 * repeatedly from a plain loop is what paces this whole script to real
 * time, the same way a blocking ALSA write would.
 *
 * Usage: qjsm portmidi-stk-live-test.js
 * Play notes and move the mod wheel (CC1) on a connected MIDI controller.
 * Ctrl+C to stop.
 */

import * as std from 'std';
import { initialize, terminate, PortMidiStream, devices, PM_FILT_ACTIVE, PM_FILT_CLOCK } from 'portmidi';
import { Flute, RtWvOut } from 'stk';

const mtof = (n) => 440 * Math.pow(2, (n - 69) / 12);

const POLYPHONY = 4;
const LOWEST_FREQUENCY = 50; /* sets Flute's internal delay-line length */

function findInputDevice() {
  for(let i = 0; i < devices.length; i++)
    if(devices[i].input)
      return i;
  return -1;
}

/*
 * Fixed pool of Flute voices, round-robin/steal-oldest allocated to
 * incoming note numbers -- STK's physical-model instruments (Flute,
 * Clarinet, Brass, Plucked, ...) are all monophonic per instance, so this
 * pooling is the standard way to get any of them to play more than one
 * note at once; it isn't specific to Flute.
 */
class VoicePool {
  constructor(size) {
    this.voices = Array.from({ length: size }, () => new Flute(LOWEST_FREQUENCY));
    this.owner = new Array(size).fill(-1); /* MIDI note currently held by each voice, or -1 */
    this.order = []; /* voice indices, oldest-assigned first */
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

function main() {
  initialize();

  const deviceId = findInputDevice();
  if(deviceId < 0) {
    terminate();
    throw new Error('No MIDI input device found -- connect a controller first');
  }

  console.log(`Using input device ${deviceId}: ${devices[deviceId].name}`);
  console.log('Playing through STK Flute. Ctrl+C to stop.');

  const input = PortMidiStream.openInput(deviceId);
  input.setFilter(PM_FILT_ACTIVE | PM_FILT_CLOCK);

  const pool = new VoicePool(POLYPHONY);
  /*
   * Wider/deeper than doc/stk-io.md's own RtWvOut example (512, 4): the
   * per-sample JS loop below (POLYPHONY tick() calls each) is slower than
   * native code, so it needs more buffered headroom to stay ahead of the
   * audio callback and avoid underruns.
   */
  const bufferFrames = 512;
  const out = new RtWvOut(1, 44100, 0, bufferFrames, 8);

  for(;;) {
    if(input.poll()) {
      for(const ev of input.read(64)) {
        const status = ev.status & 0xf0;

        if(status === 0x90 && ev.data2 > 0)
          pool.noteOn(ev.data1, ev.data2 / 127);
        else if(status === 0x80 || (status === 0x90 && ev.data2 === 0))
          pool.noteOff(ev.data1, ev.data2 / 127);
        else if(status === 0xB0)
          pool.controlChange(ev.data1, ev.data2);
      }
    }

    for(let i = 0; i < bufferFrames; i++)
      out.tick(pool.tick());
  }
}

try {
  main();
} catch(e) {
  console.error(e.message);
  std.exit(1);
}
