# samplerate bindings

`quickjs-samplerate.c` wraps [libsamplerate](http://libsndfile.github.io/libsamplerate/)
("Secret Rabbit Code", `/usr/include/samplerate.h`) as a JS module named
`samplerate`. This document was written as the design doc before
implementing it (see `quickjs-native-bindings` skill section 0); the API
below matches what was actually built.

```js
import { SampleRateConverter } from 'samplerate';
```

## Survey: existing libsamplerate bindings

- **node-libsamplerate** (npm) - `new SampleRate({type, channels, fromRate,
  toRate, ...})`, a Node.js **Transform stream** - streaming-only, no
  one-shot convenience. Converter-type constants as class statics
  (`SampleRate.SRC_SINC_MEDIUM_QUALITY`, mirroring the C enum by number).
- **python-samplerate** (PyPI) - three explicit tiers mirroring the C
  library 1:1: `samplerate.resample(data, ratio, converter_type)`
  (one-shot), `samplerate.Resampler(converter_type, channels).process(data,
  ratio, end_of_input=False)` (streaming), and `CallbackResampler` (pull-
  callback mode). Converter type as a **symbolic string**
  (`'sinc_best'`), not a raw int. Uniquely exposes the pull-callback API.
- **Rust** (`samplerate` crate + `libsamplerate-sys`) - a `Samplerate`
  struct (streaming) and a free `convert()` function (one-shot)
  **coexist**; `ConverterType` is a real Rust enum. No callback-mode
  exposure found.
- **Go** (`dh1tw/gosamplerate`) - `New(converterType, channels, bufferLen)
  (Src, error)` / `Simple(dataIn, ratio, channels, converterType)
  ([]float32, error)` - both one-shot and streaming again. `Process(dataIn,
  ratio, endOfInput) ([]float32, error)` collapses `SRC_DATA`'s
  `input_frames`/`output_frames`/`input_frames_used`/`output_frames_gen`
  entirely - frame counts come from slice lengths, the call just returns
  the produced slice. Converter type stays a raw `int` (matches the C
  enum), with `GetName`/`GetDescription` lookup helpers. No callback mode.
- **C API** - `src_new`/`src_process`/`src_delete` (stateful) and
  `src_simple` (one-shot) both exist at the C level too; `SRC_DATA` is
  float-only, with manual `input_frames_used`/`output_frames_gen`
  bookkeeping the caller must loop on. `src_callback_new`/
  `src_callback_read` (pull-based streaming) exist but were **skipped by
  3 of 4 surveyed bindings** (only Python's exposes it).

**Convergent patterns adopted**:
- **Both a one-shot call and a stateful streaming object** - the
  strongest signal, present in every binding except Node's (which is
  stream-only because it's modeling a Node `Transform`, a Node-specific
  constraint that doesn't apply here).
- **`SRC_DATA` bookkeeping is always hidden**: no binding surfaces
  `input_frames_used`/`output_frames_gen` separately - the streaming call
  just returns what was actually produced, consuming what it needs
  internally. Only `ratio` and `end_of_input` remain caller-facing knobs.
- **Callback mode dropped**: matches 3 of 4 bindings; also consistent
  with this project's own established caution around native callbacks
  (skill section 13) - `src_callback_read` calls back into user code
  synchronously from the JS thread (not a realtime audio thread, so it
  *would* be safe to bridge), but since every non-Python binding still
  skipped it, and it adds real complexity (section 12's callback-bridge
  machinery) for a case the simpler push-based loop already covers, this
  binding does the same and leaves it out of v1.
- **Converter type as a raw int constant** (Go/Node's choice), not a
  symbolic string (Python's choice) - matches this project's own raw-
  constant convention already applied in `portaudio`/`portmidi` and
  proposed for `sndfile`.

## Naming

The opaque C type is `SRC_STATE`; every binding above invented its own
friendlier name (`Resampler`, `Samplerate`, `Src`, `SampleRate`). This
binding does the same rather than exposing a cryptic `SRC` class:
**`SampleRateConverter`**, chosen over reusing the module's own name
`samplerate` as the class name too (which would read oddly as
`new samplerate.samplerate(...)` / `new samplerate.SampleRate(...)`
sitting right next to a module literally called `samplerate`).

## Module-level

| Member | Notes |
|---|---|
| `SampleRateConverter` | the only class export |

### Constants

Converter types (`src_new`'s first argument): `SRC_SINC_BEST_QUALITY`,
`SRC_SINC_MEDIUM_QUALITY`, `SRC_SINC_FASTEST`, `SRC_ZERO_ORDER_HOLD`,
`SRC_LINEAR`.

## `SampleRateConverter` - `SRC_STATE*`

### Constructor

```ts
new SampleRateConverter(converterType: number, channels: number)
```

Maps to `src_new(converterType, channels, &error)`. Throws a generic
`Error` (message from `src_strerror`) on failure - same not-yet-typed
error convention as `portaudio`/`sndfile`; see those docs' matching open
question.

### Static

| Member | Maps to | Notes |
|---|---|---|
| `SampleRateConverter.simple(input, ratio, converterType, channels)` | `src_simple` | One-shot conversion, no persistent state. `input` a `Float32Array` (checked via the shared `js_bufferview_get_kind(..., JS_TYPEDARRAY_FLOAT32, ...)` from `quickjs-typedarray.h`, see `doc/sndfile.md`). Returns a freshly-allocated `Float32Array` sized to what `src_simple` actually produced. Placed as a static method rather than a module-level free function, matching the precedent set by `PaStream.isFormatSupported` in `quickjs-portaudio.c` (this project's binding style keeps free-standing utility calls grouped under the class they conceptually belong to, not scattered at module scope). |
| `SampleRateConverter.getName(converterType)` | `src_get_name` | Human-readable name for a converter-type constant, e.g. `"Best Sinc Interpolator"`. |
| `SampleRateConverter.getDescription(converterType)` | `src_get_description` | Longer description string. |
| `SampleRateConverter.getVersion()` | `src_get_version` | Library version string. |
| `SampleRateConverter.isValidRatio(ratio)` | `src_is_valid_ratio` | `boolean` - libsamplerate rejects ratios outside roughly `[1/256, 256]`; useful to check before constructing/processing rather than after a throw. |

### Properties

| Member | Maps to | Notes |
|---|---|---|
| `.channels` | `src_get_channels` | Real native getter, unlike `soundtouch`'s cached properties - no caveat needed here. |

### Methods

| Member | Maps to | Notes |
|---|---|---|
| `.process(input, ratio, endOfInput = false, output?)` | `src_process` | `input` a `Float32Array`. When `output` (a `Float32Array`) is given, `SRC_DATA.data_out` points **directly at it** (up to `output.length` frames' worth) - zero-copy, no allocation, and the call returns just the frame count actually written (`output_frames_gen`), matching `SoundTouch.receiveSamples(buffer)`'s "caller-owned output buffer + returned count" shape (see `doc/sndfile.md`'s "Cross-binding zero-copy pipeline" section for why this convention is shared across `sndfile`/`soundtouch`/`samplerate`). When `output` is omitted, behaves as originally designed: sizes a fresh buffer internally, calls `src_process` once, and returns a newly-allocated `Float32Array` trimmed to `output_frames_gen * channels` - the convenience shape for one-off calls where reuse doesn't matter. Either way, `input_frames_used`/`output_frames_gen` bookkeeping stays hidden from the caller, matching every surveyed binding's convergence on that point. `endOfInput` maps directly to `SRC_DATA.end_of_input`; set on the last call of a stream so libsamplerate flushes its internal filter state. |
| `.setRatio(ratio)` | `src_set_ratio` | Changes the conversion ratio for subsequent `.process()` calls without resetting internal state (a smoothed transition, per libsamplerate's own doc comment on this function) - unlike passing a new `ratio` directly to `.process()`, which is also valid but documented by libsamplerate as better suited to a ratio that's already changing continuously call-to-call (e.g. varispeed) rather than a discrete jump. |
| `.reset()` | `src_reset` | Clears internal state without deleting the converter - same "keep the handle, drop the buffered state" shape as `SoundTouch.clear()`. |
| `.close()` | `src_delete` | Idempotent, matching `PaStream`/`PortMidiStream`/`SndFile`'s established convention in this project. `.process()`/`.setRatio()`/`.reset()` after `.close()` throw. |

### `[Symbol.toStringTag]`

`"SampleRateConverter"`, per skill section 21.

### Example

```js
import { SampleRateConverter, SRC_SINC_BEST_QUALITY } from 'samplerate';

// One-shot: upsample a whole buffer from 22050Hz to 44100Hz, stereo.
const input = new Float32Array(/* ... 22050Hz stereo interleaved ... */);
const output = SampleRateConverter.simple(input, 44100 / 22050, SRC_SINC_BEST_QUALITY, 2);

// Streaming, zero-copy: feed chunks as they arrive, flush on the last one,
// reusing one pre-sized output buffer for the whole stream (no per-chunk
// allocation) - see doc/sndfile.md's "Cross-binding zero-copy pipeline".
const conv = new SampleRateConverter(SRC_SINC_BEST_QUALITY, 2);
const out = new Float32Array(8192 * 2); // sized generously per-chunk
for (const [chunk, isLast] of chunks) {
  const produced = conv.process(chunk, 44100 / 22050, isLast, out);
  // ... consume out.subarray(0, produced * 2) ...
}
conv.close();
```

## Resolved during implementation

- **Typed error class or not?** Left generic (`JS_ThrowInternalError`,
  message from `src_strerror`) - matches `portaudio`/`sndfile`.
- **Pull-callback mode**: dropped, per the survey above.
- **`.process()`'s output-sizing heuristic**: implemented exactly as
  proposed - `ceil(input_frames * ratio) + 256` frames of headroom
  (`SRC_OUTPUT_HEADROOM_FRAMES` in `quickjs-samplerate.c`) for the
  no-`output`-argument case; the zero-copy `output`-buffer form pushes
  sizing onto the caller as described.

A real bug was found and fixed along the way (not specific to this
binding - see `constructor-fallback-proto-double-free` in `BUGS`):
`SampleRateConverter`'s constructor, like several existing
`quickjs-portaudio.c` constructors, fell back to a shared module-static
default prototype when `new_target.prototype` wasn't an object, then
freed it without ever having dup'd it - silently over-decrementing the
shared prototype's refcount on every construction and eventually
segfaulting on a later property access. Fixed here by `JS_DupValue`-ing
the fallback before use.
