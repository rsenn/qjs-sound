# soundtouch bindings

`quickjs-soundtouch.cpp` wraps [SoundTouch](https://www.surina.net/soundtouch/)
(`/usr/include/soundtouch/SoundTouch.h`, class `soundtouch::SoundTouch`) as
a JS module named `soundtouch`. This document was written as the design
doc before implementing it (see `quickjs-native-bindings` skill section
0); the API below matches what was actually built.

```js
import { SoundTouch } from 'soundtouch';
```

## Survey: existing SoundTouch bindings/wrappers

- **SoundTouchJS** (`cutterbl/SoundTouchJS`, a from-scratch JS *port* of
  the algorithm, not a native binding, but the most widely-used JS-facing
  API over these same concepts) - classic API: `new SoundTouch(sampleRate)`,
  property setters `.tempo`/`.pitch`/`.rate` (ratios, `1.0` = unchanged)
  plus `.pitchSemitones`/`.pitchOctaves` as convenience layers computed as
  `pitch = 2^(semitones/12)`. Streaming stays explicit: push samples in,
  call `.process()`, pull samples out - no single "process this whole
  buffer" convenience. The **current** rewrite narrowed the public surface
  to only `.pitch` (+ semitones/octaves), dropping `.tempo`/`.rate`,
  because in a Web Audio context playback rate is better handled by the
  source node itself.
- **pysoundtouch** (`jrising/pysoundtouch`, older, Python) -
  `soundtouch.SoundTouch(sampling_rate, channels)`, `set_pitch_shift(semitones)`,
  `put_samples(buffer)`/`get_samples(frame_count)`, readiness via
  `ready_count()`/`waiting_count()`, `clear()`. Only semitone-based pitch
  exposed; no tempo/rate setters.
- **Rubber Band**: no binding found wrapping both it and SoundTouch under
  one shared API - not pursued further.

**Convergent patterns**:
- Push/pull streaming (`putSamples`/`receiveSamples`) is **always kept
  explicit**, never collapsed into one synchronous call - true across both
  JS and Python bindings independently.
- **Property setters over `setXxx()` methods** in the JS-idiomatic
  wrapper; Python instead uses explicit setter functions - the
  property-vs-method choice tracks host-language idiom, and JS is the
  relevant one here.
- **Ratio (`1.0` = unchanged) is the base unit** for tempo/rate; semitones/
  octaves are a convenience layer computed from the ratio, never a
  replacement for it.
- SoundTouchJS's real-world narrowing (dropping tempo/rate) was driven by
  a **Web Audio-specific reason** (the browser already has a rate knob
  elsewhere) that doesn't apply to a general native binding - this binding
  **deliberately does not follow that narrowing** and exposes the full
  underlying `tempo`/`pitch`/`rate` trio, per skill section 0's "diverge
  only with a concrete, stated reason."

## Sample type

SoundTouch's `SAMPLETYPE` is a compile-time choice (`float` or `short`);
the system library this project links against (`libSoundTouch.so`,
confirmed via `nm -D` showing `putSamples(float const*, ...)` symbols) is
built for **float**, matching this project's own float-everywhere
convention (`PaStream`'s default `paFloat32`, STK's `StkFloat`). All
sample buffers below are `Float32Array` only - checked via
`js_bufferview_get_kind(ctx, val, JS_TYPEDARRAY_FLOAT32, &view)` from the
shared `quickjs-typedarray.h` (see `doc/sndfile.md`'s "Shared
TypedArray/buffer interop" section, used unchanged here). Samples are
interleaved per SoundTouch's own convention (a "sample" for a stereo
stream is one L+R pair).

## `SoundTouch` - `soundtouch::SoundTouch`

### Constructor

```ts
new SoundTouch(sampleRate: number, channels: number)
```

Matches `setSampleRate`/`setChannels` both being required before any
processing per the class's own doc comment; taking them as constructor
args (rather than a later `.setSampleRate()`/`.setChannels()` call) avoids
a "constructed but not yet usable" half-initialized state. Two required
positional args, no options object - matches this project's own
"don't reach for an options object for 2 params" judgement already
applied when redesigning `quickjs-portaudio.c` (an options object was
added there specifically because that constructor has 5+ optional knobs;
this one doesn't).

### Properties

None of `tempo`/`pitch`/`rate` have a native getter in the C++ API (only
`setTempo`/`setTempoChange`, `setPitch*`, `setRate*` - no `getTempo()`
etc. exist). Each property getter below reads back a value **cached in
the binding's own opaque struct at the last successful set**, not queried
from SoundTouch itself - worth documenting clearly since it's not a pure
passthrough.

| Property | Maps to (setter) | Notes |
|---|---|---|
| `.tempo` | `setTempo(double)` | Ratio, `1.0` = unchanged. |
| `.pitch` | `setPitch(double)` | Ratio, `1.0` = unchanged. |
| `.pitchSemitones` | `setPitchSemiTones(double)` | Convenience layer; reading it back computes `12 * log2(pitch)` from the cached `.pitch` ratio (SoundTouch itself does the equivalent conversion internally on write, per SoundTouchJS's convergent formula). |
| `.pitchOctaves` | `setPitchOctaves(double)` | Same idea: `log2(pitch)`. |
| `.rate` | `setRate(double)` | Ratio, `1.0` = unchanged. |
| `.channels` | `numChannels()` (real getter) | Read-only after construction (no `setChannels` exposed post-construction - matches the class's own "set channels once, up front" contract). |
| `.sampleRate` | - (no native getter) | Cached from the constructor argument, same caveat as tempo/pitch/rate. |

`setTempoChange`/`setRateChange` (percent-change variants of `setTempo`/
`setRate`) are **not** separately exposed - they're a different unit
(`newTempo = tempo * (1 + change/100)`) for the *same* underlying ratio
`.tempo` already covers, and no surveyed binding exposed both a ratio and
a percent-change setter for the same knob. Skippable, computable by the
caller if ever needed (`st.tempo = 1 + changePercent / 100`).

### Methods

| Member | Maps to | Notes |
|---|---|---|
| `.putSamples(buffer)` | `putSamples(const float*, uint numSamples)` | `buffer` a `Float32Array`; `numSamples` is `buffer.length / channels` (SoundTouch's own "a sample means one frame across all channels" convention), matching `PaStream`'s frames-not-bytes convention. Reads straight out of `buffer`, zero-copy. |
| `.receiveSamples(buffer)` | `receiveSamples(float*, uint maxSamples)` | Fills `buffer` (a pre-allocated `Float32Array`) with up to `buffer.length / channels` samples, zero-copy; returns the number of *samples* (frames) actually written - buffer is not resized/reallocated per call, matching every surveyed binding's push/pull shape (caller owns the output buffer). This caller-owned-buffer-plus-returned-count shape is exactly what `sndfile.md`'s "Cross-binding zero-copy pipeline" section builds the whole `sndfile` → `samplerate` → `soundtouch` → `sndfile` chain around. |
| `.numSamples()` | `numSamples()` | How many processed samples are ready to `.receiveSamples()`. |
| `.numUnprocessedSamples()` | `numUnprocessedSamples()` | How many input samples are still queued, not yet processed. |
| `.flush()` | `flush()` | Pushes the last buffered samples through to output; call once at the end of a stream, not mid-stream (per the class's own doc comment). |
| `.clear()` | `clear()` | Drops all buffered input/output/internal state. |
| `.setSetting(id, value)` | `setSetting(int, int)` | `id` is one of the `SETTING_*` constants below. Returns `boolean` (native return value, unlike most of this project's methods which throw - there's no error text to throw *with*, `setSetting` just reports invalid-id/value as `false`). |
| `.getSetting(id)` | `getSetting(int)` | Returns the current value for `id`. |

### Constants (`SETTING_*`, for `setSetting`/`getSetting`)

`SETTING_USE_AA_FILTER`, `SETTING_AA_FILTER_LENGTH`,
`SETTING_USE_QUICKSEEK`, `SETTING_SEQUENCE_MS`, `SETTING_SEEKWINDOW_MS`,
`SETTING_OVERLAP_MS`, `SETTING_NOMINAL_INPUT_SEQUENCE`,
`SETTING_NOMINAL_OUTPUT_SEQUENCE`, `SETTING_INITIAL_LATENCY`.

### `[Symbol.toStringTag]`

`"SoundTouch"`, per skill section 21 (every class gets one).

### Example

```js
import { SoundTouch } from 'soundtouch';

const st = new SoundTouch(44100, 2);
st.tempo = 1.15;        // 15% faster
st.pitchSemitones = -2; // down two semitones, independent of tempo

const input = new Float32Array(1024 * 2); // 1024 frames, stereo interleaved
// ... fill input ...
st.putSamples(input);
st.flush();

const output = new Float32Array(2048 * 2);
let total = 0, got;
while ((got = st.receiveSamples(output.subarray(total * 2))) > 0)
  total += got;
```

## Resolved during implementation

- **`.pitchSemitones`/`.pitchOctaves` as cached-and-derived properties**:
  kept, per the survey's convergence.
- **`setSetting`'s `boolean` return**: kept as a native `boolean`, not
  translated to a throw.
- **`SAMPLETYPE == short` builds**: not handled (still `Float32Array`-only,
  matching the currently-linked float build) - flagged for whoever
  touches this next if the linked library ever changes.

A pre-existing bug affecting this style of constructor was found and
fixed while implementing `sndfile`/`samplerate` just before this binding
(see `constructor-fallback-proto-double-free` in `BUGS`): a constructor
that falls back to a shared module-static default prototype when
`new_target.prototype` isn't an object must `JS_DupValue` that fallback
before use, not just free it - `js_soundtouch_constructor` does this
correctly from the start.
