# portaudio bindings

`quickjs-portaudio.c` wraps [PortAudio](http://www.portaudio.com/)
(`/usr/include/portaudio.h`) as a JS module named `portaudio`:

```js
import { PaStream, PaDeviceInfo, HostApiInfo, devices, hostApis } from 'portaudio';
```

This document describes the **target** API after the changes below are
applied - some of it (marked "existing") is already implemented today;
the rest ("new"/"changed") is what this plan adds. See "Plan" at the
bottom for the itemized diff against the current `quickjs-portaudio.c`.

## Design rationale

Surveyed against PyAudio, sounddevice (Python), naudiodon (Node),
`gordonklaus/portaudio` (Go), and `Sound.PortAudio` (Haskell). Two of
their choices are adopted here because they converge across independent
bindings and close real gaps in the current binding:

- **Stream configuration as one options object**, not positional flags -
  matches naudiodon's `AudioIO({inOptions, outOptions})` and Go's
  `StreamParameters{Input, Output}`. This also finally makes per-direction
  device/latency selection reachable, which today's `Pa_OpenDefaultStream`-only
  binding has no way to do at all.
- **Host APIs as their own enumerable collection**, next to `devices` -
  matches PyAudio, sounddevice, and Go all treating host-API enumeration
  as first-class, not something you have to reconstruct from device
  info.
- **Per-direction stream config (`PaStreamParameters`) as a plain JS
  object, not a class** - no surveyed binding (PyAudio, sounddevice,
  naudiodon, Go, Haskell) wraps this in a resource/handle type; it's a
  4-field POD value that exists only to be parsed at the point a stream
  is opened. The existing `PaStreamParameters` `JSClassID` is dropped -
  see "`input`/`output` shape" below.

One thing intentionally kept as-is: **no non-blocking (callback) stream
mode**. PyAudio, sounddevice, and Go all bridge PortAudio's realtime
audio-thread callback into their host language; naudiodon and Haskell's
`portaudio` package both deliberately don't, for the same reason this
binding doesn't (see `quickjs-portaudio.c:10`) - calling into a
non-thread-safe managed runtime (QuickJS, V8, GHC's RTS) from a thread
PortAudio itself created is unsafe. The two GC'd-runtime bindings agree
with the existing design here, so this stays blocking-`read()`/`write()`-only.

Also kept as-is: bare `Pa_*` names for the small set of process-lifetime
free functions (`Pa_Initialize`, `Pa_Terminate`, `Pa_Sleep`), matching
this project's own established convention (see `quickjs-portaudio.c`'s
existing exports) rather than the lowerCamelCase every surveyed binding
uses for these - local convention wins over external convention per
this project's binding guidelines.

## Module-level

| Member | Maps to | Status |
|---|---|---|
| `Pa_Initialize()` | `Pa_Initialize` | existing |
| `Pa_Terminate()` | `Pa_Terminate` | existing |
| `Pa_Sleep(msec)` | `Pa_Sleep` | existing |
| `Pa_GetSampleSize(format)` | `Pa_GetSampleSize` | existing |
| `Pa_GetVersion()` | `Pa_GetVersion` | new |
| `Pa_GetVersionText()` | `Pa_GetVersionText` | new |
| `Pa_GetLastHostErrorInfo()` | `Pa_GetLastHostErrorInfo` | new - returns `{hostApiType, errorCode, errorText}`. Matches the C API's own contract: never returns `null`, but the values are only meaningful after a previous call actually returned `paUnanticipatedHostError`. |
| `devices` | `Pa_GetDeviceCount` / `Pa_GetDeviceInfo` | existing - exotic array-like, extended (see below) |
| `hostApis` | `Pa_GetHostApiCount` / `Pa_GetHostApiInfo` | new - exotic array-like, same shape as `devices` |

All `Pa_*` calls throw a generic `Error` carrying `Pa_GetErrorText()`'s
message on a negative `PaError` (existing `js_portaudio_error()` helper) -
this plan doesn't introduce a typed error class like `portmidi`'s
`PmError`; flagged as an open question below.

### Constants

Existing, unchanged: `paNoDevice`, `paUseHostApiSpecificDeviceSpecification`,
`paContinue`, `paComplete`, `paAbort`, `paInputUnderflow`, `paInputOverflow`,
`paOutputUnderflow`, `paOutputOverflow`, `paPrimingOutput`, `paNoFlag`,
`paClipOff`, `paDitherOff`, `paNeverDropInput`,
`paPrimeOutputBuffersUsingStreamCallback`, `paPlatformSpecificFlags`,
`paFloat32`, `paInt32`, `paInt24`, `paInt16`, `paInt8`, `paUInt8`,
`paCustomFormat`, `paNonInterleaved`.

New:
- `paFramesPerBufferUnspecified` - already used internally as the
  `PaStream` constructor's default, but never exported; callers currently
  have no way to pass "unspecified" explicitly by name.
- Host API type IDs, for comparing against `HostApiInfo.type`:
  `paInDevelopment`, `paDirectSound`, `paMME`, `paASIO`, `paSoundManager`,
  `paCoreAudio`, `paOSS`, `paALSA`, `paAL`, `paBeOS`, `paWDMKS`, `paJACK`,
  `paWASAPI`, `paAudioScienceHPI`.

## `devices` - `Pa_GetDeviceInfo` (existing, extended)

Exotic array-like object, unchanged shape: `devices[i]` is a
`PaDeviceInfo`, `devices.length` is `Pa_GetDeviceCount()`. New,
non-indexed properties:

| Member | Maps to | Notes |
|---|---|---|
| `devices.defaultInput` | `Pa_GetDefaultInputDevice` | Device index, or `-1` (`paNoDevice`) if none. |
| `devices.defaultOutput` | `Pa_GetDefaultOutputDevice` | Device index, or `-1` if none. |

## `hostApis` - `Pa_GetHostApiInfo` (new)

Exotic array-like object, same pattern as `devices`: `hostApis[i]` is a
`HostApiInfo`, `hostApis.length` is `Pa_GetHostApiCount()`, plus:

| Member | Maps to | Notes |
|---|---|---|
| `hostApis.default` | `Pa_GetDefaultHostApi` | Host API index. |

## `PaDeviceInfo` - `Pa_GetDeviceInfo` (existing, unchanged)

| Member | C field |
|---|---|
| `structVersion` | `structVersion` |
| `name` | `name` |
| `hostApi` | `hostApi` - index into `hostApis` |
| `maxInputChannels` | `maxInputChannels` |
| `maxOutputChannels` | `maxOutputChannels` |
| `defaultLowInputLatency` | `defaultLowInputLatency` |
| `defaultLowOutputLatency` | `defaultLowOutputLatency` |
| `defaultHighInputLatency` | `defaultHighInputLatency` |
| `defaultHighOutputLatency` | `defaultHighOutputLatency` |
| `defaultSampleRate` | `defaultSampleRate` |

Produced by `devices[i]`; also independently constructible
(`new PaDeviceInfo(index)`), same as today.

## `HostApiInfo` - `Pa_GetHostApiInfo` (new)

| Member | C field |
|---|---|
| `structVersion` | `structVersion` |
| `type` | `type` - a `PaHostApiTypeId`, compare against the `pa*` constants above |
| `typeName` | - | `type` resolved to a string (`"ALSA"`, `"JACK"`, ...) - the same id-to-name table `PaStream.hostApiType` already builds internally, factored out and reused here |
| `name` | `name` |
| `deviceCount` | `deviceCount` |
| `defaultInputDevice` | `defaultInputDevice` - index into `devices` |
| `defaultOutputDevice` | `defaultOutputDevice` - index into `devices` |

Read-only, mainly produced by `hostApis[i]`. Also independently
constructible (`new HostApiInfo(index)`, same `Pa_GetHostApiInfo(index)`
copy-in pattern as `PaDeviceInfo`'s own constructor) for symmetry with
`PaDeviceInfo` - matching this codebase's own precedent took priority over
mirroring `portmidi`'s `PmDeviceInfo` ("not constructible, only produced")
once the two turned out to conflict during implementation.

## `input`/`output` shape - `PaStreamParameters` (removed as a class)

No `PaStreamParameters` class. Wherever a `PaStreamParameters` is needed
(only `PaStream`'s object-form constructor and `PaStream.isFormatSupported`,
below), pass a plain object:

```ts
interface PaStreamParams {
  device?: number;           // default -1 (paNoDevice)
  channelCount?: number;     // default 2
  sampleFormat?: number;     // default paFloat32
  suggestedLatency?: number; // default 0.001
}
```

Parsed field-by-field at the C boundary into a stack `PaStreamParameters`
for the duration of the `Pa_OpenStream`/`Pa_IsFormatSupported` call, same
defaults as today's constructor had. `hostApiSpecificStreamInfo` is
dropped along with the class - it only ever round-tripped as an opaque
pointer-identity string, never usably constructible from JS, and no
surveyed binding exposes it either.

## `PaStream` - `PaStream*` (existing, constructor changed, methods fixed)

### Opening

Two constructor forms, both supported (the first is today's, kept
unchanged for backward compatibility):

```ts
// existing - maps to Pa_OpenDefaultStream
new PaStream(numInputChannels = 0, numOutputChannels = 2, sampleFormat = paFloat32, sampleRate = 44100, framesPerBuffer = paFramesPerBufferUnspecified)

// new - maps to Pa_OpenStream, needed to pick a specific device / differing
// input vs output devices or latency / a specific host API
new PaStream({
  input?: PaStreamParams | null,
  output?: PaStreamParams | null,
  sampleRate?: number,        // default 44100
  framesPerBuffer?: number,   // default paFramesPerBufferUnspecified
  flags?: number,             // default paNoFlag
})
```

`input`/`output` may each be omitted or `null` for a stream with no
input (resp. output) side, matching `Pa_OpenStream`'s own
`NULL`-parameter convention. The object form is picked when the first
constructor argument is an object; the positional form otherwise - no
new argument-count ambiguity since the existing form's first argument is
always a number.

### Static

| Member | Maps to | Notes |
|---|---|---|
| `PaStream.isFormatSupported(inputParameters, outputParameters, sampleRate)` | `Pa_IsFormatSupported` | `inputParameters`/`outputParameters` are each a `PaStreamParams` object or `null`. Throws (with PortAudio's reason text) if unsupported; returns `undefined` if supported - call this before `new PaStream({...})` to validate a proposed config without opening it. |

### Properties (existing, unchanged)

`active`, `stopped`, `inputLatency`, `outputLatency`, `sampleRate`,
`time`, `cpuLoad`, `readAvailable`, `writeAvailable`, `hostApiType`
(the last one guarded by `HAVE_GETSTREAMHOSTAPITYPE`, unchanged).

### Methods (existing, error handling fixed)

| Member | Maps to | Notes |
|---|---|---|
| `.read(buffer, frames?)` | `Pa_ReadStream` | `frames` defaults to `buffer`'s length divided by frame size, as today. **Fixed**: throws if called after `.close()` instead of passing a NULL `PaStream*` into PortAudio. |
| `.write(buffer, frames?)` | `Pa_WriteStream` | Same closed-stream fix as `.read()`. |
| `.start()` | `Pa_StartStream` | **Fixed**: now throws via the same `js_portaudio_error()` path as every other call, instead of returning a raw error code on failure. |
| `.stop()` | `Pa_StopStream` | Same fix as `.start()`. |
| `.abort()` | `Pa_AbortStream` | Same fix as `.start()`. |
| `.close()` | `Pa_CloseStream` | Same throw-on-error fix. **Also**: idempotent - calling `.close()` again on an already-closed stream is a no-op, not an error (matches `PortMidiStream.close()`'s documented behavior in `doc/portmidi.md`). |

## Example

```js
import { PaStream, devices, hostApis, paFloat32 } from 'portaudio';

console.log(`default output: ${devices[devices.defaultOutput].name}`);
console.log(`host API: ${hostApis[devices[devices.defaultOutput].hostApi].typeName}`);

const output = { device: devices.defaultOutput, channelCount: 2, sampleFormat: paFloat32, suggestedLatency: 0.02 };
PaStream.isFormatSupported(null, output, 44100); // throws if this device can't do it

const stream = new PaStream({ output, sampleRate: 44100 });
stream.start();

const buf = new Float32Array(2 * 256); // 256 frames, stereo interleaved
// ... fill buf ...
stream.write(buf);

stream.stop();
stream.close();
```

## Plan (diff against current `quickjs-portaudio.c`)

1. **`PaStream` constructor**: branch on `JS_IsObject(argv[0])` vs number
   to pick `Pa_OpenStream` vs the existing `Pa_OpenDefaultStream` path
   (unchanged). For the object form, read `sampleRate`/`framesPerBuffer`/
   `flags` directly off it, and for `input`/`output` call a new helper
   `js_pastreamparameters_fromobj(ctx, JSValueConst obj, PaStreamParameters *out)`
   - `obj` may be `undefined`/`null` (returns `FALSE`, caller passes `NULL`
   to `Pa_OpenStream`) or a plain object (`FALSE` for missing, reads
   `device`/`channelCount`/`sampleFormat`/`suggestedLatency` with the same
   defaults the current `PaStreamParameters` constructor uses). No
   `JSClassID`/finalizer needed - this is section 6/8's duck-typed
   struct-by-value pattern, not a resource class.
2. **`PaStream.isFormatSupported`**: new static function
   (`JS_CFUNC_constructor`'s sibling, a plain `JS_NewCFunction2` set as a
   property on `pastream_ctor`, or a magic-dispatched module function
   grouped under the class) wrapping `Pa_IsFormatSupported`, reusing
   both `js_pastreamparameters_fromobj()` (item 1) and
   `js_portaudio_error()`.
3. **`js_pastream_method`**: add `if (!w->stream) { ... }` guard at the
   top - `close()` returns early as a no-op; every other method throws.
   Route `METHOD_START`/`STOP`/`ABORT`/`CLOSE` through
   `js_portaudio_error()` instead of `JS_NewInt32`. (Fixes
   `pastream-method-no-closed-guard` and
   `pastream-start-stop-abort-close-dont-throw` in `BUGS`.)
4. **`HostApiInfo` class**: new `JSClassID`/`JSClassDef`/proto, mirroring
   `js_padeviceinfo_*` almost exactly (struct copy in the constructor,
   read-only getters, `js_hostapiinfo_wrap()` for use by the exotic
   `hostApis` object same as `js_padeviceinfo_wrap()`). Add a
   `typeName` getter reusing the `PaHostApiTypeId`-to-string table
   currently inlined under `#ifdef HAVE_GETSTREAMHOSTAPITYPE` in
   `js_pastream_get` - factor that table out into a shared
   `static const char *pa_hostapitype_name(PaHostApiTypeId)` helper used
   by both.
5. **`hostApis` exotic object**: new `JSClassID`/`JSClassExoticMethods`,
   copy of `js_padevices_*` with `Pa_GetHostApiCount`/`Pa_GetHostApiInfo`
   in place of the device equivalents, plus a `"default"` string-key
   case in `get_own_property` for `Pa_GetDefaultHostApi()`.
6. **`devices` exotic object**: add `"defaultInput"`/`"defaultOutput"`
   string-key cases to `js_padevices_get_own_property`, alongside the
   existing `"length"` case.
7. **Module-level additions**: `Pa_GetVersion`, `Pa_GetVersionText`,
   `Pa_GetLastHostErrorInfo` as new `FUNC_*` magic cases in
   `js_portaudio_function`; export `paFramesPerBufferUnspecified` and the
   `PaHostApiTypeId` constants in `js_portaudio_funcs`.
8. **Export list**: add `HostApiInfo` and `hostApis` to both
   `js_portaudio_init` and `js_init_module_portaudio`'s export
   declarations; **remove** `PaStreamParameters`'s `JSClassID`/`JSClassDef`/
   constructor/proto and its export from both (`js_pastreamparameters_*`
   deleted outright, replaced by the `js_pastreamparameters_fromobj()`
   helper from item 1).

Mostly additive plus the two `BUGS`-filed fixes, with one removal:
`PaStreamParameters` as a class goes away (see "`input`/`output` shape"
above) - everything else already exported (`PaDeviceInfo`, `PaStream`'s
existing constructor form, `devices`) keeps its current shape.

## Open questions before implementing

- **Typed error class or not?** `portmidi` throws a typed `PmError extends
  Error` with a `.code`; this plan keeps `portaudio`'s existing plain
  `Error` (from `js_portaudio_error()`). Worth aligning the two bindings,
  or is the difference fine since `PaError`'s int codes are less commonly
  branched on than `PmError`'s?
- **`hostApiSpecificStreamInfo`**: still write-only-as-a-pointer-identity;
  none of the surveyed bindings expose ALSA/JACK/etc-specific stream info
  either (PyAudio has host-API-specific *parameter* subclasses for a few
  APIs, but that's real added scope) - confirm leaving this as-is is fine
  rather than expanding it.
- **`Pa_HostApiDeviceIndexToDeviceIndex` / `Pa_HostApiTypeIdToHostApiIndex`**:
  left out of this plan as low-value index-translation helpers, easy to
  compute in JS from `hostApis[i]`/`devices` directly - confirm skipping
  them.
