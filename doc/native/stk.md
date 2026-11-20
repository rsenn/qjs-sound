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
| FM | `BeeThree FMVoices HevyMetl PercFlut Rhodey TubeBell Wurley` | `tick`, `noteOn`, `noteOff`, `controlChange`, FM controls |
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
| `keyOn(target?)`, `keyOff(target?)` | `ADSR Asymp Envelope` | Start / release the envelope. Only `Envelope` takes a target (defaults 1 and 0). |
| `setTarget(v)`, `setValue(v)` | `ADSR Asymp Envelope` | Target level; jump straight to a value. |
| `setRate(r)` | `Envelope` too | Per-sample step of the ramp. |
| `setTime(t)` | `Asymp Envelope` | Seconds to the target. `Envelope.setTime` is relative to the current target and value (`rate = abs(target - value) / (t * sampleRate)`), so call `setTarget` first. |
| `setTau(t)`, `setT60(t)` | `Asymp` | Time constant, or time to fall by 60 dB. |
| `setAttackTime/DecayTime/ReleaseTime(t)`, `setAttackRate/DecayRate/ReleaseRate(r)`, `setAttackTarget(v)`, `setSustainLevel(v)` | `ADSR` | Segment times (seconds) or rates, attack peak, sustain level. |
| `setAllTimes(attack, decay, sustain, release)` | `ADSR` | All four at once (needs all four arguments). |
| `getState()` | `ADSR Asymp Envelope` | `ADSR`: 0 attack, 1 decay, 2 sustain, 3 release, 4 idle. |

```js
const env = new ADSR();
env.setAllTimes(0.01, 0.01, 0.5, 0.01);
env.keyOn();
for(let i = 0; i < 1500; i++) env.tick();   // 0.5, getState() is 2 (sustain)
env.keyOff();                               // falls to 0, getState() is 4 (idle)
```

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
`new PoleZero()`, `new FormSwep()`, `new Delay(delay = 0, maxDelay = 4095)`, `new DelayA(delay, maxDelay)`, `new DelayL(delay, maxDelay)`,
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
| `setDelay(n)`, `getDelay()` | `Delay DelayA DelayL` | Delay in samples; `Delay` takes an integer, `DelayA` and `DelayL` a fraction. `RangeError` below 0 or above the maximum. |
| `setMaximumDelay(n)`, `getMaximumDelay()` | `Delay DelayA DelayL` (`TapDelay` set only) | Length of the line. |
| `tapIn(value, tap)`, `tapOut(tap)` | `Delay DelayA DelayL` | Write to / read from a position `tap` samples back, without ticking. |
| `addTo(value, tap)`, `energy()` | `Delay` | Adds to a tap and returns the sum; sum of squares over the current delay length. |
| `nextOut()` | `Delay DelayA DelayL` | The sample the next `tick` will output. |
| `setTapDelays(array)`, `getTapDelays()` | `TapDelay` | Tap positions as an array of integers. |

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
| `setDelay(n)`, `setMaximumDelay(n)` | `Echo` only, in samples. |

## Instruments

`noteOn(frequency, amplitude)`, `noteOff(amplitude)`, `controlChange(number, value)`, `tick(channel?)`, plus the
controls below. Constructors take what STK takes: `new Clarinet(lowestFrequency = 8)`, `new Plucked(lowestFrequency = 10)`,
`new Mesh2D(nx, ny)` (both non-zero, else `RangeError`), `new TwinTDrum(frequency = 200)`; `BlowHole`, `Flute`,
`Mandolin` and `Saxofony` need a lowest frequency; the others take none.

All controls are on one shared prototype and dispatch by type: an instrument without a control throws `TypeError`
(`new Drummer().setFrequency(440)`), and so does a missing argument.

| Method | On | Description |
|---|---|---|
| `setFrequency(hz)` | most (not `Drummer`, `Shakers`, `Mesh2D`, `Resonate`) | Pitch. |
| `clear()` | all | Clears the internal state (a no-op where STK has none). |
| `pluck(amp)`, `pluck(amp, position)` | `Plucked Sitar StifKarp BandedWG`, `Mandolin` (with position) | Excites the string. |
| `startBlowing(amp, rate)`, `stopBlowing(rate)` | `BlowBotl BlowHole Brass Clarinet Flute Recorder Saxofony Whistle` | Breath envelope; `rate` is per sample. |
| `startBowing(amp, rate)`, `stopBowing(rate)` | `Bowed BandedWG` | Bow envelope. |
| `keyOn()`, `keyOff()` | `Resonate Simple` | Gate. |
| `strike(amp)`, `damp(amp)`, `setModeGain(i, g)`, `setRatioAndRadius(i, ratio, radius)`, `setMasterGain(g)`, `setDirectGain(g)` | `ModalBar` | Modal controls. |
| `setPreset(n)`, `setStrikePosition(p)` | `ModalBar BandedWG` | |
| `setStickHardness(h)` | `ModalBar` | |
| `setModulationDepth(d)` | `ModalBar Moog` | |
| `setModulationSpeed(s)` | `Moog` | |
| `setVibrato(g)` | `Bowed` | |
| `setLip(hz)` | `Brass` | |
| `setTonehole(v)`, `setVent(v)` | `BlowHole` | |
| `setJetReflection(c)`, `setEndReflection(c)`, `setJetDelay(r)` | `Flute` | |
| `setDetune(d)`, `setBodySize(s)`, `setPluckPosition(p)` | `Mandolin` | |
| `setBlowPosition(p)` | `Saxofony` | |
| `setStretch(s)`, `setPickupPosition(p)`, `setBaseLoopGain(g)` | `StifKarp` | |
| `setBlowPressure`, `setVibratoGain`, `setVibratoFrequency`, `setNoiseGain`, `setBreathCutoff`, `setSoftness` (value) | `Recorder` | |
| `setNX(n)`, `setNY(n)`, `setInputPosition(x, y)`, `setDecay(d)`, `inputTick(v)`, `energy()` | `Mesh2D` | `inputTick` and `energy` return numbers. |
| `setResonance(hz, radius)`, `setNotch(hz, radius)`, `setEqualGainZeroes()` | `Resonate` | |
| `setPhoneme(name)` | `VoicForm` | Returns `false` for an unknown phoneme. |
| `setVoiced(g)`, `setUnVoiced(g)`, `setFilterSweepRate(i, r)`, `setPitchSweepRate(r)`, `quiet()` | `VoicForm` | |

Some instruments are only audible after `noteOn`, which also sets their output gain and breath pressure:
`Sitar` (`pluck` alone is silent), `BlowBotl`, `Flute`, `Recorder`. A fast breath rate can drive `BlowBotl`
to `NaN` (a STK behavior).

`Tr909BassDrum` and `Tr909Percussion` are drum synths with their own setters (`setPitchEnvelope`,
`setTune`, `trigger`, `render`, ...); `TwinTDrum` has `setFrequency`, `setDecay`, `setDrive`, `strike`, `render`.

## FM instruments

`BeeThree FMVoices HevyMetl PercFlut Rhodey TubeBell Wurley`: `tick`, `noteOn`, `noteOff`, `controlChange` and

| Method | Description |
|---|---|
| `setFrequency(hz)` | Pitch. |
| `keyOn()`, `keyOff()` | Gate. |
| `setRatio(i, ratio)`, `setGain(i, gain)` | Frequency ratio and gain of operator `i`, 0 to 3; `RangeError` above 3. |
| `setModulationSpeed(s)`, `setModulationDepth(d)` | Vibrato. |
| `setControl1(v)`, `setControl2(v)` | The two instrument-specific controls. |
| `clear()` | Clears the state. |

## Guitar, Twang, Cubic

| Class | Description |
|---|---|
| `new Guitar(nStrings = 6, bodyFile?)` | `tick`, `noteOn(hz, amp, string = 0)`, `noteOff(amp, string = 0)`, `controlChange(number, value, string = -1)`, `setPluckPosition(p, string = -1)`, `setLoopGain(g, string = -1)`, `setFrequency(hz, string = 0)`, `setBodyFile(file = "")`, `clear()`. |
| `new Twang(lowestFrequency)` | Plucked string: `tick(input)`, `setFrequency`, `setLowestFrequency`, `setPluckPosition`, `setLoopGain`, `setLoopFilter(coefficients)`, `clear()`. |
| `new Cubic()` | Waveshaper: `tick(input)`, `setA1`, `setA2`, `setA3`, `setGain`, `setThreshold`. |

## Errors

`stk::StkError` becomes a JS error: `RangeError` for a bad argument, `InternalError` for the rest (a missing
rawwave file, ...), `OutOfMemory` for allocation. `TypeError` is for a wrong argument type or a setter the class lacks.
