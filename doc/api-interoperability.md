# API interoperability assessment: native modules

Scope: the eleven native modules built from this tree. Method: read each
`quickjs-*.c`/`.cpp` binding (class lists, `JS_CFUNC_*` tables, buffer
handling) plus `doc/native/*.md`; claims below cite the binding source unless
marked *(doc)*, which means the doc says so and the source was not checked.
Nothing was executed for this assessment.

| Module | Source | Wraps | Role |
|---|---|---|---|
| `sndfile` | `quickjs-sndfile.c` | libsndfile | file I/O (source + sink) |
| `portaudio` | `quickjs-portaudio.c` | PortAudio | device I/O, blocking (source + sink) |
| `portmidi` | `quickjs-portmidi.c` | PortMidi | MIDI device I/O (control source + sink) |
| `samplerate` | `quickjs-samplerate.c` | libsamplerate | sample-rate conversion (processor) |
| `soundtouch` | `quickjs-soundtouch.cpp` | SoundTouch | tempo/pitch (processor) |
| `rubberband` | `quickjs-rubberband.c` | Rubber Band | tempo/pitch (processor) |
| `aubio` | `quickjs-aubio.c` | aubio | analysis: audio in, control out |
| `stk` | `quickjs-stk.cpp`, `analog-drums.hpp` | STK, RtAudio, RtMidi | synthesis, filters, effects, I/O, MIDI |
| `sndobj` | `quickjs-sndobj.cpp` | SndObj | pull-model synthesis graph |
| `labsound` | `quickjs-labsound.cpp` | LabSound | WebAudio-style node graph |
| (helper) | `quickjs-typedarray.{c,h}` | - | shared buffer-view helpers (sndfile, samplerate, soundtouch, rubberband) |

## 1. Findings first

What decides whether two modules can talk, ranked by impact:

1. **Three incompatible audio data models.** Interleaved frame buffers
   (sndfile, portaudio, samplerate, soundtouch, STK), planar channel arrays
   (rubberband, LabSound `AudioBuffer`), and pull-graph objects that only
   connect to their own kind (SndObj, LabSound nodes). No module provides an
   interleave/deinterleave or channel-mix helper.
2. **Two sample types.** Everything is `Float32` except STK, which is
   `Float64` (`typedef double StkFloat`, `third_party/stk/include/Stk.h:78`).
   `SndFile` is the one module that accepts both, so it is the natural bridge.
   STK is the only library we build that can switch to `float` (a one-line
   typedef; section 4.4). aubio can switch the other way, to double, which
   would not help.
3. **SndObj and LabSound are islands.** SndObj has no way to take audio from
   JS. LabSound can take audio from JS only as a whole `AudioBuffer`, never as
   a stream. Details in section 7.
4. **Three independent audio device owners** (PortAudio, STK `RtWvIn/RtWvOut`,
   LabSound's RtAudio backend) and **two MIDI stacks**
   (PortMidi, RtMidi) with different event shapes and clocks.
5. **Control signals have no common representation.** MIDI bytes, aubio
   `Float32Array` results, STK `noteOn(freq, amp)`, LabSound `AudioParam`
   scheduling and SndObj modulator objects each need hand conversion.
6. **A few bindings are dead ends or hazards** (section 8): the `RtAudio`
   stream methods, `paNonInterleaved`, `getChannelData()` write-through,
   `AubioTempo` documented but not built.

Most useful direct (zero-copy, no conversion) paths that already work:

- `SndFile.read(Float32Array)` -> `SampleRateConverter` / `SoundTouch` /
  `AubioPitch` / `PaStream.write`, and back to `SndFile.write`.
- `SndFile.read(Float64Array(frames.buffer))` and
  `SndFile.write(Float64Array(frames.buffer))` for STK (`StkFrames.buffer`
  aliases STK's memory, `quickjs-stk.cpp:442`; `SndFile` accepts
  `Float64Array`, `quickjs-sndfile.c:156`). This is the only zero-copy
  STK <-> anything path.

## 2. Processing-unit classes

"Processing unit" = the base (virtual or not) that gives a family of JS classes
their shared contract. JS-visible base = what `instanceof`/shared prototype
you can actually observe.

### 2.1 STK (`quickjs-stk.cpp`): Strategy A, one JS class per level, magic ctor

The first column is the C-side level name; STK's JS-visible names are the
concrete classes. All concrete types of a level share one `JSClassID`, one prototype and a
`Symbol.toStringTag`; `instanceof` cannot tell leaves apart.

| JS base (class id) | C++ base | Contract (JS) | Concrete classes |
|---|---|---|---|
| `Stk` | `stk::Stk` | static `sampleRate`, `rawwavePath` | (statics only) |
| `StkFrames` | `stk::StkFrames` | `size/frames/channels/dataRate/empty`, `buffer`, `resize`, `interpolate`, `getChannel`, `setChannel` | (one class) |
| `Generator` | `stk::Generator` | `tick(channel=0)` -> number, or `tick(StkFrames, channel)` fills; `channelsOut` | ADSR, Asymp, BlitSaw, BlitSquare, Blit, Envelope, Granulate, Modulate, Noise, SineWave, SingWave |
| `Filter` | `stk::Filter` | `tick(x)` -> number, or `tick(StkFrames, channel)` **in place** | BiQuad, Delay, DelayA, DelayL, Fir, FormSwep, Iir, OnePole, OneZero, PoleZero, TapDelay, TwoPole, TwoZero |
| `Effect` | `stk::Effect` | `tick(in1, in2?)` -> number or `[l, r]`, or `tick(StkFrames, channel)` in place; `setEffectMix`, `clear` | Chorus, Echo, FreeVerb, JCRev, LentPitShift, NRev, PitShift, PRCRev |
| `Function` | `stk::Function` | `tick(x)`; `setA1..A3` | Cubic |
| `Instrmnt` | `stk::Instrmnt` | `tick(channel=0)` / `tick(StkFrames, channel)`; `noteOn(Hz, amp)`, `noteOff(amp)`, `controlChange(n, v)` | BandedWG, BlowBotl, BlowHole, Bowed, Brass, Clarinet, Drummer, Flute, Mandolin, Mesh2D, Plucked, Recorder, Resonate, Saxofony, Shakers, Simple, Sitar, StifKarp, VoicForm, Whistle |
| `FM` | `stk::FM` | same as `Instrmnt` | BeeThree, FMVoices, HevyMetl, PercFlut, Rhodey, TubeBell, Wurley |
| `StkWvIn` | `stk::WvIn` | `tick(channel)` / `tick(StkFrames, channel)`, `channelsOut`, `lastFrame`, `start/stop` | RtWvIn, InetWvIn |
| `StkWvOut` | `stk::WvOut` | `tick(sample)` / `tick(StkFrames)`, `clipStatus`, `frameCount`, `time`, `start/stop` | RtWvOut, InetWvOut |
| (own classes) | `stk::Instrmnt` subclasses in `analog-drums.hpp` | `noteOn/noteOff`-style plus `render(n, amp)` -> mono `StkFrames` | TwinTDrum, Tr909BassDrum, Tr909Percussion |
| I/O helpers | `MidiFileIn`, `RtMidiIn`, `RtMidiOut`, `RtAudio` | see section 5 / section 8 | - |

Note: the scalar `tick` of Generator, Effect and Filter is dispatched with a
`dynamic_cast` chain because STK does not declare a common scalar `tick`
(`quickjs-stk.cpp:666`, `:1128`, `:972`). A class added to a level without
adding its branch silently returns 0.

### 2.2 LabSound (`quickjs-labsound.cpp`): Strategy B, class per leaf, real prototype chain

```
BaseAudioContext
  AudioContext
  OfflineAudioContext        (startRendering(), length)
AudioNode                    (connect, disconnect; no JSClassID, shared methods only)
  AudioDestinationNode
  AudioScheduledSourceNode   (start, stop)
    OscillatorNode, AudioBufferSourceNode, NoiseNode, ConstantSourceNode
  GainNode, BiquadFilterNode, IIRFilterNode, DelayNode, WaveShaperNode,
  StereoPannerNode, ConvolverNode, AnalyserNode, DynamicsCompressorNode
AudioParam / AudioSetting    (value + scheduling; connectable destination)
AudioBuffer                  (data container, not a node)
AudioListener, AudioDevice   (listener inert, see section 8)
```

"Any node" arguments go through a linear class-id probe (`any_audio_node`).
Every new leaf must be added to that probe or it cannot be a `connect` target.

### 2.3 SndObj (`quickjs-sndobj.cpp`): three class levels

The names in the first column are the C-side class-id names (`js_sndobjtable`,
`js_sndobjgenerator`, `js_sndobjeffect`), not exported JS names; the JS names
are the concrete classes in the last column.

| JS base | C++ base | Contract | Concrete classes |
|---|---|---|---|
| table | `SndObj` table | `len`, `lookup`, `toArray()` (copy), `setHarm/setPhase/makeTable` | HarmTable |
| generator | `SndObj` | `process()`, `output(i)`, `block()`, `sr`, `vecsize`, `enable/disable`; modulator slots take other SndObj | Oscili, Buzz, Randi |
| effect | `SndObj` | same, plus an `input` SndObj | ADSR, Reson, Comb, Allpass, VDelay, Gain, Mixer |

### 2.4 Stand-alone classes (no shared base)

| Module | Classes | Shape |
|---|---|---|
| `sndfile` | `SndFile` | one class, mode flag picks read/write/rw |
| `portaudio` | `PaStream`, `PaDeviceInfo`, `HostApiInfo`, `PaDevices` (exotic), `PaHostApis` (exotic) | blocking `read/write` |
| `portmidi` | `PortMidiStream`, `PmDeviceInfo`, `PmDevices` (exotic) | `openInput/openOutput` factories |
| `samplerate` | `SampleRateConverter` (+ static `simple`) | push chunk, get chunk |
| `soundtouch` | `SoundTouch` | push (`putSamples`) / pull (`receiveSamples`) |
| `rubberband` | `RubberBandStretcher` | `process` / `available` / `retrieve` |
| `aubio` | `AubioNotes`, `AubioOnset`, `AubioPitch` | `process(hop)` -> `Float32Array` |

## 3. Species of processing objects

Classification by what a unit does to the signal, across all modules.

| Species | Members |
|---|---|
| **Source: file** | `SndFile` (read), `LabSound.decodeAudioData/createBufferFromFile`, `MidiFileIn` (control) |
| **Source: device** | `PaStream` (read), `RtWvIn`, `PortMidiStream` input, `RtMidiIn` |
| **Source: network** | `InetWvIn` |
| **Source: oscillator / generator** | STK `SineWave`, `Blit*`, `Noise`, `SingWave`, `Granulate`; SndObj `Oscili`, `Buzz`, `Randi`; LabSound `OscillatorNode`, `NoiseNode`, `ConstantSourceNode` |
| **Source: instrument / voice** | STK `Instrmnt` family, `FM` family, `Drummer`, `Shakers`; TwinT, TR-909 drums |
| **Source: sample playback** | LabSound `AudioBufferSourceNode` (STK `Granulate` and `Drummer` read STK rawwaves, not JS buffers) |
| **Envelope / control-rate shaper** | STK `ADSR`, `Envelope`, `Asymp`, `Modulate`; SndObj `ADSR`; LabSound `AudioParam` ramps |
| **Filter** (IIR/FIR/biquad) | STK `Filter` family; SndObj `Reson`; LabSound `BiquadFilterNode`, `IIRFilterNode` |
| **Delay / comb / allpass** | STK `Delay*`, `TapDelay`; SndObj `Comb`, `Allpass`, `VDelay`; LabSound `DelayNode` |
| **Reverb / spatial** | STK `FreeVerb`, `JCRev`, `NRev`, `PRCRev`; LabSound `ConvolverNode`; SndObj Schroeder from `Comb`+`Allpass` |
| **Modulation FX** | STK `Chorus`, `Echo`; SndObj `VDelay` (chorus/flange) |
| **Pitch shift (granular, real-time)** | STK `PitShift`, `LentPitShift` |
| **Pitch / tempo shift (high quality, buffered)** | `SoundTouch`, `RubberBandStretcher` (non-1:1 frame counts, latency) |
| **Sample-rate converter** | `SampleRateConverter` (non-1:1 frame counts) |
| **Dynamics / shaping** | LabSound `DynamicsCompressorNode`, `WaveShaperNode`, `GainNode`; SndObj `Gain`; STK `Function` (Cubic) |
| **Panning / mixing** | LabSound `StereoPannerNode`, node fan-in; SndObj `Mixer`; (SndObj `Pan` not bound) |
| **Analysis: audio -> control** | aubio `Notes`, `Onset`, `Pitch`; LabSound `AnalyserNode` (spectrum, scope) |
| **Sink: file** | `SndFile` (write), LabSound `AudioBuffer.writeToWav` |
| **Sink: device** | `PaStream` (write), `RtWvOut`, `AudioDestinationNode`/`AudioDevice`, `PortMidiStream` output, `RtMidiOut` |
| **Sink: network** | `InetWvOut` |
| **Control: parse / route** | `MidiFileIn`, `PortMidiStream` filters (`setFilter`, `setChannelMask`) |
| **Info / enumeration** | `PaDevices`, `PaHostApis`, `PmDevices`, `RtAudio` (see section 8) |

## 4. Sample formats and framing

### 4.1 Per-API reference

| Module / API | Element type | Layout | Unit of one call | Rate | Channels | Ownership of buffer |
|---|---|---|---|---|---|---|
| `SndFile.read/write(buf, frames?)` | `Int16Array`, `Int32Array`, `Float32Array` or `Float64Array`; libsndfile converts and scales | interleaved | frames; default `buf.length / channels` | file's | file's | caller (zero-copy) |
| `PaStream.read/write(buf, frames?)` | raw bytes of `sampleFormat` (default `paFloat32`; also Int32/24/16/8, UInt8); any view or `ArrayBuffer` accepted, **element type is not checked** | interleaved only (see 8.2) | frames; default byte length / `Pa_GetSampleSize * channels` | ctor, default 44100 | ctor | caller (zero-copy) |
| `SampleRateConverter.process(in, ratio, end, out?)` | `Float32Array` | interleaved | frames; output count differs from input | via `ratio` | ctor | in: caller; out: caller if given, else fresh |
| `SoundTouch.putSamples/receiveSamples` | `Float32Array` | interleaved | frames = `length / channels`; counts differ, latency | ctor | ctor | caller |
| `RubberBandStretcher.process/retrieve` | `Float32Array` per channel | **planar**: `Array<Float32Array>`, frames = shortest array | frames; counts differ, latency | ctor, default 44100 | ctor, default 2 | caller |
| `Aubio*.process(hop)` | `Float32Array`, length must equal `hopSize` | mono | one hop | ctor, default 44100 | 1 | in: caller; out: fresh 1-3 floats |
| STK `StkFrames` | `Float64` (`StkFloat`) | **interleaved**; `.buffer` aliases as an `ArrayBuffer` (view as `Float64Array`) | `frames x channels` | `Stk.sampleRate` (global, default 44100), `dataRate` per frames | per instance | shared; JS holds a `shared_ptr` while the buffer is alive |
| STK scalar `tick` | `Float64` JS number | one sample (Effect with `channelsOut > 1` returns `[l, r]`) | 1 sample | `Stk.sampleRate` | - | none |
| STK `RtWvIn/RtWvOut/Inet*` | `StkFrames` or scalar; wire formats `Stk::StkFormat` for Inet | interleaved | frames | ctor | ctor | - |
| SndObj `.block()` / `.output(i)` | `Float32Array` copy of the output vector / one number | mono vector | `vecsize` samples, default 256 | `sr`, default 44100 | 1 | fresh copy per call |
| SndObj table `.toArray()` | `Float32Array` copy | - | table | - | - | copy |
| LabSound `AudioBuffer` | `Float32` | **planar**, one `getChannelData(c)` per channel, **copy** (section 8.3) | whole buffer | buffer `sampleRate` | `numberOfChannels` | copy out; `copyToChannel(src, ch, start)` copies in |
| LabSound `AnalyserNode.get*Data(typedArray)` | `Float32Array` or `Uint8Array` | mono snapshot | `fftSize` or `frequencyBinCount` | context | 1 (downmix by LabSound) | caller fills |
| LabSound `AudioContext` | engine-internal `float` blocks | engine | - | `ctx.sampleRate`, **device nominal** (realtime) | device | not exposed |
| `PortMidiStream.read/write` | `{status, data1, data2, timestamp}` objects | event list | events | - | - | fresh objects |
| `RtMidiIn.getMessage()` | `{timeStamp, data: number[]}` | one message | 1 message | - | - | fresh |
| `MidiFileIn.getNextEvent/getNextMidiEvent` | `{deltaTime, data: number[]}` | one event | 1 event | - | - | fresh |

### 4.2 Format classes

| Class | Meaning | Members |
|---|---|---|
| **F32I** | Float32, interleaved, N channels | sndfile (any of 4 types), portaudio (default), samplerate, soundtouch |
| **F64I** | Float64, interleaved | STK `StkFrames`; also accepted by sndfile |
| **F32P** | Float32, planar | rubberband, LabSound `AudioBuffer` |
| **F32H** | Float32 mono, fixed hop | aubio (`hopSize` exactly) |
| **F32V** | Float32 mono, fixed vector (`vecsize`) | SndObj `block()` |
| **SCALAR** | one `number` per call | STK per-sample `tick`, SndObj `output(i)`, `PaStream`-less control |
| **RAW** | untyped bytes at a declared `PaSampleFormat` | portaudio |
| **GRAPH** | no buffers; objects wired to objects | SndObj inputs, LabSound `connect` |
| **EVT** | discrete events | MIDI streams, aubio results, MidiFileIn |

### 4.3 Framing and chunk-size contract

| Unit | Required chunk | Output chunk | Latency / state |
|---|---|---|---|
| `SndFile` | any | any | none |
| `PaStream` | any (blocking; underrun/overrun possible) | any | device |
| `SampleRateConverter` | any | `~ n * ratio`, headroom needed | filter delay |
| `SoundTouch` | any | variable, may be 0 until enough input | internal queue; `flush()` on end |
| `RubberBandStretcher` | any (offline mode: `study()` pass first) | variable, `available()` | `retrieve` until 0 |
| aubio | **exactly `hopSize`** | 1-3 floats | internal window of `bufSize` |
| STK in-place `tick(StkFrames)` | any frame count; channels must fit `channel` offset | same frames | per-sample state |
| SndObj | **exactly `vecsize` per `process()`**, in dependency order, caller-driven | `vecsize` | pull graph, no scheduler |
| LabSound realtime | n/a (engine-driven) | n/a | engine clock, start/stop times in seconds |
| LabSound offline | `startRendering()` renders whole `length` | one `AudioBuffer` | n/a |

Rate rules: there is no automatic rate negotiation anywhere. Defaults are
44100 in `portaudio`, `aubio`, `rubberband`, STK and SndObj; LabSound's
realtime context takes the device's nominal rate. Mixing modules therefore
needs an explicit `SampleRateConverter` or matched constructor arguments.

### 4.4 Float32 / Float64 mismatches and build-time sample types

Every `d` mark in section 6 comes from one fact: STK is `Float64`, nearly
everything else is `Float32`.

**Where the two types meet**

| # | Mismatch | Effect |
|---|---|---|
| 1 | `StkFrames` (F64I) <-> `SampleRateConverter`, `SoundTouch`, `RubberBand`, `Aubio*`, `PaStream`, LabSound `AudioBuffer`, SndObj `block()` (all F32) | a copy-and-convert loop on every block |
| 2 | `StkFrames.buffer` is sized `sizeof(stk::StkFloat) * len` (`quickjs-stk.cpp:450`), but all JS callers hard-code `new Float64Array(frames.buffer)` (`tests/twintdrum-test.js:57`, `tests/tr909*-test.js`, `doc/native/stk-io.md:95`) | the JS-visible element type is a hidden function of the STK build |
| 3 | `PaStream` has no F64 `PaSampleFormat` and does not check the element type (8.4) | an F64 view is accepted and plays garbage |
| 4 | Scalar parameters: JS numbers are double; SndObj, LabSound `AudioParam`, SoundTouch and Rubber Band hold `float` | a read-back differs from what was set (`0.1` reads back as `0.10000000149`); STK and the aubio setters (`(smpl_t)` casts) are consistent with their own type |
| 5 | Narrowing F64 -> F32 loses bits (harmless for audio samples, matters for filter coefficients and phase accumulators) | only an issue if the type is narrowed *inside* a filter, not at the module boundary |
| 6 | `SndFile` is the one module that takes `Int16/Int32/Float32/Float64Array` directly; libsndfile scales and, when writing floats into an integer format, wraps unless clipping is enabled | the safe bridge, but int<->float scaling is libsndfile's, not ours |
| 7 | aubio keeps `lsmp_t` (double) internally and exposes `smpl_t` (float) buffers | no JS-visible effect |

**Which libraries can change sample type at build time.** Only libraries we
compile ourselves count.

| Library | Built by us? | Sample type | Build-time switch | Verdict |
|---|---|---|---|---|
| **STK** | yes, always (`ExternalProject_Add`, `USE_STK`, `CMakeLists.txt:66`) | `typedef double StkFloat;` in `third_party/stk/include/Stk.h:78`; the header's own comment says it may be changed to `float` | source edit only (no macro, no CMake option) | **can become Float32.** Needs a guarded patch such as `#ifdef QJS_STK_FLOAT32 typedef float StkFloat; #else ... #endif`, applied the way `cmake/patches/aubio-static-lib.patch` is, so `third_party/stk` stays a plain checkout. The binding already uses `stk::StkFloat` where it matters (`:443`, `:450`, `analog-drums.hpp`), so it should follow. Not tried here. |
| **aubio** | yes, default (`BUILD_AUBIO=ON`, `cmake/BuildAubio.cmake`) | `smpl_t` = `float`, or `double` when `HAVE_AUBIO_DOUBLE=1` (`src/types.h:34`) | compile definition | can become Float64, which is the wrong direction (moves it away from the F32 majority). The binding hard-codes `float*` and `Float32Array` (`quickjs-aubio.c:13`, `:197`), so it would also need work. Keep float. |
| **SoundTouch** | optional (`BUILD_SOUNDTOUCH`, default off = system library) | `float` (default) or 16-bit integer (`SOUNDTOUCH_INTEGER_SAMPLES`) (`STTypes.h:65-99`) | macro | no Float64 option |
| **SndObj** | yes (sources compiled directly, `CMakeLists.txt:369`) | `float` written into the class layout (`float* m_output`, `float m_sr`, `float` operators) | none; `float` is spelled out, not typedef'd | no practical switch |
| **LabSound** | yes (`ExternalProject_Add`) | `float` (`AudioChannel`, `AudioBus`), by WebAudio design | none | no |
| **Rubber Band** | optional (`BUILD_RUBBERBAND`) | `float` API only | none | no |
| **libsamplerate** | optional (`BUILD_SAMPLERATE`) | `float` API (`SRC_DATA.data_in/out`) | none | no |
| **libsndfile** | optional (`BUILD_SNDFILE`) | all four types per call, no build choice | not needed | n/a |
| **PortAudio / PortMIDI** | optional | `PaSampleFormat` chosen at stream open; no F64 format exists | not applicable | n/a |

**Consequence.** `Float64` is reachable only for STK and aubio; `Float32` is
reachable for every module. So the only type all ten modules can share is
**Float32**, and the single change that removes the F32/F64 split is building
STK with `StkFloat = float`.

| Option | What it does to the matrix | Cost |
|---|---|---|
| Keep STK at double (today) | `d` stays in every STK <-> F32 pair; `SndFile` is the only zero-copy bridge | a conversion loop per block |
| Build STK with `float` | all `d` marks in 6.1 disappear; `StkFrames.buffer` becomes a `Float32Array` view and can go directly to `PaStream`, `SampleRateConverter`, `SoundTouch`, `AubioPitch`, with only `i`/`m`/`h` left | filter state and coefficients are float: high-Q IIR sections, very long delays/reverb tails and phase accumulators lose precision (STK's author kept double for this reason); `Float64Array(frames.buffer)` in all callers must change; STK rawwaves/`Granulate` still read at their file type |
| Keep both, make the type explicit | `StkFrames.buffer` stays F64 and a new `StkFrames.toFloat32(out)` / `fromFloat32(in)` does the loop in C | no precision change, no patch; the loop is still per block |

Recommendation: do not switch STK silently. The explicit converters
`StkFrames.toFloat32(out?)` / `fromFloat32(src)` and the `sampleType` getter
(`'f64'` or `'f32'`) now exist (`doc/native/stk-io.md`, test
`tests/stkframes-test.js`), so JS can pick the right view for whatever STK was
built with. If the per-block copy shows up in a profile, the mixed-precision
STK build is filed in `TODO.md`. Do not move aubio to double.

## 5. Control signals

| Producer | Representation | Rate / time base |
|---|---|---|
| `PortMidiStream.read()` | `{status, data1, data2, timestamp}` | `timestamp` in ms (`PortMidi.time()`) |
| `RtMidiIn.getMessage()` | `{timeStamp, data[]}` | delta seconds since previous message (RtMidi convention) *(RtMidi doc)* |
| `MidiFileIn.getNextEvent()` | `{deltaTime, data[]}` | ticks (`division`), no tempo map |
| aubio `Notes.process` | `[midiNote, velocity, midiNoteOff]` | per hop |
| aubio `Onset/Tempo.process` | `[0 \| detection time]` | per hop |
| aubio `Pitch.process` | `[value]`, unit set by `setUnit('Hz' \| 'midi' \| 'cent' \| 'bin')` | per hop |
| LabSound `AnalyserNode` | arrays (spectrum, scope) | snapshot |
| STK `Envelope/ADSR/Asymp/Modulate` | per-sample number via `tick` | sample rate |
| LabSound `AudioParam` value | number, plus scheduled automation | context time |

Consumers of control:

| Consumer | Input form |
|---|---|
| STK `Instrmnt/FM` | `noteOn(**Hz**, amp 0-1)`, `noteOff(amp)`, `controlChange(number, value)` (STK scale, 0-128) |
| STK `Filter/Effect` params | not exposed generically (per-class setters where bound) |
| SndObj | `setFreq(hz, modObj)`, `setAmp(amp, modObj)`, ...: a number or **another SndObj as modulator** |
| LabSound `AudioParam` | `value =`, `setValueAtTime`, `linearRampToValueAtTime`, `exponentialRampToValueAtTime`, `setTargetAtTime`, `cancelScheduledValues`, or `node.connect(param)` for audio-rate modulation |
| `PortMidiStream` output | `write([{status,data1,data2,timestamp}])`, `writeShort`, `writeSysEx` |
| `RtMidiOut` | `sendMessage(bytes)` |

Bridge facts to remember:

- STK takes **Hz**, MIDI carries note numbers: `440 * 2 ** ((n - 69) / 12)`.
- MIDI velocity is 0-127; STK amplitude is 0-1: `v / 127`
  (`tests/portmidi-stk-live-test.js` does this).
- aubio `Notes` already delivers a MIDI note number and velocity, so it feeds
  the same `noteOn` conversion as a MIDI keyboard.
- No scheduler binds MIDI timestamps (ms or ticks) to audio sample time in any
  module; consumers apply events when their loop reads them.

## 6. Interoperability matrices

Legend (per cell, source row -> sink column):

| Mark | Meaning |
|---|---|
| `●` | direct: same format, buffer passed unchanged, zero-copy |
| `d` | needs a Float32 <-> Float64 conversion loop |
| `i` | needs interleave / deinterleave |
| `h` | needs re-chunking to a fixed hop or vector size |
| `m` | needs channel mix or split |
| `r` | needs sample-rate match (always unless noted) |
| `○` | possible only as whole-buffer / offline hand-off, no streaming |
| `✕` | no path from JS |
| letters combine, e.g. `di` = conversion plus interleave |

`r` is not repeated in every cell; apply it whenever rates differ. Cells
cover the stated-correct direction only (source output feeding sink input).

### 6.1 Audio: sources (rows) into sinks (columns)

Sink columns: **SF** `SndFile.write`, **PA** `PaStream.write`, **SRC**
`SampleRateConverter`, **ST** `SoundTouch.put`, **RB** `RubberBand.process`,
**AU** `Aubio*.process`, **STK-f** STK `Filter/Effect` in-place on
`StkFrames`, **WVO** `RtWvOut`/`InetWvOut`, **SO** SndObj input,
**LS** LabSound (via `AudioBuffer` -> `AudioBufferSourceNode`/`ConvolverNode`).

| Source \ Sink | SF | PA | SRC | ST | RB | AU | STK-f | WVO | SO | LS |
|---|---|---|---|---|---|---|---|---|---|---|
| `SndFile.read` (choose type) | ● | ● (F32) | ● (F32) | ● (F32) | `i` (F32) | `mh` | ● (F64 read into `frames.buffer`) | ● (via StkFrames, F64) | ✕ | `i` ○ |
| `PaStream.read` | ● (F32) | ● | ● (F32) | ● (F32) | `i` | `mh` | `d` | `d` | ✕ | `i` ○ |
| `RtWvIn`, `InetWvIn` (F64) | ● (F64) | `d` | `d` | `d` | `di` | `dmh` | ● | ● | ✕ | `di` ○ |
| `SampleRateConverter` (F32I) | ● | ● | ● | ● | `i` | `mh` | `d` | `d` | ✕ | `i` ○ |
| `SoundTouch.receive` (F32I) | ● | ● | ● | ● | `i` | `mh` | `d` | `d` | ✕ | `i` ○ |
| `RubberBand.retrieve` (F32P) | `i` | `i` | `i` | `i` | ● | `h` per channel | `di` | `di` | ✕ | ○ (`copyToChannel` per channel) |
| STK `Generator/Filter/Effect/Instrmnt`, drums (F64I) | ● (F64 view of `frames.buffer`) | `d` | `d` | `d` | `di` | `dmh` | ● | ● | ✕ | `di` ○ |
| SndObj `block()` (F32V mono) | `i` if >1 ch, else ● | ● (mono stream) | ● | ● | ● (1 channel) | `h` | `d` | `d` | graph only (same objects) | ○ (1 channel) |
| LabSound `AudioBuffer` (F32P) | `i` | `i` | `i` | `i` | ● | `mh` | `di` | `di` | ✕ | ● (as buffer) |
| LabSound realtime/node output | ✕ (no tap except `AnalyserNode`) | ✕ | ✕ | ✕ | ✕ | ✕ | ✕ | ✕ | ✕ | ● (node `connect`) |
| LabSound `OfflineAudioContext` render | `i` | `i` | `i` | `i` | ● | `mh` | `di` | `di` | ✕ | ● |
| LabSound `AnalyserNode` snapshot | `h`, lossy | ✕ | ✕ | ✕ | ✕ | `h`, lossy | ✕ | ✕ | ✕ | ✕ |

Notes on selected cells:

- `SndFile.read` -> `STK-f` is `●` because `Float64Array(frames.buffer)` is a
  valid destination for `sf_readf_double`; libsndfile does the int/float scale.
- `STK` -> `SF` is `●` for the same reason in the other direction; no
  `Float32` copy is needed.
- STK -> `PA`: PortAudio has no Float64 `PaSampleFormat` (`paFloat32`,
  `paInt32/24/16/8`, `paUInt8`), so STK audio to a PortAudio stream needs `d`;
  STK's own `RtWvOut` avoids it but is a separate device owner.
- `AU` always needs mono: `m` = downmix. `h` = slice to exactly `hopSize`.
- SndObj as a **source** reaches every F32 sink with one `i` or `h`; as a
  **sink** it reaches nothing (section 7).
- LabSound `AudioBuffer` is a complete sink for JS data (via
  `createBuffer` + `copyToChannel` + `AudioBufferSourceNode`), but never a
  streaming one.

### 6.2 Control: sources (rows) into sinks (columns)

Sink columns: **STK** `Instrmnt/FM` note/controls, **SO** SndObj params,
**LS-P** LabSound `AudioParam`, **PM-O** `PortMidiStream` output,
**RT-O** `RtMidiOut`, **JS** your own logic.

| Source \ Sink | STK | SO | LS-P | PM-O | RT-O |
|---|---|---|---|---|---|
| `PortMidiStream.read` | map: note -> Hz, vel/127 | map to `setFreq/setAmp` | map to `setValueAtTime` | ● (same event shape) | map: events -> bytes |
| `RtMidiIn.getMessage` | map: bytes -> noteOn(Hz,amp) | map | map | map: bytes -> event | ● (same bytes) |
| `MidiFileIn` events | map, plus own tempo/tick scheduler | map | map | map (+ scheduler) | map (+ scheduler) |
| aubio `Notes` | map (MIDI note already) | map | map | map: result -> note events | map |
| aubio `Onset/Pitch` | trigger / Hz directly | Hz directly to `setFreq` | `setValueAtTime(hz)` | map (+ note logic) | map |
| STK envelopes (`ADSR`, ...), per sample | ✕ (no JS param API on instruments) | per-block value to `setAmp` | per-block value | ✕ | ✕ |
| SndObj generator (`Randi`, ...) as modulator | ✕ | ● (object as modulator) | `○` via `block()` mean/peak per call | ✕ | ✕ |
| LabSound node output | ✕ | ✕ | ● (`node.connect(param)`) | ✕ | ✕ |
| LabSound `AnalyserNode` data | `JS` reduction (peak, centroid) | ditto | ditto | ditto | ditto |

Reading: only two control paths are native and complete: PortMidi event shape
and `RtMidi` bytes feeding STK/LabSound by hand mapping, and
LabSound-to-LabSound `connect(param)`. Everything else is JS glue.

## 7. Sinks and sources with no useful partner

Functionality that exists but has no (or only a lossy) properly formatted
counterpart on the other side. Evidence is the binding source.

### 7.1 Whole classes

| Item | Why its output or input is stranded | Evidence |
|---|---|---|
| **SndObj effects**: `Reson`, `Comb`, `Allpass`, `VDelay`, `Gain`, `ADSR`, `Mixer` | Inputs are `SndObj*` only; there is no `SndObj` source class that wraps a `Float32Array`, file, device or STK output, and the doc scopes out `SndWave/SndRTIO`. They can process only SndObj generators (`Oscili`, `Buzz`, `Randi`). Audio from `SndFile`, `PaStream`, STK or LabSound cannot enter. | `doc/native/sndobj.md` "Not bound"; no `Float32Array` input path in `quickjs-sndobj.cpp` |
| **SndObj `Mixer`/`Gain`/etc. as post-FX for other modules** | Same reason. Useful only inside an all-SndObj chain ending at `block()`. | ditto |
| **LabSound `AudioListener`** | No `PannerNode` exists, so the listener affects nothing | `TODO.md` item 6 |
| **LabSound hardware input** | Input config is computed (`get_default_device_config`) but there is no node exposing it; the realtime graph has no live source other than oscillators/buffers | `TODO.md` item 11 |
| **LabSound `AudioDevice`** | Exposes `destinationNode` only: no way to route the destination to PortAudio, STK or a file stream | `quickjs-labsound.cpp` `audiodevice` funcs |
| **STK `RtAudio` stream methods** (`closeStream`, `startStream`, `stopStream`, `abortStream`, `isStreamOpen`, `isStreamRunning`, `getStreamTime`, `setStreamTime`, `getStreamLatency`, `getStreamSampleRate`) | The class binds device enumeration and stream lifecycle only; no `openStream` is bound (and cannot be: needs an audio-thread callback). These methods operate on a stream that JS can never open. Enumeration (`getDeviceCount/Names/Ids/Info`, defaults) remains useful. | comment at `quickjs-stk.cpp:3347`; no `openStream` in the file |
| **`PaStream` non-interleaved mode** (`paNonInterleaved`) | Constant is exported and accepted in `sampleFormat`, but `read`/`write` pass one pointer, whereas PortAudio then wants `void**`. No format-less binding for it. See 8.2. | `quickjs-portaudio.c:1138`, `:420`, `:440` |

### 7.2 Consumers of one format with no producer in it

| Consumer | Needs | Who produces it directly |
|---|---|---|
| `RubberBandStretcher.process` | planar `Array<Float32Array>` | only LabSound `AudioBuffer` (which returns copies) |
| `Aubio*.process` | mono, exactly `hopSize` | nothing; every producer needs `h` (and `m`) |
| STK `Filter/Effect` in place | `StkFrames` (F64) | only STK sources, `SndFile.read` into `frames.buffer`; all F32 modules need `d` |
| `SampleRateConverter`, `SoundTouch` | F32I | `SndFile`, `PaStream`, each other; not STK (F64) or RubberBand (planar) |
| `PaStream.write` | F32I, matching `sampleFormat` | `SndFile`, `SRC`, `ST`; not STK, RubberBand, LabSound |

### 7.3 Producers with no streaming consumer

| Producer | Stranded because |
|---|---|
| LabSound **realtime** output | The only tap is `AnalyserNode` (snapshot, mono). There is no `RecorderNode`, no `ScriptProcessor`/`AudioWorklet`, no `MediaStreamDestination` (`TODO.md` item 10). To get samples into JS you must use `OfflineAudioContext`, which renders the whole length. |
| LabSound **offline** | Result is one complete `AudioBuffer`; fine for files, not for a live loop. |
| aubio `Notes` with no consumer logic | Returns a note number and velocity; no module turns it into a stream of note events (STK `noteOn`, PortMidi write) without glue. |
| `MidiFileIn` | Yields raw ticks and bytes; no `MidiFileOut`, no sequencer and no tempo map, so playback timing is entirely user code. |
| `PortMidi` timestamps | `PortMidi.time()` (ms) is not related to any audio clock in the other modules. |

### 7.4 Asymmetries between source and sink of the same module

| Module | Source side | Missing sink side |
|---|---|---|
| `sndfile` | file read | OK, symmetric |
| STK I/O | `RtWvIn`, `InetWvIn` | no `FileWvIn`/`FileWvOut`/`FileLoop`/`FileRead`; `WvIn`/`WvOut` family bound only for realtime and network |
| `portaudio` | `read` | OK, symmetric |
| LabSound | decode file, device out | no input from device or JS stream, no recorder |
| SndObj | generators, `block()` | no input class |
| `aubio` | - | analysis only; output is control, no audio out |

## 8. Hazards and mismatches that affect interoperability

### 8.1 `AubioTempo` is documented, not built
`doc/native/aubio.md` shows `AubioTempo`; `quickjs-aubio.c` has no `Tempo` symbol
(only `AubioNotes`, `AubioOnset`, `AubioPitch`). Docs and `TODO` should say
"planned". It is also the only aubio object that would produce a beat-clock
control signal.

### 8.2 `paNonInterleaved`
Exported and accepted, but `PaStream.read/write` take one buffer and call
`Pa_ReadStream/WriteStream` with a single pointer; PortAudio's non-interleaved
mode expects an array of per-channel pointers. Using the flag would read
or write wrong memory. Either reject the flag in the constructor or bind an
array-of-buffers form (which would also make planar RubberBand output direct).

### 8.3 `AudioBuffer.getChannelData()` returns a copy
`make_float32_array` copies the channel (`JS_NewArrayBufferCopy`,
`quickjs-labsound.cpp:198`). In WebAudio the result is a live view, so
code ported from the web (`buf.getChannelData(0).set(x)`) silently changes
nothing; use `copyToChannel`. It also makes every read an allocation, which
matters for per-block use.

### 8.4 `PaStream` does not check element type
`read/write` accept any buffer and only compute the byte length. Passing a
`Float64Array` (e.g. `StkFrames.buffer` viewed as F64) to a `paFloat32`
stream is accepted and plays noise at half length. A type check or a
`d`-converting wrapper would stop the most likely STK -> PortAudio mistake.

### 8.5 Rate and clock mismatches
- STK has a process-global rate (`Stk.sampleRate`), applied at construction
  by many classes (for example `Echo`). Changing it mid-program does not
  retune live objects.
- LabSound's realtime context uses the device's nominal rate (often 48000)
  while the others default to 44100. `aubio` is constructed with a rate and
  trusts the caller.
- MIDI time bases (ms, delta seconds, ticks) have no conversion.

### 8.6 Device contention
`portaudio`, `RtWvIn/RtWvOut`, and LabSound's realtime context each open the
audio device independently. Two of them at once typically works only on
mixing back ends (PulseAudio, PipeWire, JACK) and fails or reorders on raw
ALSA. There is also a known crash when reopening a `PaStream` after close,
which `BUGS` records as `pastream-reopen-after-close-segfault`, so
"close then reopen" patterns are unsafe.

### 8.7 STK exception behavior
Per `doc/native/stk-io.md`, DSP classes let `stk::StkError` propagate as an
unhandled C++ exception, while the I/O classes convert it to a `TypeError`.
A channel-count mismatch on `tick(StkFrames, channel)` is the kind of misuse
interop code is likely to hit. *(doc; not verified in code)*

### 8.8 Lifetime across the module boundary
- SndObj retains inputs automatically (hidden strong refs), so graphs are
  safe, but this only helps inside the SndObj module.
- `StkFrames.buffer` keeps the frames alive for as long as the `ArrayBuffer`
  lives; the reverse (wrapping an existing `Float64Array` as `StkFrames`) is
  not possible, so the data flow always starts from a `StkFrames` object.
- LabSound nodes keep their context alive; an `AudioBuffer` is independent.

### 8.9 Output-size heuristics
`SampleRateConverter` and `SoundTouch`/`RubberBand` produce a variable frame
count; the caller must size the destination (see "output-sizing heuristic" in
`doc/native/samplerate.md`) and call `flush()`/`end` correctly or the tail is
lost.

## 9. Gaps, ranked

Most valuable additions to improve interoperability, roughly by leverage:

1. **A shared glue module** (plain JS or small C): `interleave(planar, out)`,
   `deinterleave(buf, ch, out)`, `f32ToF64`, `f64ToF32`, `mixdown(buf, ch)`,
   `rechunk(hop)`, and `mtof`, `vel2amp`. Removes every `i`/`d`/`m`/`h` mark
   in the matrix. Cheapest fix, affects every pair.
2. **SndObj input source**: a new `SndObj` subclass whose output vector is filled
   from a `Float32Array` each block; unlocks all seven effects for external audio.
3. **LabSound streaming bridge**: either a `RecorderNode` (TODO 10) to tap
   realtime output, or an `AudioWorklet`-like pull/push node driven from the JS
   thread (follows the "no JS on the audio thread" rule in the binding skill:
   ring buffer filled by JS, drained by the engine).
4. **LabSound live input** (TODO 11), `ChannelSplitter/Merger` (TODO 7),
   `PannerNode` (TODO 6) so the listener is useful.
5. **`PaStream` fixes**: reject `paNonInterleaved`; optionally accept the
   expected `TypedArray` kind per `sampleFormat`.
6. **STK**: `FileWvIn/FileWvOut/FileLoop` or a documented `SndFile` idiom;
   a constructor `StkFrames.fromFloat64Array(view, channels)` to wrap external
   data; a note-name/MIDI-number overload for `noteOn`.
7. **Unified MIDI event shape**: one `{status, data1, data2, time}` event
   and converters for `RtMidiIn` and `MidiFileIn`; a small tempo-map
   scheduler for `MidiFileIn`.
8. **Aubio**: build `AubioTempo`; optional internal re-chunking for
   `process(buffer)` with any length.
9. **Float32 bridge for `StkFrames`**: done: `toFloat32(out?)`,
   `fromFloat32(src)` and `sampleType`. Still open: the mixed-precision STK
   build (Float32 I/O, Float64 filter state; `TODO.md`), which removes the
   copy entirely.
10. **`RtAudio`**: either remove stream methods from the export list or
    document them as unavailable.

## 10. Bridge cookbook (all in JS today)

```js
// StkFrames (F64I) <-> Float32 interleaved
const f64 = new Float64Array(frames.buffer);   // live alias, no copy
const f32 = Float32Array.from(f64);            // d: for PaStream/SRC/ST
f64.set(f32);                                  // the other way

// F32 interleaved -> planar for RubberBand
function deinterleave(src, ch, out /* Float32Array[ch] */) {
  for (let c = 0; c < ch; c++)
    for (let i = 0, n = out[c].length; i < n; i++) out[c][i] = src[i * ch + c];
}
function interleave(planar, dst) {
  const ch = planar.length;
  for (let c = 0; c < ch; c++)
    for (let i = 0, n = planar[c].length; i < n; i++) dst[i * ch + c] = planar[c][i];
}

// stereo/multichannel interleaved -> mono hop for aubio
function mono(src, ch, hop /* Float32Array(hopSize) */) {
  for (let i = 0; i < hop.length; i++) {
    let s = 0;
    for (let c = 0; c < ch; c++) s += src[i * ch + c];
    hop[i] = s / ch;
  }
}

// MIDI note + velocity -> STK
const mtof = n => 440 * 2 ** ((n - 69) / 12);
inst.noteOn(mtof(ev.data1), ev.data2 / 127);

// SndFile directly into STK (zero-copy)
const frames = new StkFrames(4096, 2);
file.read(new Float64Array(frames.buffer), 4096);
filter.tick(frames);                           // in place
out.write(new Float64Array(frames.buffer), 4096); // SndFile sink

// LabSound AudioBuffer from JS data (whole-buffer only)
const buf = ctx.createBuffer(2, n, ctx.sampleRate);
buf.copyToChannel(left, 0);                    // not getChannelData(0).set()
buf.copyToChannel(right, 1);
new AudioBufferSourceNode(ctx, { buffer: buf }).connect(ctx.destination);
```

## 11. Verification status

- Format, framing and class lists were taken from source greps and reads of the
  files named above.
- No module was loaded or run, no test was executed, and no line of
  `quickjs-*.c`/`.cpp` was modified.
- Claims that need a run to confirm before acting on them: 8.2 (misuse of
  `paNonInterleaved`), 8.4 (silent acceptance of a `Float64Array`), 8.7 (STK
  exception behavior on bad channel counts), the effect rows of 6.1 that depend
  on STK channel rules, and the exact `RtMidiIn` timestamp semantics in
  section 5.
