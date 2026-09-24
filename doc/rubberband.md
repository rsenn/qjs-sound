# rubberband bindings

`quickjs-rubberband.c` wraps [Rubber Band](https://breakfastquay.com/rubberband/)
(`/usr/include/rubberband/rubberband-c.h`, system package
`librubberband-dev` 3.3.0, confirmed installed via `pkg-config --modversion
rubberband`) as a JS module named `rubberband`. This document was written
as the design doc before implementing it (see `quickjs-native-bindings`
skill section 0); the API below matches what was actually built.

```js
import { RubberBandStretcher } from 'rubberband';
```

Rubber Band does the same conceptual job as `soundtouch`
(`doc/soundtouch.md`) - independent tempo/pitch shifting - but with a
heavier, higher-quality phase-vocoder engine; it's a quality/CPU-cost
alternative, not a duplicate. See "Relationship to `soundtouch`/
`samplerate`" below.

There are two public APIs in `rubberband-c.h`/`RubberBandStretcher.h`: a
C++ class (`RubberBand::RubberBandStretcher`) and a C-linkage wrapper
around it (`rubberband-c.h`, "please see `RubberBandStretcher.h` for
documentation" per its own header comment - the C API is a thin,
mechanical wrapper, not an independent design). This binding targets the
**C API** (`RubberBandState`, `rubberband_*` free functions) rather than
the C++ class, matching this binding's own file extension (`.c`, not
`.cpp` like `quickjs-soundtouch.cpp`, which *does* need the C++ class
since SoundTouch has no C wrapper at all) - a plain-C target needs no
`extern "C"` shim and no C++ toolchain dependency for this one module.

## Survey: existing Rubber Band bindings

- **PyRubberband** (`bmcfee/pyrubberband`, PyPI) - the most widely used
  Python wrapper, but a **subprocess wrapper around the `rubberband` CLI
  tool**, not a native binding: `pyrb.time_stretch(y, sr, rate)`,
  `pyrb.pitch_shift(y, sr, n_steps)`, `pyrb.timemap_stretch(y, sr,
  time_map)` all write the input to a temp WAV, shell out, and read the
  result back. Whole-buffer-in/whole-buffer-out, offline only, no
  streaming API at all - a real limitation of shelling out, not a design
  choice to imitate. Still useful signal: confirms **semitones** (not a
  raw pitch ratio) is a reasonable convenience unit for pitch (mirrors
  `soundtouch`'s already-planned `.pitchSemitones`), and that a
  synchronous "stretch this whole array" convenience is worth having
  alongside the streaming object, the same way `samplerate` keeps both
  `SampleRateConverter.simple()` and the stateful streaming class.
- **Rust** (`KBone12/rubberband`, crate `rubberband` + `rubberband-sys`) -
  a genuine native binding via the C API. Splits into **two classes**,
  `OfflineStretcher` and `RealTimeStretcher`, corresponding 1:1 to the C
  API's `RubberBandOptionProcessOffline`/`ProcessRealTime` option bit;
  both expose `study()` (offline only)/`process()`/`available()`/
  `retrieve()`, naming taken **verbatim** from the C API's own
  `rubberband_study`/`_process`/`_available`/`_retrieve`. Channel data is
  **per-channel slices** (`&[I] where I: AsRef<[f32]>`), never
  interleaved - matches the C API's own `const float *const *` shape
  exactly, not reinterleaved for ergonomics. A `*Builder` type wraps the
  large options bitmask into named chainable setters (`.transients(...)`,
  `.detector(...)`, etc.) rather than a single raw int constructor
  argument. Setters (`set_time_ratio`/`set_pitch_scale`) are documented
  as illegal to call after `study`/`process` has started on the
  `OfflineStretcher` (matches the C API's own real constraint), but
  thread-safe to call concurrently with `process()` on the
  `RealTimeStretcher`.
- **JS/npm** - `@echogarden/rubberband-wasm`/`rubberband-wasm`
  (`Daninet/rubberband-wasm`) exist as WASM builds of the library for
  browser/Node use, confirming real JS-ecosystem demand for this exact
  library; their precise method-level API could not be retrieved (README/
  npm page returned no usable content during this survey - flagging
  honestly rather than guessing at their exact shape) but their mere
  existence, compiled straight from the same C API this binding targets,
  is itself useful confirmation that binding the C API directly (rather
  than inventing a different shape) is the right target.
- **Go** - no published binding found.
- **C API itself** (`rubberband-c.h`) - one opaque handle
  (`RubberBandState`), created via `rubberband_new(sampleRate, channels,
  options, initialTimeRatio, initialPitchScale)` (options is a bitmask of
  `RubberBandOption*` flags, `RubberBandOptionProcessOffline`/
  `ProcessRealTime` being one bit of it, not a separate factory/class).
  Push via `rubberband_study`/`rubberband_process`, pull via
  `rubberband_available`/`rubberband_retrieve` - **all four take/return
  `const float *const *`/`float *const *`, i.e. one pointer per channel,
  never interleaved.** aubio's own internal use of this library
  (`third_party/aubio/src/effects/pitchshift_rubberband.c`, confirmed by
  reading it directly) calls `rubberband_process` with `(const float*
  const*)&(fvec->data)` - a single-element pointer-to-pointer, since aubio
  only ever uses it in mono - which is itself further confirmation of the
  planar (non-interleaved) calling convention, not an exception to it.

**Convergent pattern**: every real binding surveyed (Rust, the C API
itself, aubio's own internal C usage) agrees on **`study`/`process`/
`available`/`retrieve` naming** and **per-channel (planar), never
interleaved, float buffers**. This is strong enough agreement, including
from the reference C API, to keep both choices as-is rather than
reshaping them to match this project's `soundtouch`/`samplerate` naming
or its otherwise-universal interleaved-buffer convention - see the two
notes below for why each is a deliberate, stated divergence rather than
an oversight.

**Why this binding does *not* reuse `soundtouch`'s `putSamples`/
`receiveSamples` naming**: those names exist in this project only because
`quickjs-soundtouch.cpp` invented them (SoundTouch's own C++ API is
`putSamples`/`receiveSamples` too, actually - so that naming already *is*
this project's "match the library" choice for that binding). Rubber
Band's own API is a different pair of verbs, and unlike SoundTouch, it
has a **`study` phase with no SoundTouch equivalent** (a distinct offline
analysis pass, run before `process`, that improves quality by letting the
stretcher see the whole signal's characteristics up front) - there's no
existing local name to collide with or reuse, so the convergent choice
(the C API's own verbs, kept verbatim by every other binding checked)
wins outright per skill section 0.

**Why buffers stay planar (array-of-`Float32Array`) instead of
interleaved**: every other binding in this project (`PaStream`,
`SoundTouch`, `SampleRateConverter`, `aubio`) uses a single interleaved
(or mono) `Float32Array`, and reshaping Rubber Band's API to match would
be the "natural" move for internal consistency - but doing so would mean
silently deinterleaving/reinterleaving on every `process()`/`retrieve()`
call, which is real CPU cost and a real copy, in a binding whose entire
value proposition (see "Relationship to `soundtouch`/`samplerate`" below)
is call-count-sensitive streaming quality. No binding surveyed, including
the C API itself, ever interleaves - reshaping it here would be inventing
a fourth API nobody uses, not converging on one three languages already
agree on. See "Cross-binding interop" below for how this planar boundary
interacts with the rest of the zero-copy pipeline.

## Relationship to `soundtouch`/`samplerate`

| | `samplerate` | `soundtouch` | `rubberband` |
|---|---|---|---|
| Changes pitch and duration together (resampling) | yes (that's its only job) | no | no |
| Independent tempo control | no | yes | yes |
| Independent pitch control | no | yes | yes |
| Engine | sinc/linear interpolation | WSOLA-family (fast, lighter CPU) | phase vocoder (R2 "Faster"/R3 "Finer" engines, `RubberBandOptionEngineFaster`/`EngineFiner`) |
| Typical quality/cost tradeoff | N/A (different job entirely) | lower CPU, adequate for real-time/casual use | higher quality especially on polyphonic/complex material, higher CPU (worse for hard real-time unless `RubberBandOptionEngineFaster` + `ProcessRealTime` is used) |
| Buffer shape | interleaved `Float32Array` | interleaved `Float32Array` | **planar**: one `Float32Array` per channel |

`rubberband` is not a replacement for either - it's a second engine for
the same "tempo/pitch shift" job `soundtouch` already covers, for
callers who want Rubber Band's quality tier and are willing to pay its
CPU cost and its planar-buffer calling convention.

## Naming

`RubberBandStretcher` - the C++ class's own name, and already the most
natural JS name available, unlike `samplerate`'s invented
`SampleRateConverter` (needed there because the real C type,
`SRC_STATE`, is anonymous and unusable as a class name). No renaming
needed here.

**One class, not two** (`OfflineStretcher`/`RealTimeStretcher` like the
Rust crate) - a deliberate divergence from that crate's split. The C
API's `options` bitmask has eleven independent option groups
(`RubberBandOptionProcessOffline`/`ProcessRealTime` is only one bit of
it, alongside transients/detector/phase/threading/window/smoothing/
formant/pitch/channels/engine); every other bit already has to flow
through the same undivided `int` argument, so splitting only the
process-mode bit into two separate classes would be inconsistent, not
simpler - the other ten groups would still need a raw bitmask argument
either way. One class with a raw `options` int (`RubberBandOption*`
constants, module-level, matching this project's raw-constant convention
already used by `sndfile`'s `SF_FORMAT_*` and `samplerate`'s `SRC_*`)
maps to the actual C API 1:1.

## Module-level

| Member | Notes |
|---|---|
| `RubberBandStretcher` | the only class export |

### Constants (`RubberBandOption*`, bitmask, pass any combination OR'd together)

Process mode: `RubberBandOptionProcessOffline` (default, value `0`),
`RubberBandOptionProcessRealTime`.

Transients: `RubberBandOptionTransientsCrisp` (default), `Mixed`,
`Smooth`.

Detector: `RubberBandOptionDetectorCompound` (default), `Percussive`,
`Soft`.

Phase: `RubberBandOptionPhaseLaminar` (default), `Independent`.

Threading: `RubberBandOptionThreadingAuto` (default), `Never`, `Always`.

Window: `RubberBandOptionWindowStandard` (default), `Short`, `Long`.

Smoothing: `RubberBandOptionSmoothingOff` (default), `SmoothingOn`.

Formant: `RubberBandOptionFormantShifted` (default), `FormantPreserved`.

Pitch: `RubberBandOptionPitchHighSpeed` (default), `PitchHighQuality`,
`PitchHighConsistency`.

Channels: `RubberBandOptionChannelsApart` (default), `ChannelsTogether`.

Engine: `RubberBandOptionEngineFaster` (default, the R2 engine),
`RubberBandOptionEngineFiner` (the R3 engine - higher quality, higher
cost).

(`RubberBandOptionStretchElastic`/`StretchPrecise` are marked obsolete in
the C header itself - omitted.)

## `RubberBandStretcher` - `RubberBandState`

### Constructor

```ts
new RubberBandStretcher(
  sampleRate: number,
  channels: number,
  options: number = RubberBandOptionProcessOffline,
  initialTimeRatio: number = 1.0,
  initialPitchScale: number = 1.0,
)
```

Maps to `rubberband_new` 1:1, in the same argument order - kept
positional (not an options object) despite having 3 optional trailing
args, because unlike `PaStream`'s options object (heterogeneous struct
fields bundled for one call) these already *are* the exact, ordered
argument list of one real C function; wrapping them in an object would
add a translation step for no benefit. Throws a generic `Error` on
failure (Rubber Band has no `_strerror`-equivalent, unlike `sndfile`/
`samplerate` - message is a fixed "rubberband: failed to create
stretcher" rather than a library-provided string) - same not-yet-typed
error convention as `portaudio`/`sndfile`/`samplerate`.

### Properties

| Member | Maps to | Notes |
|---|---|---|
| `.channels` | `rubberband_get_channel_count` | Read-only, real native getter. |
| `.timeRatio` | `rubberband_get_time_ratio` / `_set_time_ratio` | `1.0` = unchanged, matches `soundtouch.tempo`'s convention (though note Rubber Band's ratio is duration-scaling: `>1` = *slower*/longer, the opposite sense of `soundtouch.tempo`'s `>1` = *faster* - a real, unavoidable naming-vs-meaning mismatch between the two libraries' own conventions, not something this binding can paper over without lying about what the underlying call does). |
| `.pitchScale` | `rubberband_get_pitch_scale` / `_set_pitch_scale` | `1.0` = unchanged, same ratio convention as `soundtouch.pitch`. |
| `.formantScale` | `rubberband_get_formant_scale` / `_set_formant_scale` | `1.0` = unchanged; only meaningful when `RubberBandOptionFormantPreserved` is set at construction. No `soundtouch` equivalent (SoundTouch has no formant control) - a genuine capability gap this binding closes. |
| `.engineVersion` | `rubberband_get_engine_version` | Which engine (R2/R3) is actually active. |
| `.preferredStartPad` | `rubberband_get_preferred_start_pad` | Recommended silent padding before the first real sample, for best quality at the start of a stream. |
| `.startDelay` | `rubberband_get_start_delay` | Output delay introduced at the start, in samples. |
| `.latency` | `rubberband_get_latency` | Processing latency in samples - same role as `SoundTouch`'s implicit buffering delay, made explicit here since Rubber Band exposes it directly. |
| `.samplesRequired` | `rubberband_get_samples_required` | How many more input samples `.process()` currently wants before it can produce more output - lets a caller avoid feeding undersized chunks. |
| `.processSizeLimit` | `rubberband_get_process_size_limit` / `rubberband_set_max_process_size` (via a setter method, not this property - see below) | Read side is a real getter; see `.setMaxProcessSize()` for the write side (kept as an explicit method, not a setter, since the C API frames it as a one-time capacity declaration rather than an ordinary tunable, matching `.setExpectedInputDuration()`'s own method shape below). |

### Methods

| Member | Maps to | Notes |
|---|---|---|
| `.study(input, final = false)` | `rubberband_study` | `input` an **array of `Float32Array`s, one per channel** (`input.length` must equal `.channels`, each element the same length) - planar, not interleaved, see "Naming" above. Offline analysis pass; call repeatedly over the whole signal before any `.process()` calls, with `final = true` on the last chunk. Optional in the sense that skipping straight to `.process()` still works (matches the C API's own documented behavior), just with lower quality on that first pass. |
| `.process(input, final = false)` | `rubberband_process` | Same planar shape as `.study()`. Pushes input; does not itself return output - call `.retrieve()` afterward. |
| `.available()` | `rubberband_available` | Number of samples ready to `.retrieve()`, or a negative sentinel value once processing is fully finished and drained (matches the C API's own documented return convention - surfaced as-is, not translated to a `boolean`/`null`, consistent with `sndfile`/`samplerate` keeping raw C return conventions rather than inventing JS-ier shapes). |
| `.retrieve(output)` | `rubberband_retrieve` | `output` an **array of pre-allocated `Float32Array`s, one per channel** (same planar shape); fills each with up to that channel buffer's length, zero-copy, and returns the number of frames actually written - same caller-owned-buffer-plus-returned-count shape `sndfile`/`soundtouch`/`samplerate` already converged on (`doc/sndfile.md`'s "Cross-binding zero-copy pipeline"), just planar instead of interleaved. |
| `.reset()` | `rubberband_reset` | Clears all buffered state, same role as `SoundTouch.clear()`/`SampleRateConverter.reset()`. |
| `.calculateStretch()` | `rubberband_calculate_stretch` | Precomputes the stretch profile from the studied signal; only meaningful after a `.study()` pass, matches the C API's own doc note that this is normally called automatically but can be forced. |
| `.setExpectedInputDuration(samples)` | `rubberband_set_expected_input_duration` | Offline-mode quality/memory hint - total input length in samples, if known up front. |
| `.setMaxProcessSize(samples)` | `rubberband_set_max_process_size` | Declares the largest chunk size `.process()`/`.study()` will ever be called with, letting Rubber Band preallocate instead of growing buffers reactively - aubio's own usage (`rubberband_set_max_process_size(p->rb, p->hopsize)`, confirmed by reading `pitchshift_rubberband.c`) calls this once, immediately after construction, before any real processing; this binding's own worked example below does the same. |
| `.setTransientsOption(options)` / `.setDetectorOption(options)` / `.setPhaseOption(options)` / `.setFormantOption(options)` / `.setPitchOption(options)` | `rubberband_set_*_option` | Each takes the relevant `RubberBandOption*` subset (see Constants above); lets a caller change these mid-stream without reconstructing the stretcher, unlike the process-mode/threading/window/channels/engine bits, which the C API only accepts at construction time (no corresponding `rubberband_set_*` exists for those - omitted here for the same reason). |
| `.close()` | `rubberband_delete` | Idempotent, matching `PaStream`/`SndFile`/`SampleRateConverter`'s established convention in this project. |

`rubberband_set_key_frame_map` (non-linear time-mapping, mirrors
PyRubberband's `timemap_stretch`) and `rubberband_set_debug_level`/
`rubberband_set_default_debug_level` are **not** exposed in v1 - the
key-frame map is a niche offline-only feature with no analog in any other
binding surveyed here besides PyRubberband's CLI wrapper, and debug
levels are a development aid, not something a JS caller needs a stable
API for. Both are addable later without breaking anything above.

### `[Symbol.toStringTag]`

`"RubberBandStretcher"`, per skill section 21.

### Example

```js
import {
  RubberBandStretcher,
  RubberBandOptionProcessRealTime,
  RubberBandOptionEngineFiner,
} from 'rubberband';

const SR = 44100, CHANNELS = 2, HOP = 1024;

const rb = new RubberBandStretcher(
  SR, CHANNELS,
  RubberBandOptionProcessRealTime | RubberBandOptionEngineFiner,
);
rb.setMaxProcessSize(HOP);
rb.timeRatio = 1.0;        // unchanged duration
rb.pitchScale = 2 ** (3 / 12); // up 3 semitones

// Planar: one Float32Array per channel, not interleaved.
const input = Array.from({ length: CHANNELS }, () => new Float32Array(HOP));
const output = Array.from({ length: CHANNELS }, () => new Float32Array(HOP * 2));

// ... fill `input[ch]` with samples for each channel ...
rb.process(input, false);

let got;
while ((got = rb.available()) > 0) {
  const frames = rb.retrieve(output);
  // ... consume output[ch].subarray(0, frames) for each channel ...
}

rb.close();
```

## Cross-binding interop

`rubberband`'s planar buffers are a genuine seam in the otherwise fully
zero-copy `sndfile` → `samplerate` → `soundtouch` → `sndfile` pipeline
documented in `doc/sndfile.md` - `sndfile`/`samplerate`/`soundtouch` all
use a single interleaved `Float32Array`, while `rubberband` needs one
`Float32Array` per channel. Slotting `rubberband` into that pipeline
instead of (or alongside) `soundtouch` requires an explicit
deinterleave step before `.process()`/`.study()` and a reinterleave step
after `.retrieve()` - a real copy, not something a shared header can hide,
because it's an actual format mismatch between what libsndfile/
libsamplerate/SoundTouch expect and what Rubber Band's own C API expects,
not a gap in `quickjs-typedarray.h`. Worth adding a small
`js_deinterleave_f32`/`js_interleave_f32` pair to that shared header when
this binding is implemented (still zero-copy on the *read* side of each
call - it's an unavoidable single write-side copy per direction, not a
double copy) rather than duplicating that loop inside
`quickjs-rubberband.c` alone, since nothing else so far has needed it.

## Resolved during implementation

- **Typed error class or not?** Left generic (`JS_ThrowInternalError`,
  fixed "rubberband: failed to create stretcher" message) - matches
  `portaudio`/`sndfile`/`samplerate`.
- **One class vs. two**: kept as one class with a raw `options` bitmask,
  per this doc's reasoning.
- **Planar buffers**: kept planar (array of `Float32Array`, one per
  channel), not interleaved - `js_rubberband_planar_get` in
  `quickjs-rubberband.c` resolves the JS array into a stack-allocated
  `float*[]` (capped at `RB_MAX_CHANNELS` = 64) for `study`/`process`/
  `retrieve`.
- **`rubberband_set_key_frame_map`**: dropped, per the survey above.
- **Deinterleave/interleave helpers**: not added - `quickjs-soundtouch.cpp`
  (the other binding implemented alongside this one) never needed them
  either, so `quickjs-typedarray.h` stays as it was; still addable later
  if a caller actually wants to chain `rubberband` into the interleaved
  `sndfile`/`samplerate`/`soundtouch` pipeline.

A pre-existing bug affecting this style of constructor was found and
fixed while implementing `sndfile`/`samplerate` just before this binding
(see `constructor-fallback-proto-double-free` in `BUGS`): a constructor
that falls back to a shared module-static default prototype when
`new_target.prototype` isn't an object must `JS_DupValue` that fallback
before use, not just free it - `js_rubberband_constructor` does this
correctly from the start.
