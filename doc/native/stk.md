# stk

Source: `quickjs-stk.cpp`, module `stk` (`import { SineWave } from 'stk'`).

Bindings for the [Synthesis ToolKit](https://github.com/thestk/stk): generators, filters, effects,
instruments and the `StkFrames` sample buffer. The I/O classes (`RtWvIn`, `RtWvOut`, `RtAudio`,
`RtMidiIn`, `RtMidiOut`, `InetWvIn`, `InetWvOut`, `MidiFileIn`) are in [`stk-io.md`](stk-io.md).

Every class is exported flat from the module and constructed with `new`. Each instance has its own
`Symbol.toStringTag`, so `Object.prototype.toString.call(new Clarinet(10))` is `[object Clarinet]`.

```js
import { Stk, StkFrames, SineWave, BiQuad, Echo, Clarinet } from 'stk';

Stk.setSampleRate(44100);
Stk.setRawwavePath('third_party/stk/rawwaves/');

const sine = new SineWave();
sine.setFrequency(440);

const lowpass = new BiQuad();
lowpass.setLowPass(1000);

const echo = new Echo(22050);
echo.setEffectMix(0.3);

const block = new Float32Array(1024);
for(let i = 0; i < block.length; i++) block[i] = echo.tick(lowpass.tick(sine.tick()));

const out = new StkFrames(1024, 1);
out.fromFloat32(block);

const clar = new Clarinet(10);
clar.noteOn(440, 0.8);
const sample = clar.tick();
```

## Families and how they are exposed

One JS class per C++ hierarchy level, shared by every leaf of that level. The leaves are separate
constructors on the same prototype, so `instanceof` does not tell `BiQuad` from `OnePole`; use
`Object.prototype.toString.call(x)`. A method that only some leaves have is dispatched by type and
throws `TypeError` (`setPole is not supported by this filter`) on the others.

| Family | Constructors | Methods |
|---|---|---|
| Generator | `ADSR Asymp Blit BlitSaw BlitSquare Envelope Granulate Modulate Noise SineWave SingWave` | `tick`, oscillator setters |
| Filter | `BiQuad Delay DelayA DelayL Fir FormSwep Iir OnePole OneZero PoleZero TapDelay TwoPole TwoZero` | `tick`, `setGain`, `getGain`, `clear`, per-type setters |
| Effect | `Chorus Echo FreeVerb JCRev LentPitShift NRev PitShift PRCRev` | `tick`, `setEffectMix`, `clear` |
| Instrument | `BandedWG BlowBotl BlowHole Bowed Brass Clarinet Drummer Flute Mandolin Mesh2D ModalBar Moog Plucked Recorder Resonate Saxofony Shakers Simple Sitar StifKarp VoicForm Whistle TwinTDrum Tr909BassDrum Tr909Percussion` | `tick`, `noteOn`, `noteOff`, `controlChange` |
| FM | `BeeThree FMVoices HevyMetl PercFlut Rhodey TubeBell Wurley` | `tick`, `noteOn`, `noteOff`, `controlChange` |
| other | `Guitar`, `Twang`, `Cubic` | see below |

`Generator`, `Filter`, `Effect` and `FM` are also exported as (empty) namespace objects.

## Stk

Static functions on the `Stk` object (not on an instance).

| Function | Description |
|---|---|
| `Stk.sampleRate()` | Rate used by every STK object created afterwards, default 44100. |
| `Stk.setSampleRate(rate)` | Sets it. `RangeError` unless `rate > 0`. |
| `Stk.rawwavePath()` | Directory the `.raw` wavetables are read from, default `../../rawwaves/`. |
| `Stk.setRawwavePath(path)` | Sets it; end it with `/`. |

Instruments (`Clarinet`, `Flute`, `Rhodey`, the FM family, `Mandolin`, `SingWave`, ...) read wavetables
from the rawwave path when constructed. A wrong path throws `InternalError: FileRead::open: could not
open or find file (...)`, so set it first. `third_party/stk/rawwaves/` holds them in this tree.

## StkFrames

An interleaved sample buffer, the unit `tick` fills and the I/O classes exchange.

```js
const f = new StkFrames(frames, channels);   // channels default 1, filled with 0
```

| Member | Description |
|---|---|
| `size`, `frames`, `channels` | Number of samples, frames and channels (`size = frames * channels`). |
| `empty` | `true` when `size` is 0. |
| `dataRate` | Sample rate the buffer is meant for; assignable. |
| `buffer` | An `ArrayBuffer` aliasing the samples; view it with `new Float64Array(f.buffer)` or `Float32Array` as `sampleType` says. |
| `sampleType` | `'f64'` or `'f32'`, from `sizeof(StkFloat)`. |
| `resize(frames, channels?)` | Changes the size. |
| `interpolate(frame, channel?)` | Linear interpolation at a fractional frame; returns a number. |
| `getChannel(channel, dst, dstChannel)` | Copies a channel into another `StkFrames`; returns `dst`. |
| `setChannel(channel, src, srcChannel)` | Copies a channel of another `StkFrames` into this one. `TypeError` when `src` is not an `StkFrames`. |
| `toFloat32(out?)`, `fromFloat32(src)` | Float32 copies, see [`stk-io.md`](stk-io.md#stkframes-sample-type-and-float32-conversion). |

## tick

Every generator, filter, effect and instrument has `tick`, with two forms:

```js
y = x.tick(input);          // one sample in, one sample out (generators: x.tick(channel))
x.tick(frames, channel);    // fills or processes channel `channel` of an StkFrames in place, returns it
```

A generator and an instrument take no input, so their scalar form is `tick()` (an optional channel).
An effect that has two outputs returns an array of two numbers; `FreeVerb` takes two inputs:
`freeverb.tick(left, right)`. `RangeError` for a channel outside the buffer.

## Generators

`new SineWave()`, `new Blit(frequency = 220)`, `new BlitSaw(frequency = 220)`, `new BlitSquare(frequency = 220)`,
`new Noise(seed = 0)`, `new SingWave(file, raw = false)`, `new Granulate(voices, file, raw)`, `new ADSR()`,
`new Asymp()`, `new Envelope()`, `new Modulate()`.

| Method | On | Description |
|---|---|---|
| `setFrequency(hz)` | `SineWave Blit BlitSaw BlitSquare` | Oscillator frequency. |
| `setHarmonics(n)` | `Blit BlitSaw BlitSquare` | Number of harmonics, 0 for as many as fit below Nyquist. |
| `reset()` | `SineWave Blit BlitSaw BlitSquare` | Restarts the phase. |
| `setRate(r)` | `SineWave` | Phase step per sample, in table entries. |
| `addTime(t)`, `addPhase(p)`, `addPhaseOffset(p)` | `SineWave` | Shift the phase (`p` in cycles, `t` in samples). |
| `channelsOut` | all | Number of output channels (getter). |

STK's `SineWave` has no amplitude and no frequency getter; it always has unit amplitude, so scale
the output or follow it with an `ADSR`.

```js
const s = new SineWave();
s.setFrequency(Stk.sampleRate() / 4);   // one cycle every 4 samples
s.reset();
[s.tick(), s.tick(), s.tick(), s.tick()];   // 0, 1, 0, -1
```

## Filters

Constructors: `new BiQuad()`, `new OnePole(pole)`, `new OneZero(zero)`, `new TwoPole()`, `new TwoZero()`,
`new PoleZero()`, `new FormSwep()`, `new Delay()`, `new DelayA(delay, maxDelay)`, `new DelayL(delay, maxDelay)`,
`new Fir(coefficients)`, `new Iir(bCoefficients, aCoefficients)`, `new TapDelay(taps, maxDelay)`.

| Method | On | Description |
|---|---|---|
| `setGain(g)`, `getGain()`, `clear()` | all | Gain and filter state. |
| `setCoefficients(...c, clearState?)` | `OnePole OneZero` (2), `TwoPole TwoZero PoleZero` (3), `BiQuad` (5) | Coefficients as STK names them: `OnePole(b0, a1)`, `OneZero(b0, b1)`, `TwoPole(b0, a1, a2)`, `TwoZero(b0, b1, b2)`, `PoleZero(b0, b1, a1)`, `BiQuad(b0, b1, b2, a1, a2)`; `TypeError` when fewer are given. |
| `setPole(p)` | `OnePole` | Pole position. |
| `setZero(z)` | `OneZero` | Zero position. |
| `setAllpass(c)`, `setBlockZero(p = 0.99)` | `PoleZero` | Allpass, or DC blocker. |
| `setResonance(hz, radius, normalize = false)` | `TwoPole BiQuad` | Resonance. |
| `setNotch(hz, radius)` | `TwoZero BiQuad` | Notch. |
| `setLowPass(fc, q?)`, `setHighPass(fc, q?)` | `BiQuad` | `q` defaults to 1/sqrt(2). |
| `setBandPass(fc, q)`, `setBandReject(fc, q)`, `setAllPass(fc, q)` | `BiQuad` | |
| `setEqualGainZeroes()` | `BiQuad` | Zeros at DC and Nyquist. |

```js
const o = new OnePole(); o.setPole(0.5); o.setGain(2);
new Delay().setPole(0.5);   // TypeError: setPole is not supported by this filter
```

## Effects

Constructors: `new Chorus(baseDelay = 6000)`, `new Echo(maxDelay)`, `new FreeVerb()`, `new JCRev(t60 = 1)`,
`new NRev(t60 = 1)`, `new PRCRev(t60 = 1)`, `new PitShift()`, `new LentPitShift(ratio = 1, tMax)`.

| Method | Description |
|---|---|
| `setEffectMix(m)` | Dry/wet, 0 to 1. |
| `clear()` | Clears the internal delay lines. |

## Instruments and FM

`noteOn(frequency, amplitude)`, `noteOff(amplitude)`, `controlChange(number, value)`, `tick(channel?)`.
Constructors take what STK takes: `new Clarinet(lowestFrequency = 8)`, `new Plucked(lowestFrequency = 10)`,
`new Mesh2D(nx, ny)` (both non-zero, else `RangeError`), `new TwinTDrum(frequency = 200)`; the FM family and most
others take none.

`Tr909BassDrum` and `Tr909Percussion` are drum synths with their own setters (`setPitchEnvelope`,
`setTune`, `trigger`, `render`, ...); `TwinTDrum` has `setFrequency`, `setDecay`, `setDrive`, `strike`, `render`.

## Guitar, Twang, Cubic

| Class | Description |
|---|---|
| `new Guitar(nStrings = 6, bodyFile?)` | `tick`, `noteOn`, `noteOff`, `controlChange`. |
| `new Twang(lowestFrequency)` | Plucked string: `tick(input)`, `setFrequency`, `setPluckPosition`, `setLoopGain`. |
| `new Cubic()` | Waveshaper: `tick(input)`, `setA1`, `setA2`, `setA3`, `setGain`, `setThreshold`. |

## Errors

`stk::StkError` becomes a JS error: `RangeError` for a bad argument, `InternalError` for the rest (a missing
rawwave file, ...), `OutOfMemory` for allocation. `TypeError` is for a wrong argument type or a setter the class lacks.
