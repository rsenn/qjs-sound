# qjs-sound

Native audio bindings for [QuickJS](https://bellard.org/quickjs/): one loadable
module per library, usable from `qjsm` scripts.

| module        | wraps                                                      | JS API                          |
|---------------|------------------------------------------------------------|---------------------------------|
| `labsound`    | [LabSound](https://github.com/LabSound/LabSound) graph audio engine | Web Audio API (`AudioContext`, `OscillatorNode`, ...) |
| `stk`         | [Synthesis ToolKit](https://github.com/thestk/stk)         | instruments, filters, effects, I/O |
| `portaudio`   | [PortAudio](http://www.portaudio.com/)                     | device enumeration, streams     |
| `portmidi`    | [PortMidi](https://github.com/PortMidi/portmidi)           | MIDI input/output               |
| `sndfile`     | [libsndfile](http://libsndfile.github.io/libsndfile/)      | read/write audio files          |
| `samplerate`  | [libsamplerate](https://libsndfile.github.io/libsamplerate/) | sample-rate conversion        |
| `soundtouch`  | [SoundTouch](https://www.surina.net/soundtouch/)           | tempo / pitch shifting          |
| `rubberband`  | [Rubber Band](https://breakfastquay.com/rubberband/)       | time-stretching, pitch-shifting |
| `aubio`       | [aubio](https://aubio.org/)                                | pitch, onset, tempo detection   |
| `sndobj`      | [SndObj](https://github.com/sndobj/sndobj)                 | sound object library            |

Each binding is a single `quickjs-<name>.c` / `.cpp` file, built as `<name>.so`
and, optionally, as a static archive `quickjs-<name>.a` that can be embedded in
`qjsm` as a builtin module.

## Example

```js
// tests/webaudio-test.js: 1 kHz sawtooth for one second
import { AudioContext, OscillatorNode } from 'labsound';
import { setTimeout } from 'os';

const context = new AudioContext();
const o = new OscillatorNode(context, { type: 'sawtooth', frequency: 1000 });

o.connect(context.destination);
o.start();
setTimeout(() => o.stop(), 1000);
```

The `labsound` binding follows the Web Audio API closely enough that the same
script also runs in a browser (see `tests/synth.js`, `tests/effects.js`).

## Building

Requires CMake, a C/C++ compiler and QuickJS. Each library is taken from the
system unless its `BUILD_<LIB>` option is ON, in which case the vendored copy in
`third_party/` (git submodule) is built.

```sh
git clone --recurse-submodules git@github.com:rsenn/qjs-sound.git
cd qjs-sound

cmake -B build/$(cc -dumpmachine) -S .
cmake --build build/$(cc -dumpmachine)
```

| option                        | effect                                              |
|-------------------------------|-----------------------------------------------------|
| `USE_<LIB>=OFF`               | skip a binding (`LABSOUND`, `STK`, `PORTAUDIO`, `PORTMIDI`, `SNDFILE`, `SAMPLERATE`, `SOUNDTOUCH`, `RUBBERBAND`, `AUBIO`, `SNDOBJ`) |
| `BUILD_<LIB>=ON`              | build the vendored library from `third_party/`      |
| `BUILD_STATIC=ON`             | also build `quickjs-<name>.a` plus a `.module.cmake` sidecar |
| `BUILD_SHARED=OFF`            | skip the `<name>.so` modules                        |
| `HIDE_STATIC_SYMBOLS=OFF`     | keep all symbols in the static archives             |

Static archives export only `js_init_module_<name>`; every other global symbol
is made local after the build.

```sh
cmake -B /tmp/b -S . -DBUILD_STATIC=ON -DBUILD_SHARED=OFF
```

## Running

Point `QUICKJS_MODULE_PATH` at the build directory:

```sh
QUICKJS_MODULE_PATH=build/$(cc -dumpmachine) qjsm tests/aubio-test.js
```

Most scripts in `tests/` play sound or read a MIDI device and are meant to be
run by hand; there is no automated test suite.

## Layout

| path                  | contents                                                    |
|-----------------------|-------------------------------------------------------------|
| `quickjs-*.c`, `.cpp` | the bindings                                                |
| `doc/native/`         | API documentation per binding                               |
| `doc/api-interoperability.md` | comparison with other bindings of the same libraries |
| `tests/`              | example and test scripts (`*-test.js`)                      |
| `tonnetz/`            | "Tonnetz Table": touch-controlled audio-visual instrument PWA, also playable under qjsm via `tonnetz/cli.js` |
| `xi-instruments/`     | FastTracker II `.xi` instruments built with `xi-build.js` (aubio + libsamplerate + sndfile) |
| `cmake/`              | shared QuickJS cmake layer and `Find`/`Build` modules       |
| `third_party/`        | vendored libraries (submodules)                             |
| `TODO.md`, `BUGS`     | open work and known bugs                                    |

## Documentation

API notes for each binding live in [`doc/native/`](doc/native):
[aubio](doc/native/aubio.md), [portaudio](doc/native/portaudio.md),
[portmidi](doc/native/portmidi.md), [rubberband](doc/native/rubberband.md),
[samplerate](doc/native/samplerate.md), [sndfile](doc/native/sndfile.md),
[sndobj](doc/native/sndobj.md), [soundtouch](doc/native/soundtouch.md),
[stk-io](doc/native/stk-io.md).

## License

[MIT](LICENSE). The vendored libraries in `third_party/` keep their own licenses.
