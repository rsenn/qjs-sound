# portmidi bindings

`quickjs-portmidi.c` wraps [PortMidi](https://github.com/portmidi/portmidi)
(`/usr/include/portmidi.h`) as a JS module named `portmidi`:

```js
import { PortMidiStream, PmDeviceInfo, PmError, initialize, terminate } from 'portmidi';
```

The API surface deliberately mirrors the existing PortMidi bindings for
other languages ([Go](https://pkg.go.dev/github.com/markbates/portmidi),
[Haskell](https://portmidi.hexdocs.pm/PortMidi.html),
[Python/pyPortMidi](https://github.com/grantma/python-portmidi),
[pygame.midi](https://www.pygame.org/docs/ref/midi.html)) rather than
inventing new names: `countDevices`, `getDeviceInfo`, `getDefaultInputDeviceID`,
`setChannelMask`, `writeSysEx`, etc, all match those bindings' naming.
Two things it does differently, per this project's convention (see
`quickjs-portaudio.c`, `quickjs-aubio.c`):

- `PortMidiStream` is a single JS **class** wrapping `PortMidiStream*`
  (all the surveyed bindings above split it into separate `Input`/`Output`
  types or keep it a bare opaque handle threaded through free functions).
- Every failing `PmError`-returning call throws a typed **`PmError`**
  exception (`extends Error`) instead of returning an error code or a
  generic string-message error.

## Threading model

PortMidi's Linux/ALSA backend runs no internal I/O thread: `read()`/`poll()`
never block (they just check whether the kernel-buffered ALSA sequencer
queue has anything pending), and `write()`/`writeShort()` hand events
directly to the ALSA sequencer, which does the actual timestamp-accurate
delivery. So there's nothing here to make async - `read`/`write`/`poll` are
all plain synchronous calls, and simultaneous realtime input+output is
achieved the same way every other binding does it: call `poll()`/`read()`
periodically from your own loop while calling `write()` as needed.

## Module-level

| Member | Maps to | Notes |
|---|---|---|
| `initialize()` | `Pm_Initialize` | Call once before anything else. Throws `PmError`. |
| `terminate()` | `Pm_Terminate` | Throws `PmError`. |
| `countDevices()` | `Pm_CountDevices` | |
| `getDefaultInputDeviceID()` | `Pm_GetDefaultInputDeviceID` | Returns `-1` (`pmNoDevice`) if none. |
| `getDefaultOutputDeviceID()` | `Pm_GetDefaultOutputDeviceID` | Returns `-1` if none. |
| `getDeviceInfo(id)` | `Pm_GetDeviceInfo` | Returns a `PmDeviceInfo`, or `null` if `id` is out of range. |
| `devices` | `Pm_GetDeviceInfo` loop | Exotic array-like object, `devices[i]` / `devices.length`, same pattern as `portaudio`'s `devices` export. |
| `time()` | `Pt_Time` via PortTime's default clock | Current PortMidi timer, milliseconds. |
| `getErrorText(code)` | `Pm_GetErrorText` | Rarely needed directly - `PmError.message` already carries this. |
| `channel(n)` | `Pm_Channel` macro | Builds a channel-mask bit for `setChannelMask` (`n` is `0`-`15`). |

### Constants

- `PmError` enum values as plain numbers: `pmNoError`, `pmGotData`,
  `pmHostError`, `pmInvalidDeviceId`, `pmInsufficientMemory`,
  `pmBufferTooSmall`, `pmBufferOverflow`, `pmBadPtr`, `pmBadData`,
  `pmInternalError`, `pmBufferMaxSize`.
- `pmNoDevice` (`-1`).
- Filter bitmasks for `setFilter`: `PM_FILT_ACTIVE`, `PM_FILT_SYSEX`,
  `PM_FILT_CLOCK`, `PM_FILT_PLAY`, `PM_FILT_TICK`, `PM_FILT_FD`,
  `PM_FILT_UNDEFINED`, `PM_FILT_RESET`, `PM_FILT_REALTIME`, `PM_FILT_NOTE`,
  `PM_FILT_CHANNEL_AFTERTOUCH`, `PM_FILT_POLY_AFTERTOUCH`,
  `PM_FILT_AFTERTOUCH`, `PM_FILT_PROGRAM`, `PM_FILT_CONTROL`,
  `PM_FILT_PITCHBEND`, `PM_FILT_MTC`, `PM_FILT_SONG_POSITION`,
  `PM_FILT_SONG_SELECT`, `PM_FILT_TUNE`, `PM_FILT_SYSTEMCOMMON`.

## `PmDeviceInfo` - `Pm_GetDeviceInfo` (read-only)

| Member | C field |
|---|---|
| `interf` | `interf` - underlying MIDI API, e.g. `"ALSA"` |
| `name` | `name` |
| `input` | `input` (boolean) |
| `output` | `output` (boolean) |
| `opened` | `opened` (boolean) |

Not constructible from JS (no public constructor) - only produced by
`getDeviceInfo()` / `devices[i]`.

## `PmError` - thrown on failure

```ts
class PmError extends Error {
  code: number;    // the PmError enum value (negative)
  message: string; // Pm_GetErrorText(code), plus Pm_GetHostErrorText()
                    // appended when code === pmHostError
}
```

Every module function and `PortMidiStream` method that wraps a
`PmError`-returning C call throws one of these instead of returning the
code. Success (`pmNoError`, `pmGotData` after a `poll()`) never throws.

## `PortMidiStream` - `PortMidiStream*`

No public constructor; open a stream with one of the two static factories
(mirrors `Pm_OpenInput`/`Pm_OpenOutput`'s differing parameter lists -
Go and Haskell make the same split):

```ts
PortMidiStream.openInput(deviceId: number, bufferSize = 4096): PortMidiStream
PortMidiStream.openOutput(deviceId: number, bufferSize = 256, latency = 0): PortMidiStream
```

Both throw `PmError` on failure (bad device, device already opened, wrong
direction, etc).

### Events

`read()`/`write()` use **decoded** event objects - not the raw packed
`PmMessage` int - matching Go's `Event{Status,Data1,Data2,Timestamp}` and
pyPortMidi's `[[status,data1,data2],timestamp]`:

```ts
interface PmEvent {
  status: number;
  data1: number;
  data2: number;
  timestamp: number; // PmTimestamp, milliseconds
}
```

### Methods / properties

| Member | Maps to | Notes |
|---|---|---|
| `.read(maxEvents = 1024)` | `Pm_Read` | Returns `PmEvent[]`, possibly empty. Throws `PmError` (e.g. `pmBufferOverflow`) on failure - the overflow itself does not lose already-buffered events already returned. |
| `.write(events)` | `Pm_Write` | `events` is a `PmEvent[]` (array of `{status,data1,data2,timestamp}`); re-encoded via `Pm_Message`. Throws if `events.length` doesn't fit PortMidi's constraints. |
| `.writeShort(status, data1 = 0, data2 = 0, when = 0)` | `Pm_WriteShort` | Convenience for a single non-sysex message; `when` of `0` means "send immediately" per `Pm_WriteShort`'s own semantics. |
| `.writeSysEx(when, bytes)` | `Pm_WriteSysEx` | `bytes` is a `Uint8Array`/`DataView` (or a plain `ArrayBuffer`); `Pm_WriteSysEx` has no length parameter, it scans the buffer for the `0xF7` EOX byte itself, so `bytes` must end with `0xF7`. |
| `.poll()` | `Pm_Poll` | Returns `boolean` (`TRUE`/`FALSE`); throws `PmError` on an actual error return. |
| `.abort()` | `Pm_Abort` | Output streams only. |
| `.close()` | `Pm_Close` | Idempotent - closing twice is a no-op, not an error. |
| `.setFilter(mask)` | `Pm_SetFilter` | Input streams only. `mask` built from the `PM_FILT_*` constants. |
| `.setChannelMask(mask)` | `Pm_SetChannelMask` | `mask` built from `channel(n)`, OR'd together. |
| `.synchronize()` | `Pm_Synchronize` | Rarely needed - see `portmidi.h`'s own doc comment on it. |
| `.hasHostError` (getter) | `Pm_HasHostError` | boolean. |
| `.deviceId` (getter) | - | The device ID this stream was opened with (stored at open time, not a PortMidi call). |
| `.isOutput` (getter) | - | `true` for streams opened via `openOutput`. |

### Example

```js
import { initialize, terminate, PortMidiStream, PM_FILT_ACTIVE } from 'portmidi';

initialize();

const input = PortMidiStream.openInput(0);
const output = PortMidiStream.openOutput(1, 256, 0);
input.setFilter(PM_FILT_ACTIVE);

for (;;) {
  if (input.poll()) {
    for (const ev of input.read(64)) {
      output.writeShort(ev.status, ev.data1, ev.data2); // simple MIDI thru
    }
  }
}

input.close();
output.close();
terminate();
```

## Open questions before implementing

- `time()` - `portmidi.h` doesn't expose a `Pm_Time()`/timer function
  directly (it takes a caller-supplied `PmTimeProcPtr`, defaulting to
  PortTime's `Pt_Time()` when `NULL` is passed to `openInput`/`openOutput`).
  Exposing a module-level `time()` means linking PortTime's `Pt_Time()`
  directly - confirm that's desired, or drop it from the surface.
- `.deviceId`/`.isOutput` are bookkeeping this binding adds (PortMidi
  itself doesn't expose them back off a `PortMidiStream*`) - fine to keep,
  drop, or rename.
