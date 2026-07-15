# STK realtime/streaming I/O bindings

`quickjs-stk.cpp` wraps STK's I/O layer — realtime audio (`RtAudio`, `RtWvIn`,
`RtWvOut`), realtime MIDI (`RtMidi`), network audio streaming (`InetWvIn`,
`InetWvOut`), and standard MIDI file reading (`MidiFileIn`) — as JS classes
exported from the `stk` module, alongside the DSP classes (filters,
generators, effects, instruments) documented by the class list in
`quickjs-stk.cpp` itself.

All classes below are constructed with `new` and exported directly from the
module, e.g.:

```js
import { RtWvOut, StkFrames } from 'stk';
```

## Error handling

Unlike the DSP classes (`BiQuad`, `SineWave`, etc.), which currently let
any `stk::StkError` thrown from a constructor propagate as an unhandled C++
exception, every class documented here catches `stk::StkError` /
`RtMidiError` and re-throws as a normal catchable JS `TypeError`. Realtime
and network I/O fail far more often in ordinary use (missing device, bad
host, no MIDI ports, file not found) than the DSP classes, so this was worth
the extra code. Wrap calls in `try`/`catch` accordingly:

```js
try {
  const midi = new MidiFileIn('missing.mid');
} catch (e) {
  console.error(e.message); // "MidiFileIn: error opening or finding file (missing.mid)."
}
```

## Class hierarchy

Two base classes exist purely to share `tick`/property implementations
across their realtime and network subclasses (they mirror how
`Filter`/`Generator`/`Effect`/`StkInstrmnt` back the DSP classes) —
neither is exported or constructible on its own:

- **`StkWvIn`** (wraps `stk::WvIn`) → `RtWvIn`, `InetWvIn`
- **`StkWvOut`** (wraps `stk::WvOut`) → `RtWvOut`, `InetWvOut`

---

## `RtWvIn` — realtime audio input (`RtWvIn.h`)

Blocking realtime audio capture via RtAudio's ALSA/JACK backends, buffered
internally by STK.

```js
new RtWvIn(nChannels = 1, sampleRate = Stk.sampleRate, deviceIndex = 0,
              bufferFrames = 512, nBuffers = 20)
```

`deviceIndex` is STK's own device-position convention (`0` = default input
device, `1` = first enumerated device, ...) — **not** the device `id` values
returned by `RtAudio.getDeviceIds()`.

| Member | Description |
|---|---|
| `tick(channel = 0)` | Compute a frame, return one channel's sample. Starts the stream if stopped. |
| `tick(frames, channel = 0)` | Fill an `StkFrames` starting at `channel`; returns the same `StkFrames`. |
| `channelsOut` | Number of channels (getter). |
| `lastFrame` | Last computed frame as a plain `Array` of channel values (getter). |
| `start()` | Start the input stream (also happens automatically on first `tick`). |
| `stop()` | Stop the input stream. |

## `RtWvOut` — realtime audio output (`RtWvOut.h`)

Blocking realtime audio playback, same buffering model as `RtWvIn`.

```js
new RtWvOut(nChannels = 1, sampleRate = Stk.sampleRate, deviceIndex = 0,
               bufferFrames = 512, nBuffers = 20)
```

| Member | Description |
|---|---|
| `tick(sample)` | Output one sample to all channels. |
| `tick(frames)` | Output an entire `StkFrames` buffer. |
| `clipStatus()` | `true` if clipping (values outside ±1.0) has been detected since instantiation or the last reset. |
| `resetClipStatus()` | Clear the clipping flag. |
| `frameCount` | Total sample frames output so far (getter). |
| `time` | Seconds of data output so far (getter). |
| `start()` | Start the output stream (also happens automatically on first `tick`). |
| `stop()` | Stop the output stream. |

```js
import { RtWvOut, StkFrames } from 'stk';

const out = new RtWvOut(1, 44100, 0 /* default device */, 512, 4);
const frames = new StkFrames(256, 1);
const buf = new Float64Array(frames.buffer);
for (let i = 0; i < 256; i++) buf[i] = Math.sin(i * 0.05) * 0.2;
out.tick(frames);
out.stop();
```

## `InetWvIn` — network audio streaming input (`InetWvIn.h`)

Receives streamed audio over a TCP or UDP socket. `listen()` blocks (TCP) or
returns immediately (UDP) waiting for a peer.

```js
new InetWvIn(bufferFrames = 1024, nBuffers = 8)
```

| Member | Description |
|---|---|
| `listen(port = 2006, nChannels = 1, format = 2, protocol = 0)` | `format` is one of the raw `Stk::StkFormat` values (`1`=SINT8, `2`=SINT16, `4`=SINT24, `8`=SINT32, `0x10`=FLOAT32, `0x20`=FLOAT64 — default `2`/SINT16 matches STK's own default). `protocol` is `0` = TCP, `1` = UDP (`stk::Socket::ProtocolType`). For TCP this blocks until a client connects; for UDP it returns immediately. |
| `isConnected()` | `true` if a connection exists or buffered input remains to be read. |
| `tick(channel = 0)` / `tick(frames, channel = 0)` | Same shape as `RtWvIn.tick`. |
| `channelsOut`, `lastFrame` | Inherited from `StkWvIn`. |

## `InetWvOut` — network audio streaming output (`InetWvOut.h`)

Streams audio out over a TCP or UDP socket, big-endian on the wire.

```js
new InetWvOut(packetFrames = 1024)
```

| Member | Description |
|---|---|
| `connect(port, protocol = 0, hostname = 'localhost', nChannels = 1, format = 2)` | Same `protocol`/`format` encoding as `InetWvIn.listen`. |
| `disconnect()` | Flush and close the connection, if any. |
| `tick(sample)` / `tick(frames)` | Same shape as `RtWvOut.tick`. |
| `clipStatus()`, `resetClipStatus()`, `frameCount`, `time` | Inherited from `StkWvOut`. |

```js
import { InetWvIn, InetWvOut, StkFrames } from 'stk';

const PROTO_UDP = 1, FORMAT_SINT16 = 2;

const wvIn = new InetWvIn(256, 4);
wvIn.listen(6007, 1, FORMAT_SINT16, PROTO_UDP);

const wvOut = new InetWvOut(256);
wvOut.connect(6007, PROTO_UDP, 'localhost', 1, FORMAT_SINT16);

const frames = new StkFrames(64, 1);
new Float64Array(frames.buffer).forEach((_, i, a) => (a[i] = Math.sin(i * 0.1) * 0.5));
wvOut.tick(frames);
wvOut.disconnect();
```

> **Note:** `InetWvIn.tick()` blocks reading from its internal socket
> thread until data is available — round-tripping a single UDP packet in a
> tight script needs a short delay between the `wvOut.tick()` send and the
> `wvIn.tick()` read, and is inherently timing-sensitive (this is STK's own
> documented blocking behavior, not something the bindings add).

## `MidiFileIn` — standard MIDI file reader (`MidiFileIn.h`)

```js
new MidiFileIn(fileName)
```

Throws if the file can't be opened or isn't a valid MIDI file.

| Member | Description |
|---|---|
| `format` | MIDI file format: `0`, `1`, or `2` (getter). |
| `numberOfTracks` | Track count (getter). |
| `division` | Raw division field from the file header — parse per the MIDI File spec if the MSB is set (getter). |
| `rewindTrack(track = 0)` | Reset a track's read position and tempo state. |
| `getTickSeconds(track = 0)` | Current seconds-per-tick for a track (changes as "Set Tempo" meta events are read). |
| `getNextEvent(track = 0)` | Next raw event (including meta/sysex). Returns `{ deltaTime, data }` — `data` is empty when the track is exhausted. |
| `getNextMidiEvent(track = 0)` | Next MIDI *channel* event only (meta/sysex are skipped, tempo is still tracked internally). Same `{ deltaTime, data }` shape. |

```js
import { MidiFileIn } from 'stk';

const mf = new MidiFileIn('song.mid');
console.log(mf.format, mf.numberOfTracks, mf.division);

for (let ev = mf.getNextMidiEvent(1); ev.data.length; ev = mf.getNextMidiEvent(1)) {
  console.log(ev.deltaTime, ev.data); // e.g. 0 [144, 60, 100] = note-on C4 vel 100
}
```

Track `0` in a format-1 file is typically the tempo/conductor track and has
no channel events — use `getNextEvent(0)` to see its meta events, or read
channel messages from tracks `1..numberOfTracks-1`.

## `RtMidiIn` / `RtMidiOut` — realtime MIDI (`RtMidi.h`)

`RtMidiIn`/`RtMidiOut` are declared in the **global** C++ namespace by STK
(unlike everything else), but are exposed here the same way as other STK
classes.

```js
new RtMidiIn(api = RtMidi.UNSPECIFIED, clientName = 'RtMidi Input Client', queueSizeLimit = 100)
new RtMidiOut(api = RtMidi.UNSPECIFIED, clientName = 'RtMidi Output Client')
```

`api` is a raw `RtMidi::Api` enum value; `0` (`UNSPECIFIED`) auto-selects a
compiled backend (ALSA/JACK on Linux).

**`RtMidiIn`** (message polling, not callback-based — see [Scope
limitations](#scope-limitations)):

| Member | Description |
|---|---|
| `openPort(portNumber = 0, portName = 'RtMidi Input')` | Connect to an existing MIDI input port. |
| `openVirtualPort(portName = 'RtMidi Input')` | Create a virtual port other software can connect to (Linux ALSA/JACK, not Windows). |
| `closePort()` | Close the current connection, if any. |
| `isPortOpen()` | `true` if connected via `openPort` (not `openVirtualPort`). |
| `getPortCount()` | Number of available input ports. |
| `getPortName(portNumber = 0)` | Name of a given port. |
| `ignoreTypes(sysex = true, time = true, sense = true)` | Which message types to drop rather than queue. |
| `getMessage()` | Pop the next queued message: `{ timeStamp, data }` (`data` empty if none pending). Non-blocking. |
| `getCurrentApi()` | The `RtMidi::Api` value actually in use. |

**`RtMidiOut`**:

| Member | Description |
|---|---|
| `openPort(portNumber = 0, portName = 'RtMidi Output')` | Connect to an existing MIDI output port. |
| `openVirtualPort(portName = 'RtMidi Output')` | Create a virtual output port. |
| `closePort()` | Close the current connection, if any. |
| `isPortOpen()` | `true` if connected via `openPort`. |
| `getPortCount()` | Number of available output ports. |
| `getPortName(portNumber = 0)` | Name of a given port. |
| `sendMessage(bytes)` | Send a raw MIDI message immediately; `bytes` is an array-like of byte values, e.g. `[0x90, 60, 100]` for note-on. |
| `getCurrentApi()` | The `RtMidi::Api` value actually in use. |

```js
import { RtMidiIn, RtMidiOut } from 'stk';

const midiOut = new RtMidiOut();
console.log(midiOut.getPortCount(), midiOut.getPortName(0));
midiOut.openPort(0);
midiOut.sendMessage([0x90, 60, 100]); // note-on C4

const midiIn = new RtMidiIn();
midiIn.openPort(0);
const msg = midiIn.getMessage();
if (msg.data.length) console.log(msg.timeStamp, msg.data);
```

> `sendMessage()` on the ALSA backend does not throw when no port is open —
> it silently no-ops (this is upstream RtMidi behavior, reported only via
> `RtMidiError::WARNING`, which STK deliberately doesn't throw for).

## `RtAudio` — audio device enumeration (`RtAudio.h`)

```js
new RtAudio(api = RtAudio.UNSPECIFIED)
```

| Member | Description |
|---|---|
| `getCurrentApi()` | The `RtAudio::Api` value actually in use. |
| `getDeviceCount()` | Number of audio devices found. |
| `getDeviceIds()` | `Array` of device ids (opaque, not positions — see note below). |
| `getDeviceNames()` | `Array` of device name strings, same order as `getDeviceIds()`. |
| `getDeviceInfo(deviceId)` | `{ id, name, outputChannels, inputChannels, duplexChannels, isDefaultOutput, isDefaultInput, sampleRates, currentSampleRate, preferredSampleRate, nativeFormats }`. |
| `getDefaultOutputDevice()` / `getDefaultInputDevice()` | Default device id. |
| `closeStream()`, `startStream()`, `stopStream()`, `abortStream()` | Stream lifecycle (`start`/`stop`/`abort` return an `RtAudioErrorType` int; `0` = no error). |
| `getErrorText()` | Last error/warning message. |
| `isStreamOpen()`, `isStreamRunning()` | Stream state. |
| `getStreamTime()` / `setStreamTime(seconds)` | Elapsed stream time. |
| `getStreamLatency()` | Reported latency in sample frames. |
| `getStreamSampleRate()` | Actual sample rate in use by an open stream. |
| `showWarnings(value = true)` | Toggle warning output. |

```js
import { RtAudio } from 'stk';

const audio = new RtAudio();
for (const id of audio.getDeviceIds()) {
  const info = audio.getDeviceInfo(id);
  console.log(id, info.name, `in=${info.inputChannels} out=${info.outputChannels}`);
}
console.log('default output device id:', audio.getDefaultOutputDevice());
```

> **Device id vs. device index:** `RtAudio`'s device `id` values (from
> `getDeviceIds()`/`getDeviceInfo()`) are **not** the same numbering as the
> `deviceIndex` argument to `RtWvIn`/`RtWvOut` (STK's older
> position-based convention: `0` = default, `1` = first enumerated device,
> ...). Don't pass a `RtAudio` device id straight into `RtWvIn`/`RtWvOut`.

### Scope limitations

`RtAudio.openStream()` (the raw `RtAudioCallback`-based streaming API) and
`RtMidiIn.setCallback()` are **not** bound. Both would require invoking a
JS callback from RtAudio's/RtMidi's own native audio/MIDI thread — QuickJS
is not thread-safe, so calling back into the engine from a thread other than
the one running the interpreter is unsafe. Use `RtWvIn`/`RtWvOut` for
realtime audio (STK's own blocking wrapper around the same callback
machinery, buffered internally so it's safe to drive from a single JS-side
tick loop) and `RtMidiIn.getMessage()` polling for realtime MIDI input.

## Build notes

`libstk.a` already compiles `RtAudio.cpp`, `RtMidi.cpp`, `RtWvIn/Out.cpp`,
`InetWvIn/Out.cpp`, `Socket.cpp`, `TcpServer/Client.cpp`, `UdpSocket.cpp`,
`Thread.cpp`, and `Mutex.cpp` unconditionally (`third_party/stk/CMakeLists.txt`
globs `src/*.cpp` with no filtering). The top-level `CMakeLists.txt` links
`pthread`, `asound`, and `jack` into the `qjs-stk` module (Linux/Android only)
since these bindings now actually call into that code.
