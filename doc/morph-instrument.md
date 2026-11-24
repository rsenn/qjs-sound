# Morph instrument (examples/morph-instrument.js)

A monophonic, MIDI-controlled instrument: two voices (physical models) are
morphed in the spectral domain and sent through a space macro. It is the
sound engine of the music architecture in `doc/music-model.md`: a MIDI
port in, audio out, every parameter a controller.

```
voice A --> PVA --\
                   PVMorph --> PVTransp --> PVBlur --> PVS --> echo --> out
voice B --> PVA --/
```

PVA, PVMorph, PVTransp, PVBlur, PVS are SndObj's phase vocoder classes
(`doc/native/sndobj.md`); the voices are STK models and glassloom's own
physical models (`app/acid/winds.js`, `app/acid/strings.js`).

## Running

```sh
QUICKJS_MODULE_PATH=build/x86_64-linux-gnu qjsm examples/morph-instrument.js --a vibes --b marimba --fft 1024
```

It opens a virtual ALSA MIDI input named `Morph` (WebMIDI lists it as
Morph) and plays through RtWvOut (PipeWire). Options are listed in the
file's header comment. The useful ones:

| Option             | Does                                                        |
|--------------------|-------------------------------------------------------------|
| `--a`, `--b`       | the voices at start; program change switches them live      |
| `--fft N`          | FFT size, default 2048; 1024 is lighter, blurs notes below about 110 Hz |
| `--wheel space\|morph` | what CC 1 does; default space (the morph is then CC 3)  |
| `--hold MS`        | shortest note, default 300: a tap is held until then        |
| `--low N`          | lowest note played as it is, default 55; lower notes are folded up and shifted back |
| `--debug`          | log every MIDI message with its block number and record wavs |
| `--record BASE`    | record BASE.wav (output), BASE-a.wav, BASE-b.wav            |
| `--replay LOG`     | offline: feed the `midi <block> <bytes>` lines of a debug log |
| `--test`           | offline check of the morph positions, no devices            |

## Voices (program change)

Program change selects the voice: channel 1 for A, channel 2 for B; program
n is the nth entry below. A program equal to the current voice is ignored;
a new voice is calibrated to the same loudness and keeps the controller
values.

| n  | Voice        | Kind                                              |
|----|--------------|---------------------------------------------------|
| 0  | Bowed        | STK, sustained                                    |
| 1  | Clarinet     | STK, sustained                                    |
| 2  | Flute        | STK, sustained                                    |
| 3  | BlowBotl     | STK, sustained                                    |
| 4  | Brass        | STK, sustained                                    |
| 5  | Saxofony     | STK, sustained                                    |
| 6  | glass:flute  | glassloom Winds model, sustained                  |
| 7  | glass:bottle | glassloom Winds model, sustained                  |
| 8  | glass:guitar | glassloom Karplus-Strong string, rings after release |
| 9  | glass:harp   | glassloom Karplus-Strong string, rings after release |
| 10 | marimba      | STK ModalBar preset 0, struck                     |
| 11 | vibes        | STK ModalBar preset 1, struck                     |
| 12 | agogo        | STK ModalBar preset 2, struck                     |
| 13 | Plucked      | STK, plucked                                      |
| 14 | Mandolin     | STK, plucked                                      |
| 15 | StifKarp     | STK, plucked                                      |

Not usable: STK `Guitar` (NaN through the frames tick), `BandedWG` (nearly
silent through noteOn). A struck or plucked voice is struck on every note
(a legato note over a held one would otherwise stay silent) and is not held
for `--hold`; a sustained voice glides on legato notes.

## Controllers

Values 0..127, defaults in the file's `PARAMS` table (printed at start).

| CC  | Controls                                  | CC  | Controls                         |
|-----|-------------------------------------------|-----|----------------------------------|
| 1   | space macro (or the morph, `--wheel morph`) | 14  | A excitation (bow, stick hardness) |
| 3   | morph A to B                              | 15  | A position                       |
| 70  | morph curve, linear to snap               | 16  | A vibrato depth                  |
| 71  | amplitude morph offset (centre 64)        | 17  | A vibrato rate                   |
| 74  | frequency morph offset (centre 64)        | 18  | B excitation (breath, jet)       |
| 72  | spectral pitch shift, +-12 semitones      | 19  | B position / noise               |
| 73  | keep formants (on at 64 and above)        | 20  | B vibrato depth                  |
| 75  | spectral blur, 0 to 1 s                   | 21  | B vibrato rate                   |
| 76  | bend range, 1 to 12 semitones             | 7   | master volume                    |
| 77  | B detune, +-50 cents                      | 91  | echo wet                         |
| 78  | B octave (below 43, middle, above 85)     | 92  | echo time, 20 to 800 ms          |
| 79  | A level                                   | 93  | echo feedback                    |
| 80  | B level                                   | 64  | sustain pedal                    |

Also: pitch bend (range from CC 76), channel pressure (vibrato of both
voices), CC 120 and 123 (all off).

### The space macro (CC 1)

One wheel for everything that makes a note leave the room: the voices'
motor vibrato gets deeper and faster, the spectral smear grows from 12 ms
to 1.5 s in seven steps, the echo wet and feedback open, and the spectrum's
pitch warbles by up to about 5 percent. The wheel only sets a target; the
applied value glides to it a little each block, because an instant change
of the echo or the smear clicks. The echo time is not tied to the wheel
(CC 92). Known: the top of the wheel is about 13 dB louder than the
bottom (see TODO.md).

## Facts worth knowing

- Level: the chain leaves the voices about 12 dB down; `MAKEUP` (x4) brings
  it back and a soft limiter (linear to 0.7, then a knee) catches peaks.
  PVA's scale is 826 / FFT so the level does not depend on the FFT size.
- Models are wrong or silent below about MIDI 55 (STK Bowed erratic, the
  glass flute accurate from 45), so lower notes are played octaves up and
  shifted down by PVTransp; a phase vocoder with a 1024 FFT has 43 Hz bins,
  poor for notes below about 110 Hz.
- A voice that goes non-finite is reset and logged ("voice went
  non-finite"); a non-finite output block is logged with the state of the
  dry voices.
- Load: about 35 percent of one core at hop 256 (5.8 ms), one startup
  underrun, nothing else, with four lanes of CC at full rate in glassloom.
- Debugging a live session: `--debug` writes the MIDI log and wavs;
  `--replay LOG` re-runs the notes offline, counts clicks at block
  boundaries and reports a held note with silent output. That is how the
  silent-voice and the wheel-click problems were found (see ChangeLog).

## glassloom

glassloom (the web interface, `~/Sources/glassloom`, work in /dev) has a
Morph device: matched by port name before FluidSynth, object type
`mmorph`, 6 rings per page and 5 pages for 25 controllers (a tap on the
centre turns the page), a lane for every controller, "Mod wheel:
Morph/Space" toggle that only changes the CC 1 label, all 25 values sent on
connect. Not exposed: sustain, pitch bend, channel pressure, program change.
Open points are in TODO.md.

## Related

- `examples/audio-report.js`: aubio report of a recording (level, onsets,
  pitch per slice, pitch classes).
- `examples/snn-player.js`: a spiking network that plays this instrument
  (or any MIDI port); see `doc/music-model.md` section 11.
