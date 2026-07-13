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
| `OfflineAudioContext` | `lab::AudioContext(isOffline=true)` | Half-bound | Constructor threads the flag through; no `startRendering()`/result path. See item 12. |
| `AudioNode` | `lab::AudioNode` | Bound (no shared JS proto) | Abstract base — `connect`/`disconnect` duplicated per node's funcs table today rather than inherited. |
| `AudioParam` | `lab::AudioParam` | Bound | Full automation methods present (`setValueAtTime`, ramps, `setTargetAtTime`, `cancelScheduledValues`). |
| `AudioParamMap` | — | N/A | Only used by `AudioWorkletNode.parameters`; moot until AudioWorklet exists. |
| `AudioScheduledSourceNode` | `lab::AudioScheduledSourceNode` | Bound (no shared JS proto) | Abstract base for Oscillator/AudioBufferSource/ConstantSource — `start`/`stop` duplicated per node rather than inherited, same pattern as `AudioNode`. |
| `AnalyserNode` | `lab::AnalyserNode` | **Not bound** | Item 3. |
| `AudioBuffer` | `lab::AudioBus` | Bound, but constructor-less | Only produced via `decodeAudioData`/`createBufferFromFile`; no `new AudioBuffer(...)`. Item 8. `audiobuffer_ctor` global even exists (line 55) but is never assigned — dead like `ConvolverNode`. |
| `AudioBufferSourceNode` | `lab::SampledAudioNode` | Bound | |
| `AudioDestinationNode` | `lab::AudioDestinationNode` | Bound | |
| `AudioListener` | `lab::AudioListener` | Bound | Currently inert — nothing produces spatialized output until `PannerNode` is bound (item 6). |
| `BiquadFilterNode` | `lab::BiquadFilterNode` | Bound | Most complete node binding in the file — good template for others. |
| `ChannelMergerNode` | `lab::ChannelMergerNode` | **Not bound** | Item 7. |
| `ChannelSplitterNode` | `lab::ChannelSplitterNode` | **Not bound** | Item 7. |
| `ConstantSourceNode` | `lab::ConstantSourceNode` | **Not bound** | Item 5. |
| `ConvolverNode` | `lab::ConvolverNode` | **Stubbed** | Class id + header included, nothing else. Item 2. |
| `DelayNode` | `lab::DelayNode` | Bound | `delayTime` uses the `AudioSetting` shim (immediate-only), not a real `AudioParam` — see the comment at line ~77; automation calls on it silently collapse to `setFloat`, a real spec deviation worth flagging to callers. |
| `DynamicsCompressorNode` | `lab::DynamicsCompressorNode` | **Not bound** | Item 4. |
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
