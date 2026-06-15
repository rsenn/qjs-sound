#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include "LabSound/LabSound.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "LabSound/core/AudioNodeOutput.h"
#include "LabSound/core/AudioBus.h"
#include "LabSound/core/SampledAudioNode.h"
#include "LabSound/extended/AudioContextLock.h"
#include "LabSound/extended/AudioFileReader.h"
#include "LabSound/extended/NoiseNode.h"
#include "LabSound/core/AudioSetting.h"
#include "LabSound/core/DelayNode.h"
#include "LabSound/core/WaveShaperNode.h"
#include "LabSound/core/StereoPannerNode.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

extern int js_stk_init(JSContext* ctx, JSModuleDef* m);
extern "C" void js_init_module_stk(JSContext* ctx, JSModuleDef*);

static JSClassID js_audiocontext_class_id;
static JSClassID js_audiodestinationnode_class_id;
static JSClassID js_audiolistener_class_id;
static JSClassID js_audiodevice_class_id;
static JSClassID js_oscillatornode_class_id;
static JSClassID js_gainnode_class_id;
static JSClassID js_biquadfilternode_class_id;
static JSClassID js_audiobuffersourcenode_class_id;
static JSClassID js_noisenode_class_id;
static JSClassID js_delaynode_class_id;
static JSClassID js_waveshapernode_class_id;
static JSClassID js_stereopannernode_class_id;
static JSClassID js_audiobuffer_class_id;
static JSClassID js_audiosetting_class_id;
static JSClassID js_audioparam_class_id;

static JSValue audiocontext_proto, audiocontext_ctor;
static JSValue audiodestinationnode_proto, audiodestinationnode_ctor;
static JSValue audiolistener_proto, audiolistener_ctor;
static JSValue audiodevice_proto, audiodevice_ctor;
static JSValue oscillatornode_proto, oscillatornode_ctor;
static JSValue gainnode_proto, gainnode_ctor;
static JSValue biquadfilternode_proto, biquadfilternode_ctor;
static JSValue audiobuffersourcenode_proto, audiobuffersourcenode_ctor;
static JSValue noisenode_proto, noisenode_ctor;
static JSValue delaynode_proto, delaynode_ctor;
static JSValue waveshapernode_proto, waveshapernode_ctor;
static JSValue stereopannernode_proto, stereopannernode_ctor;
static JSValue audiobuffer_proto, audiobuffer_ctor;
static JSValue audiosetting_proto;
static JSValue audioparam_proto;

typedef std::shared_ptr<lab::AudioContext> AudioContextPtr;
typedef std::shared_ptr<lab::AudioDestinationNode> AudioDestinationNodePtr;
typedef std::shared_ptr<lab::AudioListener> AudioListenerPtr;
typedef std::shared_ptr<lab::AudioDevice> AudioDevicePtr;

struct JsAudioNode {
  std::shared_ptr<lab::AudioNode> node;
  AudioContextPtr ctx;
};

struct JsAudioParam {
  std::shared_ptr<lab::AudioParam> param;
};

struct JsAudioBuffer {
  std::shared_ptr<lab::AudioBus> bus;
};

// Shim so DelayNode.delayTime can be `delay.delayTime.value = 0.3` isomorphic
// with browsers — lab uses AudioSetting (no automation) for it, so the
// scheduling methods just collapse to immediate setFloat.
struct JsAudioSetting {
  std::shared_ptr<lab::AudioSetting> setting;
};

/* ---------- helpers ---------- */

static JsAudioNode*
any_audio_node(JSValueConst v) {
  void* p;
  if((p = JS_GetOpaque(v, js_audiodestinationnode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_oscillatornode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_gainnode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_biquadfilternode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_audiobuffersourcenode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_noisenode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_delaynode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_waveshapernode_class_id)))
    return static_cast<JsAudioNode*>(p);
  if((p = JS_GetOpaque(v, js_stereopannernode_class_id)))
    return static_cast<JsAudioNode*>(p);
  return nullptr;
}

static JSValue
make_audio_setting_js(JSContext* ctx, std::shared_ptr<lab::AudioSetting> s) {
  auto* w = static_cast<JsAudioSetting*>(js_mallocz(ctx, sizeof(JsAudioSetting)));
  new(w) JsAudioSetting{std::move(s)};
  JSValue obj = JS_NewObjectProtoClass(ctx, audiosetting_proto, js_audiosetting_class_id);
  if(JS_IsException(obj)) {
    w->~JsAudioSetting();
    js_free(ctx, w);
    return obj;
  }
  JS_SetOpaque(obj, w);
  return obj;
}

// Read a 1-D float buffer from either a Float32Array or a plain Array.
static int
read_float_array(JSContext* ctx, JSValueConst val, std::vector<float>& out) {
  size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);
  if(!JS_IsException(buf) && bytes_per_element == sizeof(float)) {
    size_t ab_size = 0;
    uint8_t* ab_data = JS_GetArrayBuffer(ctx, &ab_size, buf);
    if(ab_data) {
      const float* floats = reinterpret_cast<const float*>(ab_data + byte_offset);
      size_t count = byte_length / sizeof(float);
      out.assign(floats, floats + count);
      JS_FreeValue(ctx, buf);
      return 0;
    }
  }
  if(!JS_IsException(buf))
    JS_FreeValue(ctx, buf);

  JSValue lenv = JS_GetPropertyStr(ctx, val, "length");
  if(JS_IsUndefined(lenv) || JS_IsException(lenv)) {
    JS_FreeValue(ctx, lenv);
    return -1;
  }
  uint32_t len = 0;
  JS_ToUint32(ctx, &len, lenv);
  JS_FreeValue(ctx, lenv);
  out.resize(len);
  for(uint32_t i = 0; i < len; i++) {
    JSValue e = JS_GetPropertyUint32(ctx, val, i);
    double d = 0;
    JS_ToFloat64(ctx, &d, e);
    out[i] = static_cast<float>(d);
    JS_FreeValue(ctx, e);
  }
  return 0;
}

static JSValue
make_audio_buffer_js(JSContext* ctx, std::shared_ptr<lab::AudioBus> bus) {
  auto* w = static_cast<JsAudioBuffer*>(js_mallocz(ctx, sizeof(JsAudioBuffer)));
  new(w) JsAudioBuffer{std::move(bus)};
  JSValue obj = JS_NewObjectProtoClass(ctx, audiobuffer_proto, js_audiobuffer_class_id);
  if(JS_IsException(obj)) {
    w->~JsAudioBuffer();
    js_free(ctx, w);
    return obj;
  }
  JS_SetOpaque(obj, w);
  return obj;
}

// Anchor a node JS object into a hidden "__nodes" array on its AudioContext
// AND give the node a JS-level "context" back-reference. lab's graph holds
// raw back-pointers to nodes, so letting QuickJS finalize a node wrapper
// while audio is still rendering corrupts the graph. The cycle node→context→
// __nodes→node is fine — QuickJS's cycle collector handles it, and as long
// as anything from the outside reaches one node, the whole graph stays live.
static void
anchor_node_in_context(JSContext* ctx, JSValueConst ac_jsval, JSValueConst node_jsval) {
  if(JS_IsUndefined(ac_jsval) || !JS_IsObject(ac_jsval))
    return;

  JS_SetPropertyStr(ctx, node_jsval, "context", JS_DupValue(ctx, ac_jsval));

  JSValue arr = JS_GetPropertyStr(ctx, ac_jsval, "__nodes");
  if(JS_IsUndefined(arr) || JS_IsException(arr)) {
    JS_FreeValue(ctx, arr);
    arr = JS_NewArray(ctx);
    JS_SetPropertyStr(ctx, ac_jsval, "__nodes", JS_DupValue(ctx, arr));
  }
  uint32_t len = 0;
  JSValue len_v = JS_GetPropertyStr(ctx, arr, "length");
  JS_ToUint32(ctx, &len, len_v);
  JS_FreeValue(ctx, len_v);
  JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, node_jsval));
  JS_FreeValue(ctx, arr);
}

static JSValue
make_audio_node_js(JSContext* ctx, JSValueConst proto, JSClassID class_id, std::shared_ptr<lab::AudioNode> node, AudioContextPtr ac) {
  auto* w = static_cast<JsAudioNode*>(js_mallocz(ctx, sizeof(JsAudioNode)));
  new(w) JsAudioNode{std::move(node), std::move(ac)};
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, class_id);
  if(JS_IsException(obj)) {
    w->~JsAudioNode();
    js_free(ctx, w);
    return obj;
  }
  JS_SetOpaque(obj, w);
  return obj;
}

static JSValue
make_audio_param_js(JSContext* ctx, std::shared_ptr<lab::AudioParam> param) {
  auto* w = static_cast<JsAudioParam*>(js_mallocz(ctx, sizeof(JsAudioParam)));
  new(w) JsAudioParam{std::move(param)};
  JSValue obj = JS_NewObjectProtoClass(ctx, audioparam_proto, js_audioparam_class_id);
  if(JS_IsException(obj)) {
    w->~JsAudioParam();
    js_free(ctx, w);
    return obj;
  }
  JS_SetOpaque(obj, w);
  return obj;
}

static std::pair<lab::AudioStreamConfig, lab::AudioStreamConfig>
get_default_device_config() {
  const auto devices = lab::AudioDevice_RtAudio::MakeAudioDeviceList();
  lab::AudioDeviceInfo outInfo, inInfo;
  for(const auto& info : devices) {
    if(info.is_default_output)
      outInfo = info;
    if(info.is_default_input)
      inInfo = info;
  }

  lab::AudioStreamConfig outputConfig;
  if(outInfo.index != -1) {
    outputConfig.device_index = outInfo.index;
    outputConfig.desired_channels = std::min(uint32_t(2), outInfo.num_output_channels);
    outputConfig.desired_samplerate = outInfo.nominal_samplerate;
  } else {
    outputConfig.device_index = 0;
    outputConfig.desired_channels = 2;
    outputConfig.desired_samplerate = 44100;
  }

  lab::AudioStreamConfig inputConfig;
  if(inInfo.index != -1) {
    inputConfig.device_index = inInfo.index;
    inputConfig.desired_channels = std::min(uint32_t(1), inInfo.num_input_channels);
    inputConfig.desired_samplerate = outputConfig.desired_samplerate;
  }
  return {inputConfig, outputConfig};
}

/* ---------- AudioContext ---------- */

static JSValue
js_audiocontext_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  bool isOffline = false, autoDispatchEvents = true;

  if(argc > 0)
    isOffline = JS_ToBool(ctx, argv[0]);
  if(argc > 1)
    autoDispatchEvents = JS_ToBool(ctx, argv[1]);

  auto ac = std::make_shared<lab::AudioContext>(isOffline, autoDispatchEvents);

  if(!isOffline) {
    auto cfg = get_default_device_config();
    auto device = std::make_shared<lab::AudioDevice_RtAudio>(cfg.first, cfg.second);
    auto dest = std::make_shared<lab::AudioDestinationNode>(*ac, device);
    device->setDestinationNode(dest);
    ac->setDestinationNode(dest);
  }

  auto* sac = static_cast<AudioContextPtr*>(js_mallocz(ctx, sizeof(AudioContextPtr)));
  new(sac) AudioContextPtr(ac);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiocontext_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_audiocontext_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, sac);
  return obj;

fail:
  sac->~AudioContextPtr();
  js_free(ctx, sac);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AC_PROP_SAMPLERATE,
  AC_PROP_DESTINATION,
  AC_PROP_LISTENER,
  AC_PROP_CURRENTTIME,
  AC_PROP_CURRENTSAMPLEFRAME,
  AC_PROP_PREDICTED_CURRENTTIME,
};

static JSValue
js_audiocontext_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioContextPtr* sac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id));
  if(!sac)
    return JS_EXCEPTION;

  switch(magic) {
    case AC_PROP_SAMPLERATE: return JS_NewFloat64(ctx, (*sac)->sampleRate());
    case AC_PROP_DESTINATION: {
      auto dest = (*sac)->destinationNode();
      if(!dest)
        return JS_NULL;
      return make_audio_node_js(ctx, audiodestinationnode_proto, js_audiodestinationnode_class_id, std::static_pointer_cast<lab::AudioNode>(dest), *sac);
    }
    case AC_PROP_LISTENER: {
      auto al = (*sac)->listener();
      JSValue ret = JS_NewObjectProtoClass(ctx, audiolistener_proto, js_audiolistener_class_id);
      auto* p = static_cast<AudioListenerPtr*>(js_mallocz(ctx, sizeof(AudioListenerPtr)));
      new(p) AudioListenerPtr(al);
      JS_SetOpaque(ret, p);
      return ret;
    }
    case AC_PROP_CURRENTTIME: return JS_NewFloat64(ctx, (*sac)->currentTime());
    case AC_PROP_CURRENTSAMPLEFRAME: return JS_NewInt64(ctx, (*sac)->currentSampleFrame());
    case AC_PROP_PREDICTED_CURRENTTIME: return JS_NewFloat64(ctx, (*sac)->predictedCurrentTime());
  }
  return JS_UNDEFINED;
}

static JSValue
js_audiocontext_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  AudioContextPtr* sac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id));
  if(!sac)
    return JS_EXCEPTION;
  if(argc < 2)
    return JS_ThrowTypeError(ctx, "AudioContext.connect requires (destination, source)");
  JsAudioNode* dst = any_audio_node(argv[0]);
  JsAudioNode* src = any_audio_node(argv[1]);
  if(!dst || !src)
    return JS_ThrowTypeError(ctx, "arguments must be AudioNodes");
  int destIdx = 0, srcIdx = 0;
  if(argc > 2)
    JS_ToInt32(ctx, &destIdx, argv[2]);
  if(argc > 3)
    JS_ToInt32(ctx, &srcIdx, argv[3]);
  (*sac)->connect(dst->node, src->node, destIdx, srcIdx);
  return JS_UNDEFINED;
}

static JSValue
js_audiocontext_decode_audio_data(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  AudioContextPtr* sac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id));
  if(!sac)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "decodeAudioData requires an ArrayBuffer");

  size_t size = 0;
  uint8_t* buf = JS_GetArrayBuffer(ctx, &size, argv[0]);
  if(!buf)
    return JS_ThrowTypeError(ctx, "argument must be an ArrayBuffer");

  std::vector<uint8_t> data(buf, buf + size);
  auto bus = lab::MakeBusFromMemory(data, false);
  if(!bus)
    return JS_ThrowInternalError(ctx, "decodeAudioData: failed to decode");

  JSValue ab = make_audio_buffer_js(ctx, bus);
  // Return a resolved Promise for browser-compat. await on a non-Promise
  // also works, so callers can use `await ctx.decodeAudioData(...)` in both
  // qjs and browsers.
  JSValue resolving[2];
  JSValue promise = JS_NewPromiseCapability(ctx, resolving);
  JS_Call(ctx, resolving[0], JS_UNDEFINED, 1, &ab);
  JS_FreeValue(ctx, resolving[0]);
  JS_FreeValue(ctx, resolving[1]);
  JS_FreeValue(ctx, ab);
  return promise;
}

static JSValue
js_audiocontext_create_buffer_from_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  AudioContextPtr* sac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id));
  if(!sac)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "createBufferFromFile requires a path");

  const char* path = JS_ToCString(ctx, argv[0]);
  if(!path)
    return JS_EXCEPTION;

  bool mixToMono = false;
  if(argc > 1)
    mixToMono = JS_ToBool(ctx, argv[1]);

  auto bus = lab::MakeBusFromFile(path, mixToMono);
  JS_FreeCString(ctx, path);
  if(!bus)
    return JS_ThrowInternalError(ctx, "createBufferFromFile: failed to load");

  return make_audio_buffer_js(ctx, bus);
}

static void
js_audiocontext_finalizer(JSRuntime* rt, JSValue val) {
  AudioContextPtr* sac = static_cast<AudioContextPtr*>(JS_GetOpaque(val, js_audiocontext_class_id));
  if(sac) {
    sac->~AudioContextPtr();
    js_free_rt(rt, sac);
  }
}

static JSClassDef js_audiocontext_class = {
    .class_name = "AudioContext",
    .finalizer = js_audiocontext_finalizer,
};

static const JSCFunctionListEntry js_audiocontext_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiocontext_get, 0, AC_PROP_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("destination", js_audiocontext_get, 0, AC_PROP_DESTINATION),
    JS_CGETSET_MAGIC_DEF("destinationNode", js_audiocontext_get, 0, AC_PROP_DESTINATION),
    JS_CGETSET_MAGIC_DEF("listener", js_audiocontext_get, 0, AC_PROP_LISTENER),
    JS_CGETSET_MAGIC_DEF("currentTime", js_audiocontext_get, 0, AC_PROP_CURRENTTIME),
    JS_CGETSET_MAGIC_DEF("currentSampleFrame", js_audiocontext_get, 0, AC_PROP_CURRENTSAMPLEFRAME),
    JS_CGETSET_MAGIC_DEF("predictedCurrentTime", js_audiocontext_get, 0, AC_PROP_PREDICTED_CURRENTTIME),
    JS_CFUNC_DEF("connect", 2, js_audiocontext_connect),
    JS_CFUNC_DEF("decodeAudioData", 1, js_audiocontext_decode_audio_data),
    JS_CFUNC_DEF("createBufferFromFile", 1, js_audiocontext_create_buffer_from_file),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioContext", JS_PROP_CONFIGURABLE),
};

/* ---------- shared AudioNode methods ---------- */

static JSValue
js_audionode_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* src = any_audio_node(this_val);
  if(!src)
    return JS_ThrowTypeError(ctx, "this is not an AudioNode");
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "connect requires a destination");
  JsAudioNode* dst = any_audio_node(argv[0]);
  if(!dst)
    return JS_ThrowTypeError(ctx, "destination must be an AudioNode");
  int srcIdx = 0, dstIdx = 0;
  if(argc > 1)
    JS_ToInt32(ctx, &srcIdx, argv[1]);
  if(argc > 2)
    JS_ToInt32(ctx, &dstIdx, argv[2]);
  src->ctx->connect(dst->node, src->node, dstIdx, srcIdx);
  return JS_DupValue(ctx, argv[0]);
}

static JSValue
js_audionode_disconnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* src = any_audio_node(this_val);
  if(!src)
    return JS_ThrowTypeError(ctx, "this is not an AudioNode");
  if(argc < 1) {
    src->ctx->disconnect(src->node, 0);
  } else {
    JsAudioNode* dst = any_audio_node(argv[0]);
    if(!dst)
      return JS_ThrowTypeError(ctx, "destination must be an AudioNode");
    src->ctx->disconnect(dst->node, src->node, 0, 0);
  }
  return JS_UNDEFINED;
}

static void
js_audionode_finalize_with(JSRuntime* rt, JSValue val, JSClassID cid) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque(val, cid));
  if(w) {
    w->~JsAudioNode();
    js_free_rt(rt, w);
  }
}

/* ---------- AudioDestinationNode ---------- */

static JSValue
js_audiodestinationnode_get_name(JSContext* ctx, JSValueConst this_val) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_audiodestinationnode_class_id));
  if(!w)
    return JS_EXCEPTION;
  return JS_NewString(ctx, w->node->name());
}

static JSValue
js_audiodestinationnode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  return JS_ThrowTypeError(ctx, "use AudioContext.destination instead");
}

static void
js_audiodestinationnode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_audiodestinationnode_class_id);
}

static JSClassDef js_audiodestinationnode_class = {
    .class_name = "AudioDestinationNode",
    .finalizer = js_audiodestinationnode_finalizer,
};

static const JSCFunctionListEntry js_audiodestinationnode_funcs[] = {
    JS_CGETSET_DEF("name", js_audiodestinationnode_get_name, NULL),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDestinationNode", JS_PROP_CONFIGURABLE),
};

/* ---------- OscillatorNode ---------- */

static lab::OscillatorType
parse_oscillator_type(const char* s) {
  if(!s)
    return lab::OscillatorType::SINE;
  if(!strcmp(s, "sine"))
    return lab::OscillatorType::SINE;
  if(!strcmp(s, "square"))
    return lab::OscillatorType::SQUARE;
  if(!strcmp(s, "sawtooth"))
    return lab::OscillatorType::SAWTOOTH;
  if(!strcmp(s, "triangle"))
    return lab::OscillatorType::TRIANGLE;
  if(!strcmp(s, "fast-sine"))
    return lab::OscillatorType::FAST_SINE;
  if(!strcmp(s, "falling-sawtooth"))
    return lab::OscillatorType::FALLING_SAWTOOTH;
  return lab::OscillatorType::SINE;
}

static const char*
osc_type_to_string(lab::OscillatorType t) {
  switch(t) {
    case lab::OscillatorType::SINE: return "sine";
    case lab::OscillatorType::FAST_SINE: return "fast-sine";
    case lab::OscillatorType::SQUARE: return "square";
    case lab::OscillatorType::SAWTOOTH: return "sawtooth";
    case lab::OscillatorType::FALLING_SAWTOOTH: return "falling-sawtooth";
    case lab::OscillatorType::TRIANGLE: return "triangle";
    case lab::OscillatorType::CUSTOM: return "custom";
    default: return "sine";
  }
}

static JSValue
js_oscillator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "OscillatorNode requires an AudioContext");

  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;

  auto osc = std::make_shared<lab::OscillatorNode>(*ac);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v;

    v = JS_GetPropertyStr(ctx, argv[1], "type");
    if(!JS_IsUndefined(v) && !JS_IsException(v)) {
      const char* s = JS_ToCString(ctx, v);
      if(s) {
        osc->setType(parse_oscillator_type(s));
        JS_FreeCString(ctx, s);
      }
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "frequency");
    if(JS_IsNumber(v)) {
      double f;
      JS_ToFloat64(ctx, &f, v);
      osc->frequency()->setValue((float)f);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "detune");
    if(JS_IsNumber(v)) {
      double d;
      JS_ToFloat64(ctx, &d, v);
      osc->detune()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, oscillatornode_proto);
  }

  JSValue obj = make_audio_node_js(ctx, proto, js_oscillatornode_class_id, std::static_pointer_cast<lab::AudioNode>(osc), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_oscillator_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto osc = std::dynamic_pointer_cast<lab::OscillatorNode>(w->node);
  if(!osc)
    return JS_ThrowInternalError(ctx, "not an OscillatorNode");
  float when = 0.0f;
  if(argc > 0) {
    double t;
    if(JS_ToFloat64(ctx, &t, argv[0]))
      return JS_EXCEPTION;
    when = (float)t;
  }
  osc->start(when);
  return JS_UNDEFINED;
}

static JSValue
js_oscillator_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto osc = std::dynamic_pointer_cast<lab::OscillatorNode>(w->node);
  if(!osc)
    return JS_ThrowInternalError(ctx, "not an OscillatorNode");
  float when = 0.0f;
  if(argc > 0) {
    double t;
    if(JS_ToFloat64(ctx, &t, argv[0]))
      return JS_EXCEPTION;
    when = (float)t;
  }
  osc->stop(when);
  return JS_UNDEFINED;
}

enum {
  OSC_PROP_TYPE,
  OSC_PROP_FREQUENCY,
  OSC_PROP_DETUNE,
};

static JSValue
js_oscillator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto osc = std::dynamic_pointer_cast<lab::OscillatorNode>(w->node);
  if(!osc)
    return JS_ThrowInternalError(ctx, "not an OscillatorNode");
  switch(magic) {
    case OSC_PROP_TYPE: return JS_NewString(ctx, osc_type_to_string(osc->type()));
    case OSC_PROP_FREQUENCY: return make_audio_param_js(ctx, osc->frequency());
    case OSC_PROP_DETUNE: return make_audio_param_js(ctx, osc->detune());
  }
  return JS_UNDEFINED;
}

static JSValue
js_oscillator_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto osc = std::dynamic_pointer_cast<lab::OscillatorNode>(w->node);
  if(!osc)
    return JS_ThrowInternalError(ctx, "not an OscillatorNode");
  if(magic == OSC_PROP_TYPE) {
    const char* s = JS_ToCString(ctx, value);
    if(s) {
      osc->setType(parse_oscillator_type(s));
      JS_FreeCString(ctx, s);
    }
  }
  return JS_UNDEFINED;
}

static void
js_oscillatornode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_oscillatornode_class_id);
}

static JSClassDef js_oscillatornode_class = {
    .class_name = "OscillatorNode",
    .finalizer = js_oscillatornode_finalizer,
};

static const JSCFunctionListEntry js_oscillatornode_funcs[] = {
    JS_CFUNC_DEF("start", 0, js_oscillator_start),
    JS_CFUNC_DEF("stop", 0, js_oscillator_stop),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_CGETSET_MAGIC_DEF("type", js_oscillator_get, js_oscillator_set, OSC_PROP_TYPE),
    JS_CGETSET_MAGIC_DEF("frequency", js_oscillator_get, 0, OSC_PROP_FREQUENCY),
    JS_CGETSET_MAGIC_DEF("detune", js_oscillator_get, 0, OSC_PROP_DETUNE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "OscillatorNode", JS_PROP_CONFIGURABLE),
};

/* ---------- GainNode ---------- */

static JSValue
js_gain_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "GainNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;
  auto g = std::make_shared<lab::GainNode>(*ac);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v = JS_GetPropertyStr(ctx, argv[1], "gain");
    if(JS_IsNumber(v)) {
      double f;
      JS_ToFloat64(ctx, &f, v);
      g->gain()->setValue((float)f);
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, gainnode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_gainnode_class_id, std::static_pointer_cast<lab::AudioNode>(g), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_gain_get_gain(JSContext* ctx, JSValueConst this_val) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_gainnode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto g = std::dynamic_pointer_cast<lab::GainNode>(w->node);
  if(!g)
    return JS_ThrowInternalError(ctx, "not a GainNode");
  return make_audio_param_js(ctx, g->gain());
}

static void
js_gainnode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_gainnode_class_id);
}

static JSClassDef js_gainnode_class = {
    .class_name = "GainNode",
    .finalizer = js_gainnode_finalizer,
};

static const JSCFunctionListEntry js_gainnode_funcs[] = {
    JS_CGETSET_DEF("gain", js_gain_get_gain, NULL),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "GainNode", JS_PROP_CONFIGURABLE),
};

/* ---------- BiquadFilterNode ---------- */

static lab::FilterType
parse_filter_type(const char* s) {
  if(!s)
    return lab::FilterType::LOWPASS;
  if(!strcmp(s, "lowpass"))
    return lab::FilterType::LOWPASS;
  if(!strcmp(s, "highpass"))
    return lab::FilterType::HIGHPASS;
  if(!strcmp(s, "bandpass"))
    return lab::FilterType::BANDPASS;
  if(!strcmp(s, "lowshelf"))
    return lab::FilterType::LOWSHELF;
  if(!strcmp(s, "highshelf"))
    return lab::FilterType::HIGHSHELF;
  if(!strcmp(s, "peaking"))
    return lab::FilterType::PEAKING;
  if(!strcmp(s, "notch"))
    return lab::FilterType::NOTCH;
  if(!strcmp(s, "allpass"))
    return lab::FilterType::ALLPASS;
  return lab::FilterType::LOWPASS;
}

static const char*
filter_type_to_string(lab::FilterType t) {
  switch(t) {
    case lab::FilterType::LOWPASS: return "lowpass";
    case lab::FilterType::HIGHPASS: return "highpass";
    case lab::FilterType::BANDPASS: return "bandpass";
    case lab::FilterType::LOWSHELF: return "lowshelf";
    case lab::FilterType::HIGHSHELF: return "highshelf";
    case lab::FilterType::PEAKING: return "peaking";
    case lab::FilterType::NOTCH: return "notch";
    case lab::FilterType::ALLPASS: return "allpass";
    default: return "lowpass";
  }
}

static JSValue
js_biquadfilter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "BiquadFilterNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;
  auto f = std::make_shared<lab::BiquadFilterNode>(*ac);

  // lab's BiquadFilterNode descriptor sets initialChannelCount=0, so the
  // AudioNode base class doesn't add an output and any connection sourced
  // from it would silently no-op. Add a stereo output under a graph lock.
  if(f->numberOfOutputs() == 0) {
    lab::ContextGraphLock gLock(ac.get(), "BiquadFilterNode.addOutput");
    f->addOutput(gLock, std::unique_ptr<lab::AudioNodeOutput>(new lab::AudioNodeOutput(f.get(), 2)));
  }

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v;

    v = JS_GetPropertyStr(ctx, argv[1], "type");
    if(!JS_IsUndefined(v) && !JS_IsException(v)) {
      const char* s = JS_ToCString(ctx, v);
      if(s) {
        f->setType(parse_filter_type(s));
        JS_FreeCString(ctx, s);
      }
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "frequency");
    if(JS_IsNumber(v)) {
      double d;
      JS_ToFloat64(ctx, &d, v);
      f->frequency()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "Q");
    if(JS_IsNumber(v)) {
      double d;
      JS_ToFloat64(ctx, &d, v);
      f->q()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "detune");
    if(JS_IsNumber(v)) {
      double d;
      JS_ToFloat64(ctx, &d, v);
      f->detune()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "gain");
    if(JS_IsNumber(v)) {
      double d;
      JS_ToFloat64(ctx, &d, v);
      f->gain()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, biquadfilternode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_biquadfilternode_class_id, std::static_pointer_cast<lab::AudioNode>(f), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

enum {
  BQ_PROP_TYPE,
  BQ_PROP_FREQUENCY,
  BQ_PROP_Q,
  BQ_PROP_DETUNE,
  BQ_PROP_GAIN,
};

static JSValue
js_biquadfilter_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_biquadfilternode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto f = std::dynamic_pointer_cast<lab::BiquadFilterNode>(w->node);
  if(!f)
    return JS_ThrowInternalError(ctx, "not a BiquadFilterNode");
  switch(magic) {
    case BQ_PROP_TYPE: return JS_NewString(ctx, filter_type_to_string(f->type()));
    case BQ_PROP_FREQUENCY: return make_audio_param_js(ctx, f->frequency());
    case BQ_PROP_Q: return make_audio_param_js(ctx, f->q());
    case BQ_PROP_DETUNE: return make_audio_param_js(ctx, f->detune());
    case BQ_PROP_GAIN: return make_audio_param_js(ctx, f->gain());
  }
  return JS_UNDEFINED;
}

static JSValue
js_biquadfilter_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_biquadfilternode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto f = std::dynamic_pointer_cast<lab::BiquadFilterNode>(w->node);
  if(!f)
    return JS_ThrowInternalError(ctx, "not a BiquadFilterNode");
  if(magic == BQ_PROP_TYPE) {
    const char* s = JS_ToCString(ctx, value);
    if(s) {
      f->setType(parse_filter_type(s));
      JS_FreeCString(ctx, s);
    }
  }
  return JS_UNDEFINED;
}

static void
js_biquadfilternode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_biquadfilternode_class_id);
}

static JSClassDef js_biquadfilternode_class = {
    .class_name = "BiquadFilterNode",
    .finalizer = js_biquadfilternode_finalizer,
};

static JSValue
js_audionode_io_count(JSContext* ctx, JSValueConst this_val, int magic) {
  JsAudioNode* w = any_audio_node(this_val);
  if(!w)
    return JS_EXCEPTION;
  return JS_NewInt32(ctx, magic == 0 ? w->node->numberOfInputs() : w->node->numberOfOutputs());
}

static const JSCFunctionListEntry js_biquadfilternode_funcs[] = {
    JS_CGETSET_MAGIC_DEF("type", js_biquadfilter_get, js_biquadfilter_set, BQ_PROP_TYPE),
    JS_CGETSET_MAGIC_DEF("frequency", js_biquadfilter_get, 0, BQ_PROP_FREQUENCY),
    JS_CGETSET_MAGIC_DEF("Q", js_biquadfilter_get, 0, BQ_PROP_Q),
    JS_CGETSET_MAGIC_DEF("detune", js_biquadfilter_get, 0, BQ_PROP_DETUNE),
    JS_CGETSET_MAGIC_DEF("gain", js_biquadfilter_get, 0, BQ_PROP_GAIN),
    JS_CGETSET_MAGIC_DEF("numberOfInputs", js_audionode_io_count, 0, 0),
    JS_CGETSET_MAGIC_DEF("numberOfOutputs", js_audionode_io_count, 0, 1),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "BiquadFilterNode", JS_PROP_CONFIGURABLE),
};

/* ---------- AudioParam ---------- */

static JSValue
js_audioparam_get_value(JSContext* ctx, JSValueConst this_val) {
  JsAudioParam* w = static_cast<JsAudioParam*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id));
  if(!w)
    return JS_EXCEPTION;
  return JS_NewFloat64(ctx, w->param->value());
}

static JSValue
js_audioparam_set_value(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  JsAudioParam* w = static_cast<JsAudioParam*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id));
  if(!w)
    return JS_EXCEPTION;
  double f;
  if(JS_ToFloat64(ctx, &f, value))
    return JS_EXCEPTION;
  w->param->setValue((float)f);
  return JS_UNDEFINED;
}

enum {
  AP_METHOD_SET_VALUE_AT_TIME,
  AP_METHOD_LINEAR_RAMP,
  AP_METHOD_EXPONENTIAL_RAMP,
  AP_METHOD_SET_TARGET_AT_TIME,
  AP_METHOD_CANCEL_SCHEDULED,
};

static JSValue
js_audioparam_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JsAudioParam* w = static_cast<JsAudioParam*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id));
  if(!w)
    return JS_EXCEPTION;

  double a = 0, b = 0, c = 0;
  switch(magic) {
    case AP_METHOD_CANCEL_SCHEDULED: {
      if(argc < 1)
        return JS_ThrowTypeError(ctx, "cancelScheduledValues requires (startTime)");
      if(JS_ToFloat64(ctx, &a, argv[0]))
        return JS_EXCEPTION;
      w->param->cancelScheduledValues((float)a);
      break;
    }
    case AP_METHOD_SET_VALUE_AT_TIME:
    case AP_METHOD_LINEAR_RAMP:
    case AP_METHOD_EXPONENTIAL_RAMP: {
      if(argc < 2)
        return JS_ThrowTypeError(ctx, "method requires (value, time)");
      if(JS_ToFloat64(ctx, &a, argv[0]))
        return JS_EXCEPTION;
      if(JS_ToFloat64(ctx, &b, argv[1]))
        return JS_EXCEPTION;
      if(magic == AP_METHOD_SET_VALUE_AT_TIME)
        w->param->setValueAtTime((float)a, (float)b);
      else if(magic == AP_METHOD_LINEAR_RAMP)
        w->param->linearRampToValueAtTime((float)a, (float)b);
      else
        w->param->exponentialRampToValueAtTime((float)a, (float)b);
      break;
    }
    case AP_METHOD_SET_TARGET_AT_TIME: {
      if(argc < 3)
        return JS_ThrowTypeError(ctx, "setTargetAtTime requires (target, time, timeConstant)");
      if(JS_ToFloat64(ctx, &a, argv[0]))
        return JS_EXCEPTION;
      if(JS_ToFloat64(ctx, &b, argv[1]))
        return JS_EXCEPTION;
      if(JS_ToFloat64(ctx, &c, argv[2]))
        return JS_EXCEPTION;
      w->param->setTargetAtTime((float)a, (float)b, (float)c);
      break;
    }
  }
  return JS_DupValue(ctx, this_val);
}

enum {
  AP_PROP_NAME,
  AP_PROP_MIN,
  AP_PROP_MAX,
  AP_PROP_DEFAULT,
};

static JSValue
js_audioparam_get_meta(JSContext* ctx, JSValueConst this_val, int magic) {
  JsAudioParam* w = static_cast<JsAudioParam*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id));
  if(!w)
    return JS_EXCEPTION;
  switch(magic) {
    case AP_PROP_NAME: return JS_NewString(ctx, w->param->name().c_str());
    case AP_PROP_MIN: return JS_NewFloat64(ctx, w->param->minValue());
    case AP_PROP_MAX: return JS_NewFloat64(ctx, w->param->maxValue());
    case AP_PROP_DEFAULT: return JS_NewFloat64(ctx, w->param->defaultValue());
  }
  return JS_UNDEFINED;
}

static void
js_audioparam_finalizer(JSRuntime* rt, JSValue val) {
  JsAudioParam* w = static_cast<JsAudioParam*>(JS_GetOpaque(val, js_audioparam_class_id));
  if(w) {
    w->~JsAudioParam();
    js_free_rt(rt, w);
  }
}

static JSClassDef js_audioparam_class = {
    .class_name = "AudioParam",
    .finalizer = js_audioparam_finalizer,
};

static const JSCFunctionListEntry js_audioparam_funcs[] = {
    JS_CGETSET_DEF("value", js_audioparam_get_value, js_audioparam_set_value),
    JS_CGETSET_MAGIC_DEF("name", js_audioparam_get_meta, 0, AP_PROP_NAME),
    JS_CGETSET_MAGIC_DEF("minValue", js_audioparam_get_meta, 0, AP_PROP_MIN),
    JS_CGETSET_MAGIC_DEF("maxValue", js_audioparam_get_meta, 0, AP_PROP_MAX),
    JS_CGETSET_MAGIC_DEF("defaultValue", js_audioparam_get_meta, 0, AP_PROP_DEFAULT),
    JS_CFUNC_MAGIC_DEF("setValueAtTime", 2, js_audioparam_method, AP_METHOD_SET_VALUE_AT_TIME),
    JS_CFUNC_MAGIC_DEF("linearRampToValueAtTime", 2, js_audioparam_method, AP_METHOD_LINEAR_RAMP),
    JS_CFUNC_MAGIC_DEF("exponentialRampToValueAtTime", 2, js_audioparam_method, AP_METHOD_EXPONENTIAL_RAMP),
    JS_CFUNC_MAGIC_DEF("setTargetAtTime", 3, js_audioparam_method, AP_METHOD_SET_TARGET_AT_TIME),
    JS_CFUNC_MAGIC_DEF("cancelScheduledValues", 1, js_audioparam_method, AP_METHOD_CANCEL_SCHEDULED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioParam", JS_PROP_CONFIGURABLE),
};

/* ---------- AudioListener (kept minimal) ---------- */

static JSValue
js_audiolistener_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  auto* sal = static_cast<AudioListenerPtr*>(js_mallocz(ctx, sizeof(AudioListenerPtr)));
  new(sal) AudioListenerPtr(std::make_shared<lab::AudioListener>());

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiolistener_proto);
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiolistener_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, sal);
  return obj;

fail:
  sal->~AudioListenerPtr();
  js_free(ctx, sal);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_audiolistener_finalizer(JSRuntime* rt, JSValue val) {
  AudioListenerPtr* sal = static_cast<AudioListenerPtr*>(JS_GetOpaque(val, js_audiolistener_class_id));
  if(sal) {
    sal->~AudioListenerPtr();
    js_free_rt(rt, sal);
  }
}

static JSClassDef js_audiolistener_class = {
    .class_name = "AudioListener",
    .finalizer = js_audiolistener_finalizer,
};

static const JSCFunctionListEntry js_audiolistener_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioListener", JS_PROP_CONFIGURABLE),
};

/* ---------- AudioDevice (kept) ---------- */

static JSValue
js_audiodevice_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  auto cfg = get_default_device_config();
  auto* sad = static_cast<AudioDevicePtr*>(js_mallocz(ctx, sizeof(AudioDevicePtr)));
  new(sad) AudioDevicePtr(std::make_shared<lab::AudioDevice_RtAudio>(cfg.first, cfg.second));

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiodevice_proto);
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiodevice_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, sad);
  return obj;

fail:
  sad->~AudioDevicePtr();
  js_free(ctx, sad);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum { DEVICE_DESTINATION_NODE };

static JSValue
js_audiodevice_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioDevicePtr* sad = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodevice_class_id));
  if(!sad)
    return JS_EXCEPTION;
  if(magic == DEVICE_DESTINATION_NODE) {
    JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, value, js_audiodestinationnode_class_id));
    if(!w)
      return JS_ThrowInternalError(ctx, "value must be AudioDestinationNode");
    auto dest = std::dynamic_pointer_cast<lab::AudioDestinationNode>(w->node);
    if(!dest)
      return JS_ThrowInternalError(ctx, "value must be AudioDestinationNode");
    (*sad)->setDestinationNode(dest);
  }
  return JS_UNDEFINED;
}

static void
js_audiodevice_finalizer(JSRuntime* rt, JSValue val) {
  AudioDevicePtr* sad = static_cast<AudioDevicePtr*>(JS_GetOpaque(val, js_audiodevice_class_id));
  if(sad) {
    sad->~AudioDevicePtr();
    js_free_rt(rt, sad);
  }
}

static JSClassDef js_audiodevice_class = {
    .class_name = "AudioDevice",
    .finalizer = js_audiodevice_finalizer,
};

static const JSCFunctionListEntry js_audiodevice_funcs[] = {
    JS_CGETSET_MAGIC_DEF("destinationNode", 0, js_audiodevice_set, DEVICE_DESTINATION_NODE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDevice", JS_PROP_CONFIGURABLE),
};

/* ---------- AudioBuffer ---------- */

enum {
  AB_PROP_DURATION,
  AB_PROP_SAMPLERATE,
  AB_PROP_NUMCHANNELS,
  AB_PROP_LENGTH,
};

static JSValue
js_audiobuffer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsAudioBuffer* w = static_cast<JsAudioBuffer*>(JS_GetOpaque2(ctx, this_val, js_audiobuffer_class_id));
  if(!w || !w->bus)
    return JS_EXCEPTION;
  switch(magic) {
    case AB_PROP_DURATION: return JS_NewFloat64(ctx, double(w->bus->length()) / w->bus->sampleRate());
    case AB_PROP_SAMPLERATE: return JS_NewFloat64(ctx, w->bus->sampleRate());
    case AB_PROP_NUMCHANNELS: return JS_NewInt32(ctx, w->bus->numberOfChannels());
    case AB_PROP_LENGTH: return JS_NewInt32(ctx, w->bus->length());
  }
  return JS_UNDEFINED;
}

static void
js_audiobuffer_finalizer(JSRuntime* rt, JSValue val) {
  JsAudioBuffer* w = static_cast<JsAudioBuffer*>(JS_GetOpaque(val, js_audiobuffer_class_id));
  if(w) {
    w->~JsAudioBuffer();
    js_free_rt(rt, w);
  }
}

static JSClassDef js_audiobuffer_class = {
    .class_name = "AudioBuffer",
    .finalizer = js_audiobuffer_finalizer,
};

static const JSCFunctionListEntry js_audiobuffer_funcs[] = {
    JS_CGETSET_MAGIC_DEF("duration", js_audiobuffer_get, 0, AB_PROP_DURATION),
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiobuffer_get, 0, AB_PROP_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("numberOfChannels", js_audiobuffer_get, 0, AB_PROP_NUMCHANNELS),
    JS_CGETSET_MAGIC_DEF("length", js_audiobuffer_get, 0, AB_PROP_LENGTH),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioBuffer", JS_PROP_CONFIGURABLE),
};

/* ---------- AudioBufferSourceNode (lab::SampledAudioNode) ---------- */

static JSValue
js_absource_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "AudioBufferSourceNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;

  auto src = std::make_shared<lab::SampledAudioNode>(*ac);

  bool wantLoop = false;
  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v;

    v = JS_GetPropertyStr(ctx, argv[1], "buffer");
    if(!JS_IsUndefined(v) && !JS_IsNull(v)) {
      JsAudioBuffer* ab = static_cast<JsAudioBuffer*>(JS_GetOpaque2(ctx, v, js_audiobuffer_class_id));
      if(ab && ab->bus)
        src->setBus(ab->bus);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "loop");
    if(!JS_IsUndefined(v))
      wantLoop = JS_ToBool(ctx, v);
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "playbackRate");
    if(JS_IsNumber(v)) {
      double d; JS_ToFloat64(ctx, &d, v);
      src->playbackRate()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "detune");
    if(JS_IsNumber(v)) {
      double d; JS_ToFloat64(ctx, &d, v);
      src->detune()->setValue((float)d);
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, audiobuffersourcenode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_audiobuffersourcenode_class_id, std::static_pointer_cast<lab::AudioNode>(src), ac);
  JS_FreeValue(ctx, proto);
  if(wantLoop)
    JS_SetPropertyStr(ctx, obj, "loop", JS_TRUE);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_absource_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_audiobuffersourcenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto src = std::dynamic_pointer_cast<lab::SampledAudioNode>(w->node);
  if(!src)
    return JS_ThrowInternalError(ctx, "not a SampledAudioNode");

  double when = 0, offset = 0;
  if(argc > 0)
    JS_ToFloat64(ctx, &when, argv[0]);
  if(argc > 1)
    JS_ToFloat64(ctx, &offset, argv[1]);

  bool wantLoop = false;
  JSValue lv = JS_GetPropertyStr(ctx, this_val, "loop");
  if(!JS_IsUndefined(lv))
    wantLoop = JS_ToBool(ctx, lv);
  JS_FreeValue(ctx, lv);
  int loopCount = wantLoop ? -1 : 0;

  if(offset > 0)
    src->start((float)when, (float)offset, loopCount);
  else
    src->start((float)when, loopCount);
  return JS_UNDEFINED;
}

static JSValue
js_absource_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_audiobuffersourcenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto src = std::dynamic_pointer_cast<lab::SampledAudioNode>(w->node);
  if(!src)
    return JS_ThrowInternalError(ctx, "not a SampledAudioNode");
  double when = 0;
  if(argc > 0)
    JS_ToFloat64(ctx, &when, argv[0]);
  src->stop((float)when);
  return JS_UNDEFINED;
}

enum {
  ABSRC_PROP_PLAYBACKRATE,
  ABSRC_PROP_DETUNE,
};

static JSValue
js_absource_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_audiobuffersourcenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto src = std::dynamic_pointer_cast<lab::SampledAudioNode>(w->node);
  if(!src)
    return JS_ThrowInternalError(ctx, "not a SampledAudioNode");
  switch(magic) {
    case ABSRC_PROP_PLAYBACKRATE: return make_audio_param_js(ctx, src->playbackRate());
    case ABSRC_PROP_DETUNE: return make_audio_param_js(ctx, src->detune());
  }
  return JS_UNDEFINED;
}

static void
js_audiobuffersourcenode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_audiobuffersourcenode_class_id);
}

static JSClassDef js_audiobuffersourcenode_class = {
    .class_name = "AudioBufferSourceNode",
    .finalizer = js_audiobuffersourcenode_finalizer,
};

static const JSCFunctionListEntry js_audiobuffersourcenode_funcs[] = {
    JS_CFUNC_DEF("start", 0, js_absource_start),
    JS_CFUNC_DEF("stop", 0, js_absource_stop),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_CGETSET_MAGIC_DEF("playbackRate", js_absource_get, 0, ABSRC_PROP_PLAYBACKRATE),
    JS_CGETSET_MAGIC_DEF("detune", js_absource_get, 0, ABSRC_PROP_DETUNE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioBufferSourceNode", JS_PROP_CONFIGURABLE),
};

/* ---------- NoiseNode ---------- */

static lab::NoiseNode::NoiseType
parse_noise_type(const char* s) {
  if(!s)
    return lab::NoiseNode::WHITE;
  if(!strcmp(s, "white")) return lab::NoiseNode::WHITE;
  if(!strcmp(s, "pink")) return lab::NoiseNode::PINK;
  if(!strcmp(s, "brown")) return lab::NoiseNode::BROWN;
  return lab::NoiseNode::WHITE;
}

static const char*
noise_type_to_string(lab::NoiseNode::NoiseType t) {
  switch(t) {
    case lab::NoiseNode::PINK: return "pink";
    case lab::NoiseNode::BROWN: return "brown";
    case lab::NoiseNode::WHITE:
    default: return "white";
  }
}

static JSValue
js_noise_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "NoiseNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;
  auto n = std::make_shared<lab::NoiseNode>(*ac);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v = JS_GetPropertyStr(ctx, argv[1], "type");
    if(!JS_IsUndefined(v) && !JS_IsException(v)) {
      const char* s = JS_ToCString(ctx, v);
      if(s) {
        n->setType(parse_noise_type(s));
        JS_FreeCString(ctx, s);
      }
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, noisenode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_noisenode_class_id, std::static_pointer_cast<lab::AudioNode>(n), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_noise_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_noisenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto n = std::dynamic_pointer_cast<lab::NoiseNode>(w->node);
  if(!n)
    return JS_ThrowInternalError(ctx, "not a NoiseNode");
  float when = 0.0f;
  if(argc > 0) {
    double t; if(JS_ToFloat64(ctx, &t, argv[0])) return JS_EXCEPTION;
    when = (float)t;
  }
  n->start(when);
  return JS_UNDEFINED;
}

static JSValue
js_noise_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_noisenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto n = std::dynamic_pointer_cast<lab::NoiseNode>(w->node);
  if(!n)
    return JS_ThrowInternalError(ctx, "not a NoiseNode");
  float when = 0.0f;
  if(argc > 0) {
    double t; if(JS_ToFloat64(ctx, &t, argv[0])) return JS_EXCEPTION;
    when = (float)t;
  }
  n->stop(when);
  return JS_UNDEFINED;
}

static JSValue
js_noise_get_type(JSContext* ctx, JSValueConst this_val) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_noisenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto n = std::dynamic_pointer_cast<lab::NoiseNode>(w->node);
  if(!n)
    return JS_ThrowInternalError(ctx, "not a NoiseNode");
  return JS_NewString(ctx, noise_type_to_string(n->type()));
}

static JSValue
js_noise_set_type(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_noisenode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto n = std::dynamic_pointer_cast<lab::NoiseNode>(w->node);
  if(!n)
    return JS_ThrowInternalError(ctx, "not a NoiseNode");
  const char* s = JS_ToCString(ctx, value);
  if(s) {
    n->setType(parse_noise_type(s));
    JS_FreeCString(ctx, s);
  }
  return JS_UNDEFINED;
}

static void
js_noisenode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_noisenode_class_id);
}

static JSClassDef js_noisenode_class = {
    .class_name = "NoiseNode",
    .finalizer = js_noisenode_finalizer,
};

static const JSCFunctionListEntry js_noisenode_funcs[] = {
    JS_CFUNC_DEF("start", 0, js_noise_start),
    JS_CFUNC_DEF("stop", 0, js_noise_stop),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_CGETSET_DEF("type", js_noise_get_type, js_noise_set_type),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "NoiseNode", JS_PROP_CONFIGURABLE),
};

/* ---------- AudioSetting (delayTime shim) ---------- */

static JSValue
js_audiosetting_get_value(JSContext* ctx, JSValueConst this_val) {
  JsAudioSetting* w = static_cast<JsAudioSetting*>(JS_GetOpaque2(ctx, this_val, js_audiosetting_class_id));
  if(!w || !w->setting)
    return JS_EXCEPTION;
  return JS_NewFloat64(ctx, w->setting->valueFloat());
}

static JSValue
js_audiosetting_set_value(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  JsAudioSetting* w = static_cast<JsAudioSetting*>(JS_GetOpaque2(ctx, this_val, js_audiosetting_class_id));
  if(!w || !w->setting)
    return JS_EXCEPTION;
  double d;
  if(JS_ToFloat64(ctx, &d, value))
    return JS_EXCEPTION;
  w->setting->setFloat(static_cast<float>(d));
  return JS_UNDEFINED;
}

// All AudioParam-like methods collapse to immediate setFloat. AudioSetting
// has no scheduler in lab, so the time arg is ignored. This keeps scripts
// that use setValueAtTime / linearRamp on DelayNode.delayTime running cleanly
// in qjs even though they only get the final value, not a smooth ramp.
static JSValue
js_audiosetting_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsAudioSetting* w = static_cast<JsAudioSetting*>(JS_GetOpaque2(ctx, this_val, js_audiosetting_class_id));
  if(!w || !w->setting)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_DupValue(ctx, this_val);
  double d;
  if(JS_ToFloat64(ctx, &d, argv[0]))
    return JS_EXCEPTION;
  w->setting->setFloat(static_cast<float>(d));
  return JS_DupValue(ctx, this_val);
}

static void
js_audiosetting_finalizer(JSRuntime* rt, JSValue val) {
  JsAudioSetting* w = static_cast<JsAudioSetting*>(JS_GetOpaque(val, js_audiosetting_class_id));
  if(w) {
    w->~JsAudioSetting();
    js_free_rt(rt, w);
  }
}

static JSClassDef js_audiosetting_class = {
    .class_name = "AudioParam",
    .finalizer = js_audiosetting_finalizer,
};

static const JSCFunctionListEntry js_audiosetting_funcs[] = {
    JS_CGETSET_DEF("value", js_audiosetting_get_value, js_audiosetting_set_value),
    JS_CFUNC_DEF("setValueAtTime", 2, js_audiosetting_method),
    JS_CFUNC_DEF("linearRampToValueAtTime", 2, js_audiosetting_method),
    JS_CFUNC_DEF("exponentialRampToValueAtTime", 2, js_audiosetting_method),
    JS_CFUNC_DEF("setTargetAtTime", 3, js_audiosetting_method),
    JS_CFUNC_DEF("cancelScheduledValues", 1, js_audiosetting_method),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioParam", JS_PROP_CONFIGURABLE),
};

/* ---------- DelayNode ---------- */

static JSValue
js_delay_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "DelayNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;

  double maxDelay = 2.0;
  double initialDelay = 0.0;
  bool haveInitial = false;
  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v = JS_GetPropertyStr(ctx, argv[1], "maxDelayTime");
    if(JS_IsNumber(v))
      JS_ToFloat64(ctx, &maxDelay, v);
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "delayTime");
    if(JS_IsNumber(v)) {
      JS_ToFloat64(ctx, &initialDelay, v);
      haveInitial = true;
    }
    JS_FreeValue(ctx, v);
  }

  auto d = std::make_shared<lab::DelayNode>(*ac, maxDelay);

  if(d->numberOfOutputs() == 0) {
    lab::ContextGraphLock gLock(ac.get(), "DelayNode.addOutput");
    d->addOutput(gLock, std::unique_ptr<lab::AudioNodeOutput>(new lab::AudioNodeOutput(d.get(), 2)));
  }

  if(haveInitial)
    d->delayTime()->setFloat(static_cast<float>(initialDelay));

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, delaynode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_delaynode_class_id, std::static_pointer_cast<lab::AudioNode>(d), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_delay_get_delaytime(JSContext* ctx, JSValueConst this_val) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_delaynode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto d = std::dynamic_pointer_cast<lab::DelayNode>(w->node);
  if(!d)
    return JS_ThrowInternalError(ctx, "not a DelayNode");
  return make_audio_setting_js(ctx, d->delayTime());
}

static void
js_delaynode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_delaynode_class_id);
}

static JSClassDef js_delaynode_class = {
    .class_name = "DelayNode",
    .finalizer = js_delaynode_finalizer,
};

static const JSCFunctionListEntry js_delaynode_funcs[] = {
    JS_CGETSET_DEF("delayTime", js_delay_get_delaytime, NULL),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "DelayNode", JS_PROP_CONFIGURABLE),
};

/* ---------- WaveShaperNode ---------- */

static lab::OverSampleType
parse_oversample(const char* s) {
  if(!s)
    return lab::OverSampleType::NONE;
  if(!strcmp(s, "none")) return lab::OverSampleType::NONE;
  if(!strcmp(s, "2x"))   return lab::OverSampleType::_2X;
  if(!strcmp(s, "4x"))   return lab::OverSampleType::_4X;
  return lab::OverSampleType::NONE;
}

static const char*
oversample_to_string(lab::OverSampleType t) {
  switch(t) {
    case lab::OverSampleType::_2X: return "2x";
    case lab::OverSampleType::_4X: return "4x";
    case lab::OverSampleType::NONE:
    default: return "none";
  }
}

static JSValue
js_waveshaper_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "WaveShaperNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;
  auto ws = std::make_shared<lab::WaveShaperNode>(*ac);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v;

    v = JS_GetPropertyStr(ctx, argv[1], "curve");
    if(!JS_IsUndefined(v) && !JS_IsNull(v)) {
      std::vector<float> curve;
      if(read_float_array(ctx, v, curve) == 0 && !curve.empty())
        ws->setCurve(curve);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[1], "oversample");
    if(!JS_IsUndefined(v) && !JS_IsException(v)) {
      const char* s = JS_ToCString(ctx, v);
      if(s) {
        ws->setOversample(parse_oversample(s));
        JS_FreeCString(ctx, s);
      }
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, waveshapernode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_waveshapernode_class_id, std::static_pointer_cast<lab::AudioNode>(ws), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_waveshaper_get_oversample(JSContext* ctx, JSValueConst this_val) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_waveshapernode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto ws = std::dynamic_pointer_cast<lab::WaveShaperNode>(w->node);
  if(!ws)
    return JS_ThrowInternalError(ctx, "not a WaveShaperNode");
  return JS_NewString(ctx, oversample_to_string(ws->oversample()));
}

static JSValue
js_waveshaper_set_oversample(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_waveshapernode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto ws = std::dynamic_pointer_cast<lab::WaveShaperNode>(w->node);
  if(!ws)
    return JS_ThrowInternalError(ctx, "not a WaveShaperNode");
  const char* s = JS_ToCString(ctx, value);
  if(s) {
    ws->setOversample(parse_oversample(s));
    JS_FreeCString(ctx, s);
  }
  return JS_UNDEFINED;
}

static JSValue
js_waveshaper_set_curve(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_waveshapernode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto ws = std::dynamic_pointer_cast<lab::WaveShaperNode>(w->node);
  if(!ws)
    return JS_ThrowInternalError(ctx, "not a WaveShaperNode");
  std::vector<float> curve;
  if(read_float_array(ctx, value, curve) == 0 && !curve.empty())
    ws->setCurve(curve);
  return JS_UNDEFINED;
}

static void
js_waveshapernode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_waveshapernode_class_id);
}

static JSClassDef js_waveshapernode_class = {
    .class_name = "WaveShaperNode",
    .finalizer = js_waveshapernode_finalizer,
};

static const JSCFunctionListEntry js_waveshapernode_funcs[] = {
    JS_CGETSET_DEF("oversample", js_waveshaper_get_oversample, js_waveshaper_set_oversample),
    JS_CGETSET_DEF("curve", NULL, js_waveshaper_set_curve),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WaveShaperNode", JS_PROP_CONFIGURABLE),
};

/* ---------- StereoPannerNode ---------- */

static JSValue
js_stereopanner_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "StereoPannerNode requires an AudioContext");
  AudioContextPtr* acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id));
  if(!acptr)
    return JS_EXCEPTION;
  AudioContextPtr ac = *acptr;
  auto sp = std::make_shared<lab::StereoPannerNode>(*ac);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue v = JS_GetPropertyStr(ctx, argv[1], "pan");
    if(JS_IsNumber(v)) {
      double d; JS_ToFloat64(ctx, &d, v);
      sp->pan()->setValue(static_cast<float>(d));
    }
    JS_FreeValue(ctx, v);
  }

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;
  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stereopannernode_proto);
  }
  JSValue obj = make_audio_node_js(ctx, proto, js_stereopannernode_class_id, std::static_pointer_cast<lab::AudioNode>(sp), ac);
  JS_FreeValue(ctx, proto);
  anchor_node_in_context(ctx, argv[0], obj);
  return obj;
}

static JSValue
js_stereopanner_get_pan(JSContext* ctx, JSValueConst this_val) {
  JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_stereopannernode_class_id));
  if(!w)
    return JS_EXCEPTION;
  auto sp = std::dynamic_pointer_cast<lab::StereoPannerNode>(w->node);
  if(!sp)
    return JS_ThrowInternalError(ctx, "not a StereoPannerNode");
  return make_audio_param_js(ctx, sp->pan());
}

static void
js_stereopannernode_finalizer(JSRuntime* rt, JSValue val) {
  js_audionode_finalize_with(rt, val, js_stereopannernode_class_id);
}

static JSClassDef js_stereopannernode_class = {
    .class_name = "StereoPannerNode",
    .finalizer = js_stereopannernode_finalizer,
};

static const JSCFunctionListEntry js_stereopannernode_funcs[] = {
    JS_CGETSET_DEF("pan", js_stereopanner_get_pan, NULL),
    JS_CFUNC_DEF("connect", 1, js_audionode_connect),
    JS_CFUNC_DEF("disconnect", 0, js_audionode_disconnect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StereoPannerNode", JS_PROP_CONFIGURABLE),
};

/* ---------- module init ---------- */

int
js_labsound_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_audiocontext_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiocontext_class_id, &js_audiocontext_class);
  audiocontext_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiocontext_proto, js_audiocontext_funcs, countof(js_audiocontext_funcs));
  JS_SetClassProto(ctx, js_audiocontext_class_id, audiocontext_proto);
  audiocontext_ctor = JS_NewCFunction2(ctx, js_audiocontext_constructor, "AudioContext", 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, audiocontext_ctor, audiocontext_proto);

  JS_NewClassID(&js_audiodestinationnode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiodestinationnode_class_id, &js_audiodestinationnode_class);
  audiodestinationnode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiodestinationnode_proto, js_audiodestinationnode_funcs, countof(js_audiodestinationnode_funcs));
  JS_SetClassProto(ctx, js_audiodestinationnode_class_id, audiodestinationnode_proto);
  audiodestinationnode_ctor = JS_NewCFunction2(ctx, js_audiodestinationnode_constructor, "AudioDestinationNode", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, audiodestinationnode_ctor, audiodestinationnode_proto);

  JS_NewClassID(&js_audiolistener_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiolistener_class_id, &js_audiolistener_class);
  audiolistener_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiolistener_proto, js_audiolistener_funcs, countof(js_audiolistener_funcs));
  JS_SetClassProto(ctx, js_audiolistener_class_id, audiolistener_proto);
  audiolistener_ctor = JS_NewCFunction2(ctx, js_audiolistener_constructor, "AudioListener", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, audiolistener_ctor, audiolistener_proto);

  JS_NewClassID(&js_audiodevice_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiodevice_class_id, &js_audiodevice_class);
  audiodevice_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiodevice_proto, js_audiodevice_funcs, countof(js_audiodevice_funcs));
  JS_SetClassProto(ctx, js_audiodevice_class_id, audiodevice_proto);
  audiodevice_ctor = JS_NewCFunction2(ctx, js_audiodevice_constructor, "AudioDevice", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, audiodevice_ctor, audiodevice_proto);

  JS_NewClassID(&js_oscillatornode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_oscillatornode_class_id, &js_oscillatornode_class);
  oscillatornode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, oscillatornode_proto, js_oscillatornode_funcs, countof(js_oscillatornode_funcs));
  JS_SetClassProto(ctx, js_oscillatornode_class_id, oscillatornode_proto);
  oscillatornode_ctor = JS_NewCFunction2(ctx, js_oscillator_constructor, "OscillatorNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, oscillatornode_ctor, oscillatornode_proto);

  JS_NewClassID(&js_gainnode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_gainnode_class_id, &js_gainnode_class);
  gainnode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, gainnode_proto, js_gainnode_funcs, countof(js_gainnode_funcs));
  JS_SetClassProto(ctx, js_gainnode_class_id, gainnode_proto);
  gainnode_ctor = JS_NewCFunction2(ctx, js_gain_constructor, "GainNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, gainnode_ctor, gainnode_proto);

  JS_NewClassID(&js_biquadfilternode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_biquadfilternode_class_id, &js_biquadfilternode_class);
  biquadfilternode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, biquadfilternode_proto, js_biquadfilternode_funcs, countof(js_biquadfilternode_funcs));
  JS_SetClassProto(ctx, js_biquadfilternode_class_id, biquadfilternode_proto);
  biquadfilternode_ctor = JS_NewCFunction2(ctx, js_biquadfilter_constructor, "BiquadFilterNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, biquadfilternode_ctor, biquadfilternode_proto);

  JS_NewClassID(&js_audiobuffer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiobuffer_class_id, &js_audiobuffer_class);
  audiobuffer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiobuffer_proto, js_audiobuffer_funcs, countof(js_audiobuffer_funcs));
  JS_SetClassProto(ctx, js_audiobuffer_class_id, audiobuffer_proto);

  JS_NewClassID(&js_audiobuffersourcenode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiobuffersourcenode_class_id, &js_audiobuffersourcenode_class);
  audiobuffersourcenode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiobuffersourcenode_proto, js_audiobuffersourcenode_funcs, countof(js_audiobuffersourcenode_funcs));
  JS_SetClassProto(ctx, js_audiobuffersourcenode_class_id, audiobuffersourcenode_proto);
  audiobuffersourcenode_ctor = JS_NewCFunction2(ctx, js_absource_constructor, "AudioBufferSourceNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, audiobuffersourcenode_ctor, audiobuffersourcenode_proto);

  JS_NewClassID(&js_noisenode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_noisenode_class_id, &js_noisenode_class);
  noisenode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, noisenode_proto, js_noisenode_funcs, countof(js_noisenode_funcs));
  JS_SetClassProto(ctx, js_noisenode_class_id, noisenode_proto);
  noisenode_ctor = JS_NewCFunction2(ctx, js_noise_constructor, "NoiseNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, noisenode_ctor, noisenode_proto);

  JS_NewClassID(&js_delaynode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_delaynode_class_id, &js_delaynode_class);
  delaynode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, delaynode_proto, js_delaynode_funcs, countof(js_delaynode_funcs));
  JS_SetClassProto(ctx, js_delaynode_class_id, delaynode_proto);
  delaynode_ctor = JS_NewCFunction2(ctx, js_delay_constructor, "DelayNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, delaynode_ctor, delaynode_proto);

  JS_NewClassID(&js_waveshapernode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_waveshapernode_class_id, &js_waveshapernode_class);
  waveshapernode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, waveshapernode_proto, js_waveshapernode_funcs, countof(js_waveshapernode_funcs));
  JS_SetClassProto(ctx, js_waveshapernode_class_id, waveshapernode_proto);
  waveshapernode_ctor = JS_NewCFunction2(ctx, js_waveshaper_constructor, "WaveShaperNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, waveshapernode_ctor, waveshapernode_proto);

  JS_NewClassID(&js_stereopannernode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stereopannernode_class_id, &js_stereopannernode_class);
  stereopannernode_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stereopannernode_proto, js_stereopannernode_funcs, countof(js_stereopannernode_funcs));
  JS_SetClassProto(ctx, js_stereopannernode_class_id, stereopannernode_proto);
  stereopannernode_ctor = JS_NewCFunction2(ctx, js_stereopanner_constructor, "StereoPannerNode", 2, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, stereopannernode_ctor, stereopannernode_proto);

  JS_NewClassID(&js_audiosetting_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiosetting_class_id, &js_audiosetting_class);
  audiosetting_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audiosetting_proto, js_audiosetting_funcs, countof(js_audiosetting_funcs));
  JS_SetClassProto(ctx, js_audiosetting_class_id, audiosetting_proto);

  JS_NewClassID(&js_audioparam_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audioparam_class_id, &js_audioparam_class);
  audioparam_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, audioparam_proto, js_audioparam_funcs, countof(js_audioparam_funcs));
  JS_SetClassProto(ctx, js_audioparam_class_id, audioparam_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "AudioContext", audiocontext_ctor);
    JS_SetModuleExport(ctx, m, "AudioDestinationNode", audiodestinationnode_ctor);
    JS_SetModuleExport(ctx, m, "AudioListener", audiolistener_ctor);
    JS_SetModuleExport(ctx, m, "AudioDevice", audiodevice_ctor);
    JS_SetModuleExport(ctx, m, "OscillatorNode", oscillatornode_ctor);
    JS_SetModuleExport(ctx, m, "GainNode", gainnode_ctor);
    JS_SetModuleExport(ctx, m, "BiquadFilterNode", biquadfilternode_ctor);
    JS_SetModuleExport(ctx, m, "AudioBufferSourceNode", audiobuffersourcenode_ctor);
    JS_SetModuleExport(ctx, m, "NoiseNode", noisenode_ctor);
    JS_SetModuleExport(ctx, m, "DelayNode", delaynode_ctor);
    JS_SetModuleExport(ctx, m, "WaveShaperNode", waveshapernode_ctor);
    JS_SetModuleExport(ctx, m, "StereoPannerNode", stereopannernode_ctor);
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_labsound(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "AudioContext");
  JS_AddModuleExport(ctx, m, "AudioDestinationNode");
  JS_AddModuleExport(ctx, m, "AudioListener");
  JS_AddModuleExport(ctx, m, "AudioDevice");
  JS_AddModuleExport(ctx, m, "OscillatorNode");
  JS_AddModuleExport(ctx, m, "GainNode");
  JS_AddModuleExport(ctx, m, "BiquadFilterNode");
  JS_AddModuleExport(ctx, m, "AudioBufferSourceNode");
  JS_AddModuleExport(ctx, m, "NoiseNode");
  JS_AddModuleExport(ctx, m, "DelayNode");
  JS_AddModuleExport(ctx, m, "WaveShaperNode");
  JS_AddModuleExport(ctx, m, "StereoPannerNode");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if((m = JS_NewCModule(ctx, module_name, js_labsound_init))) {
    js_init_module_labsound(ctx, m);
  }
  return m;
}
