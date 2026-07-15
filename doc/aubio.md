# aubio bindings

`quickjs-aubio.c` wraps three of [aubio](https://aubio.org/)'s analysis
objects — note detection (`notes/notes.h`), onset detection
(`onset/onset.h`), and pitch detection (`pitch/pitch.h`) — as JS classes
exported from the `aubio` module:

```js
import { AubioNotes, AubioOnset, AubioPitch } from 'aubio';
```

All three share the same constructor shape and a `process()` method that
runs one hop of analysis:

```js
new AubioNotes(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
new AubioOnset(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
new AubioPitch(method = 'default', bufSize = 1024, hopSize = 256, sampleRate = 44100)
```

`method` selects the underlying algorithm (aubio-specific strings, e.g.
`'default'`, `'hfc'`, `'complex'` for onset; `'default'`, `'yin'`,
`'yinfft'`, `'schmitt'`, `'fcomb'`, `'mcomb'` for pitch). The constructor
throws `InternalError` if aubio rejects the parameters (unknown method,
`hopSize` larger than `bufSize`, etc).

`process(input)` takes a `Float32Array` of exactly `hopSize` samples —
`smpl_t` is `float` in this build (`HAVE_AUBIO_DOUBLE` is unset), matching
`Float32Array` — and throws `RangeError` if the length doesn't match. It
returns a newly allocated `Float32Array` with the algorithm's output vector.
Input is read directly out of the passed buffer without copying; the
returned array is always a fresh copy.

---

## `AubioNotes` — note onset/pitch/off detection (`notes/notes.h`)

`process()` returns a length-3 `Float32Array`: `[midiNote, velocity,
midiNoteOff]`. `midiNote` is `0` when no note was found; `midiNoteOff` is
the note to turn off, or `-1` if none.

| Member | Description |
|---|---|
| `process(input)` | Run one hop of note detection, see above. |
| `silence` | Silence threshold in dB (get/set). |
| `minioiMs` | Minimum inter-onset interval in milliseconds (get/set). |
| `releaseDrop` | Release drop level in dB — how far the level must fall below the note's initial level before a note-off is emitted (get/set, default `10`). |

```js
import { AubioNotes } from 'aubio';

const notes = new AubioNotes('default', 1024, 256, 44100);
const hop = new Float32Array(256); // ... fill with samples ...
const [midiNote, velocity, midiNoteOff] = notes.process(hop);
if (midiNote) console.log(`note on ${midiNote} vel ${velocity}`);
if (midiNoteOff >= 0) console.log(`note off ${midiNoteOff}`);
```

## `AubioOnset` — onset detection (`onset/onset.h`)

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

## `AubioPitch` — pitch detection (`pitch/pitch.h`)

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

---

## End-to-end example

Feeding a 440 Hz sine through all three at once (verified against the
actual bindings — onset fires on the attack, pitch settles near 440 Hz,
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

`third_party/aubio/src/CMakeLists.txt` is patched to build `aubio` as a
`STATIC` library (upstream hardcodes `SHARED`, ignoring `BUILD_SHARED_LIBS`)
— same kind of direct submodule patch as `third_party/stk`. The top-level
`CMakeLists.txt` builds it via `add_subdirectory(third_party/aubio/src)`
with `CMAKE_POSITION_INDEPENDENT_CODE` set `ON` around that call, producing
`libaubio.a` with `-fPIC`, then links it into `qjs-aubio` via
`make_module(aubio c)`.

Only `third_party/aubio/src` is added — not aubio's top-level
`CMakeLists.txt`, which also pulls in `examples/` and `tests/` (extra
dependencies like Python-generated test fixtures that aubio's own build
needs but this binding doesn't). aubio's optional dependencies (`sndfile`,
`samplerate`, `rubberband`, `libav*`, `vorbis*`, `flac`) are auto-detected
by aubio's own `CMakeLists.txt` via `pkg-config` and enabled if present;
none are required for `AubioNotes`/`AubioOnset`/`AubioPitch`, which only
need the core (`fvec`/`spectral`/`temporal`/`notes`/`onset`/`pitch`)
sources.
