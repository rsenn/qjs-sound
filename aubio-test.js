// Demo for the quickjs-aubio bindings: analyze an audio file with
// AubioNotes, AubioOnset and AubioPitch, writing each detector's output to
// its own text file.
//
// Usage:
//   qjs -m --std aubio-test.js <audio-file> [outputPrefix]
//
// <audio-file> is decoded via LabSound's AudioContext.createBufferFromFile()
// (mixed down to mono), so anything libnyquist can read (WAV, and depending
// on build config OGG/FLAC/etc.) works, not just WAV.
//
// Writes three tab-separated text files (default prefix = input file name
// without its extension):
//   <prefix>.onset.txt  one line per detected onset:  time_s  strength
//   <prefix>.pitch.txt  one line per analysis hop:     time_s  hz  confidence
//   <prefix>.notes.txt  one line per note on/off event: time_s  event  midiNote  velocity

import * as std from 'std';
import { AudioContext } from 'labsound';
import { AubioNotes, AubioOnset, AubioPitch } from 'aubio';

const BUF_SIZE = 1024;
const HOP_SIZE = 512;

function baseName(path) {
  return path.replace(/^.*\//, '').replace(/\.[^./]+$/, '');
}

function main() {
  const inPath = scriptArgs[1];
  if(!inPath) {
    console.error('usage: aubio-test.js <audio-file> [outputPrefix]');
    std.exit(1);
  }
  const prefix = scriptArgs[2] || baseName(inPath);

  const ctx = new AudioContext(true /* offline, no playback device needed */);
  const buffer = ctx.createBufferFromFile(inPath, true /* mixToMono */);
  const sampleRate = buffer.sampleRate;
  const samples = buffer.getChannelData(0);

  console.log(`${inPath}: ${samples.length} samples @ ${sampleRate}Hz (${(samples.length / sampleRate).toFixed(2)}s)`);

  const onset = new AubioOnset('default', BUF_SIZE, HOP_SIZE, sampleRate);
  const pitch = new AubioPitch('yinfft', BUF_SIZE, HOP_SIZE, sampleRate);
  const notes = new AubioNotes('default', BUF_SIZE, HOP_SIZE, sampleRate);
  pitch.setUnit('Hz');

  const onsetFile = std.open(`${prefix}.onset.txt`, 'w');
  const pitchFile = std.open(`${prefix}.pitch.txt`, 'w');
  const notesFile = std.open(`${prefix}.notes.txt`, 'w');

  onsetFile.puts('# time_s\tstrength\n');
  pitchFile.puts('# time_s\thz\tconfidence\n');
  notesFile.puts('# time_s\tevent\tmidiNote\tvelocity\n');

  let onsetCount = 0, noteCount = 0;
  const hop = new Float32Array(HOP_SIZE);

  for(let offset = 0; offset < samples.length; offset += HOP_SIZE) {
    const len = Math.min(HOP_SIZE, samples.length - offset);
    hop.fill(0);
    hop.set(samples.subarray(offset, offset + len));

    const timeS = offset / sampleRate;

    const [onsetValue] = onset.process(hop);
    if(onsetValue !== 0) {
      onsetFile.puts(`${onset.lastS.toFixed(6)}\t${onsetValue.toFixed(6)}\n`);
      onsetCount++;
    }

    const [hz] = pitch.process(hop);
    pitchFile.puts(`${timeS.toFixed(6)}\t${hz.toFixed(3)}\t${pitch.confidence.toFixed(3)}\n`);

    const [midiNote, velocity, midiNoteOff] = notes.process(hop);
    if(midiNote !== 0) {
      notesFile.puts(`${timeS.toFixed(6)}\ton\t${midiNote}\t${velocity.toFixed(1)}\n`);
      noteCount++;
    }
    if(midiNoteOff !== 0)
      notesFile.puts(`${timeS.toFixed(6)}\toff\t${midiNoteOff}\t0\n`);
  }

  onsetFile.close();
  pitchFile.close();
  notesFile.close();

  console.log(`${onsetCount} onsets -> ${prefix}.onset.txt`);
  console.log(`pitch track -> ${prefix}.pitch.txt`);
  console.log(`${noteCount} notes -> ${prefix}.notes.txt`);
}

main();
