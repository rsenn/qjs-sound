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
5. `JSClassDef` + finalizer (`js_audionode_finalize_with`) + funcs table.
   `connect`/`disconnect` are now inherited via the prototype chain (see
   below) — don't re-list them in the new node's funcs table, just add
   whatever properties/methods are unique to it plus `[Symbol.toStringTag]`.
   If the node is an `AudioScheduledSourceNode` (has `start`/`stop`), chain
   its proto to `audioscheduledsourcenode_proto` instead of `audionode_proto`
   and likewise skip `start`/`stop` unless its signature needs to differ from
   the plain `(when)` form (`AudioBufferSourceNode` overrides `start` for its
   offset/loop args but still inherits `stop`).
6. Wire into `js_labsound_init` (class + proto + `JS_SetPrototype(ctx,
   xxx_proto, audionode_proto)` or `audioscheduledsourcenode_proto` + ctor +
   `JS_SetModuleExport`) **and** `js_init_module_labsound`
   (`JS_AddModuleExport`) — both are required, both are easy to forget one of.

---

## 1. ✅ DONE — Node → AudioParam connections (`connect()` fix, not a new class)

**Leverage: highest — unlocks LFO/modulation patterns for every node already
bound, no new class needed.**

`js_audionode_connect`/`js_audionode_disconnect` and
`js_audiocontext_connect` now fall back to `any_audio_param(argv[0])`
(`JS_GetOpaque` on `js_audioparam_class_id`) when the destination isn't a
node, and route to `lab::AudioContext::connectParam`/`disconnectParam`.
`node.connect(param, output)` returns `undefined` per spec (no chaining off
an `AudioParam`), while node-to-node `connect` still returns the destination.
Verified with `constantSource.connect(gain.gain)`.

---

## 2. ✅ DONE — `ConvolverNode`

Constructor takes `(ctx, {buffer, normalize})`; `buffer`/`normalize` are plain
get/set properties (no `AudioParam`s, matches spec). Chained to
`audionode_proto` (not `audioscheduledsourcenode_proto`) since ConvolverNode
has no `start`/`stop` in the spec, even though `lab::ConvolverNode` happens to
derive from `AudioScheduledSourceNode` internally. Added to `any_audio_node()`.

---

## 3. ✅ DONE — `AnalyserNode`

`getFloatFrequencyData`/`getByteFrequencyData`/`getFloatTimeDomainData`/
`getByteTimeDomainData` write into a caller-supplied `Float32Array`/
`Uint8Array`, sized to `min(callerLength, frequencyBinCount-or-fftSize)` per
spec (LabSound's vectors don't auto-resize — they silently no-op on a
zero-length vector, so the binding pre-sizes a scratch `std::vector` itself).
The node self-registers via `ac->addAutomaticPullNode()` in its constructor
(required per `AnalyserNode.h`'s own doc comment when not connected to
`destination`) and unregisters in its finalizer — skipping the unregister
would leak a live LabSound node forever.

**Not done:** `fftSize` setter doesn't validate power-of-two-in-[32,32768]
per spec (passes straight to `lab::AnalyserNode::setFftSize`) — low-risk gap,
but a caller passing a bad value gets whatever LabSound does internally
rather than a spec-shaped `RangeError`.

---

## 4. ✅ DONE — `DynamicsCompressorNode`

`threshold`/`knee`/`ratio`/`attack`/`release` are real `AudioParam`s (bound
via `make_audio_param_js`, so full automation works); `reduction` is a
read-only float getter, not an `AudioParam`.

---

## 5. ✅ DONE — `ConstantSourceNode`

`offset` is a real `AudioParam`; `start`/`stop` are fully inherited from
`audioscheduledsourcenode_proto` (no node-specific override needed — its
constructor just chains to that proto like `OscillatorNode`/`NoiseNode`).
Verified as an `AudioParam` modulation driver: `constantSource.connect(gain.gain)`.

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

## 8. ✅ DONE — Real `AudioBuffer` construction

`new AudioBuffer({numberOfChannels, length, sampleRate})` builds a
`lab::AudioBus` directly. `getChannelData(channel)` returns a **copy**
(`Float32Array`, built via a small `make_float32_array` helper since this
quickjs.h has no `JS_NewFloat32Array`-style constructor helper — it wraps a
copied `ArrayBuffer` with the global `Float32Array` ctor), not a live view —
a deliberate spec deviation: the underlying `AudioBus` can be read by the
audio render thread mid-playback (e.g. an actively-playing
`AudioBufferSourceNode`), so a zero-copy alias would be a real data race.
`copyToChannel`/`copyFromChannel` implemented as plain offset-bounded memcpys.

Also added `AudioBuffer.prototype.writeToWav(path, mixToMono)` (not originally
scoped here, but natural to land alongside): reuses the exact recipe from
`RecorderNode::writeRecordingToWav` (build an `nqr::AudioData`, call
`nqr::encode_wav_to_disk`), generalized to write any `AudioBus` — works on a
buffer from `decodeAudioData`, `createBufferFromFile`, `new AudioBuffer(...)`,
or `OfflineAudioContext.startRendering()` (item 12) alike. Needed adding
`third_party/LabSound/third_party/libnyquist/include` to `qjs-labsound`'s
`INCLUDE_DIRECTORIES` in `CMakeLists.txt` — libnyquist headers weren't on the
module's include path before (only linked, not include-visible).

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

## 12. ✅ DONE — `OfflineAudioContext` completion

Kept the boolean-flag shape (`new AudioContext(true, autoDispatchEvents,
{numberOfChannels, length, sampleRate})`) rather than splitting out a real
`OfflineAudioContext` class — still a documented deviation from the spec's
distinct-constructor shape, but avoids doubling the class/proto/export
boilerplate for what's otherwise identical machinery.

When `isOffline`, the constructor now builds the destination with
`lab::AudioDevice_Null` (previously: no destination at all was created for
offline contexts — `ctx.destination` was `null` and nothing could ever be
connected to it). `length` is stashed as a hidden `__offlineLength` JS
property on the context object (mirrors the existing `__nodes` bookkeeping
pattern in `anchor_node_in_context` — there's no room for it in the
`AudioContextPtr*` opaque pointer without changing every call site that casts
it).

`AudioContext.prototype.startRendering()` pulls the graph synchronously via
`lab::AudioDestinationNode::offlineRender(&scratchBus, quantum)` in
`AudioNode::ProcessingSizeInFrames` (128-frame) chunks — no thread, no
realtime pacing, since `offlineRender` already renders faster than realtime
on the calling thread — accumulating into a pre-sized result `AudioBus`,
trimming the final partial quantum. Returns a resolved Promise (same trick as
`decodeAudioData`) wrapping the result as a real `AudioBuffer`, which composes
directly with item 8's `writeToWav()`:
```js
const buf = await offlineCtx.startRendering();
buf.writeToWav('out.wav');
```
Verified end-to-end: rendered buffer length/duration match the requested
`length`, contains actual non-silent audio, and the written WAV round-trips
through `ffprobe` as `pcm_f32le`/44100Hz/2ch/1.000000s.

---

## Complete WebAudio API class inventory

Every interface in the spec, its LabSound backing (if any), and current
binding status. "Needs custom C++" means LabSound has no equivalent type at
all — the feature would have to be implemented from scratch in
`quickjs-labsound.cpp`, not just wrapped.

| WebAudio interface | LabSound equivalent | Status | Notes |
|---|---|---|---|
| `BaseAudioContext` | `lab::AudioContext` | Bound | LabSound doesn't split base/online/offline into separate types the way the spec does; single class + `isOffline` bool. |
| `AudioContext` | `lab::AudioContext` | Bound | |
| `OfflineAudioContext` | `lab::AudioContext(isOffline=true)` | Bound | `AudioDevice_Null`-backed destination + `startRendering()` returning a real `AudioBuffer`. See item 12. |
| `AudioNode` | `lab::AudioNode` | Bound | Abstract base — `connect`/`disconnect` live once on a shared `audionode_proto` and are inherited by every node's prototype via `JS_SetPrototype`, rather than duplicated per funcs table. |
| `AudioParam` | `lab::AudioParam` | Bound | Full automation methods present (`setValueAtTime`, ramps, `setTargetAtTime`, `cancelScheduledValues`). |
| `AudioParamMap` | — | N/A | Only used by `AudioWorkletNode.parameters`; moot until AudioWorklet exists. |
| `AudioScheduledSourceNode` | `lab::AudioScheduledSourceNode` | Bound | Abstract base for Oscillator/AudioBufferSource/Noise/ConstantSource — generic `start(when)`/`stop(when)` live once on `audioscheduledsourcenode_proto` (chained under `audionode_proto`) via `dynamic_pointer_cast<lab::AudioScheduledSourceNode>`. `AudioBufferSourceNode` overrides `start` on its own proto for its extra offset/loop args but still inherits the shared `stop`. |
| `AnalyserNode` | `lab::AnalyserNode` | Bound | Item 3. `fftSize` setter doesn't validate power-of-two range per spec. |
| `AudioBuffer` | `lab::AudioBus` | Bound | Item 8. `new AudioBuffer(...)` plus `getChannelData`/`copyToChannel`/`copyFromChannel`/`writeToWav`. `getChannelData` copies rather than aliasing (spec gives a live view) — deliberate, avoids a data race with the audio render thread. |
| `AudioBufferSourceNode` | `lab::SampledAudioNode` | Bound | |
| `AudioDestinationNode` | `lab::AudioDestinationNode` | Bound | |
| `AudioListener` | `lab::AudioListener` | Bound | Currently inert — nothing produces spatialized output until `PannerNode` is bound (item 6). |
| `BiquadFilterNode` | `lab::BiquadFilterNode` | Bound | Most complete node binding in the file — good template for others. |
| `ChannelMergerNode` | `lab::ChannelMergerNode` | **Not bound** | Item 7. |
| `ChannelSplitterNode` | `lab::ChannelSplitterNode` | **Not bound** | Item 7. |
| `ConstantSourceNode` | `lab::ConstantSourceNode` | Bound | Item 5. |
| `ConvolverNode` | `lab::ConvolverNode` | Bound | Item 2. |
| `DelayNode` | `lab::DelayNode` | Bound | `delayTime` uses the `AudioSetting` shim (immediate-only), not a real `AudioParam` — see the comment at line ~77; automation calls on it silently collapse to `setFloat`, a real spec deviation worth flagging to callers. |
| `DynamicsCompressorNode` | `lab::DynamicsCompressorNode` | Bound | Item 4. |
| `GainNode` | `lab::GainNode` | Bound | |
| `IIRFilterNode` | — | **Needs custom C++** | No IIR node anywhere in LabSound (checked all of `core/` and `extended/`). Would mean implementing a generic difference-equation processor from scratch — LabSound's `AudioProcessor`/`AudioBasicProcessorNode` base classes are the right hook point, but the DSP itself doesn't exist yet. |
| `MediaElementAudioSourceNode` | — | **N/A / needs custom C++** | No `HTMLMediaElement` concept exists in a QuickJS environment; would need an entirely invented "media element" shim first. Low value outside a DOM. |
| `MediaStreamAudioDestinationNode` | — | **Needs custom C++** | No `MediaStream` type anywhere in LabSound; would need a from-scratch shim. |
| `MediaStreamAudioSourceNode` | `lab::AudioHardwareInputNode` (partial) | **Needs custom C++** | LabSound has live hardware-input capture (item 11) but no `MediaStream`/`MediaStreamTrack` wrapper around it — binding hardware input directly (non-spec API) is far cheaper than building real `MediaStream` semantics. |
| `MediaStreamTrackAudioSourceNode` | — | **Needs custom C++** | Same gap as above. |
| `OscillatorNode` | `lab::OscillatorNode` | Bound | `type` accepts LabSound-extension values (`"fast-sine"`, `"falling-sawtooth"`) beyond the spec's four — a deviation to document, not necessarily fix. No `setPeriodicWave()`. |
| `PannerNode` | `lab::PannerNode` | **Not bound** | Item 6. |
| `PeriodicWave` | `lab::PeriodicWave` | **Not bound** | Item 9. |
| `ScriptProcessorNode` | — | **Deprecated in spec — skip** | LabSound's `FunctionNode` (extended) is the closest spirit-match (native callback per block) but bridging that callback into JS per audio quantum has real perf/threading cost for a deprecated API; not worth it. |
| `StereoPannerNode` | `lab::StereoPannerNode` | Bound | |
| `WaveShaperNode` | `lab::WaveShaperNode` | Bound | |
| `AudioWorklet` | — | **Needs custom C++ (large)** | No equivalent concept in LabSound at all. |
| `AudioWorkletNode` | — | **Needs custom C++ (large)** | Same. |
| `AudioWorkletProcessor` | — | **Needs custom C++ (large)** | Same — would require running QuickJS callbacks on the audio render thread per quantum, with all the GC/threading hazards that implies. This is the single biggest lift on this list; everything else is wrapping existing LabSound DSP, this is inventing a JS-on-audio-thread execution model from nothing. |
| `AudioWorkletGlobalScope` | — | **Needs custom C++ (large)** | Part of the same effort as above. |
| `OfflineAudioCompletionEvent` | — | Covered by item 12 | Spec models this as an event; a resolved Promise (as already used for `decodeAudioData`) covers the same use case without inventing an Event system. |

---

## LabSound classes with no WebAudio equivalent (bindable anyway)

These are LabSound's own additions — no spec interface to match, so no
"deviation" to reconcile, just a straightforward wrap if/when useful. Ordered
by rough relevance to what this project already does (`synth.js`,
`drumsampler.js`, `effects.js` are all live-performance synth/effects code).

| LabSound class | What it does | Why it'd be worth binding here |
|---|---|---|
| `ADSRNode` (extended) | Native envelope generator | `synth.js`'s `ADSR` class (line 23) currently hand-schedules envelopes via chained `AudioParam` automation calls in JS — a native `ADSRNode` could replace that with less per-note JS overhead and one call instead of 3-4 scheduling calls per envelope stage. Highest-value item in this list given current code. |
| `SupersawNode` (extended) | Detuned multi-oscillator unison voice | Direct fit for pad/lead synth voices; `synth.js`'s `vco` is currently a single `OscillatorNode` — this would be a drop-in richer voice option. |
| `BPMDelay` (extended, `BPMDelayNode.h`) | `DelayNode` subclass with tempo-synced delay time | Cheap to bind (it *is* a `DelayNode` — same shape as item 2's `ConvolverNode` work) and useful for any rhythmic delay effect alongside `effects.js`. |
| `PingPongDelayNode` (extended) | Composite stereo ping-pong delay | Built as a `Subgraph` (multiple internal nodes wired together), not a plain `AudioNode` — binding pattern differs from everything else in the file; would need its own wrapper shape rather than reusing `make_audio_node_js`. |
| `PolyBLEPNode` (extended) | Band-limited (anti-aliased) oscillator | Higher audio quality alternative to `OscillatorNode` for square/saw waves at high frequencies; same constructor shape as `OscillatorNode`, cheap port. |
| `PWMNode` (extended) | Pulse-width-modulation oscillator | Common analog-synth voice type not otherwise available. |
| `SfxrNode` (extended) | Procedural 8-bit/retro SFX generator (sfxr/bfxr-style) | Good fit for `drumsampler.js`-style one-shot hits without needing sample files. |
| `GranulationNode` (extended) | Granular-synthesis buffer playback | Different playback model than `AudioBufferSourceNode`; useful for texture/pad effects, no spec equivalent exists. |
| `ClipNode` (extended) | Hard clipper/distortion | Simpler/cheaper alternative to `WaveShaperNode` curves for basic distortion. |
| `DiodeNode` (extended) | Diode-clipper distortion (subclasses `WaveShaperNode`) | Analog-modeling distortion; since it's a `WaveShaperNode` subclass, binding is nearly free once `WaveShaperNode`'s pattern is reused. |
| `PeakCompNode` (extended) | Simple peak compressor/limiter | Lighter-weight alternative to full `DynamicsCompressorNode` (item 4) for basic limiting. |
| `PowerMonitorNode` (extended) | RMS/power-level metering | Cheaper alternative to `AnalyserNode` (item 3) when only a level meter is needed, not full FFT data. |
| `SpectralMonitorNode` (extended) | FFT-based spectral analysis | Overlaps with `AnalyserNode`'s frequency-domain data; only worth binding if its API offers something `AnalyserNode` doesn't. |
| `RecorderNode` (extended) | Capture graph output to WAV | Already noted as item 10 — listed here too since it's the clearest "no spec equivalent" case. |
| `FunctionNode` (extended) | Native per-block callback node | Could expose a small set of *built-in* canned processing functions from C++, but can't safely bridge to arbitrary JS callbacks per audio quantum (same perf/threading issue as `ScriptProcessorNode`/`AudioWorklet` above) — bind only the native-callback use case, not a JS-scriptable one. |
| `PdNode` (extended) | Embeds a Pure Data (libpd) patch | Requires linking libpd as an additional dependency — only worth it if Pure Data patches are actually part of the workflow; skip otherwise. |

**Not listed above (internal infrastructure, not meant to be user-facing node
classes):** `AudioBasicInspectorNode`/`AudioBasicProcessorNode` (base classes
other nodes derive from), `AudioProcessor`, `AudioNodeDescriptor`,
`AudioParamDescriptor`, `AudioSettingDescriptor`, `RealtimeAnalyser` (wrapped
*by* `AnalyserNode`, not exposed directly), `SpatializationNode` (internal to
`PannerNode`), `AudioSourceProvider`, `AudioSummingJunction`,
`AudioNodeInput`/`AudioNodeOutput`, `ConcurrentQueue`, `VectorMath`,
`WindowFunctions`, `Mixing`, `Util`, `Logging`, `Profiler`, `Registry`,
`AudioFileReader` (already used internally by `decodeAudioData`/
`createBufferFromFile`).
