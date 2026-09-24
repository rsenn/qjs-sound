# sndfile bindings

`quickjs-sndfile.c` wraps [libsndfile](http://libsndfile.github.io/libsndfile/)
(`/usr/include/sndfile.h`) as a JS module named `sndfile`. This document
was written as the design doc before implementing it (see
`quickjs-native-bindings` skill section 0); the API below matches what
was actually built.

```js
import { SndFile } from 'sndfile';
```

## Survey: existing libsndfile bindings

- **PySoundFile** (`soundfile`, PyPI) - one `SoundFile` class, `mode` string
  picks read/write/rw; write-mode params (samplerate/channels/format)
  become constructor args instead of a separate struct; top-level
  `sf.read()`/`sf.write()` convenience functions; `sf.info()` for
  metadata without a full decode; formats/subtypes exposed as **strings**
  (`'WAV'`, `'PCM_16'`), translated internally.
- **Go** (`gosndfile`) - single `Open(name, mode, *Info) (*File, error)`;
  `Info` struct mirrors `SF_INFO` field-for-field; **both** items- and
  frames-based read/write exposed (`ReadItems`/`ReadFrames`,
  `WriteItems`/`WriteFrames`), generic over `[]int16/int32/float32/float64`.
- **Rust** (`sndfile` crate) - `OpenOptions::ReadOnly(...).from_path(...)`,
  mode via enum; format/subtype as symbolic `MajorFormat`/`SubtypeFormat`
  enums, not raw ints; generic I/O trait over the four sample types.
  Notably, the broader Rust ecosystem mostly avoids libsndfile in favor of
  native decoders (hound, symphonia) - a deliberate ecosystem choice, not
  a gap.
- **JS/Node** - no actively maintained direct binding exists at all; this
  is new ground, not something to converge with.
- **C API itself** - `sf_open`/`sf_readf_*`/`sf_writef_*` (frames-based)
  and `sf_read_*`/`sf_write_*` (items-based) both exist for
  short/int/float/double; every high-level wrapper surveyed treats
  **frames-based as primary**, items-based as secondary or omitted
  entirely. `sf_command` (log info, dithering, etc.) is likewise
  **omitted** by every wrapper surveyed - not a general-purpose escape
  hatch anywhere.

**Convergent pattern adopted**: one class, a mode flag picks read/write/rw
(not separate classes - matches this project's own `PaStream`/
`PortMidiStream` precedent of one class over a direction split too).
Frames-based read/write only; `sf_command` and items-based read/write left
out of v1.

**Where this binding deliberately diverges from PySoundFile/Rust**: format
and subtype stay **raw `SF_FORMAT_*` integers**, not translated strings/
enums. This matches local precedent (`quickjs-portaudio.c`'s `paFloat32`
etc., `quickjs-portmidi.c`'s `pmNoError` etc. are both raw C constants,
never translated to strings) - per the binding skill's section 0, an
established local convention wins over external convention when the two
conflict. No top-level `read()`/`write()` convenience functions either,
for the same reason: neither `PaStream` nor `PortMidiStream` has module-
level shortcuts, everything goes through the class.

## Shared TypedArray/buffer interop (new: `quickjs-typedarray.h`)

Both `sndfile` and `soundtouch` need the same thing `quickjs-portaudio.c`
already does ad hoc in `js_pastream_get_buffer()`: resolve a JS argument
that could be a `TypedArray`, `DataView`, or plain `ArrayBuffer` into a
raw pointer, and (for `sndfile`, which reads/writes 4 different native
sample types) know *which* element type the caller passed. Per skill
section 8 ("once conversion logic outgrows one file, factor it into an
auxiliary utils pair, shared across bindings"), this becomes a small new
header pair used by both new bindings (and optionally retrofitted into
`quickjs-portaudio.c` later - not part of this change):

```c
/* quickjs-typedarray.h - shared by quickjs-sndfile.c, quickjs-soundtouch.cpp */

typedef enum {
  JS_TYPEDARRAY_NONE = 0,   /* plain ArrayBuffer/DataView - no element type */
  JS_TYPEDARRAY_INT8,
  JS_TYPEDARRAY_UINT8,
  JS_TYPEDARRAY_UINT8_CLAMPED,
  JS_TYPEDARRAY_INT16,
  JS_TYPEDARRAY_UINT16,
  JS_TYPEDARRAY_INT32,
  JS_TYPEDARRAY_UINT32,
  JS_TYPEDARRAY_FLOAT32,
  JS_TYPEDARRAY_FLOAT64,
} JSTypedArrayKind;

typedef struct {
  uint8_t* ptr;          /* already offset into the backing ArrayBuffer */
  size_t byte_length;
  size_t element_size;   /* 1/2/4/8; 1 for JS_TYPEDARRAY_NONE */
  JSTypedArrayKind kind;
} JSBufferView;

/* Resolves argument `val` (TypedArray, DataView, or plain ArrayBuffer) to
 * a view with no copy. Returns FALSE and throws a TypeError if `val` is
 * none of those. */
BOOL js_bufferview_get(JSContext* ctx, JSValueConst val, JSBufferView* out);

/* `js_bufferview_get` restricted to one element kind - throws naming
 * `expected` (e.g. "Float32Array") on any mismatch, including
 * JS_TYPEDARRAY_NONE (a plain ArrayBuffer never satisfies a kind check). */
BOOL js_bufferview_get_kind(JSContext* ctx, JSValueConst val, JSTypedArrayKind kind, JSBufferView* out);

const char* js_typedarray_kind_name(JSTypedArrayKind kind);

/* Hands a native js_malloc'd buffer to JS as a new TypedArray of `kind`,
 * no copy - freed via js_free_rt when the ArrayBuffer dies. `count` is in
 * ELEMENTS. */
JSValue js_typedarray_from_malloc(JSContext* ctx, void* data, size_t count, JSTypedArrayKind kind);

/* Copies `count` elements from `data` into a freshly allocated TypedArray
 * of `kind`. */
JSValue js_typedarray_from_copy(JSContext* ctx, const void* data, size_t count, JSTypedArrayKind kind);
```

Plain C (no templates), usable unchanged from a `.c` translation unit
(`quickjs-sndfile.c`) and a `.cpp` one (`quickjs-soundtouch.cpp`) alike -
this is *not* `qjs-opencv`'s `js_typed_array.hpp` (that one is templated
C++ tied to `cv::Mat`/`cv::Ptr`; too heavy and the wrong language for a
plain-C libsndfile binding). `js_bufferview_get`'s implementation follows
skill section 7 exactly: try `JS_GetTypedArrayBuffer` first (covers both
`TypedArray` and `DataView`), fall back to `JS_GetArrayBuffer` for a bare
`ArrayBuffer`.

`SndFile.read()`/`.write()` (below) use `js_bufferview_get` (any kind
accepted) and switch on `view.kind` to pick `sf_readf_short`/`_int`/
`_float`/`_double`; `soundtouch`'s `putSamples`/`receiveSamples` use
`js_bufferview_get_kind(..., JS_TYPEDARRAY_FLOAT32, ...)` since SoundTouch
is float-only.

## Cross-binding zero-copy pipeline

The whole point of `quickjs-typedarray.h` existing as one *shared* header
rather than being reinvented separately by each new binding is that
`sndfile`, `soundtouch`, and `samplerate` (`doc/soundtouch.md`,
`doc/samplerate.md`) can then be chained - decode, resample, process,
re-encode - **without a single JS-side or native-side copy** anywhere in
the steady-state loop, because every one of them already reads its input
straight out of the caller's `ArrayBuffer` (via `js_bufferview_get*`) and,
where it produces bulk audio data rather than a handful of scalars, writes
its output straight into a caller-supplied buffer too:

| Binding / method | Reads from caller buffer (zero-copy) | Writes into caller buffer (zero-copy) |
|---|---|---|
| `SndFile.read(buffer, frames?)` | - | yes (always) |
| `SndFile.write(buffer, frames?)` | yes (always) | - |
| `SoundTouch.putSamples(buffer)` | yes (always) | - |
| `SoundTouch.receiveSamples(buffer)` | - | yes (always) |
| `SampleRateConverter.process(input, ratio, endOfInput, output?)` | yes (always) | yes, when `output` is passed (see `doc/samplerate.md`) |
| `AubioOnset`/`AubioPitch`/`AubioNotes`/`AubioTempo`.`process(input)` (`doc/aubio.md`) | yes (always) | no (output is 1-3 floats, always a fresh copy - not worth optimizing) |

Because every stage's *input* side is already zero-copy, and both
`SndFile.read`/`.write` and `SoundTouch`'s push/pull pair write straight
into whatever buffer the caller hands them, the only stage where a
resampling binding's *output length legitimately differs from its input
length* (`samplerate`, since resampling changes the frame count) needs the
explicit `output` parameter added above to stay copy-free too - every
other stage in the chain preserves frame count, so reusing one
caller-owned buffer per stage is enough on its own.

**Worked example** - read a WAV, resample it, run it through SoundTouch,
tap `aubio` for analysis on the same data mid-pipeline, write the result
back out - with a fixed, tiny set of buffers allocated once up front and
reused for the entire file, no per-chunk allocation in the loop at all:

```js
import { SndFile, SFM_READ, SFM_WRITE, SF_FORMAT_WAV, SF_FORMAT_PCM_16 } from 'sndfile';
import { SampleRateConverter, SRC_SINC_BEST_QUALITY } from 'samplerate';
import { SoundTouch } from 'soundtouch';
import { AubioPitch } from 'aubio';

const TARGET_RATE = 48000;
const CHUNK = 4096; // frames per iteration

const src = new SndFile('in.wav', SFM_READ);
const { channels, samplerate } = src;
const ratio = TARGET_RATE / samplerate;

const dst = new SndFile('out.wav', SFM_WRITE, {
  samplerate: TARGET_RATE, channels,
  format: SF_FORMAT_WAV | SF_FORMAT_PCM_16,
});

const conv = new SampleRateConverter(SRC_SINC_BEST_QUALITY, channels);
const st = new SoundTouch(TARGET_RATE, channels);
st.pitchSemitones = 2; // "do something interesting": shift up two semitones
const pitch = new AubioPitch('yinfft', CHUNK, CHUNK, TARGET_RATE); // bufSize == hopSize: one hop per chunk here

// Allocated once, reused for the whole file - this is the entire
// working-buffer footprint of the pipeline.
const inBuf = new Float32Array(CHUNK * channels);
const resampled = new Float32Array((Math.ceil(CHUNK * ratio) + 256) * channels); // headroom, see doc/samplerate.md
const outBuf = new Float32Array(CHUNK * channels);

let frames;
while ((frames = src.read(inBuf, CHUNK)) > 0) {
  const isLast = frames < CHUNK;
  const input = frames === CHUNK ? inBuf : inBuf.subarray(0, frames * channels);

  // 1. Resample straight into `resampled` - no allocation (doc/samplerate.md).
  const produced = conv.process(input, ratio, isLast, resampled);
  const resampledView = resampled.subarray(0, produced * channels);

  // 2. Tap `aubio` for analysis on the same buffer `soundtouch` is about
  //    to consume - process() only reads it, so this is free to do
  //    in-line without disturbing the pipeline (mono only; a real
  //    multi-channel script would mix down first).
  if (channels === 1 && produced === CHUNK) {
    const [hz] = pitch.process(resampledView);
    if (hz > 0) console.log(`~${hz.toFixed(1)}Hz`);
  }

  // 3. Push into SoundTouch, flush on the last chunk.
  st.putSamples(resampledView);
  if (isLast) st.flush();

  // 4. Pull whatever SoundTouch has ready straight into `outBuf`, write
  //    it straight out - both zero-copy.
  let got;
  while ((got = st.receiveSamples(outBuf)) > 0)
    dst.write(outBuf, got);
}

src.close();
dst.close();
conv.close();
```

This is the concrete case all three "planned" docs (`sndfile`,
`soundtouch`, `samplerate`) were designed against together, not
independently - it's why `samplerate.process()` grew the optional
`output` parameter above (without it, this loop would allocate a fresh
`Float32Array` every chunk, undoing the whole point), and why
`soundtouch`'s push/pull methods and `sndfile`'s read/write were designed
around caller-owned buffers from the start rather than each returning
freshly-allocated arrays independently.

## Module-level

| Member | Notes |
|---|---|
| `SndFile` | the only export besides constants |

### Constants

Raw `SF_FORMAT_*` major types: `SF_FORMAT_WAV`, `SF_FORMAT_AIFF`,
`SF_FORMAT_AU`, `SF_FORMAT_RAW`, `SF_FORMAT_FLAC`, `SF_FORMAT_OGG`,
`SF_FORMAT_CAF`, `SF_FORMAT_W64`, ... (the full list `sndfile.h` defines).

Subtypes: `SF_FORMAT_PCM_S8`, `SF_FORMAT_PCM_16`, `SF_FORMAT_PCM_24`,
`SF_FORMAT_PCM_32`, `SF_FORMAT_PCM_U8`, `SF_FORMAT_FLOAT`,
`SF_FORMAT_DOUBLE`, `SF_FORMAT_ULAW`, `SF_FORMAT_ALAW`,
`SF_FORMAT_VORBIS`, `SF_FORMAT_OPUS`, ... (full list).

Masks (for decoding an existing file's `.format`):
`SF_FORMAT_TYPEMASK`, `SF_FORMAT_SUBMASK`, `SF_FORMAT_ENDMASK`.

Endianness: `SF_ENDIAN_FILE`, `SF_ENDIAN_LITTLE`, `SF_ENDIAN_BIG`,
`SF_ENDIAN_CPU`.

Open mode (first-class, not a string, matching local raw-constant
convention): `SFM_READ`, `SFM_WRITE`, `SFM_RDWR`.

Seek whence (libsndfile borrows libc's own values, re-exported so callers
don't need a separate import): `SEEK_SET` (0), `SEEK_CUR` (1), `SEEK_END`
(2).

Error codes: `SF_ERR_NO_ERROR`, `SF_ERR_UNRECOGNISED_FORMAT`,
`SF_ERR_SYSTEM`, `SF_ERR_MALFORMED_FILE`, `SF_ERR_UNSUPPORTED_ENCODING`.

## `SndFile` - `SNDFILE*`

### Opening

```ts
new SndFile(path: string, mode: number /* SFM_READ | SFM_WRITE | SFM_RDWR */, info?: {
  samplerate: number,
  channels: number,
  format: number,   // major | subtype, e.g. SF_FORMAT_WAV | SF_FORMAT_PCM_16
})
```

`info` is **required** whenever `mode` includes `SFM_WRITE` (i.e.
`SFM_WRITE` or `SFM_RDWR`) - matches `sf_open`'s own contract of needing
a caller-filled `SF_INFO` for write. Ignored for pure `SFM_READ`;
libsndfile fills every field in from the file itself. `info` is a plain
object, not a class - same reasoning as `PaStreamParameters`'s recent
conversion to a plain object in `quickjs-portaudio.c`: it exists only to
be read once at open time, no identity or lifetime of its own.

Throws a generic `Error` (message from `sf_strerror`) on failure - matches
`portaudio`'s current (not yet typed) error convention; see that binding's
open question about a typed error class, still unresolved, so this
doesn't invent a third style.

### Static

| Member | Notes |
|---|---|
| `SndFile.info(path)` | Opens `SFM_READ`, snapshots `{frames, samplerate, channels, format, sections, seekable}` into a plain object, closes, returns it - no live handle kept. Mirrors PySoundFile's `sf.info()`. |

### Properties (read-only, reflect the live `SF_INFO`)

`frames`, `samplerate`, `channels`, `format`, `sections`, `seekable`.

### Methods

| Member | Maps to | Notes |
|---|---|---|
| `.read(buffer, frames?)` | `sf_readf_short/int/float/double` | Dispatches on `buffer`'s TypedArray kind via `js_bufferview_get` (`Int16Array`→short, `Int32Array`→int, `Float32Array`→float, `Float64Array`→double; anything else throws). `frames` defaults to `buffer`'s length divided by `channels`, matching `PaStream.read()`'s own default-frames convention. Returns actual frames read (may be less than requested at EOF). |
| `.write(buffer, frames?)` | `sf_writef_short/int/float/double` | Same dispatch/default as `.read()`. Returns actual frames written. |
| `.seek(frames, whence)` | `sf_seek` | `whence` is `SEEK_SET`/`SEEK_CUR`/`SEEK_END`. Returns the new frame offset. |
| `.close()` | `sf_close` | Idempotent - matches `PaStream.close()`/`PortMidiStream.close()`'s established convention in this project; calling it twice, or calling `.read()`/`.write()`/`.seek()` after close, throws (except a second `.close()`, which is a no-op). |

### Example

```js
import { SndFile, SFM_READ, SFM_WRITE, SF_FORMAT_WAV, SF_FORMAT_PCM_16 } from 'sndfile';

const info = SndFile.info('in.wav');
console.log(`${info.frames} frames, ${info.channels}ch @ ${info.samplerate}Hz`);

const src = new SndFile('in.wav', SFM_READ);
const dst = new SndFile('out.wav', SFM_WRITE, {
  samplerate: src.samplerate,
  channels: src.channels,
  format: SF_FORMAT_WAV | SF_FORMAT_PCM_16,
});

const chunk = new Float32Array(1024 * src.channels);
let frames;
while ((frames = src.read(chunk)) > 0)
  dst.write(chunk, frames);

src.close();
dst.close();
```

## Resolved during implementation

- **Typed error class or not?** Left generic (`JS_ThrowInternalError`,
  message from `sf_strerror`) - matches `portaudio`'s current convention.
- **`sf_command`**: dropped entirely, per the survey above.
- **Items-based read/write**: dropped, frames-only, per the survey above.
- **`quickjs-typedarray.h` placement**: `quickjs-typedarray.c` is added to
  `COMMON_SOURCES` in the top-level `CMakeLists.txt` right before the
  `make_module(sndfile c)`/`make_module(samplerate c)` calls (and unset
  right after), so it's compiled once per module that needs it with no
  new CMake machinery.

A real bug was found and fixed along the way (not specific to this
binding - see `constructor-fallback-proto-double-free` in `BUGS`):
`SndFile`'s and `SampleRateConverter`'s constructors, like several
existing `quickjs-portaudio.c` constructors, fell back to a shared
module-static default prototype when `new_target.prototype` wasn't an
object, then freed it without ever having dup'd it - silently
over-decrementing the shared prototype's refcount on every construction
and eventually segfaulting on a later property access. Fixed here by
`JS_DupValue`-ing the fallback before use.
