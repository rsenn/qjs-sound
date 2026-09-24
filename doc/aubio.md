# aubio bindings

`quickjs-aubio.c` wraps four of [aubio](https://aubio.org/)'s analysis
objects - note detection (`notes/notes.h`), onset detection
(`onset/onset.h`), pitch detection (`pitch/pitch.h`), and (planned, see
`AubioTempo` below) beat/tempo tracking (`tempo/tempo.h`) - as JS classes
exported from the `aubio` module:

```js
import { AubioNotes, AubioOnset, AubioPitch, AubioTempo } from 'aubio';
```

All four share the same constructor shape and a `process()` method that
runs one hop of analysis:

```js
new AubioNotes(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
new AubioOnset(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
new AubioPitch(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
new AubioTempo(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
```

`method` selects the underlying algorithm (aubio-specific strings, e.g.
`'default'`, `'hfc'`, `'complex'` for onset; `'default'`, `'yin'`,
`'yinfft'`, `'schmitt'`, `'fcomb'`, `'mcomb'` for pitch; `'default'` only,
for now, for tempo - see below). The constructor throws `InternalError` if
aubio rejects the parameters (unknown method, `hopSize` larger than
`bufSize`, etc).

`process(input)` takes a `Float32Array` of exactly `hopSize` samples -
`smpl_t` is `float` in this build (`HAVE_AUBIO_DOUBLE` is unset), matching
`Float32Array` - and throws `RangeError` if the length doesn't match. It
returns a newly allocated `Float32Array` with the algorithm's output vector.
Input is read directly out of the passed buffer without copying; the
returned array is always a fresh copy (all four output vectors are 1-3
elements, so this copy is negligible - unlike the multi-kilosample audio
buffers passed to `sndfile`/`soundtouch`/`samplerate`, where copying would
actually matter; see `doc/sndfile.md`'s "Cross-binding zero-copy pipeline"
section for how those three stay copy-free end to end. `aubio`'s classes
can tap directly into any buffer moving through that pipeline - `process()`
only reads it, never mutates it, so running e.g. `AubioTempo`/`AubioPitch`
on a chunk immediately after `SampleRateConverter.process()` fills it,
before handing that same chunk on to `SoundTouch`, costs nothing extra).

## Survey: existing aubio bindings

- **python-aubio** (PyPI, the reference/official binding) - one class per
  algorithm object, matching this project's own existing per-object-class
  shape 1:1: `aubio.tempo(method, buf_size, hop_size, samplerate)`, called
  via `__call__` (`tempo(samples)`) rather than a named method - a
  Python-idiomatic choice (callable objects) that doesn't transfer to JS,
  where `.process()` is the established local name (already used by
  `AubioNotes`/`AubioOnset`/`AubioPitch`, kept for `AubioTempo` too, per
  skill section 0's "match the codebase you're adding to" rule).
  `hop_size` **defaults to 512** for every object, not 256 - see below.
  Exposes `get_bpm()`, `get_confidence()`, `get_period()`,
  `get_last()`/`get_delay()` etc. as plain getter methods (Python has no
  first-class property-with-setter sugar as convenient as JS's, so a
  method is the idiomatic choice there); this binding already renders the
  equivalent aubio getter/setter pairs as JS accessor properties
  everywhere else (`onset.threshold`, `pitch.tolerance`, ...) - `AubioTempo`
  follows that existing local convention, not python-aubio's method-per-
  getter shape.
- **aubio-rs** (Rust crate) - `Tempo::new(mode, buf_size, hop_size,
  samplerate)`, `.do_result(&input, &mut FVec)` / a `.do_result_simple()`
  convenience returning a `bool` (any beat this hop). Confirms "one object
  per detector, opaque handle, buffer in/out" as the cross-language norm;
  the boolean convenience is a Rust-idiomatic simplification of exactly
  the "`0` vs. nonzero in `out[0]`" convention this binding's `process()`
  already returns raw, matching `AubioOnset` - not adopted separately here
  since JS callers can already write `if (tempo.process(hop)[0])`.
- **aubio-go** (Go) - mirrors the C API almost verbatim, including a
  `Tempo` object with `Do(in, out *SimpleBuffer)`, `GetBpm()`,
  `GetConfidence()`, `GetLastS()`. No convenience wrapping at all - the
  weakest signal of the three, but still agrees on "one object per
  detector."
- **C API itself** (`tempo/tempo.h`) - `new_aubio_tempo`/`aubio_tempo_do`/
  `del_aubio_tempo`, output is an `fvec_t` of length 1 whose `[0]` is `0`
  when no beat this hop, else the beat's fractional position within the
  hop (`0 <= x < 1`) - confirmed by reading `aubio_tempo_do` in
  `third_party/aubio/src/tempo/tempo.c` and aubio's own
  `examples/aubiotrack.c`, which treats it exactly like `AubioOnset`
  already does (`is_beat = tempo_out->data[0]; if (is_beat && ...)`, i.e.
  any nonzero value, not a special `1+x` encoding - that's an `AubioOnset`-
  specific convention, not shared by tempo).

**Convergent pattern**: every binding surveyed, including the reference
implementation, treats tempo tracking as its own opaque per-object class
with the same buffer-in/buffer-out shape as onset/pitch/notes detection -
strong confirmation that `AubioTempo` belongs in this binding as a fourth
class matching the existing three's shape, not as a method bolted onto
`AubioOnset` (they share peak-picking machinery internally in aubio's own
C implementation, but no surveyed binding exposes that fact, and neither
does this one).

**`hopSize` default discrepancy (256 here vs. 512 in python-aubio and
aubio's own example CLIs)**: this binding's existing `AubioNotes`/
`AubioOnset`/`AubioPitch` already ship with `hopSize = 256` as their
default, and per skill section 0, changing an already-implemented,
already-tested local default to match an external convention is exactly
the kind of silent-rewrite this skill warns against - the shipped default
isn't wrong (256 is a perfectly valid hop size, just finer-grained than
python-aubio's default), just different, and real code may already depend
on it. **Decision: `AubioTempo` keeps `hopSize = 256` for consistency with
its three existing siblings**, not python-aubio's 512; a caller who wants
512 (matching upstream aubio's own CLI defaults) passes it explicitly, the
same as today.

---

## `AubioNotes` - note onset/pitch/off detection (`notes/notes.h`)

`process()` returns a length-3 `Float32Array`: `[midiNote, velocity,
midiNoteOff]`. `midiNote` is `0` when no note was found; `midiNoteOff` is
the note to turn off, or `0` if none (matching aubio's own
`examples/aubionotes.c`, which checks `obuf->data[2] != 0`). Note that on
the very first note-on of a stream, aubio itself emits a spurious
`midiNoteOff` of `-1` alongside it - an artifact of its internal "no
current note" sentinel, not a real event; the `!== 0` check still handles
it the same way aubio's own reference tool does.

| Member | Description |
|---|---|
| `process(input)` | Run one hop of note detection, see above. |
| `silence` | Silence threshold in dB (get/set). |
| `minioiMs` | Minimum inter-onset interval in milliseconds (get/set). |
| `releaseDrop` | Release drop level in dB - how far the level must fall below the note's initial level before a note-off is emitted (get/set, default `10`). |

```js
import { AubioNotes } from 'aubio';

const notes = new AubioNotes('default', 1024, 256, 44100);
const hop = new Float32Array(256); // ... fill with samples ...
const [midiNote, velocity, midiNoteOff] = notes.process(hop);
if (midiNote) console.log(`note on ${midiNote} vel ${velocity}`);
if (midiNoteOff !== 0) console.log(`note off ${midiNoteOff}`);
```

## `AubioOnset` - onset detection (`onset/onset.h`)

`process()` returns a length-1 `Float32Array`: `0` when no onset was found,
or `1 + a` (`a` in `[0, 1]`) giving the fractional position of the onset
within the hop.

| Member | Description |
|---|---|
| `process(input)` | Run one hop of onset detection, see above. |
| `setDefaultParameters(mode)` | Re-apply the default parameter set for a given onset mode; called internally by the constructor. Returns `0` on success. |
| `reset()` | Reset current time and last-onset time to `0`. |
| `last` | Sample position of the last detected onset (get). |
| `lastS` / `lastMs` | Same, in seconds / milliseconds (get). |
| `threshold` | Peak-picking threshold (get/set). |
| `silence` | Silence threshold in dB (get/set). |
| `minioi` / `minioiS` / `minioiMs` | Minimum inter-onset interval, in samples / seconds / milliseconds (get/set). |
| `delay` / `delayS` / `delayMs` | Constant system delay subtracted from detection time, in samples / seconds / milliseconds (get/set). |
| `awhitening` | Adaptive whitening enabled (`0`/`1`) (get/set). |
| `compression` | Logarithmic compression factor, `0` to disable (get/set). |
| `descriptor` | Current value of the onset detection function (get). |
| `thresholdedDescriptor` | Current value after threshold is applied (get). |

```js
import { AubioOnset } from 'aubio';

const onset = new AubioOnset('default', 1024, 256, 44100);
const hop = new Float32Array(256); // ... fill with samples ...
const result = onset.process(hop);
if (result[0] !== 0) console.log(`onset at sample ${onset.last}`);
```

## `AubioPitch` - pitch detection (`pitch/pitch.h`)

`process()` returns a length-1 `Float32Array` holding the detected pitch, in
the unit selected by `setUnit()`.

| Member | Description |
|---|---|
| `process(input)` | Run one hop of pitch detection, see above. |
| `setUnit(mode)` | Output unit: `'Hz'` (default), `'midi'`, `'cent'`, or `'bin'`. Returns `0` on success. |
| `tolerance` | YIN/YINFFT tolerance threshold (get/set, default `0.15` for `yin`, `0.85` for `yinfft`). |
| `silence` | Silence threshold in dB, below which pitch is ignored (get/set). |
| `confidence` | Confidence of the last detection (get). |

```js
import { AubioPitch } from 'aubio';

const pitch = new AubioPitch('yinfft', 1024, 256, 44100);
pitch.setUnit('Hz');
const hop = new Float32Array(256); // ... fill with samples ...
const [hz] = pitch.process(hop);
console.log(hz, pitch.confidence);
```

## `AubioTempo` - beat/tempo tracking (`tempo/tempo.h`)

`process()` returns a length-1 `Float32Array`: `0` when no beat was
detected this hop, or the beat's fractional position within the hop
(`0 <= x < 1`) otherwise - check truthiness (`result[0] !== 0`, or just
`if (result[0])`), the same convention `AubioOnset` already uses, not the
`1 + a` offset that's specific to `AubioOnset`'s own output encoding (see
the survey above - confirmed against `aubio_tempo_do`'s real source).

| Member | Description |
|---|---|
| `process(input)` | Run one hop of beat tracking, see above. |
| `threshold` | Peak-picking threshold (get/set), same role as `AubioOnset.threshold`. |
| `silence` | Silence threshold in dB (get/set). |
| `delay` / `delayS` / `delayMs` | Constant system delay subtracted from detection time, in samples / seconds / milliseconds (get/set) - same trio as `AubioOnset`. |
| `last` / `lastS` / `lastMs` | Sample position of the last detected beat, in samples / seconds / milliseconds (get). |
| `period` / `periodS` | Currently observed beat period, in samples / seconds (get); `0` if no consistent tempo has been found yet. |
| `bpm` | Currently observed tempo in beats per minute (get); `0` if no consistent value found. |
| `confidence` | Confidence of the current tempo estimate (get); higher is more confident, `0` if none. |
| `tatumSignature` | Number of tatums (subdivisions) per beat, `1`-`64` (**set-only** - `tempo.h` exposes `aubio_tempo_set_tatum_signature` but no matching getter; reading it back isn't possible without caching, and unlike `soundtouch`'s cached tempo/pitch/rate this project isn't introducing a cache for a single write-only, rarely-touched tuning knob - confirm this is acceptable before implementing). |
| `wasTatum()` | `aubio_tempo_was_tatum` - `2` if a beat was detected this hop, `1` if a (non-beat) tatum was, `0` otherwise. A method, not a property, since it's a one-shot check of "what just happened this hop" rather than durable object state - matches `process()` itself being a method for the same reason. |
| `lastTatum` | Position of the last detected tatum, in samples (get). |

```js
import { AubioTempo } from 'aubio';

const tempo = new AubioTempo('default', 1024, 256, 44100);
const hop = new Float32Array(256); // ... fill with samples ...
const [isBeat] = tempo.process(hop);
if (isBeat) console.log(`beat at sample ${tempo.last}, ~${tempo.bpm.toFixed(1)} BPM`);
```

## Not yet bound

aubio ships several more analysis/synthesis objects beyond the four
covered above - `specdesc` (raw spectral-descriptor onset functions,
lower-level than `onset`'s own use of them), `filterbank` (mel/other
filterbank energies), `pvoc` (phase vocoder, used internally by several
objects above including `tempo`), `tss` (transient/steady-state
separation), `mfcc`, `dct`/`fft` (raw transform primitives), `sampler`
and `wavetable` (playback/synthesis, not analysis), and `source`/`sink`
(aubio's own file I/O, largely redundant with this project's own
`sndfile` binding). None of these are bound - deliberately out of scope
for now, since nothing in the current `qjs-sound` scripts needs them and
none showed up as a gap in the cross-language survey above (only tempo
did). Flagging here so a future pass doesn't have to re-derive the list.

---

## End-to-end example

Feeding a 440 Hz sine through all three at once (verified against the
actual bindings - onset fires on the attack, pitch settles near 440 Hz,
and notes reports MIDI 69 / A4):

```js
import { AubioNotes, AubioOnset, AubioPitch } from 'aubio';

const SR = 44100, HOP = 256, BUF = 1024, FREQ = 440;

const notes = new AubioNotes('default', BUF, HOP, SR);
const onset = new AubioOnset('default', BUF, HOP, SR);
const pitch = new AubioPitch('yinfft', BUF, HOP, SR);
pitch.setUnit('Hz');

for (let h = 0; h < 40; h++) {
  const buf = new Float32Array(HOP);
  for (let i = 0; i < HOP; i++) {
    const t = (h * HOP + i) / SR;
    buf[i] = 0.8 * Math.sin(2 * Math.PI * FREQ * t);
  }

  const [isOnset] = onset.process(buf);
  if (isOnset) console.log(`onset at hop ${h}`);

  const [hz] = pitch.process(buf);
  const [midiNote, velocity] = notes.process(buf);
  if (midiNote) console.log(`note ${midiNote} vel ${velocity}, pitch ~${hz.toFixed(1)}Hz`);
}
```

## Build notes

Upstream `third_party/aubio/src/CMakeLists.txt` hardcodes
`add_library (aubio SHARED)`, ignoring `BUILD_SHARED_LIBS`, so it can't
produce the static `libaubio.a` this binding needs. Rather than carrying a
local commit in the submodule, the top-level `CMakeLists.txt` applies
`cmake/patches/aubio-static-lib.patch` (`SHARED` -> `STATIC`) to
`third_party/aubio` at configure time via `git apply`, checked first with
`git apply --reverse --check` so re-running `cmake` is a no-op once the
patch is already applied. This keeps `third_party/aubio` a plain checkout
of upstream - unlike `third_party/stk`, which does carry local commits.

After patching, `add_subdirectory(third_party/aubio/src)` builds it with
`CMAKE_POSITION_INDEPENDENT_CODE` set `ON` around that call, producing
`libaubio.a` with `-fPIC`, then links it into `qjs-aubio` via
`make_module(aubio c)`.

Only `third_party/aubio/src` is added - not aubio's top-level
`CMakeLists.txt`, which also pulls in `examples/` and `tests/` (extra
dependencies like Python-generated test fixtures that aubio's own build
needs but this binding doesn't). aubio's optional dependencies (`sndfile`,
`samplerate`, `rubberband`, `libav*`, `vorbis*`, `flac`) are auto-detected
by aubio's own `CMakeLists.txt` via `pkg-config` and enabled if present;
none are required for `AubioNotes`/`AubioOnset`/`AubioPitch`, which only
need the core (`fvec`/`spectral`/`temporal`/`notes`/`onset`/`pitch`)
sources.
