# WebAudio binding TODO

Ordered by leverage: cheapest/most-unlocking first. Each item lists the LabSound
type to wrap and the specific WebAudio-spec quirks/deviations to handle given
how `quickjs-labsound.cpp` is currently built.

## Boilerplate checklist (applies to every node added below)

Adding a node currently means touching **six** spots - easy to half-do:

1. `static JSClassID js_xxxnode_class_id;` near the top.
2. `static JSValue xxxnode_proto, xxxnode_ctor;` near the top.
3. Constructor function following the existing pattern: read `argv[0]` as
   `AudioContext` via `JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id)`,
   build the `lab::` node, call `make_audio_node_js(...)`, then
   `anchor_node_in_context(ctx, argv[0], obj)` - skipping the anchor call is
   a use-after-free waiting to happen since lab's graph keeps raw back-pointers.
4. **Add the new class id to `any_audio_node()` (line ~86-108).** This is the
   one most likely to be forgotten - without it, `.connect()`/`.disconnect()`
   and `ctx.connect()` silently reject the new node type as neither a valid
   source nor destination.
5. `JSClassDef` + finalizer (`js_audionode_finalize_with`) + funcs table.
   `connect`/`disconnect` are now inherited via the prototype chain (see
   below) - don't re-list them in the new node's funcs table, just add
   whatever properties/methods are unique to it plus `[Symbol.toStringTag]`.
   If the node is an `AudioScheduledSourceNode` (has `start`/`stop`), chain
   its proto to `audioscheduledsourcenode_proto` instead of `audionode_proto`
   and likewise skip `start`/`stop` unless its signature needs to differ from
   the plain `(when)` form (`AudioBufferSourceNode` overrides `start` for its
   offset/loop args but still inherits `stop`).
6. Wire into `js_labsound_init` (class + proto + `JS_SetPrototype(ctx,
   xxx_proto, audionode_proto)` or `audioscheduledsourcenode_proto` + ctor +
   `JS_SetModuleExport`) **and** `js_init_module_labsound`
   (`JS_AddModuleExport`) - both are required, both are easy to forget one of.

---

## 1. ✅ DONE - Node → AudioParam connections (`connect()` fix, not a new class)

**Leverage: highest - unlocks LFO/modulation patterns for every node already
bound, no new class needed.**

`js_audionode_connect`/`js_audionode_disconnect` and
`js_audiocontext_connect` now fall back to `any_audio_param(argv[0])`
(`JS_GetOpaque` on `js_audioparam_class_id`) when the destination isn't a
node, and route to `lab::AudioContext::connectParam`/`disconnectParam`.
`node.connect(param, output)` returns `undefined` per spec (no chaining off
an `AudioParam`), while node-to-node `connect` still returns the destination.
Verified with `constantSource.connect(gain.gain)`.

---

## 2. ✅ DONE - `ConvolverNode`

Constructor takes `(ctx, {buffer, normalize})`; `buffer`/`normalize` are plain
get/set properties (no `AudioParam`s, matches spec). Chained to
`audionode_proto` (not `audioscheduledsourcenode_proto`) since ConvolverNode
has no `start`/`stop` in the spec, even though `lab::ConvolverNode` happens to
derive from `AudioScheduledSourceNode` internally. Added to `any_audio_node()`.

---

## 3. ✅ DONE - `AnalyserNode`

`getFloatFrequencyData`/`getByteFrequencyData`/`getFloatTimeDomainData`/
`getByteTimeDomainData` write into a caller-supplied `Float32Array`/
`Uint8Array`, sized to `min(callerLength, frequencyBinCount-or-fftSize)` per
spec (LabSound's vectors don't auto-resize - they silently no-op on a
zero-length vector, so the binding pre-sizes a scratch `std::vector` itself).
The node self-registers via `ac->addAutomaticPullNode()` in its constructor
(required per `AnalyserNode.h`'s own doc comment when not connected to
`destination`) and unregisters in its finalizer - skipping the unregister
would leak a live LabSound node forever.

**Not done:** `fftSize` setter doesn't validate power-of-two-in-[32,32768]
per spec (passes straight to `lab::AnalyserNode::setFftSize`) - low-risk gap,
but a caller passing a bad value gets whatever LabSound does internally
rather than a spec-shaped `RangeError`.

---

## 4. ✅ DONE - `DynamicsCompressorNode`

`threshold`/`knee`/`ratio`/`attack`/`release` are real `AudioParam`s (bound
via `make_audio_param_js`, so full automation works); `reduction` is a
read-only float getter, not an `AudioParam`.

---

## 5. ✅ DONE - `ConstantSourceNode`

`offset` is a real `AudioParam`; `start`/`stop` are fully inherited from
`audioscheduledsourcenode_proto` (no node-specific override needed - its
constructor just chains to that proto like `OscillatorNode`/`NoiseNode`).
Verified as an `AudioParam` modulation driver: `constantSource.connect(gain.gain)`.

---

## 6. `PannerNode` (3D spatialization)

**Leverage: medium** - `AudioListener` is already bound (`js_audiolistener_*`)
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

**Leverage: medium** - needed for any multichannel routing (e.g. mixing mono
synth voices into specific stereo/surround channels); currently no way to
address individual channels at all.

Quirks to address:
- These nodes are defined almost entirely by output/input *count*, fixed at
  construction (`new ChannelSplitterNode(ctx, {numberOfOutputs: N})`) - no
  runtime-mutable properties, so binding is mostly just getting the
  constructor's `numberOfOutputs`/`numberOfInputs` option threaded to
  LabSound's node and otherwise reusing the connect/disconnect boilerplate
  with explicit channel indices (which `js_audionode_connect` already
  supports via `srcIdx`/`dstIdx` params - good, no fix needed there).

---

## 8. ✅ DONE - Real `AudioBuffer` construction

`new AudioBuffer({numberOfChannels, length, sampleRate})` builds a
`lab::AudioBus` directly. `getChannelData(channel)` returns a **copy**
(`Float32Array`, built via a small `make_float32_array` helper since this
quickjs.h has no `JS_NewFloat32Array`-style constructor helper - it wraps a
copied `ArrayBuffer` with the global `Float32Array` ctor), not a live view -
a deliberate spec deviation: the underlying `AudioBus` can be read by the
audio render thread mid-playback (e.g. an actively-playing
`AudioBufferSourceNode`), so a zero-copy alias would be a real data race.
`copyToChannel`/`copyFromChannel` implemented as plain offset-bounded memcpys.

Also added `AudioBuffer.prototype.writeToWav(path, mixToMono)` (not originally
scoped here, but natural to land alongside): reuses the exact recipe from
`RecorderNode::writeRecordingToWav` (build an `nqr::AudioData`, call
`nqr::encode_wav_to_disk`), generalized to write any `AudioBus` - works on a
buffer from `decodeAudioData`, `createBufferFromFile`, `new AudioBuffer(...)`,
or `OfflineAudioContext.startRendering()` (item 12) alike. Needed adding
`third_party/LabSound/third_party/libnyquist/include` to `qjs-labsound`'s
`INCLUDE_DIRECTORIES` in `CMakeLists.txt` - libnyquist headers weren't on the
module's include path before (only linked, not include-visible).

---

## 9. `PeriodicWave` + `OscillatorNode.setPeriodicWave()`

**Leverage: low-medium** - blocks custom (non-builtin) oscillator waveforms.
`OscillatorNode` binding currently only supports the fixed `type` enum
(sine/square/sawtooth/triangle/fast-sine/falling-sawtooth - note the last two
are LabSound extensions, not in the WebAudio spec, so `type` already deviates
from spec by accepting extra values; document this rather than "fixing" it).

Quirks to address:
- `new PeriodicWave(ctx, {real, imag, disableNormalization})` takes Fourier
  coefficient arrays - reuse `read_float_array` for `real`/`imag`.
- `setPeriodicWave()` on `OscillatorNode` needs adding to
  `js_oscillatornode_funcs`, and `type` should report `"custom"` afterward
  per spec (currently `js_oscillator_get`'s `OSC_PROP_TYPE` just stringifies
  whatever `lab::OscillatorType` is set - confirm LabSound has a `CUSTOM`
  enum value or this needs a side-channel flag on `JsAudioNode`).

---

## 10. `RecorderNode`

**Leverage: low-medium, project-specific** - useful for capturing the
already-bound synths/effects (`synth.js`, `drumsampler.js`) to a file/buffer
for offline use, but not part of the WebAudio spec at all (LabSound
extension) - no spec deviation to reconcile, just a straightforward wrap of
`lab::RecorderNode`'s start/stop/write-to-wav API.

---

## 11. Live input (`AudioHardwareInputNode.h`) / mic capture

**Leverage: low** - no equivalent to `navigator.mediaDevices.getUserMedia()`
+ `MediaStreamAudioSourceNode` exists, and there's no `MediaStream` concept in
this codebase at all. `AudioDevice`/`AudioDevice_RtAudio` binding already
handles *output* device selection (`js_audiodevice_*`); input needs the
mirror image plumbed through `get_default_device_config()`'s already-computed
`inputConfig` (line 253), which is computed but currently unused for
anything JS-visible.

Quirks to address:
- No spec-compliant way to fake `MediaStream` - likely need a
  qjs-sound-specific `AudioContext.createHardwareInputNode()` or similar
  rather than `getUserMedia`, and document that this is a deliberate
  non-spec escape hatch.

---

## 12. ✅ DONE - `OfflineAudioContext` completion

Kept the boolean-flag shape (`new AudioContext(true, autoDispatchEvents,
{numberOfChannels, length, sampleRate})`) rather than splitting out a real
`OfflineAudioContext` class - still a documented deviation from the spec's
distinct-constructor shape, but avoids doubling the class/proto/export
boilerplate for what's otherwise identical machinery.

When `isOffline`, the constructor now builds the destination with
`lab::AudioDevice_Null` (previously: no destination at all was created for
offline contexts - `ctx.destination` was `null` and nothing could ever be
connected to it). `length` is stashed as a hidden `__offlineLength` JS
property on the context object (mirrors the existing `__nodes` bookkeeping
pattern in `anchor_node_in_context` - there's no room for it in the
`AudioContextPtr*` opaque pointer without changing every call site that casts
it).

`AudioContext.prototype.startRendering()` pulls the graph synchronously via
`lab::AudioDestinationNode::offlineRender(&scratchBus, quantum)` in
`AudioNode::ProcessingSizeInFrames` (128-frame) chunks - no thread, no
realtime pacing, since `offlineRender` already renders faster than realtime
on the calling thread - accumulating into a pre-sized result `AudioBus`,
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
all - the feature would have to be implemented from scratch in
`quickjs-labsound.cpp`, not just wrapped.

| WebAudio interface | LabSound equivalent | Status | Notes |
|---|---|---|---|
| `BaseAudioContext` | `lab::AudioContext` | Bound | LabSound doesn't split base/online/offline into separate types the way the spec does; single class + `isOffline` bool. |
| `AudioContext` | `lab::AudioContext` | Bound | |
| `OfflineAudioContext` | `lab::AudioContext(isOffline=true)` | Bound | `AudioDevice_Null`-backed destination + `startRendering()` returning a real `AudioBuffer`. See item 12. |
| `AudioNode` | `lab::AudioNode` | Bound | Abstract base - `connect`/`disconnect` live once on a shared `audionode_proto` and are inherited by every node's prototype via `JS_SetPrototype`, rather than duplicated per funcs table. |
| `AudioParam` | `lab::AudioParam` | Bound | Full automation methods present (`setValueAtTime`, ramps, `setTargetAtTime`, `cancelScheduledValues`). |
| `AudioParamMap` | - | N/A | Only used by `AudioWorkletNode.parameters`; moot until AudioWorklet exists. |
| `AudioScheduledSourceNode` | `lab::AudioScheduledSourceNode` | Bound | Abstract base for Oscillator/AudioBufferSource/Noise/ConstantSource - generic `start(when)`/`stop(when)` live once on `audioscheduledsourcenode_proto` (chained under `audionode_proto`) via `dynamic_pointer_cast<lab::AudioScheduledSourceNode>`. `AudioBufferSourceNode` overrides `start` on its own proto for its extra offset/loop args but still inherits the shared `stop`. |
| `AnalyserNode` | `lab::AnalyserNode` | Bound | Item 3. `fftSize` setter doesn't validate power-of-two range per spec. |
| `AudioBuffer` | `lab::AudioBus` | Bound | Item 8. `new AudioBuffer(...)` plus `getChannelData`/`copyToChannel`/`copyFromChannel`/`writeToWav`. `getChannelData` copies rather than aliasing (spec gives a live view) - deliberate, avoids a data race with the audio render thread. |
| `AudioBufferSourceNode` | `lab::SampledAudioNode` | Bound | |
| `AudioDestinationNode` | `lab::AudioDestinationNode` | Bound | |
| `AudioListener` | `lab::AudioListener` | Bound | Currently inert - nothing produces spatialized output until `PannerNode` is bound (item 6). |
| `BiquadFilterNode` | `lab::BiquadFilterNode` | Bound | Most complete node binding in the file - good template for others. |
| `ChannelMergerNode` | `lab::ChannelMergerNode` | **Not bound** | Item 7. |
| `ChannelSplitterNode` | `lab::ChannelSplitterNode` | **Not bound** | Item 7. |
| `ConstantSourceNode` | `lab::ConstantSourceNode` | Bound | Item 5. |
| `ConvolverNode` | `lab::ConvolverNode` | Bound | Item 2. |
| `DelayNode` | `lab::DelayNode` | Bound | `delayTime` uses the `AudioSetting` shim (immediate-only), not a real `AudioParam` - see the comment at line ~77; automation calls on it silently collapse to `setFloat`, a real spec deviation worth flagging to callers. |
| `DynamicsCompressorNode` | `lab::DynamicsCompressorNode` | Bound | Item 4. |
| `GainNode` | `lab::GainNode` | Bound | |
| `IIRFilterNode` | - | **Needs custom C++** | No IIR node anywhere in LabSound (checked all of `core/` and `extended/`). Would mean implementing a generic difference-equation processor from scratch - LabSound's `AudioProcessor`/`AudioBasicProcessorNode` base classes are the right hook point, but the DSP itself doesn't exist yet. |
| `MediaElementAudioSourceNode` | - | **N/A / needs custom C++** | No `HTMLMediaElement` concept exists in a QuickJS environment; would need an entirely invented "media element" shim first. Low value outside a DOM. |
| `MediaStreamAudioDestinationNode` | - | **Needs custom C++** | No `MediaStream` type anywhere in LabSound; would need a from-scratch shim. |
| `MediaStreamAudioSourceNode` | `lab::AudioHardwareInputNode` (partial) | **Needs custom C++** | LabSound has live hardware-input capture (item 11) but no `MediaStream`/`MediaStreamTrack` wrapper around it - binding hardware input directly (non-spec API) is far cheaper than building real `MediaStream` semantics. |
| `MediaStreamTrackAudioSourceNode` | - | **Needs custom C++** | Same gap as above. |
| `OscillatorNode` | `lab::OscillatorNode` | Bound | `type` accepts LabSound-extension values (`"fast-sine"`, `"falling-sawtooth"`) beyond the spec's four - a deviation to document, not necessarily fix. No `setPeriodicWave()`. |
| `PannerNode` | `lab::PannerNode` | **Not bound** | Item 6. |
| `PeriodicWave` | `lab::PeriodicWave` | **Not bound** | Item 9. |
| `ScriptProcessorNode` | - | **Deprecated in spec - skip** | LabSound's `FunctionNode` (extended) is the closest spirit-match (native callback per block) but bridging that callback into JS per audio quantum has real perf/threading cost for a deprecated API; not worth it. |
| `StereoPannerNode` | `lab::StereoPannerNode` | Bound | |
| `WaveShaperNode` | `lab::WaveShaperNode` | Bound | |
| `AudioWorklet` | - | **Needs custom C++ (large)** | No equivalent concept in LabSound at all. Plan: "AudioWorklet on WAMR" at the end of this file. |
| `AudioWorkletNode` | - | **Needs custom C++ (large)** | Same. |
| `AudioWorkletProcessor` | - | **Needs custom C++ (large)** | Same - would require running QuickJS callbacks on the audio render thread per quantum, with all the GC/threading hazards that implies. This is the single biggest lift on this list; everything else is wrapping existing LabSound DSP, this is inventing a JS-on-audio-thread execution model from nothing. |
| `AudioWorkletGlobalScope` | - | **Needs custom C++ (large)** | Part of the same effort as above. |
| `OfflineAudioCompletionEvent` | - | Covered by item 12 | Spec models this as an event; a resolved Promise (as already used for `decodeAudioData`) covers the same use case without inventing an Event system. |

---

## LabSound classes with no WebAudio equivalent (bindable anyway)

These are LabSound's own additions - no spec interface to match, so no
"deviation" to reconcile, just a straightforward wrap if/when useful. Ordered
by rough relevance to what this project already does (`synth.js`,
`drumsampler.js`, `effects.js` are all live-performance synth/effects code).

| LabSound class | What it does | Why it'd be worth binding here |
|---|---|---|
| `ADSRNode` (extended) | Native envelope generator | `synth.js`'s `ADSR` class (line 23) currently hand-schedules envelopes via chained `AudioParam` automation calls in JS - a native `ADSRNode` could replace that with less per-note JS overhead and one call instead of 3-4 scheduling calls per envelope stage. Highest-value item in this list given current code. |
| `SupersawNode` (extended) | Detuned multi-oscillator unison voice | Direct fit for pad/lead synth voices; `synth.js`'s `vco` is currently a single `OscillatorNode` - this would be a drop-in richer voice option. |
| `BPMDelay` (extended, `BPMDelayNode.h`) | `DelayNode` subclass with tempo-synced delay time | Cheap to bind (it *is* a `DelayNode` - same shape as item 2's `ConvolverNode` work) and useful for any rhythmic delay effect alongside `effects.js`. |
| `PingPongDelayNode` (extended) | Composite stereo ping-pong delay | Built as a `Subgraph` (multiple internal nodes wired together), not a plain `AudioNode` - binding pattern differs from everything else in the file; would need its own wrapper shape rather than reusing `make_audio_node_js`. |
| `PolyBLEPNode` (extended) | Band-limited (anti-aliased) oscillator | Higher audio quality alternative to `OscillatorNode` for square/saw waves at high frequencies; same constructor shape as `OscillatorNode`, cheap port. |
| `PWMNode` (extended) | Pulse-width-modulation oscillator | Common analog-synth voice type not otherwise available. |
| `SfxrNode` (extended) | Procedural 8-bit/retro SFX generator (sfxr/bfxr-style) | Good fit for `drumsampler.js`-style one-shot hits without needing sample files. |
| `GranulationNode` (extended) | Granular-synthesis buffer playback | Different playback model than `AudioBufferSourceNode`; useful for texture/pad effects, no spec equivalent exists. |
| `ClipNode` (extended) | Hard clipper/distortion | Simpler/cheaper alternative to `WaveShaperNode` curves for basic distortion. |
| `DiodeNode` (extended) | Diode-clipper distortion (subclasses `WaveShaperNode`) | Analog-modeling distortion; since it's a `WaveShaperNode` subclass, binding is nearly free once `WaveShaperNode`'s pattern is reused. |
| `PeakCompNode` (extended) | Simple peak compressor/limiter | Lighter-weight alternative to full `DynamicsCompressorNode` (item 4) for basic limiting. |
| `PowerMonitorNode` (extended) | RMS/power-level metering | Cheaper alternative to `AnalyserNode` (item 3) when only a level meter is needed, not full FFT data. |
| `SpectralMonitorNode` (extended) | FFT-based spectral analysis | Overlaps with `AnalyserNode`'s frequency-domain data; only worth binding if its API offers something `AnalyserNode` doesn't. |
| `RecorderNode` (extended) | Capture graph output to WAV | Already noted as item 10 - listed here too since it's the clearest "no spec equivalent" case. |
| `FunctionNode` (extended) | Native per-block callback node | Could expose a small set of *built-in* canned processing functions from C++, but can't safely bridge to arbitrary JS callbacks per audio quantum (same perf/threading issue as `ScriptProcessorNode`/`AudioWorklet` above) - bind only the native-callback use case, not a JS-scriptable one. |
| `PdNode` (extended) | Embeds a Pure Data (libpd) patch | Requires linking libpd as an additional dependency - only worth it if Pure Data patches are actually part of the workflow; skip otherwise. |

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

---

## sndobj module bindings

`quickjs-sndobj.cpp` wraps a first pass of `third_party/sndobj` - 11 classes
across a `Table` family, a generator family, and an effect family (see
`doc/native/sndobj.md`). 114 headers exist under
`third_party/sndobj/include/SndObj/`; 18 are touched by the current binding
(11 exported, plus `DelayLine`/`Filter`/`Oscil`/`Rand`/`Randh`/`SndObj`/`Table`
used internally as bases, not exported directly). The other 96 are listed
below, grouped by what they'd need to bind.

### ✅ Bound

`HarmTable`, `Oscili`, `Buzz`, `Randi`, `ADSR`, `Reson`, `Comb`, `Allpass`,
`VDelay`, `Gain`, `Mixer` (plus internal-only bases `DelayLine`, `Filter`,
`Oscil`, `Rand`, `Randh`, `SndObj`, `Table`).

### Not yet bound

| Group | Classes | Notes |
|---|---|---|
| Oscillators/generators | `Osc`, `Osci`, `Oscilt`, `PhOscili`, `FastOsc`, `SyncGrain` | Same shape as `Oscili`/`Buzz`; cheap additions to the existing generator family. `FastOsc`/`Osc`/`Osci` are `Oscili`'s own ancestors (truncating/alternate lookup) - lower priority than genuinely new generators (`SyncGrain`, granular). |
| Envelope | `IADSR` | Interpolating `ADSR` variant - trivial addition alongside `ADSR`. |
| Wavetables | `EnvTable`, `HammingTable`, `ImpulseTable`, `LoPassTable`, `NoteTable`, `PlnTable`, `PVEnvTable`, `PVTable`, `SndTable`, `SpecEnvTable`, `TrisegTable`, `UsrDefTable`, `UsrHarmTable` | All extend the same `Table` base already wired up for `HarmTable`; each is one more magic-dispatched constructor case in the existing `Table` family. `SndTable` needs `sndfile`-style file loading; `PVEnvTable`/`PVTable`/`SpecEnvTable` depend on the FFT family below. |
| Filters | `Ap`, `ButtBP`, `ButtBR`, `ButtHP`, `ButtLP`, `Fir`, `HiPass`, `Hilb`, `LowPass`, `Lp`, `TpTz` | Same shape as `Reson`/`Filter`; extends the existing effect family with more filter constructor cases. |
| Delay/tap utilities | `Loop`, `Tap`, `Tapi` | Extend `DelayLine` like `Comb`/`Allpass`/`VDelay` already do. |
| Physical modeling | `StringFlt`, `Pluck` | Karplus-Strong string; `Pluck` extends `StringFlt`. Good next addition for a plucked-string voice. |
| Pitch tracking/shifting | `Pitch`, `Ptrack` | Pitch analysis/shifting; `Pitch` depends on the FFT/phase-vocoder family below. |
| Mixing/utility | `Balance`, `Interp`, `Lookup`, `Lookupi`, `Phase`, `Ring`, `Unit` | Small standalone utility `SndObj`s - `Ring` (ring modulation) is the most directly synth-relevant. |
| Stereo pan | `Pan` | Deliberately deferred (see `doc/native/sndobj.md`'s "Not bound" section) - its constructor manufactures two extra live `SndObj` taps (`left`/`right`), a different lifetime shape than every class bound so far. |
| MIDI | `Bend`, `MidiIn`, `MidiMap` | MIDI input classes; project already has a separate `portmidi` module - only worth binding if driving an sndobj graph directly from MIDI (bypassing JS) becomes useful. |
| FFT/phase-vocoder family | `AdSyn`, `Convol`, `FFT`, `FFT_alt`, `IFAdd`, `IFFT`, `IFFT_alt`, `IFGram`, `PVA`, `PVBlur`, `PVFilter`, `PVMask`, `PVMix`, `PVMorph`, `PVRead`, `PVS`, `PVTransp`, `ReSyn`, `SinAnal`, `SinSyn`, `SpecCart`, `SpecCombine`, `SpecIn`, `SpecInterp`, `SpecMult`, `SpecPolar`, `SpecSplit`, `SpecThresh`, `SpecVoc` | Blocked: needs a vendored `rfftw/rfftw.h` this tree doesn't ship on its include path (see `CMakeLists.txt`'s `sndobj_SOURCES` comment). Would need either vendoring `rfftw` properly or porting these to a different FFT backend before any of this group can compile. |
| File/realtime/MIDI I/O | `SndASIO`, `SndAiff`, `SndBuffer`, `SndCoreAudio`, `SndFIO`, `SndIO`, `SndIn`, `SndJackIO`, `SndMidi`, `SndMidiIn`, `SndPVOCEX`, `SndRTIO`, `SndRTThread`, `SndRead`, `SndSinIO`, `SndThread`, `SndWave`, `SndWaveX` | Out of scope by design - `sndfile`/`portaudio`/`portmidi` modules already cover file I/O, realtime audio, and MIDI I/O respectively; binding sndobj's own IO layer would duplicate them. |

## STK mixed-precision build (probable, not started)

**Goal:** `StkFrames` and every `tick()` interface become Float32, so
`frames.buffer` is a `Float32Array` that goes straight to `PaStream`,
`SampleRateConverter`, `SoundTouch`, `RubberBand` (per channel) and `Aubio*`
with no copy (`doc/api-interoperability.md` section 4.4). Internal state that
needs the precision stays Float64. Done so far: `StkFrames.toFloat32` /
`fromFloat32` / `sampleType` as the no-patch bridge.

**Do this only if** the per-block `toFloat32` copy shows up in a profile, or
the Float64 view in `tests/*` keeps causing mistakes. Measure first: render a
high-Q low-frequency `BiQuad`/`TwoPole` and a long `Asymp` decay with
`StkFloat` as double and as float, and compare.

**Design**
- Add a second typedef in `third_party/stk/include/Stk.h`:
  `StkFloat` (I/O, float) and `StkAccum` (double). Deliver as a guarded patch
  in `cmake/patches/` (option `QJS_STK_FLOAT32`), applied at configure time
  like `aubio-static-lib.patch`, so `third_party/stk` stays a plain checkout.
- Required Float64 (`StkAccum`): filter coefficients and history. `Filter.h`
  holds `b_`, `a_` and `inputs_`/`outputs_`, and the last two are `StkFrames`,
  which would silently become float. Same members in `BiQuad`, `OnePole`,
  `OneZero`, `TwoPole`, `TwoZero`, `PoleZero`, `Fir`, `Iir`, `FormSwep` and the
  `Delay*`/`TapDelay` classes (14 headers). Also the allpass/interpolation
  state of `DelayA`/`DelayL` and the feedback state of the reverbs
  (`JCRev`, `NRev`, `PRCRev`, `FreeVerb`); listen for float error in long tails.
- Optional: phase accumulators (`SineWave`, `Blit*`, `Modulate`), only if a
  very slow LFO drifts. Envelope `value_` does not need it; at most keep
  `rate_` and the `Asymp` factor in double.
- Vector-taking constructors (`Fir(std::vector<StkFloat>&)`, `Iir(...)`)
  change signature; `quickjs-stk.cpp` does not call them.
- `analog-drums.hpp` follows `StkFloat`; move its filter/phase state to
  `StkAccum` by hand.
- Binding: `StkFrames.sampleType` already reports `'f32'`; `.buffer` already
  sizes from `sizeof(stk::StkFloat)`. Update the callers that hard-code
  `new Float64Array(frames.buffer)` (`tests/twintdrum-test.js`,
  `tests/tr909*-test.js`, `doc/native/stk-io.md`) to check `sampleType`.

**Risk:** a member left as `StkFloat` that should be `StkAccum` shows up only
as an audible difference in long IIR tails; the comparison render above is the
regression test.


---

## FFT bindings

Seven FFT APIs live in `third_party/` (list: `find-sources.sh third_party/
-iname "*fft*.h"`, minus `ext*`, `internal*`, nested vendored copies). Only
`LabSound`'s `AnalyserNode` is reachable from JS today (windowed Blackman,
smoothed, power-of-2 only), which is not enough for `tests/fft-test.js`,
whose moodbar pipeline is a hand-written O(N²) DFT.

**Shared JS shape** (one class per backend, same surface, so scripts can
swap backends):

```js
const fft = new FFT(254);          // size; throws RangeError if unsupported
fft.size;                          // 254
fft.forward(input, re, im);        // real input -> bins 0..size/2
fft.power(input, out);             // |X[k]|^2, out.length = size/2 + 1
fft.inverse(re, im, output);       // where the backend has it
```

Buffers are `Float32Array`/`Float64Array` (backend picks its native type,
converts when it differs), no per-call allocation. Scaling is the library's
own; `fft.scale` reports it (`1` for unnormalised, `1/size` for inverse).

### Ranking, easiest first

| # | API | Header | Sizes | Types | Effort | Why |
|---|---|---|---|---|---|---|
| 1 | signalsmith-dsp | `dsp/fft.h`, `dsp/include/signalsmith-dsp/fft.h` | any (generic step for primes) | float, double | tiny | header-only template, no link, no globals; `FFT<V>`, `ModifiedRealFFT<V>` in `fft.h`, `WindowedFFT` in `spectral.h` |
| 2 | aubio `fft` | `aubio/src/spectral/fft.h` | pow2 (ooura); any only with FFTW3 | float (smpl_t) | small | C API, `quickjs-aubio.c` already has the `fvec`/`cvec` plumbing (see "Not yet bound" in `doc/native/aubio.md`); `new_aubio_fft`, `aubio_fft_do`/`rdo`, `get_norm`/`get_phas` |
| 3 | LabSound ooura | `LabSound/third_party/ooura/fftsg.h` | pow2 | float, double | small | free templates `rdft`, `cdft`, `ddct`, `dfct` with `ip`/`w` work arrays; wrap `ip`/`w` in the object; no real-FFT unpacking helpers, so `forward()` has to unpack the packed rdft layout |
| 4 | rubberband `FFT` | `rubberband/src/common/FFT.h` | pow2, throws `InvalidSize` | float, double | medium | clean class (`forward`, `forwardMagnitude`, `forwardPolar`, `inverse*`) but it is a `src/common` internal: needs its include dir and `FFT.cpp` plus the chosen backend (`HAVE_FFTW3`/`HAVE_KISSFFT`/builtin) linked into `quickjs-rubberband`; `forwardMagnitude` is exactly the moodbar power step |
| 5 | marsyas `fft` | `marsyas/src/marsyas/fft.h` | pow2 | `mrs_real` | medium | class `fft::rfft(x, N, forward)` / `cfft` on a plain array is easy; the cost is building marsyas for `mrs_real` and the `AutoCorrelationFFT`, `PeakSynthFFT`, `SimulMaskingFft` MarSystems, which need the whole `MarSystem` graph: bind only the raw `fft` class |
| 6 | opus `kiss_fft` | `libnyquist/third_party/opus/celt/kiss_fft.h` | factors 2,3,4,5 only | float | medium | `opus_fft_alloc`/`opus_fft` in an Opus-internal copy, symbols shared with a vendored Opus, complex only, no real-FFT wrapper; bind only if a project needs it (no moodbar: 127 is prime) |
| 7 | sndobj FFT family | `sndobj/include/SndObj/FFT.h` etc. | by `rfftw` | float | large, blocked | `FFT`, `IFFT`, `FFT_alt`, `IFFT_alt` are `SndObj` graph nodes, blocked on the missing `rfftw/rfftw.h` (see the sndobj section). Unblock by a shim mapping the `rfftw` calls to system FFTW3, or port to backend #1, then do `FFT`/`IFFT` first and the `PV*`/`Spec*` family after |

Skipped on purpose: `aubio/src/pitch/pitchyinfft.h` (internal of
`AubioPitch('yinfft')`), the `rfftw` and `fftw-int` headers under
`sndobj/include` (headers of the missing library, not an API).

Outside the list, but the exact reference: system FFTW3 (`pkg-config fftw3`
is installed). It is what `gstfastspectrum` itself uses
(`fftw_plan_dft_r2c_1d(254, ..., FFTW_ESTIMATE)`), handles any size, and
is the only way to compare bit-for-bit. Optional `USE_FFTW=ON` backend of
the same `FFT` class; not a `third_party/` item.

### Which one for moodbar

`gstfastspectrum` takes `bands=128`, hence `nfft = 2*128 - 2 = 254` real
samples, unwindowed, and sums `|X[k]|^2 / nfft^2` for bins 0..127. 254 is
2 * 127, and 127 is prime. That decides:

- Power-of-2 only: ooura (#3), rubberband (#4), marsyas (#5), aubio built
  with its own ooura fallback (#2). Padding to 256 changes bin centres and the Bark band
  assignment, so the colours drift from real `.mood` files. Not acceptable
  for a moodbar port.
- kiss (#6) rejects 127.
- **signalsmith (#1) is the pick**: size 254 works directly (two radix-2
  halves around one generic 127-point step, one pass instead of the 254x127
  direct sums), header-only, double precision like `fftw_plan_dft_r2c_1d`,
  float variant for the rest. Check its scaling (unnormalised forward, same
  as FFTW) against `fft-test.js`'s `/ (NFFT * NFFT)`.
- **aubio (#2) is the second pick**, and needs no new module. The system
  `libaubio` links `libfftw3f`, so its FFTW code path (no power-of-2 check)
  should accept 254; untested. It is float, so expect a few low-bit
  differences from the double-precision reference. The vendored
  `third_party/aubio` (`BUILD_AUBIO=ON`) falls back to ooura and rejects 254
  unless configured with FFTW3 (`HAVE_FFTW3F`).
- rubberband links FFTW too, but its `FFT` class is an internal and
  power-of-2 only, so that does not help.
- FFTW3 stays the oracle: render a track with both and diff the `.mood`
  bytes against `~/Sources/moodbar`'s output.

### Tasks (in this order)

1. [ ] `quickjs-fft.cpp` new module `fft` with backend #1 (`FFT` class,
       `size`, `forward`, `power`, `inverse`, `Float32Array`/`Float64Array`
       in, no allocation per call); `doc/native/fft.md`; `tests/fft-bind-test.js`
       (impulse -> flat spectrum, sine -> single bin, forward/inverse
       round trip, sizes 8, 254, 255, 1024).
2. [ ] Switch `tests/fft-test.js` `makeSpectrum()` to `new FFT(NFFT).power()`;
       keep the DFT as a `--dft` check that both give the same colours.
3. [ ] Compare against FFTW3 on `Stereo MCs - Black Gold.mood` (repo root,
       untracked) and the reference `moodbar` output; record the max byte
       difference in the test header.
4. [ ] `USE_FFTW` backend (system FFTW3) behind the same class.
5. [ ] aubio `fft` (second moodbar backend): `AubioFFT` (`do`, `rdo`,
       `norm`, `phas`) in `quickjs-aubio.c`; `RangeError` for a size the
       backend rejects. Build side is done: `cmake/FindAubio.cmake` accepts
       a system aubio only if `new_aubio_fft(254)` succeeds, else
       `BuildAubio.cmake` builds `third_party/aubio` with float FFTW3
       (`cmake/{Find,Build}FFTW3.cmake`: system `fftw3f`, or download
       FFTW 3.3.10 into `third_party/fftw3`, static -fPIC).
6. [ ] ooura backend (pow2, small, no dependency beyond LabSound's copy).
7. [ ] rubberband `FFT` backend: `forwardMagnitude`/`forwardPolar` exposed
       as `fft.magnitude()` / `fft.polar()`.
8. [ ] marsyas `fft` and opus `kiss_fft` backends (only if a user asks).
9. [ ] sndobj: unblock `rfftw` (shim over FFTW3 or backend #1), then
       `FFT`/`IFFT`, then the `PV*`/`Spec*` family (updates the
       "FFT/phase-vocoder family" row above).
10. [ ] Update `doc/api-interoperability.md` with a section comparing the
        FFT surfaces (sizes, types, scaling, layout of packed vs split).

### Plan: `AubioFFT` (aubio `spectral/fft.h`) in `quickjs-aubio.c`

Wraps `aubio_fft_t` (`new_aubio_fft`, `del_aubio_fft`, `aubio_fft_do`,
`_rdo`, `_do_complex`, `_rdo_complex`, `_get_norm`, `_get_phas`,
`_get_realimag`, `_get_spectrum`). Facts the API is built on (read from
`third_party/aubio/src/spectral/fft.c`, not yet run from JS):

- Real input of `size` samples; `float` (`smpl_t`), `Float32Array` only.
- Two spectrum forms. `cvec`: `norm[]` and `phas[]`, `size/2 + 1` bins each.
  Half-complex `fvec` of `size` floats: `[r0, r1, .. rN, iN-1, .. i1]`
  (FFTW `R2HC`, which is the path `-DHAVE_FFTW3F` without `HAVE_COMPLEX_H`
  picks).
- `new_aubio_fft` returns NULL (and prints to stderr) for `size < 2`, and
  for non-power-of-2 on the ooura fallback; the FFTW build takes any size.
- Forward is unnormalised; check what `rdo` does with scale, and document it.

**JS shape.** aubio's Python binding (`fft(win_s)`, call it with a `fvec` to
get a `cvec` with `.norm` / `.phas`, `rdo()` back) is the survey reference;
the spectrum is a plain `{ norm, phas }` object of two `Float32Array`
(duck-typed, no `cvec` class, per the skill's struct-by-value rule):

````js
import { AubioFFT } from 'aubio';

const fft = new AubioFFT(254);    // RangeError if the backend rejects it
fft.size;                         // 254
fft.bins;                         // 128 (size / 2 + 1)

const spec = fft.forward(input);  // { norm, phas }, fresh Float32Arrays
fft.forward(input, spec);         // reuses spec.norm / spec.phas, no allocation
const out = fft.inverse(spec);    // Float32Array(size); inverse(spec, out) reuses
const hc = fft.forwardComplex(input);   // half-complex Float32Array(size)
fft.inverseComplex(hc, out);

AubioFFT.norm(hc, spec);          // aubio_fft_get_norm / _phas / _spectrum
AubioFFT.phas(hc, spec);
AubioFFT.realimag(spec, hc);      // aubio_fft_get_realimag
````

No views onto the native scratch memory are returned: aubio overwrites its
`compspec` on every call, so results are copied out (or written into the
caller's arrays). `power` is not wrapped: the moodbar step is
`norm[k] * norm[k] / (size * size)` in JS.

**Steps**

1. [ ] `JSAubioFFT { aubio_fft_t* obj; uint_t size; }`, class id, finalizer
       (`del_aubio_fft`), constructor with the `new_target.prototype`
       pattern copied from `js_aubionotes_constructor`; `RangeError`
       `"aubio: fft size %u not supported by this build"` when NULL.
2. [ ] Getters `size`, `bins`; `Symbol.toStringTag` `"AubioFFT"`.
3. [ ] `js_aubio_get_fvec_ptr` already resolves a `Float32Array`; add
       `js_aubio_get_cvec(ctx, val, bins, &cvec_t)` reading `norm` and
       `phas` properties, each a `Float32Array` of exactly `bins`
       (`TypeError` for the wrong type, `RangeError` for the wrong length),
       and a `js_aubio_new_cvec` that builds a fresh pair for the no-`out`
       call.
4. [ ] Methods by magic (one function, `JS_CFUNC_MAGIC_DEF`): `forward`,
       `inverse`, `forwardComplex`, `inverseComplex`; static `norm`,
       `phas`, `realimag` (wrap the `get_*` helpers, which take no fft
       object, so they live on the constructor).
5. [ ] Every length check against `size` / `bins` before the call: aubio
       reads `size` floats from the input with no bounds check.
6. [ ] `tests/aubio-fft-test.js`: impulse gives `norm == 1` everywhere;
       sine on bin k gives one peak of `size / 2`; `inverse(forward(x))`
       equals `x` (note scale); `forwardComplex` equals a naive DFT to
       1e-3; sizes 8, 254, 255, 1024; size 1 and wrong-length arrays throw.
       The 254/255 cases are skipped when the constructor throws
       (ooura build), with a message saying so.
7. [ ] `doc/native/aubio.md`: new `AubioFFT` section with the block above,
       the size rule, the scaling and the cvec / half-complex layouts;
       drop `fft` from "Not yet bound"; build note that FFTW3 is always
       on (`cmake/FindAubio.cmake`).
8. [ ] Moodbar: in `tests/fft-test.js` use `AubioFFT(NFFT)` for
       `makeSpectrum()` when available; compare its colours with the DFT
       path (`--dft`) and record the largest byte difference (float vs the
       double-precision reference).
9. [ ] Fill in the common `FFT` surface of task 1 (`power(input, out)`,
       `scale`) as a thin JS wrapper over `AubioFFT` if the signalsmith
       module lands later.

**Not in scope here**: aubio's `pvoc` (windowed phase vocoder),
`filterbank`, `mfcc`, `dct` — they sit on top of the same `fft` and get
their own entries when needed.

---

## More libraries to bind (live-coding synth / sequencer stack) (postponed)

Goal: one JS process, LabSound as the mixing graph, the other libs as
sources, effects and analysers. See the live-coding plan: a look-ahead
sequencer on `ctx.currentTime` with a hot-swappable pattern registry,
run from `qjsm repl.js`.

### Already vendored, not yet bound

| Dir | What | Plan |
|---|---|---|
| `third_party/dsp/` | Signalsmith DSP v1.7.1: header-only C++ (`filters.h` biquads, `delay.h`, `envelopes.h`, `fft.h`, `spectral.h` STFT, `windows.h`, `mix.h`, `rates.h`, `curves.h`) | `quickjs-signalsmith.cpp`: `Biquad`, `Delay`, `FFT`/`RealFFT`, `STFT`, envelopes. Also fills FFT task 9. Per-sample classes need block-wise `process(in, out)` over `Float32Array`, not per-sample calls from JS |
| `third_party/marsyas/` | MARSYAS audio-analysis framework (GPL-2) | Heavy and unmaintained. Bind only the MIR pieces aubio lacks (beat/tempo, chroma, spectral flux) or skip; check that it still builds before spending time |

### Candidates (licences from memory, verify before vendoring)

Ranked by value for live coding vs. effort. Header-only / plain-C first.

| Lib | Licence | Why | Effort |
|---|---|---|---|
| DaisySP (electro-smith) | MIT | oscillators, ladder/SVF filters, reverb, delay, drums (kick/snare/hihat), ADSR, compressor; no allocation, block-friendly | low, as `Daisy*` classes |
| Soundpipe (PaulBatchelor) | MIT | ~100 plain-C modules (Csound-derived): reverbs, physical models, filters; C, so easiest binding shape | low, one class per ugen |
| Maximilian (micknoise) | MIT | live-coding oriented, already has a JS (emscripten) API to mirror; oscillators, sample players, envelopes, granular | low-medium |
| chowdsp_wdf | BSD-3 | wave-digital-filter analog circuit models (diode clippers, tone stacks) | low, header-only |
| Q (cycfi) | MIT | modern C++ DSP: pitch detection, envelope followers, biquads | medium (templates) |
| Airwindows via airwin2rack | MIT | ~400 effects behind one uniform process interface; "can be used as a static library" | medium, one `AirwinEffect(name)` class |
| Mutable Instruments (`eurorack/plaits`, `rings`, `clouds`, `stmlib`) | MIT (stmlib/modules, check each) | Plaits: 16+ synth engines in one class; Rings, Clouds as effects. Strong sound for little API | medium, fixed-point/block API |
| TinySoundFont + TinyMidiLoader | MIT | SF2 sampler in two headers; GM playback without FluidSynth | low |
| FluidSynth | LGPL-2.1 | full SF2 synth, good for `portmidi` input -> sound | medium |
| sfizz | BSD-2 | SFZ sampler library with a C API | medium |
| Geonkick | GPL-3 | drum synth engine, has a standalone lib split | medium; GPL |
| libpd | BSD-3 | run Pure Data patches as nodes | medium |
| Faust (generated C++) | output free to license | compile `.dsp` to a C++ class, wrap generically; `faust2*` has no QuickJS target so write a small architecture file | medium; pays off for many effects at once |
| CLAP / LV2 host (`clap` headers, `lilv`) | MIT / ISC | host existing plugins as nodes (Surge XT, Vital, LSP, Calf) | high, but unlocks all of those |
| libsoundio / RtAudio / RtMidi | MIT | only if PortAudio/PortMidi prove lacking | skip |
| KFR | GPL / commercial | very fast FFT/filters | skip, licence |
| DSPFilters (vinniefalco) | MIT | classic IIR designs (Butterworth, Chebyshev, Elliptic) | low |
| CHOC (Tracktion) | ISC | header-only helpers (MIDI file parse, audio file IO, FIFO, value types) | low, as a source of parts |
| concurrentqueue / readerwriterqueue / SPSCQueue | BSD / MIT | needed if MIDI callbacks must hop onto the JS thread | low, internal use |

### DPF and JUCE

Neither is a library to bind; both are frameworks to build plugins
*with* (they own the plugin entry points and the host-facing lifecycle).

- DPF (ISC) is the better fit if we ever want to ship a binding-based
  synth as an LV2/VST3/CLAP plugin: the plugin's `run()` would call into
  an embedded QuickJS. Worth a prototype only after the live-coding path
  works.
- JUCE is AGPL-3 / commercial, large, and brings its own message loop,
  which fights QuickJS's event loop. `juce_dsp` is not usable alone
  without most of the module system. Not recommended.
- To use the plugins built on them, host them (CLAP / LV2 row above).

### Suggested order

1. `signalsmith` (already vendored; completes FFT work).
2. `daisysp` (drums + filters + reverb, fits the sequencer).
3. `soundpipe` or `maximilian`: pick one for breadth.
4. `tinysoundfont` for GM playback from `portmidi`.
5. A CLAP host node, the single binding that opens up third-party synths.
6. Plaits engines, then Faust glue.

### Cross-cutting design

- Every DSP lib exposes block processing on `Float32Array`; a generic
  `ScriptProcessor`-style bridge (a LabSound node whose `process` calls a
  native object, never JS) lets any of them sit in the graph, so the
  audio thread never enters the JS engine.
- Parameters set from JS are written to atomics / smoothed on the
  audio side; no locks in `process`.

---

## Live-coding environment (postponed)

Synths and sequencers driven from a `qjsm repl.js` session. Not started;
the library work above is postponed too.

### Threading facts the design rests on

- LabSound runs an audio render thread (device callback pulls the graph)
  and a graph update thread (spawned by `AudioContext::lazyInitialize`).
- The JS thread shares state with them through `ContextRenderLock`,
  `ContextGraphLock` and mutexes in `AudioParamTimeline` /
  `AudioSummingJunction`. The binding already takes these in `sweepNodes`
  and `AudioNode.disconnect`.
- Audio keeps playing while the REPL waits for input or evaluates a line.

### Design

```js
seq.set('kick', [1,0,0,0,1,0,0,0]);   // redefine at any time
synth.lead = (t, note) => { /* nodes scheduled at time t */ };
seq.bpm = 120;
```

- Look-ahead scheduler: a `setInterval` tick of ~25 ms schedules events
  ~100 ms ahead against `ctx.currentTime` (`osc.start(t)`,
  `param.setValueAtTime(v, t)`); timing lives in the audio clock, so a
  slow REPL evaluation does not make it jitter.
- Hot-swap: patterns and instruments are plain objects in a global
  registry that every tick re-reads; redefining takes effect on the next
  step, no restart.
- Existing parts to reuse: `tests/synth.js` (ADSR), `drumsampler.js`,
  `effects.js`.

### Tasks (in this order)

1. [ ] Check that `repl.js` keeps the event loop running (timers fire
       while it waits for input); fix or wrap if not.
2. [ ] `tests/seq.js`: look-ahead scheduler + pattern registry, steps and
       bpm, `start()` / `stop()`; test with a click track.
3. [ ] Instrument interface `(t, note, vel) => void` and a registry;
       port `synth.js` and `drumsampler.js` onto it.
4. [ ] Stress test: redefine an instrument every few hundred ms for
       minutes; watch node count, GC and `sweepNodes` for leaks or
       use-after-free.
5. [ ] MIDI in: `portmidi` callback must reach the JS thread through a
       queue, never call JS from the portmidi thread.
6. [ ] `ADSRNode` (see the extended-node table) to cut per-note JS cost.
7. [ ] Later: DSP libraries from "More libraries to bind" as instruments
       and effects; CLAP host node for third-party synths.

### Risks

- Node churn from redefining synths on every edit (GC timing vs
  `sweepNodes`) is the fragile part; task 4 comes before anything else
  is built on it.
- A long synchronous JS call (big FFT) delays the scheduler tick; the
  100 ms look-ahead covers short stalls only.

---

## AudioWorklet on WAMR (postponed)

`AudioWorkletNode` whose processor is a WebAssembly module run by WAMR on
LabSound's render thread. No QuickJS on the audio thread, so no GC pauses;
the `process(inputs, outputs, parameters)` contract is kept, the body is
compiled code. Uses the C interface in `qjs-modules/include/wasm-backend.h`
(`WB_*`), not the JS `WebAssembly` object.

```js
const mod = await WebAssembly.compile(fs.readFileSync('gain.wasm'));
await ctx.audioWorklet.addModule(mod);            // or addModule('gain.wasm')
const node = new AudioWorkletNode(ctx, 'gain', {
  numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
  processorOptions: { /* copied into the module at init */ }
});
node.parameters.get('gain').value = 0.5;          // AudioParam, automatable
node.port.postMessage({ type: 'reset' });
node.onprocessorerror = e => console.log(e);
osc.connect(node).connect(ctx.destination);
```

### Findings in the current WAMR build (`qjs-modules/cmake/wamr/`)

- It is the **classic interpreter**: `FAST_INTERP=0`, `AOT=0`, `JIT=0`,
  so likely slower than the default wasm3. Fine for a gain stage, too
  slow for reverbs or polyphony. Audio needs `WAMR_BUILD_FAST_INTERP=1`
  at least, ideally AOT (`wamrc` ahead of time, near native).
- `wasm_runtime_full_init` runs once per process; `wamr-backend.c` never
  calls `wasm_runtime_init_thread_env()`. WAMR requires that for any
  thread other than the one that initialised it, so the render thread
  must call it (and `destroy_thread_env` on teardown) before the first
  `WB_CallFunc`. Verify whether `WB_NewRuntime` per thread covers this.
- `WB_GetMemoryData` is valid only until the next `WB_CallFunc`; re-read
  the pointer and size after each call (the module may grow memory).
- WASI is off and `LIBC_BUILTIN` is on: processors must be freestanding
  (no file or clock imports); `malloc` comes from the module itself.

### Module ABI (what a processor `.wasm` exports)

```c
// all exports optional except process; sizes in frames, 128 per quantum
int  wk_init(int sampleRate, int nIn, int nOut, int nParam);   // once
int  wk_alloc(int bytes);              // returns offset into memory
int  wk_process(int inPtr, int outPtr, int paramPtr, int frames); // 0 = stop
void wk_message(int ptr, int len);     // main thread -> processor
```

- `inPtr` / `outPtr`: planar `float` buffers, `frames * channels` each,
  inputs then outputs, laid out by the host at `wk_init` time.
- `paramPtr`: per parameter, 128 floats if a-rate, 1 float if k-rate.
- Host copies LabSound `AudioBus` channels into the memory, calls
  `wk_process`, copies the outputs back. Return 0 ends the node, as
  `process()` returning `false` does in the spec.
- A tiny `worklet.h` plus a C and an AssemblyScript example define the
  convention; WAM 2.0 plugins need an adapter exporting the same names.

### C++ node (`WasmWorkletNode : lab::AudioNode`)

- `process(ContextRenderLock&, int)`: pull inputs, write params, drain the
  inbound queue into `wk_message`, call `wk_process`, copy outputs.
- Also `tailTime` / `latencyTime` / `propagatesSilence` (silent input and
  no tail: skip the call and output silence).
- Owns `WBRuntime` / `WBContext` / `WBInstance`; created and instantiated
  on the JS thread (compile cost), called only from the render thread.
  Instance is never touched from two threads.
- One `lab::AudioParam` per descriptor; a-rate via `AudioParam::calculateSampleAccurateValues`,
  k-rate via `value()`.
- **No allocation, no locks, no syscalls in `process`.** A trap makes the
  node output silence, set `failed`, and queue an error event.

### JS side

- `ctx.audioWorklet.addModule(bytes | module | path)`: compiles with
  `WB_NewModule`, lists `parameterDescriptors` from a custom section
  `"worklet"` (JSON: `name`, `defaultValue`, `minValue`, `maxValue`,
  `automationRate`) so no JS has to run to learn them; registers the name.
- `new AudioWorkletNode(ctx, name, options)`: read `argv[0]` as
  `AudioContext`, build the node, `make_audio_node_js`, then
  `anchor_node_in_context` (see boilerplate checklist), and add the class
  id to `any_audio_node()`.
- `node.parameters`: `AudioParamMap` (read-only Map-like) over the
  node's `AudioParam` wrappers.
- `node.port`: `MessagePort`-like with `postMessage(bytes|string|object)`
  and `onmessage`; objects go through JSON. Two lock-free SPSC queues
  (main -> audio, audio -> main); the audio -> main side is drained by
  the JS event loop (timer or `uv`-style wakeup), never calling JS from
  the render thread.
- `onprocessorerror` fired from that same drain.

### Tasks (in this order)

1. [ ] qjs-modules: add a `WAMR_BUILD_FAST_INTERP` option (and AOT with
       `wamrc`) to `cmake/wamr/CMakeLists.txt`; measure a 128-frame biquad
       under wasm3, WAMR classic, fast-interp, AOT; record numbers here.
2. [ ] Render thread: call `wasm_runtime_init_thread_env()` (backend or
       node), test `WB_CallFunc` from a non-init thread.
3. [ ] `worklet.h` ABI + `tests/worklet/gain.c` built with
       `clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-all`
       (gain with one a-rate param, one message); `.wasm` committed.
4. [ ] `WasmWorkletNode` in `quickjs-labsound.cpp` (or
       `quickjs-labsound-worklet.cpp`): `process` with copy in/out,
       silence on trap; test with `tests/worklet-test.js` (render an
       `OfflineAudioContext`, compare to a JS-computed gain).
5. [ ] Parameters: descriptor section, a-rate vs k-rate, automation
       (`linearRampToValueAtTime` through the node), `AudioParamMap`.
6. [ ] `port`: SPSC queues, `postMessage`/`onmessage`, error event.
7. [ ] Lifetime: finalizer disconnects under render + graph locks
       (as `sweepNodes`), instance freed after the render thread can no
       longer reach it; stress test creating and dropping nodes while
       playing.
8. [ ] `doc/native/labsound.md` section (header-block style as in the
       skill), update the AudioWorklet rows above, `BUGS` for anything
       found.
9. [ ] Later: JS processors on a second QuickJS runtime (true
       `AudioWorkletGlobalScope`, GC risk); WAM 2.0 plugin adapter;
       Faust `.dsp` -> wasm32 -> `wk_*` wrapper.

### Open questions

- AOT `.aot` files are per-target and need `wamrc`; ship `.wasm` and
  compile on load (`wamrc` as a library, if available), or accept
  interpreter speed for portability?
- Should `wk_process` see planar or interleaved audio? Planar chosen
  (matches `AudioBus`, WAM, Faust); revisit if a target lib wants otherwise.
