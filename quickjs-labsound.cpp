#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include "LabSound/LabSound.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "LabSound/core/AudioNodeOutput.h"
#include "LabSound/extended/AudioContextLock.h"

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
static JSClassID js_audioparam_class_id;

static JSValue audiocontext_proto, audiocontext_ctor;
static JSValue audiodestinationnode_proto, audiodestinationnode_ctor;
static JSValue audiolistener_proto, audiolistener_ctor;
static JSValue audiodevice_proto, audiodevice_ctor;
static JSValue oscillatornode_proto, oscillatornode_ctor;
static JSValue gainnode_proto, gainnode_ctor;
static JSValue biquadfilternode_proto, biquadfilternode_ctor;
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
  return nullptr;
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
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if((m = JS_NewCModule(ctx, module_name, js_labsound_init))) {
    js_init_module_labsound(ctx, m);
  }
  return m;
}
