# WebAudio binding TODO

Ordered by leverage: cheapest/most-unlocking first. Each item lists the LabSound
type to wrap and the specific WebAudio-spec quirks/deviations to handle given
how `quickjs-labsound.cpp` is currently built.

## Boilerplate checklist (applies to every node added below)

Adding a node currently means touching **six** spots — easy to half-do:

1. `static JSClassID js_xxxnode_class_id;` near the top.
2. `static JSValue xxxnode_proto, xxxnode_ctor;` near the top.
3. Constructor function following the existing pattern: read `argv[0]` as
   `AudioContext` via `JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id)`,
   build the `lab::` node, call `make_audio_node_js(...)`, then
   `anchor_node_in_context(ctx, argv[0], obj)` — skipping the anchor call is
   a use-after-free waiting to happen since lab's graph keeps raw back-pointers.
4. **Add the new class id to `any_audio_node()` (line ~86-108).** This is the
   one most likely to be forgotten — without it, `.connect()`/`.disconnect()`
   and `ctx.connect()` silently reject the new node type as neither a valid
   source nor destination.
5. `JSClassDef` + finalizer (`js_audionode_finalize_with`) + funcs table
   (remember `connect`/`disconnect`/`[Symbol.toStringTag]` — copy-pasted per
   node today, no shared prototype).
6. Wire into `js_labsound_init` (class + proto + ctor + `JS_SetModuleExport`)
   **and** `js_init_module_labsound` (`JS_AddModuleExport`) — both are
   required, both are easy to forget one of.

---

## 1. Node → AudioParam connections (`connect()` fix, not a new class)

**Leverage: highest — unlocks LFO/modulation patterns for every node already
bound, no new class needed.**

`js_audionode_connect` (line ~452) and `AudioContext.connect` (line ~347) both
resolve the destination via `any_audio_node(argv[0])`, so a call like
`lfo.connect(gain.gain)` (connecting a node's output into an `AudioParam`, the
core WebAudio modulation idiom) currently throws `"destination must be an
AudioNode"`. `lab::AudioContext::connect` / `connectParam` supports this on the
C++ side (`AudioParam` sinks); the JS wrapper never tried recognizing an
`AudioParam` JS object as a valid destination.

**Fix:** in `js_audionode_connect` and `js_audiocontext_connect`, if
`any_audio_node(argv[0])` fails, fall back to `JS_GetOpaque2(ctx, argv[0],
js_audioparam_class_id)` and route to `lab::AudioContext::connectParam(param,
src->node, srcIdx)` (check LabSound's actual API name/signature in
`AudioContext.h`). Returning `this_val`/dest per spec chaining still applies.

---

## 2. `ConvolverNode` — finish the stub

**Leverage: very high, very cheap.** The class id is declared
(`js_convolvernode_class_id`, line 38) and the header is already included, but
there's no constructor, proto, `JSClassDef`, funcs table, or module export —
dead code. Mirror `WaveShaperNode` (closest analog: also takes a buffer/curve
at construction).

Quirks to address:
- WebAudio's `ConvolverNode` takes a `buffer` (an `AudioBuffer`) and a
  `normalize` bool in its options dict — wire both from `argv[1]`, converting
  the JS `AudioBuffer` wrapper (`JsAudioBuffer`/`lab::AudioBus`) the same way
  `AudioBufferSourceNode.buffer` does (see `js_absource_constructor` sections
  around line 1260-1310 for the existing buffer-unwrap pattern).
- No `AudioParam`s on this node — just `buffer` and `normalize` as plain
  get/set properties, not `AudioSetting`/`AudioParam` wrappers.
- Add `ConvolverNode` to `any_audio_node()` (step 4 above) — reverb chains
  need `.connect()` to work.

---

## 3. `AnalyserNode`

**Leverage: high.** Nothing today gives JS access to waveform/frequency data —
blocks any metering, visualization, or level-driven synthesis
(`fx-test.js`/`effects.js` style patches would want this for feedback-driven
effects).

Quirks to address:
- WebAudio exposes `getFloatTimeDomainData`, `getByteTimeDomainData`,
  `getFloatFrequencyData`, `getByteFrequencyData` — all of which write into a
  caller-supplied `TypedArray`. The binding needs to accept a `Float32Array`/
  `Uint8Array` argument and copy LabSound's `lab::AnalyserNode` output into it
  (reuse the `JS_GetTypedArrayBuffer` pattern already in `read_float_array`,
  line ~126, but write-direction instead of read).
- `fftSize` must be a power of two in [32, 32768] per spec — LabSound's
  `AnalyserNode` may not validate this the same way; validate in the setter
  and throw `IndexSizeError`-equivalent (`JS_ThrowRangeError`) to match
  browser behavior, since nothing else in this codebase currently validates
  ranges like this.
- `smoothingTimeConstant`, `minDecibels`, `maxDecibels`, `frequencyBinCount`
  are plain numeric properties, not `AudioParam`s.

---

## 4. `DynamicsCompressorNode`

**Leverage: high** — standard mastering/bus-limiting node, commonly the last
node before `destination` in real chains; currently nothing in this codebase
can glue a mix bus together without clipping.

Quirks to address:
- Params (`threshold`, `knee`, `ratio`, `attack`, `release`) are real
  `AudioParam`s in the spec — bind them via `make_audio_param_js` like
  `BiquadFilterNode`'s `frequency`/`Q`/`gain`, **not** the `AudioSetting` shim
  used for `DelayNode.delayTime` (that shim silently drops real scheduling; a
  compressor's attack/release curves are commonly automated).
- `reduction` is a read-only float (current gain reduction in dB) — plain
  getter, no setter, not an `AudioParam`.

---

## 5. `ConstantSourceNode`

**Leverage: medium-high, near-zero implementation cost** — a one-`AudioParam`
source (`offset`) most useful as an LFO/constant modulation driver once item 1
(param connections) lands. Binding this before item 1 has little value; do
them together or in this order.

Quirks to address:
- `offset` is an `AudioParam` (bind like `GainNode.gain`).
- Has `start()`/`stop()` like `OscillatorNode` — reuse that pattern directly.

---

## 6. `PannerNode` (3D spatialization)

**Leverage: medium** — `AudioListener` is already bound (`js_audiolistener_*`)
but nothing produces spatialized output, so the listener binding is currently
inert.

Quirks to address:
- Spec has two spatialization models with very different property sets:
  `panningModel` (`"equalpower"`/`"HRTF"`), `distanceModel`
  (`"linear"`/`"inverse"`/`"exponential"`), plus `positionX/Y/Z` and
  `orientationX/Y/Z` as `AudioParam`s (newer spec) vs. the older
  `setPosition()`/`setOrientation()` methods LabSound's `PannerNode.h` likely
  still exposes. Check which shape `lab::PannerNode` uses and decide whether
  to bind the modern per-axis `AudioParam` properties, the legacy setter
  methods, or both (both is what real browsers do today).
- `coneInnerAngle`/`coneOuterAngle`/`coneOuterGain`,
  `maxDistance`/`refDistance`/`rolloffFactor` are plain numeric properties,
  not `AudioParam`s.

---

## 7. `ChannelSplitterNode` / `ChannelMergerNode`

**Leverage: medium** — needed for any multichannel routing (e.g. mixing mono
synth voices into specific stereo/surround channels); currently no way to
address individual channels at all.

Quirks to address:
- These nodes are defined almost entirely by output/input *count*, fixed at
  construction (`new ChannelSplitterNode(ctx, {numberOfOutputs: N})`) — no
  runtime-mutable properties, so binding is mostly just getting the
  constructor's `numberOfOutputs`/`numberOfInputs` option threaded to
  LabSound's node and otherwise reusing the connect/disconnect boilerplate
  with explicit channel indices (which `js_audionode_connect` already
  supports via `srcIdx`/`dstIdx` params — good, no fix needed there).

---

## 8. Real `AudioBuffer` construction (`new AudioBuffer(...)` / `ctx.createBuffer()`)

**Leverage: medium** — today `AudioBuffer` can only be *produced* by
`decodeAudioData`/`createBufferFromFile` (line 366-419); there's no way to
synthesize a buffer from scratch in JS (e.g. building a custom noise/impulse
buffer for `ConvolverNode` above, or a wavetable). No JS constructor is
registered at all for `AudioBuffer` (`audiobuffer_ctor` is declared at line 55
but never assigned/exported — dead like `ConvolverNode`).

Quirks to address:
- Spec constructor: `new AudioBuffer({numberOfChannels, length, sampleRate})`,
  plus `getChannelData(channel)` (returns a live `Float32Array` view, not a
  copy — check whether `lab::AudioBus` channel storage can be safely aliased
  into a QuickJS `ArrayBuffer` without a copy, or whether copy-in/copy-out on
  `getChannelData`/`copyToChannel` is the only safe option given LabSound's
  own buffer lifetime).
- `copyFromChannel`/`copyToChannel` per spec for partial reads/writes.

---

## 9. `PeriodicWave` + `OscillatorNode.setPeriodicWave()`

**Leverage: low-medium** — blocks custom (non-builtin) oscillator waveforms.
`OscillatorNode` binding currently only supports the fixed `type` enum
(sine/square/sawtooth/triangle/fast-sine/falling-sawtooth — note the last two
are LabSound extensions, not in the WebAudio spec, so `type` already deviates
from spec by accepting extra values; document this rather than "fixing" it).

Quirks to address:
- `new PeriodicWave(ctx, {real, imag, disableNormalization})` takes Fourier
  coefficient arrays — reuse `read_float_array` for `real`/`imag`.
- `setPeriodicWave()` on `OscillatorNode` needs adding to
  `js_oscillatornode_funcs`, and `type` should report `"custom"` afterward
  per spec (currently `js_oscillator_get`'s `OSC_PROP_TYPE` just stringifies
  whatever `lab::OscillatorType` is set — confirm LabSound has a `CUSTOM`
  enum value or this needs a side-channel flag on `JsAudioNode`).

---

## 10. `RecorderNode`

**Leverage: low-medium, project-specific** — useful for capturing the
already-bound synths/effects (`synth.js`, `drumsampler.js`) to a file/buffer
for offline use, but not part of the WebAudio spec at all (LabSound
extension) — no spec deviation to reconcile, just a straightforward wrap of
`lab::RecorderNode`'s start/stop/write-to-wav API.

---

## 11. Live input (`AudioHardwareInputNode.h`) / mic capture

**Leverage: low** — no equivalent to `navigator.mediaDevices.getUserMedia()`
+ `MediaStreamAudioSourceNode` exists, and there's no `MediaStream` concept in
this codebase at all. `AudioDevice`/`AudioDevice_RtAudio` binding already
handles *output* device selection (`js_audiodevice_*`); input needs the
mirror image plumbed through `get_default_device_config()`'s already-computed
`inputConfig` (line 253), which is computed but currently unused for
anything JS-visible.

Quirks to address:
- No spec-compliant way to fake `MediaStream` — likely need a
  qjs-sound-specific `AudioContext.createHardwareInputNode()` or similar
  rather than `getUserMedia`, and document that this is a deliberate
  non-spec escape hatch.

---

## 12. `OfflineAudioContext` completion

**Leverage: low for this project** (`synth.js`/`drumsampler.js`/`effects.js`
are all realtime-performance code) but worth finishing since it's
half-wired: `AudioContext`'s constructor already threads an `isOffline` bool
into `lab::AudioContext` (line 267-274), but there is no `startRendering()`
method, no completion promise/`oncomplete` event, and no way to pull the
rendered result back out as an `AudioBuffer`. Right now constructing with
`isOffline=true` produces an object that can build a graph but can never
actually render or return audio to JS.

Quirks to address:
- Spec: `OfflineAudioContext` is really a distinct constructor/class (`new
  OfflineAudioContext(numberOfChannels, length, sampleRate)`), not
  `AudioContext(true)` — current single-constructor-with-bool-flag shape is
  already a deviation; decide whether to keep the boolean-flag shape (simpler,
  already partially done) or split out a real `OfflineAudioContext` class to
  match spec call sites more closely.
- `startRendering()` should return a Promise resolving to an `AudioBuffer`
  (use the same resolved-Promise trick as `decodeAudioData`, line 384-394,
  once rendering is actually synchronous under the hood, or a real async
  completion if LabSound's offline render runs on another thread).
