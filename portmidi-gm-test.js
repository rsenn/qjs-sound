/*
 * Demo for the quickjs-portmidi bindings: sends a real short piece of music
 * (chord progression + bassline + arpeggiated melody, General MIDI
 * instruments) out through a PortMidiStream to whatever GM-compatible
 * software or hardware synth is listening.
 *
 * PortMidi itself is I/O only -- it has no synth engine (see
 * doc/portmidi.md), so this needs a real GM synth to actually hear
 * anything. The easiest one to set up is FluidSynth against any .sf2
 * soundfont, registered as an ALSA sequencer client so PortMidi sees it as
 * an output device:
 *
 *   fluidsynth -a pulseaudio -m alsa_seq -o synth.midi-bank-select=gm \
 *       /usr/share/sounds/sf2/FluidR3_GM.sf2
 *
 * (Debian/Ubuntu ship FluidR3_GM.sf2 in the fluid-soundfont-gm package.)
 * Then run this script while fluidsynth keeps running in another terminal.
 *
 * Usage: qjsm portmidi-gm-test.js
 */

import * as std from 'std';
import { initialize, terminate, PortMidiStream, devices } from 'portmidi';
import { setTimeout } from 'os';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const CHANNEL_PIANO = 0, CHANNEL_BASS = 1, CHANNEL_MELODY = 2;
const GM_ACOUSTIC_GRAND_PIANO = 0, GM_ACOUSTIC_BASS = 32, GM_LEAD_SQUARE = 80;

const NOTE_ON = 0x90, NOTE_OFF = 0x80, PROGRAM_CHANGE = 0xC0;

/*
 * C major - G major - A minor - F major, the "four chords" progression --
 * picked because it's instantly recognizable as actual music rather than a
 * scale/arpeggio test tone.
 */
const PROGRESSION = [
  { chord: [60, 64, 67], bass: 36 }, /* C */
  { chord: [55, 59, 62], bass: 43 }, /* G */
  { chord: [57, 60, 64], bass: 45 }, /* Am */
  { chord: [53, 57, 60], bass: 41 }, /* F */
];

function findOutputDevice() {
  for(let i = 0; i < devices.length; i++)
    if(devices[i].output && /fluid|synth|gm|general\s*midi/i.test(devices[i].name))
      return i;

  for(let i = 0; i < devices.length; i++)
    if(devices[i].output)
      return i;

  return -1;
}

async function main() {
  initialize();

  const deviceId = findOutputDevice();
  if(deviceId < 0) {
    terminate();
    throw new Error('No MIDI output device found -- start a GM synth (e.g. FluidSynth) first');
  }

  console.log(`Using output device ${deviceId}: ${devices[deviceId].name}`);

  const out = PortMidiStream.openOutput(deviceId, 256, 0);

  out.writeShort(PROGRAM_CHANGE | CHANNEL_PIANO,  GM_ACOUSTIC_GRAND_PIANO);
  out.writeShort(PROGRAM_CHANGE | CHANNEL_BASS,   GM_ACOUSTIC_BASS);
  out.writeShort(PROGRAM_CHANGE | CHANNEL_MELODY, GM_LEAD_SQUARE);

  const bpm = 100;
  const beatMs = 60000 / bpm;
  const barMs = beatMs * 4;
  const repeats = 2;

  for(let r = 0; r < repeats; r++) {
    for(const { chord, bass } of PROGRESSION) {
      for(const note of chord)
        out.writeShort(NOTE_ON | CHANNEL_PIANO, note, 75);
      out.writeShort(NOTE_ON | CHANNEL_BASS, bass, 90);

      /* Eighth-note arpeggio over the chord (+ an octave-up repeat of the
      root) fills the bar with melodic motion instead of a static pad.
      */
      const melodyNotes = [...chord, chord[0] + 12];
      const stepMs = barMs / (melodyNotes.length * 2);

      for(const note of melodyNotes) {
        out.writeShort(NOTE_ON | CHANNEL_MELODY, note, 100);
        await sleep(stepMs);
        out.writeShort(NOTE_OFF | CHANNEL_MELODY, note, 0);
        await sleep(stepMs);
      }

      for(const note of chord)
        out.writeShort(NOTE_OFF | CHANNEL_PIANO, note, 0);
      out.writeShort(NOTE_OFF | CHANNEL_BASS, bass, 0);
    }
  }

  out.close();
  terminate();
}

main().catch((e) => {
  console.error(e.message);
  std.exit(1);
});
