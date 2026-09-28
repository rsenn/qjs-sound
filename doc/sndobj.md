# SndObj bindings

`quickjs-sndobj.cpp` wraps a subset of Victor Lazzarini's SndObj library
(`third_party/sndobj`) as classes exported from the `sndobj` module: a
wavetable, three source generators, and seven signal-processing effects,
enough to build a synth with sound-sculpting (filtering), modulation
(LFO-style inputs), and an FX chain (delay/reverb primitives).

```js
import { HarmTable, Oscili, Buzz, Randi, ADSR, Reson, Comb, Allpass, VDelay, Gain, Mixer } from 'sndobj';
```

File I/O is intentionally out of scope here - the `sndfile` module already
covers reading/writing WAV files. Every processing class below exposes its
raw output samples via `.output(pos)`/`.block()` for JS to hand off to
`sndfile`, or to any other consumer.

## The pull-model graph, and driving it

SndObj is a pull-based audio graph: an object is constructed with a pointer
to its upstream input (and, for many classes, extra modulator inputs for
frequency/amplitude/bandwidth/gain), and every audio block **the caller**
must call `.process()` on each node once, **in dependency order** - there is
no automatic recursion. This mirrors the C++ library's own examples
(`third_party/sndobj/src/examples/schroeder.cpp`) and its idiom exactly;
this binding adds no scheduler on top.

```js
const sine = new HarmTable(4096, 1, HarmTable.SINE);
const lfo = new Randi(6, 0.15);                       // smooth random LFO
const osc = new Oscili(sine, 220, 0.5, null, lfo);    // amp modulated by lfo
const env = new ADSR(0.01, 1, 0.1, 0.7, 0.3, 1.0, osc);
const filt = new Reson(800, 150, env);
const gain = new Gain(-6, filt);

for (let block = 0; block < 100; block++) {
  lfo.process();
  osc.process();
  env.process();
  filt.process();
  gain.process();
  const samples = gain.block(); // Float32Array, length === gain.vecsize
}
```

## Lifetime: connected objects are kept alive automatically

SndObj's own C++ objects hold raw, non-owning pointers to their
inputs/modulators. This binding compensates by retaining a strong (hidden,
non-enumerable) JS reference to every connected dependency on the object
that uses it, so a modulator or input can never be garbage-collected while
something native still points to it - drop your own references to the
whole graph and it is released correctly, cascading through the retained
chain via ordinary refcounting.

## Shared API (generators and effects)

Every generator and effect below shares this API:

- **`.process()`** - runs one block (`SndObj::DoProcess`); throws if the
  underlying object reports an error afterward.
- **`.output(pos)`** - reads one already-computed sample (`SndObj::Output`).
- **`.block()`** - convenience: returns the whole current output vector as
  a `Float32Array` of length `.vecsize`.
- **`.sr`** (get/set), **`.vecsize`** (get/set), **`.error`** (get) -
  `GetSr`/`SetSr`, `GetVectorSize`/`SetVectorSize`, `GetError`.
- **`.enable()`** / **`.disable()`**.

## `HarmTable` - harmonic wavetable (`HarmTable.h`)

```js
new HarmTable()
new HarmTable(length, harmonics, type, phase = 0)
```

`type` is one of `HarmTable.SINE`, `HarmTable.SAW`, `HarmTable.SQUARE`,
`HarmTable.BUZZ` (mirroring the C++ `HarmTable.h` enum).

- **`.len`** (get) - table length.
- **`.lookup(pos)`** - one sample, wrapped modulo length.
- **`.toArray()`** - a **copy** of the table as a `Float32Array` (not a live
  view - `MakeTable()` can reallocate the backing array).
- **`.setHarm(harmonics, type)`**, **`.setPhase(phase)`**, **`.makeTable()`**
  (rebuilds the table after changing harmonics/phase; returns `boolean`).

## Generators (no audio input, only modulators)

### `Oscili` - interpolating table-lookup oscillator (`Oscili.h`)

```js
new Oscili(table, freq = 440, amp = 1, freqMod = null, ampMod = null, vecsize, sr)
```

- **`.setFreq(hz, mod = null)`**, **`.setAmp(amp, mod = null)`** - `mod` is
  any generator/effect instance whose output drives the parameter; omit or
  pass `null` to clear it.
- **`.setTable(table)`**, **`.setPhase(phase)`**.

### `Buzz` - band-limited pulse/buzz generator (`Buzz.h`)

```js
new Buzz(freq = 440, amp = 1, harmonics = 1, freqMod = null, ampMod = null, vecsize, sr)
```

- **`.setFreq(hz, mod = null)`**, **`.setAmp(amp, mod = null)`**,
  **`.setHarm(harmonics)`**.

### `Randi` - interpolated (ramped) random, a smooth LFO (`Randi.h`)

```js
new Randi(freq = 1, amp = 1, freqMod = null, ampMod = null, vecsize, sr)
```

- **`.setFreq(hz, mod = null)`**, **`.setAmp(amp, mod = null)`**.

## Effects (require an audio-signal input)

### `ADSR` - attack/decay/sustain/release envelope (`ADSR.h`)

```js
new ADSR(attack, maxAmp, decay, sustain, release, duration, input = null, vecsize, sr)
```

Applies the envelope to `input`'s output (not a modulator - the signal
being shaped).

- **`.setMaxAmp(amp)`**, **`.sustain()`**, **`.release()`**, **`.restart()`**,
  **`.setADSR(attack, decay, sustain, release)`**, **`.setDur(duration)`**.

### `Reson` - modulatable 2nd-order bandpass filter (`Reson.h`)

```js
new Reson(freq, bw, input, freqMod = null, bwMod = null, vecsize, sr)
```

- **`.setFreq(hz, mod = null)`**, **`.setBW(bw, mod = null)`**.

### `Comb` / `Allpass` - delay-line building blocks for reverb (`Comb.h`, `Allpass.h`)

```js
new Comb(gain, delayTime, input, vecsize, sr)
new Allpass(gain, delayTime, input, vecsize, sr)
```

- **`.setGain(gain)`**.

Combine several `Comb`s through a `Mixer`, then chain through a couple of
`Allpass`es, to build a Schroeder-style reverb (see
`third_party/sndobj/src/examples/schroeder.cpp` for the canonical topology):

```js
const mix = new Mixer();
for (const dt of [0.0297, 0.0371, 0.0411, 0.0437])
  mix.addObj(new Comb(0.7, dt, dry));
const ap1 = new Allpass(0.7, 0.01, mix);
const ap2 = new Allpass(0.7, 0.0017, ap1);
```

### `VDelay` - modulatable delay with feedback/feedforward/direct gain (`VDelay.h`)

Chorus/flanger/echo building block.

```js
new VDelay(maxDelayTime, delayTime, fdbGain, fwdGain, dirGain, input,
           timeMod = null, fdbMod = null, fwdMod = null, dirMod = null, vecsize, sr)
```

- **`.setMaxDelayTime(t)`**, **`.setDelayTime(t)`**,
  **`.setFdbgain(gain, mod = null)`**, **`.setFwdgain(gain, mod = null)`**,
  **`.setDirgain(gain, mod = null)`**.

### `Gain` - level control (`Gain.h`)

```js
new Gain(gainDb, input, vecsize, sr)
```

- **`.setGain(dB)`**, **`.setGainM(multiplier)`**, **`.dBToAmp(dB)`**.

### `Mixer` - N-input summer (`Mix.h`)

```js
new Mixer(vecsize, sr) // always starts empty; populate with addObj()
```

- **`.addObj(obj)`**, **`.deleteObj(obj)`**, **`.objNo`** (get) - number of
  currently mixed inputs.

## Not bound (possible future work)

- **`Pan`** - constant-power stereo pan. Its constructor manufactures two
  extra live `SndObj` taps (`left`/`right`), a different lifetime shape
  than every class above (which only ever take caller-supplied
  dependencies); left out of this first pass. Run two independent mono
  chains for stereo placement in the meantime.
- File/realtime-device/MIDI I/O (`SndWave`, `SndRTIO`, `SndMidi*`, ...) -
  use the `sndfile` module for file I/O instead.
- The FFT/phase-vocoder family (`PVA`, `SinAnal`, `Convol`, ...) - needs a
  vendored `rfftw` header this tree doesn't ship on its own include path.
