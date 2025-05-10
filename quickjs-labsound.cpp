#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include "LabSound/LabSound.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "cpputils.h"
#include <array>
#include <map>
#include <vector>
#include <algorithm>

static ClassId js_audiobuffer_class_id, js_audiocontext_class_id, js_audiolistener_class_id, js_audiodevice_class_id, js_audionode_class_id, js_audiodestinationnode_class_id,
    js_audioscheduledsourcenode_class_id, js_oscillatornode_class_id, js_audiosummingjunction_class_id, js_audioparam_class_id;
static JSValue audiobuffer_proto, audiobuffer_ctor, audiocontext_proto, audiocontext_ctor, audiolistener_proto, audiolistener_ctor, audiodevice_proto, audiodevice_ctor, audionode_proto,
    audionode_ctor, audiodestinationnode_proto, audiodestinationnode_ctor, audioscheduledsourcenode_proto, audioscheduledsourcenode_ctor, oscillatornode_proto, oscillatornode_ctor,
    audiosummingjunction_proto, audiosummingjunction_ctor, audioparam_proto, audioparam_ctor;

typedef std::shared_ptr<lab::AudioBus> AudioBufferPtr;
typedef std::shared_ptr<lab::AudioContext> AudioContextPtr;
typedef std::shared_ptr<lab::AudioDestinationNode> AudioDestinationNodePtr;
typedef std::shared_ptr<lab::AudioListener> AudioListenerPtr;
typedef std::shared_ptr<lab::AudioDevice> AudioDevicePtr;
typedef std::shared_ptr<lab::AudioParam> AudioParamPtr;
typedef std::shared_ptr<lab::AudioSummingJunction> AudioSummingJunctionPtr;
typedef ClassPtr<lab::AudioNode> AudioNodePtr;
typedef ClassPtr<lab::AudioScheduledSourceNode> AudioScheduledSourceNodePtr;
typedef ClassPtr<lab::OscillatorNode> OscillatorNodePtr;

typedef ClassPtr<lab::AudioBus, int> AudioChannelPtr;
typedef std::weak_ptr<lab::AudioBus> AudioBufferIndex;

template<> struct std::less<AudioBufferIndex> {
  bool
  operator()(const AudioBufferIndex& a, const AudioBufferIndex& b) const {
    std::shared_ptr<lab::AudioBus> s1(a), s2(b);

    return s1.get() < s2.get();
  }
};

typedef std::map<AudioBufferIndex, std::vector<JSObject*>> ChannelMap;

static ChannelMap channel_map;

static JSValue
js_float32array_ctor(JSContext* ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue f32arr = JS_GetPropertyStr(ctx, global, "Float32Array");
  JS_FreeValue(ctx, global);
  return f32arr;
}

static void
js_audiochannel_free(JSRuntime* rt, void* opaque, void* ptr) {
  AudioChannelPtr* ac = static_cast<AudioChannelPtr*>(opaque);

  ac->~AudioChannelPtr();

  js_free_rt(rt, ac);
}

static std::vector<JSObject*>*
js_audiobuffer_channelobjs(JSContext* ctx, std::shared_ptr<lab::AudioBus>& bus) {
  std::vector<JSObject*>* ret = nullptr;

  for(auto& [k, v] : channel_map) {
    std::shared_ptr<lab::AudioBus> ab(k);

    if(ab.get() == bus.get()) {
      ret = &v;
      break;
    }
  }

  return ret;
}

static JSObject*&
js_audiobuffer_channels(JSContext* ctx, AudioChannelPtr& ac) {
  const auto count = std::erase_if(channel_map, [ctx](const auto& item) {
    const auto& [key, value] = item;
    const bool expired = key.expired();

    if(expired)
      for(JSObject* ptr : value)
        if(ptr)
          JS_FreeValue(ctx, to_js(ptr));

    return expired;
  });

  if(count > 0)
    std::cerr << "Erased " << count << " expired references" << std::endl;

  std::shared_ptr<lab::AudioBus> bus(ac);

  std::vector<JSObject*>* obj;
  const auto len = std::max(ac.value + 1, bus->length());

  if((obj = js_audiobuffer_channelobjs(ctx, bus))) {
    if(obj->size() < len)
      obj->resize(len);
  } else {
    AudioBufferIndex key(ac);

    channel_map.emplace(std::make_pair(key, std::vector<JSObject*>(len, nullptr)));

    obj = &channel_map[key];
  }

  return (*obj)[ac.value];
}

static JSValue
js_audiochannel_create(JSContext* ctx, AudioChannelPtr& ac) {
  AudioChannelPtr* ptr;
  JSValue ret = JS_UNDEFINED;
  JSObject*& obj = js_audiobuffer_channels(ctx, ac);

  if(obj)
    return JS_DupValue(ctx, to_js(obj));

  if(!(ptr = js_malloc<AudioChannelPtr>(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  new(ptr) AudioChannelPtr(ac);

  lab::AudioChannel* ch = (*ptr)->channel(ptr->value);

  JSValue f32arr = js_float32array_ctor(ctx);
  JSValue args[] = {
      JS_NewArrayBuffer(ctx, (uint8_t*)ch->mutableData(), ch->length() * sizeof(float), &js_audiochannel_free, ptr, FALSE),
      JS_NewUint32(ctx, 0),
      JS_NewUint32(ctx, ch->length()),
  };

  ret = JS_CallConstructor(ctx, f32arr, countof(args), args);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
  JS_FreeValue(ctx, f32arr);

  obj = from_js<JSObject*>(JS_DupValue(ctx, ret));

  return ret;
}

static int
js_channel_get(JSContext* ctx, JSValueConst value) {
  static const char* const channel_names[] = {"Left", "Right", "Center", "Mono", "LFE", "SurroundLeft", "SurroundRight", "BackLeft", "BackRight"};
  const char* str;
  int32_t n = -1;

  if((str = JS_ToCString(ctx, value))) {
    if(!strcasecmp(str, "first")) {
      n = 0;
    } else
      for(size_t i = 0; i < countof(channel_names); ++i)
        if(!strcasecmp(str, channel_names[i])) {
          n = i;
          break;
        }

    JS_FreeCString(ctx, str);
  }

  if(n == -1)
    JS_ToInt32(ctx, &n, value);

  return n;
}

static JSValue
js_audiobuffer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  uint64_t length = 0, numberOfChannels = 1;
  double sampleRate = 44100;

  if(argc > 0) {
    JSValue value;
    if(JS_IsObject(argv[0])) {
      if(js_has_property(ctx, argv[0], "length")) {
        value = JS_GetPropertyStr(ctx, argv[0], "length");
        JS_ToIndex(ctx, &length, value);
        JS_FreeValue(ctx, value);
      }

      if(js_has_property(ctx, argv[0], "numberOfChannels")) {
        value = JS_GetPropertyStr(ctx, argv[0], "numberOfChannels");
        JS_ToIndex(ctx, &numberOfChannels, value);
        JS_FreeValue(ctx, value);
      }

      if(js_has_property(ctx, argv[0], "sampleRate")) {
        value = JS_GetPropertyStr(ctx, argv[0], "sampleRate");
        JS_ToFloat64(ctx, &sampleRate, value);
        JS_FreeValue(ctx, value);
      }

      if(length == 0 && sampleRate > 0 && js_has_property(ctx, argv[0], "duration")) {
        double duration;
        value = JS_GetPropertyStr(ctx, argv[0], "duration");
        JS_ToFloat64(ctx, &duration, value);
        length = duration * sampleRate;
        JS_FreeValue(ctx, value);
      }

    } else {
      JS_ToIndex(ctx, &length, argv[0]);

      if(argc > 1)
        JS_ToIndex(ctx, &numberOfChannels, argv[1]);

      if(argc > 2)
        JS_ToFloat64(ctx, &sampleRate, argv[2]);
    }
  }

  AudioBufferPtr* ab = js_malloc<AudioBufferPtr>(ctx);

  new(ab) AudioBufferPtr(std::make_shared<lab::AudioBus>(numberOfChannels, length));

  (*ab)->setSampleRate(sampleRate);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiobuffer_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiobuffer_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ab);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  BUFFER_COPY_FROM_CHANNEL,
  BUFFER_COPY_TO_CHANNEL,
  BUFFER_GET_CHANNEL_DATA,
  BUFFER_TOPOLOGY_MATCHES,
  BUFFER_SCALE,
  BUFFER_RESET,
  BUFFER_COPY_FROM,
  BUFFER_SUM_FROM,
  BUFFER_NORMALIZE,
};

static JSValue
js_audiobuffer_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioBufferPtr* ab;
  JSValue ret = JS_UNDEFINED;

  if(!(ab = js_audiobuffer_class_id.opaque<AudioBufferPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case BUFFER_COPY_FROM_CHANNEL: {
      int ch = js_channel_get(ctx, argv[1]);

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number out of range 0 < ch < %d", (*ab)->numberOfChannels());

      lab::AudioChannel* src = (*ab)->channel(ch);

      size_t offset, length, bytes_per_element;
      JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &length, &bytes_per_element);
      uint8_t* buf;
      size_t size;

      if(!(buf = JS_GetArrayBuffer(ctx, &size, buffer)) || bytes_per_element != sizeof(float)) {
        JS_FreeValue(ctx, buffer);
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");
      }

      JS_FreeValue(ctx, buffer);

      uint32_t start = 0;

      if(argc > 2)
        JS_ToUint32(ctx, &start, argv[2]);

      if(start >= src->length())
        return JS_ThrowRangeError(ctx, "startInChannel %d greater than source size %d", start, src->length());

      lab::AudioChannel dest(reinterpret_cast<float*>(buf), length);

      dest.copyFromRange(src, start, std::min(length, size_t(src->length() - start)));
      ret = JS_TRUE;

      break;
    }
    case BUFFER_COPY_TO_CHANNEL: {
      int ch = js_channel_get(ctx, argv[1]);

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number out of range 0 < ch < %d", (*ab)->numberOfChannels());

      lab::AudioChannel* dest = (*ab)->channel(ch);

      size_t offset, length, bytes_per_element;
      JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &length, &bytes_per_element);
      uint8_t* buf;
      size_t size;

      if(!(buf = JS_GetArrayBuffer(ctx, &size, buffer)) || bytes_per_element != sizeof(float)) {
        JS_FreeValue(ctx, buffer);
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");
      }

      JS_FreeValue(ctx, buffer);

      uint32_t start = 0;

      if(argc > 2)
        JS_ToUint32(ctx, &start, argv[2]);

      if(start >= dest->length())
        return JS_ThrowRangeError(ctx, "startInChannel %d greater than destination size %d", start, dest->length());

      lab::AudioChannel src(reinterpret_cast<float*>(buf), length);

      memcpy(dest->mutableData() + offset, src.mutableData(), std::min(length, dest->length() - offset) * sizeof(float));

      // dest->copyFrom(&src);
      ret = JS_TRUE;

      break;
    }
    case BUFFER_GET_CHANNEL_DATA: {
      int ch = js_channel_get(ctx, argv[0]);

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number out of range 0 < ch < %d", (*ab)->numberOfChannels());

      lab::AudioChannel* ac = (*ab)->channel(ch);
      AudioChannelPtr acptr(*ab, ch);

      ret = js_audiochannel_create(ctx, acptr);
      break;
    }

    case BUFFER_TOPOLOGY_MATCHES: {
      AudioBufferPtr* other;

      if(!(other = js_audiobuffer_class_id.opaque<AudioBufferPtr>(this_val)))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->topologyMatches(*(*other));
      break;
    }

    case BUFFER_SCALE: {
      double arg = 1;
      JS_ToFloat64(ctx, &arg, argv[0]);
      (*ab)->scale(arg);
      break;
    }
    case BUFFER_RESET: {
      (*ab)->reset();
      break;
    }
    case BUFFER_COPY_FROM: {
      AudioBufferPtr* other;

      if(!(other = js_audiobuffer_class_id.opaque<AudioBufferPtr>(this_val)))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyFrom(*(*other));
      break;
    }
    case BUFFER_SUM_FROM: {
      AudioBufferPtr* other;

      if(!(other = js_audiobuffer_class_id.opaque<AudioBufferPtr>(this_val)))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->sumFrom(*(*other));
      break;
    }
    case BUFFER_NORMALIZE: {
      (*ab)->normalize();
      break;
    }
  }

  return ret;
}

enum {
  BUFFER_LENGTH,
  BUFFER_DURATION,
  BUFFER_NUMBER_OF_CHANNELS,
  BUFFER_SAMPLERATE,
  BUFFER_SILENT,
  BUFFER_ZERO,
  BUFFER_MAX_ABS_VALUE,
  BUFFER_FIRST_TIME,
};

static JSValue
js_audiobuffer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioBufferPtr* ab;
  JSValue ret = JS_UNDEFINED;

  if(!(ab = static_cast<AudioBufferPtr*>(JS_GetOpaque2(ctx, this_val, js_audiobuffer_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case BUFFER_LENGTH: {
      ret = JS_NewUint32(ctx, (*ab)->length());
      break;
    }
    case BUFFER_DURATION: {
      double rate = (*ab)->sampleRate();
      double len = (*ab)->length();

      if(len == 0)
        ret = JS_NewInt32(ctx, 0);
      else if(rate > 0)
        ret = JS_NewFloat64(ctx, len / rate);
      else
        ret = (JSValue){.u = {.float64 = NAN}, .tag = JS_TAG_FLOAT64};

      break;
    }
    case BUFFER_NUMBER_OF_CHANNELS: {
      ret = JS_NewInt32(ctx, (*ab)->numberOfChannels());
      break;
    }
    case BUFFER_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*ab)->sampleRate());
      break;
    }
    case BUFFER_SILENT: {
      ret = JS_NewBool(ctx, (*ab)->isSilent());
      break;
    }
    case BUFFER_ZERO: {
      ret = JS_NewBool(ctx, (*ab)->isZero());
      break;
    }
    case BUFFER_MAX_ABS_VALUE: {
      ret = JS_NewFloat64(ctx, (*ab)->maxAbsValue());
      break;
    }
    case BUFFER_FIRST_TIME: {
      ret = JS_NewBool(ctx, (*ab)->isFirstTime());
      break;
    }
  }

  return ret;
}

static JSValue
js_audiobuffer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioBufferPtr* ab;
  JSValue ret = JS_UNDEFINED;

  if(!(ab = static_cast<AudioBufferPtr*>(JS_GetOpaque2(ctx, this_val, js_audiobuffer_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case BUFFER_LENGTH: {
      int oldlen = (*ab)->length();
      uint32_t newlen;
      JS_ToUint32(ctx, &newlen, value);

      if(newlen > oldlen)
        return JS_ThrowRangeError(ctx, "new length must be equal or smaller than current length (%d)", oldlen);

      (*ab)->resizeSmaller(newlen);
      break;
    }
    /*case BUFFER_NUMBER_OF_CHANNELS: {
      int32_t old = (*ab)->numberOfChannels(), n;
      JS_ToInt32(ctx, &n, value);

      if(old != n)
        (*ab)->setNumberOfChannels(n);
      break;
    }*/
    case BUFFER_SAMPLERATE: {
      double r = 0;

      JS_ToFloat64(ctx, &r, value);

      (*ab)->setSampleRate(r);
      break;
    }
    case BUFFER_SILENT: {
      if(!JS_ToBool(ctx, value))
        (*ab)->clearSilentFlag();
      break;
    }
    case BUFFER_ZERO: {
      if(JS_ToBool(ctx, value))
        (*ab)->zero();
      break;
    }
  }

  return ret;
}

static void
js_audiobuffer_finalizer(JSRuntime* rt, JSValue val) {
  AudioBufferPtr* ab;

  if((ab = static_cast<AudioBufferPtr*>(JS_GetOpaque(val, js_audiobuffer_class_id)))) {
    ab->~AudioBufferPtr();
    js_free_rt(rt, ab);
  }
}

static JSClassDef js_audiobuffer_class = {
    .class_name = "AudioBuffer",
    .finalizer = js_audiobuffer_finalizer,
};

static const JSCFunctionListEntry js_audiobuffer_funcs[] = {
    JS_CFUNC_MAGIC_DEF("topologyMatches", 1, js_audiobuffer_methods, BUFFER_TOPOLOGY_MATCHES),
    JS_CFUNC_MAGIC_DEF("scale", 1, js_audiobuffer_methods, BUFFER_SCALE),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_audiobuffer_methods, BUFFER_RESET),
    JS_CFUNC_MAGIC_DEF("copyFrom", 1, js_audiobuffer_methods, BUFFER_COPY_FROM),
    JS_CFUNC_MAGIC_DEF("sumFrom", 1, js_audiobuffer_methods, BUFFER_SUM_FROM),
    JS_CFUNC_MAGIC_DEF("normalize", 0, js_audiobuffer_methods, BUFFER_NORMALIZE),
    JS_CFUNC_MAGIC_DEF("copyFromChannel", 2, js_audiobuffer_methods, BUFFER_COPY_FROM_CHANNEL),
    JS_CFUNC_MAGIC_DEF("copyToChannel", 2, js_audiobuffer_methods, BUFFER_COPY_TO_CHANNEL),
    JS_CFUNC_MAGIC_DEF("getChannelData", 1, js_audiobuffer_methods, BUFFER_GET_CHANNEL_DATA),
    JS_CGETSET_MAGIC_DEF("length", js_audiobuffer_get, 0, BUFFER_LENGTH),
    JS_CGETSET_MAGIC_DEF("duration", js_audiobuffer_get, 0, BUFFER_DURATION),
    JS_CGETSET_MAGIC_DEF("numberOfChannels", js_audiobuffer_get, 0, BUFFER_NUMBER_OF_CHANNELS),
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiobuffer_get, 0, BUFFER_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("silent", js_audiobuffer_get, 0, BUFFER_SILENT),
    JS_CGETSET_MAGIC_DEF("zero", js_audiobuffer_get, 0, BUFFER_ZERO),
    JS_CGETSET_MAGIC_DEF("maxAbsValue", js_audiobuffer_get, 0, BUFFER_MAX_ABS_VALUE),
    JS_CGETSET_MAGIC_DEF("firstTime", js_audiobuffer_get, 0, BUFFER_FIRST_TIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioBuffer", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiocontext_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  bool isOffline = false, autoDispatchEvents = true;

  if(argc > 0)
    isOffline = JS_ToBool(ctx, argv[0]);
  if(argc > 1)
    autoDispatchEvents = JS_ToBool(ctx, argv[1]);

  AudioContextPtr* ac = js_malloc<AudioContextPtr>(ctx);

  new(ac) AudioContextPtr(std::make_shared<lab::AudioContext>(isOffline, autoDispatchEvents));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiocontext_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiocontext_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ac);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  CONTEXT_SAMPLERATE,
  CONTEXT_DESTINATION,
  CONTEXT_LISTENER,
  CONTEXT_CURRENTTIME,
  CONTEXT_CURRENTSAMPLEFRAME,
  CONTEXT_PREDICTED_CURRENTTIME,
};

static JSValue
js_audiocontext_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!(ac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case CONTEXT_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*ac)->sampleRate());
      break;
    }
    case CONTEXT_DESTINATION: {
      AudioDestinationNodePtr sadn = (*ac)->destinationNode();

      ret = JS_NewObjectProtoClass(ctx, audiodestinationnode_proto, js_audiodestinationnode_class_id);

      AudioDestinationNodePtr* ptr = js_malloc<AudioDestinationNodePtr>(ctx);

      new(ptr) AudioDestinationNodePtr(sadn);

      JS_SetOpaque(ret, ptr);
      break;
    }
    case CONTEXT_LISTENER: {
      AudioListenerPtr sal = (*ac)->listener();

      ret = JS_NewObjectProtoClass(ctx, audiolistener_proto, js_audiolistener_class_id);

      AudioListenerPtr* ptr = js_malloc<AudioListenerPtr>(ctx);

      new(ptr) AudioListenerPtr(sal);

      JS_SetOpaque(ret, ptr);
      break;
    }
    case CONTEXT_CURRENTTIME: {
      ret = JS_NewFloat64(ctx, (*ac)->currentTime());
      break;
    }
    case CONTEXT_CURRENTSAMPLEFRAME: {
      ret = JS_NewInt64(ctx, (*ac)->currentSampleFrame());
      break;
    }
    case CONTEXT_PREDICTED_CURRENTTIME: {
      ret = JS_NewFloat64(ctx, (*ac)->predictedCurrentTime());
      break;
    }
  }

  return ret;
}

static JSValue
js_audiocontext_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!(ac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case CONTEXT_DESTINATION: {
      AudioDestinationNodePtr* sadn;

      if(!(sadn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, value, js_audiodestinationnode_class_id))))
        return JS_ThrowInternalError(ctx, "value must be AudioDestinationNode");

      (*ac)->setDestinationNode(*sadn);

      break;
    }
  }

  return ret;
}

static void
js_audiocontext_finalizer(JSRuntime* rt, JSValue val) {
  AudioContextPtr* ac;

  if((ac = static_cast<AudioContextPtr*>(JS_GetOpaque(val, js_audiocontext_class_id)))) {
    ac->~AudioContextPtr();
    js_free_rt(rt, ac);
  }
}

static JSClassDef js_audiocontext_class = {
    .class_name = "AudioContext",
    .finalizer = js_audiocontext_finalizer,
};

static const JSCFunctionListEntry js_audiocontext_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiocontext_get, 0, CONTEXT_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("destination", js_audiocontext_get, js_audiocontext_set, CONTEXT_DESTINATION),
    JS_CGETSET_MAGIC_DEF("listener", js_audiocontext_get, 0, CONTEXT_LISTENER),
    JS_CGETSET_MAGIC_DEF("currentTime", js_audiocontext_get, 0, CONTEXT_CURRENTTIME),
    JS_CGETSET_MAGIC_DEF("currentSampleFrame", js_audiocontext_get, 0, CONTEXT_CURRENTSAMPLEFRAME),
    JS_CGETSET_MAGIC_DEF("predictedCurrentTime", js_audiocontext_get, 0, CONTEXT_PREDICTED_CURRENTTIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioContext", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiolistener_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  AudioListenerPtr* al = js_malloc<AudioListenerPtr>(ctx);

  new(al) AudioListenerPtr(std::make_shared<lab::AudioListener>());

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiolistener_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiolistener_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, al);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_POSITION_X,
  PROP_POSITION_Y,
  PROP_POSITION_Z,
};

static JSValue
js_audiolistener_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioListenerPtr* al;
  JSValue ret = JS_UNDEFINED;

  if(!(al = static_cast<AudioListenerPtr*>(JS_GetOpaque2(ctx, this_val, js_audiolistener_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_POSITION_X: {
      ret = JS_NewFloat64(ctx, (*al)->positionX()->value());
      break;
    }
    case PROP_POSITION_Y: {
      ret = JS_NewFloat64(ctx, (*al)->positionY()->value());
      break;
    }
    case PROP_POSITION_Z: {
      ret = JS_NewFloat64(ctx, (*al)->positionZ()->value());
      break;
    }
  }

  return ret;
}
static JSValue
js_audiolistener_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioListenerPtr* al;
  JSValue ret = JS_UNDEFINED;

  if(!(al = static_cast<AudioListenerPtr*>(JS_GetOpaque2(ctx, this_val, js_audiolistener_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_POSITION_X: {
      (*al)->positionX()->setValue(from_js<double>(ctx, value));
      break;
    }
    case PROP_POSITION_Y: {
      (*al)->positionY()->setValue(from_js<double>(ctx, value));
      break;
    }
    case PROP_POSITION_Z: {
      (*al)->positionZ()->setValue(from_js<double>(ctx, value));
      break;
    }
  }

  return ret;
}

static void
js_audiolistener_finalizer(JSRuntime* rt, JSValue val) {
  AudioListenerPtr* al;

  if((al = static_cast<AudioListenerPtr*>(JS_GetOpaque(val, js_audiolistener_class_id)))) {
    al->~AudioListenerPtr();
    js_free_rt(rt, al);
  }
}

static JSClassDef js_audiolistener_class = {
    .class_name = "AudioListener",
    .finalizer = js_audiolistener_finalizer,
};

static const JSCFunctionListEntry js_audiolistener_funcs[] = {
    JS_CGETSET_MAGIC_DEF("positionX", js_audiolistener_get, js_audiolistener_set, PROP_POSITION_X),
    JS_CGETSET_MAGIC_DEF("positionY", js_audiolistener_get, js_audiolistener_set, PROP_POSITION_Y),
    JS_CGETSET_MAGIC_DEF("positionZ", js_audiolistener_get, js_audiolistener_set, PROP_POSITION_Z),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioListener", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiodevice_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  lab::AudioStreamConfig in_config, out_config;
  AudioDevicePtr* ad = js_malloc<AudioDevicePtr>(ctx);

  in_config.device_index = 0;
  in_config.desired_channels = 2;
  in_config.desired_samplerate = 44100;

  out_config.device_index = 0;
  out_config.desired_channels = 2;
  out_config.desired_samplerate = 44100;

  new(ad) AudioDevicePtr(std::make_shared<lab::AudioDevice_RtAudio>(in_config, out_config));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiodevice_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiodevice_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ad);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  DEVICE_DESTINATION,
};

static JSValue
js_audiodevice_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!(ad = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodevice_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audiodevice_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!(ad = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodevice_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case DEVICE_DESTINATION: {
      AudioDestinationNodePtr* sadn;

      if(!(sadn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, value, js_audiodestinationnode_class_id))))
        return JS_ThrowInternalError(ctx, "value must be AudioDestinationNode");

      (*ad)->setDestinationNode(*sadn);

      break;
    }
  }

  return ret;
}

static void
js_audiodevice_finalizer(JSRuntime* rt, JSValue val) {
  AudioDevicePtr* ad;

  if((ad = static_cast<AudioDevicePtr*>(JS_GetOpaque(val, js_audiodevice_class_id)))) {
    ad->~AudioDevicePtr();
    js_free_rt(rt, ad);
  }
}

static JSClassDef js_audiodevice_class = {
    .class_name = "AudioDevice",
    .finalizer = js_audiodevice_finalizer,
};

static const JSCFunctionListEntry js_audiodevice_funcs[] = {
    JS_CGETSET_MAGIC_DEF("destination", 0, js_audiodevice_set, DEVICE_DESTINATION),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDevice", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audionode_wrap(JSContext* ctx, JSValueConst new_target, AudioNodePtr& anode) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioNodePtr* an = js_malloc<AudioNodePtr>(ctx);

  new(an) AudioNodePtr(anode, anode.value);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audionode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audionode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, an);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AUDIONODE_ISSCHEDULEDNODE,
  AUDIONODE_INITIALIZE,
  AUDIONODE_UNINITIALIZE,
  AUDIONODE_ISINITIALIZED,
  AUDIONODE_NUMBEROFINPUTS,
  AUDIONODE_NUMBEROFOUTPUTS,
  AUDIONODE_PARAMNAMES,
  AUDIONODE_PARAMSHORTNAMES,
  AUDIONODE_PARAMINDEX,
  AUDIONODE_SETTINGNAMES,
  AUDIONODE_SETTINGSHORTNAMES,
  AUDIONODE_SETTINGINDEX,
};

static JSValue
js_audionode_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!(an = js_audionode_class_id.opaque<AudioNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODE_ISSCHEDULEDNODE: {
      ret = JS_NewBool(ctx, (*an)->isScheduledNode());
      break;
    }
    case AUDIONODE_INITIALIZE: {
      (*an)->initialize();
      break;
    }
    case AUDIONODE_UNINITIALIZE: {
      (*an)->uninitialize();
      break;
    }
    case AUDIONODE_ISINITIALIZED: {
      ret = JS_NewBool(ctx, (*an)->isInitialized());
      break;
    }
    case AUDIONODE_NUMBEROFINPUTS: {
      ret = JS_NewInt32(ctx, (*an)->numberOfInputs());
      break;
    }
    case AUDIONODE_NUMBEROFOUTPUTS: {
      ret = JS_NewInt32(ctx, (*an)->numberOfOutputs());
      break;
    }
    case AUDIONODE_PARAMNAMES: {
      ret = to_js(ctx, (*an)->paramNames());
      break;
    }
    case AUDIONODE_PARAMSHORTNAMES: {
      ret = to_js(ctx, (*an)->paramShortNames());
      break;
    }
    case AUDIONODE_PARAMINDEX: {
      const char* str;

      if((str = JS_ToCString(ctx, argv[0]))) {
        ret = to_js(ctx, (*an)->param_index(str));
        JS_FreeCString(ctx, str);
      }

      break;
    }
    case AUDIONODE_SETTINGNAMES: {
      ret = to_js(ctx, (*an)->settingNames());
      break;
    }
    case AUDIONODE_SETTINGSHORTNAMES: {
      ret = to_js(ctx, (*an)->settingShortNames());
      break;
    }
    case AUDIONODE_SETTINGINDEX: {
      const char* str;

      if((str = JS_ToCString(ctx, argv[0]))) {
        ret = to_js(ctx, (*an)->setting_index(str));
        JS_FreeCString(ctx, str);
      }

      break;
    }
  }

  return ret;
}

enum {
  AUDIONODE_NAME,
  AUDIONODE_CHANNELCOUNT,
};

static JSValue
js_audionode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!(an = js_audionode_class_id.opaque<AudioNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODE_NAME: {
      ret = JS_NewString(ctx, (*an)->name());
      break;
    }
    case AUDIONODE_CHANNELCOUNT: {
      ret = JS_NewInt32(ctx, (*an)->channelCount());
      break;
    }
  }

  return ret;
}

static JSValue
js_audionode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;
  double d;

  if(!(an = js_audionode_class_id.opaque<AudioNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &d, value);

  switch(magic) {
    case AUDIONODE_CHANNELCOUNT: {
      int32_t n = -1;
      JS_ToInt32(ctx, &n, value);

      /*ContextGraphLock gLock(this, "AudioContext::handlePreRenderTasks()");
      (*an)->setChannelCount();*/
      break;
    }
  }

  return ret;
}

static void
js_audionode_finalizer(JSRuntime* rt, JSValue val) {
  AudioNodePtr* an;

  if((an = js_audionode_class_id.opaque<AudioNodePtr>(val))) {
    an->~AudioNodePtr();
    js_free_rt(rt, an);
  }
}

static JSClassDef js_audionode_class = {
    .class_name = "AudioNode",
    .finalizer = js_audionode_finalizer,
};

static const JSCFunctionListEntry js_audionode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("isScheduledNode", 0, js_audionode_methods, AUDIONODE_ISSCHEDULEDNODE),
    JS_CFUNC_MAGIC_DEF("initialize", 0, js_audionode_methods, AUDIONODE_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("uninitialize", 0, js_audionode_methods, AUDIONODE_UNINITIALIZE),
    JS_CFUNC_MAGIC_DEF("isInitialized", 0, js_audionode_methods, AUDIONODE_ISINITIALIZED),
    JS_CFUNC_MAGIC_DEF("numberOfInputs", 0, js_audionode_methods, AUDIONODE_NUMBEROFINPUTS),
    JS_CFUNC_MAGIC_DEF("numberOfOutputs", 0, js_audionode_methods, AUDIONODE_NUMBEROFOUTPUTS),
    JS_CFUNC_MAGIC_DEF("paramNames", 0, js_audionode_methods, AUDIONODE_PARAMNAMES),
    JS_CFUNC_MAGIC_DEF("paramShortNames", 0, js_audionode_methods, AUDIONODE_PARAMSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("param_index", 1, js_audionode_methods, AUDIONODE_PARAMINDEX),
    JS_CFUNC_MAGIC_DEF("settingNames", 0, js_audionode_methods, AUDIONODE_SETTINGNAMES),
    JS_CFUNC_MAGIC_DEF("settingShortNames", 0, js_audionode_methods, AUDIONODE_SETTINGSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("setting_index", 1, js_audionode_methods, AUDIONODE_SETTINGINDEX),
    JS_CGETSET_MAGIC_DEF("name", js_audionode_get, 0, AUDIONODE_NAME),
    JS_CGETSET_MAGIC_FLAGS_DEF("channelCount", js_audionode_get, js_audionode_set, AUDIONODE_CHANNELCOUNT, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiodestinationnode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  lab::AudioContext* ac = nullptr;
  std::shared_ptr<lab::AudioDevice> device;

  if(argc > 0) {
    AudioContextPtr* acptr;

    if(!(acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id))))
      return JS_EXCEPTION;

    ac = acptr->get();
  }

  if(!ac)
    return JS_ThrowInternalError(ctx, "argument 1 must be AudioContext");

  if(argc > 1) {
    AudioDevicePtr* adptr;

    if(!(adptr = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, argv[1], js_audiodevice_class_id))))
      return JS_EXCEPTION;

    device = *adptr;
  }

  if(!device.get())
    return JS_ThrowInternalError(ctx, "argument 2 must be AudioDevice");

  AudioDestinationNodePtr* adn = js_malloc<AudioDestinationNodePtr>(ctx);

  new(adn) AudioDestinationNodePtr(std::make_shared<lab::AudioDestinationNode>(*ac, device));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiodestinationnode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiodestinationnode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, adn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_NAME,
};

static JSValue
js_audiodestinationnode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!(adn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodestinationnode_class_id))))
    return JS_EXCEPTION;

  if(adn->get() == nullptr)
    return JS_UNDEFINED;

  switch(magic) {
    case PROP_NAME: {
      ret = JS_NewString(ctx, (*adn)->name());
      break;
    }
  }

  return ret;
}

enum {
  AUDIODESTINATION_RESET,
};

static JSValue
js_audiodestinationnode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!(adn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodestinationnode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIODESTINATION_RESET: {
      adn->reset();
      break;
    }
  }

  return ret;
}

static void
js_audiodestinationnode_finalizer(JSRuntime* rt, JSValue val) {
  AudioDestinationNodePtr* adn;

  if((adn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque(val, js_audiodestinationnode_class_id)))) {
    adn->~AudioDestinationNodePtr();
    js_free_rt(rt, adn);
  }
}

static JSClassDef js_audiodestinationnode_class = {
    .class_name = "AudioDestinationNode",
    .finalizer = js_audiodestinationnode_finalizer,
};

static const JSCFunctionListEntry js_audiodestinationnode_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name", js_audiodestinationnode_get, 0, PROP_NAME),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_audiodestinationnode_method, AUDIODESTINATION_RESET),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDestinationNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audioscheduledsourcenode_wrap(JSContext* ctx, JSValueConst new_target, AudioScheduledSourceNodePtr& anode) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioScheduledSourceNodePtr* assn = js_malloc<AudioScheduledSourceNodePtr>(ctx);

  new(assn) AudioScheduledSourceNodePtr(anode, anode.value);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audioscheduledsourcenode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audioscheduledsourcenode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, assn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  IS_PLAYING_OR_SCHEDULED,
};

static JSValue
js_audioscheduledsourcenode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioScheduledSourceNodePtr* assn;
  JSValue ret = JS_UNDEFINED;

  if(!(assn = static_cast<AudioScheduledSourceNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audioscheduledsourcenode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case IS_PLAYING_OR_SCHEDULED: {
      ret = JS_NewBool(ctx, (*assn)->isPlayingOrScheduled());
      break;
    }
  }

  return ret;
}

static void
js_audioscheduledsourcenode_finalizer(JSRuntime* rt, JSValue val) {
  AudioScheduledSourceNodePtr* assn;

  if((assn = static_cast<AudioScheduledSourceNodePtr*>(JS_GetOpaque(val, js_audioscheduledsourcenode_class_id)))) {
    assn->~AudioScheduledSourceNodePtr();
    js_free_rt(rt, assn);
  }
}

static JSClassDef js_audioscheduledsourcenode_class = {
    .class_name = "AudioScheduledSourceNode",
    .finalizer = js_audioscheduledsourcenode_finalizer,
};

static const JSCFunctionListEntry js_audioscheduledsourcenode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("isPlayingOrScheduled", 0, js_audioscheduledsourcenode_method, IS_PLAYING_OR_SCHEDULED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioScheduledSourceNode", JS_PROP_CONFIGURABLE),
};

static const std::array<const char*, 8> oscillator_types = {
    "oscillator_none",
    "sine",
    "fast_sine",
    "square",
    "sawtooth",
    "falling_sawtooth",
    "triangle",
    "custom",
};

static JSValue
js_oscillatornode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  AudioContextPtr* acptr;

  if(!(acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id))))
    return JS_EXCEPTION;
  lab::AudioContext& ac = *acptr->get();

  OscillatorNodePtr* on = js_malloc<OscillatorNodePtr>(ctx);

  new(on) OscillatorNodePtr(std::make_shared<lab::OscillatorNode>(ac), *acptr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = oscillatornode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_oscillatornode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, on);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  OSCILLATORNODE_START,
  OSCILLATORNODE_STARTWHEN,
  OSCILLATORNODE_STOP,
  OSCILLATORNODE_CONNECT,
};

static JSValue
js_oscillatornode_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<OscillatorNodePtr*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case OSCILLATORNODE_START: {
      double when;
      JS_ToFloat64(ctx, &when, argv[0]);

      (*on)->start(when);
      break;
    }
    case OSCILLATORNODE_STARTWHEN: {
      (*on)->startWhen();
      break;
    }
    case OSCILLATORNODE_STOP: {
      double when;
      JS_ToFloat64(ctx, &when, argv[0]);

      (*on)->stop(when);
      break;
    }
    case OSCILLATORNODE_CONNECT: {
      AudioNodePtr* an;

      if(!(an = js_audionode_class_id.opaque<AudioNodePtr>(ctx, this_val)))
        return JS_EXCEPTION;

      on->value->connect(*an, *on);
      break;
    }
  }

  return ret;
}

enum {
  OSCILLATORNODE_TYPE,
  OSCILLATORNODE_AMPLITUDE,
  OSCILLATORNODE_FREQUENCY,
  OSCILLATORNODE_DETUNE,
  OSCILLATORNODE_BIAS,
};

static JSValue
js_oscillatornode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<OscillatorNodePtr*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case OSCILLATORNODE_TYPE: {
      ret = JS_NewInt32(ctx, (*on)->type());
      break;
    }
    case OSCILLATORNODE_AMPLITUDE: {
      ret = JS_NewFloat64(ctx, (*on)->amplitude()->value());
      break;
    }
    case OSCILLATORNODE_FREQUENCY: {
      ret = JS_NewFloat64(ctx, (*on)->frequency()->value());
      break;
    }
    case OSCILLATORNODE_DETUNE: {
      ret = JS_NewFloat64(ctx, (*on)->detune()->value());
      break;
    }
    case OSCILLATORNODE_BIAS: {
      ret = JS_NewFloat64(ctx, (*on)->bias()->value());
      break;
    }
  }

  return ret;
}

static JSValue
js_oscillatornode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;
  double d;

  if(!(on = static_cast<OscillatorNodePtr*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id))))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &d, value);

  switch(magic) {
    case OSCILLATORNODE_TYPE: {
      int32_t type = -1;
      const char* arg;

      if(JS_IsString(value) && (arg = JS_ToCString(ctx, value))) {

        const auto it = std::find_if(oscillator_types.begin(), oscillator_types.end(), [arg](const char* str) -> bool { return !strcasecmp(arg, str); });

        if(it != oscillator_types.end())
          type = std::distance(oscillator_types.begin(), it);

        JS_FreeCString(ctx, arg);
      }

      if(type == -1)
        JS_ToInt32(ctx, &type, value);

      (*on)->setType(lab::OscillatorType(type));
      break;
    }
    case OSCILLATORNODE_AMPLITUDE: {
      (*on)->amplitude()->setValue(d);
      break;
    }
    case OSCILLATORNODE_FREQUENCY: {
      (*on)->frequency()->setValue(d);
      break;
    }
    case OSCILLATORNODE_DETUNE: {
      (*on)->detune()->setValue(d);
      break;
    }
    case OSCILLATORNODE_BIAS: {
      (*on)->bias()->setValue(d);
      break;
    }
  }

  return ret;
}

static void
js_oscillatornode_finalizer(JSRuntime* rt, JSValue val) {
  OscillatorNodePtr* on;

  if((on = static_cast<OscillatorNodePtr*>(JS_GetOpaque(val, js_oscillatornode_class_id)))) {
    on->~OscillatorNodePtr();
    js_free_rt(rt, on);
  }
}

static JSClassDef js_oscillatornode_class = {
    .class_name = "OscillatorNode",
    .finalizer = js_oscillatornode_finalizer,
};

static const JSCFunctionListEntry js_oscillatornode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("start", 1, js_oscillatornode_methods, OSCILLATORNODE_START),
    JS_CFUNC_MAGIC_DEF("startWhen", 0, js_oscillatornode_methods, OSCILLATORNODE_STARTWHEN),
    JS_CFUNC_MAGIC_DEF("stop", 1, js_oscillatornode_methods, OSCILLATORNODE_STOP),

    JS_CFUNC_MAGIC_DEF("connect", 1, js_oscillatornode_methods, OSCILLATORNODE_CONNECT),
    JS_CGETSET_MAGIC_FLAGS_DEF("amplitude", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_AMPLITUDE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("frequency", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_FREQUENCY, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("detune", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_DETUNE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("bias", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_BIAS, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "OscillatorNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiosummingjunction_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  AudioContextPtr* acptr;

  if(!(acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id))))
    return JS_EXCEPTION;
  lab::AudioContext& ac = *acptr->get();

  AudioSummingJunctionPtr* on = js_malloc<AudioSummingJunctionPtr>(ctx);

  new(on) AudioSummingJunctionPtr(std::make_shared<lab::AudioSummingJunction>());

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiosummingjunction_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiosummingjunction_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, on);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {

};

static JSValue
js_audiosummingjunction_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioSummingJunctionPtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<AudioSummingJunctionPtr*>(JS_GetOpaque2(ctx, this_val, js_audiosummingjunction_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

enum {

};

static JSValue
js_audiosummingjunction_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioSummingJunctionPtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<AudioSummingJunctionPtr*>(JS_GetOpaque2(ctx, this_val, js_audiosummingjunction_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audiosummingjunction_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioSummingJunctionPtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<AudioSummingJunctionPtr*>(JS_GetOpaque2(ctx, this_val, js_audiosummingjunction_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_audiosummingjunction_finalizer(JSRuntime* rt, JSValue val) {
  AudioSummingJunctionPtr* on;

  if((on = static_cast<AudioSummingJunctionPtr*>(JS_GetOpaque(val, js_audiosummingjunction_class_id)))) {
    on->~AudioSummingJunctionPtr();
    js_free_rt(rt, on);
  }
}

static JSClassDef js_audiosummingjunction_class = {
    .class_name = "AudioSummingJunction",
    .finalizer = js_audiosummingjunction_finalizer,
};

static const JSCFunctionListEntry js_audiosummingjunction_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioSummingJunction", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audioparam_wrap(JSContext* ctx, JSValueConst new_target, AudioParamPtr& anode) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioParamPtr* an = js_malloc<AudioParamPtr>(ctx);

  new(an) AudioParamPtr(anode);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audioparam_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audioparam_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, an);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AUDIOPARAM_VALUE,
  AUDIOPARAM_SETVALUE,
  AUDIOPARAM_SETVALUEATTIME,
};

static JSValue
js_audioparam_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioParamPtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<AudioParamPtr*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_VALUE: {
      break;
    }
    case AUDIOPARAM_SETVALUE: {
      break;
    }
    case AUDIOPARAM_SETVALUEATTIME: {
      break;
    }
  }

  return ret;
}

enum {

};

static JSValue
js_audioparam_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioParamPtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<AudioParamPtr*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audioparam_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioParamPtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<AudioParamPtr*>(JS_GetOpaque2(ctx, this_val, js_audioparam_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_audioparam_finalizer(JSRuntime* rt, JSValue val) {
  AudioParamPtr* on;

  if((on = static_cast<AudioParamPtr*>(JS_GetOpaque(val, js_audioparam_class_id)))) {
    on->~AudioParamPtr();
    js_free_rt(rt, on);
  }
}

static JSClassDef js_audioparam_class = {
    .class_name = "AudioParam",
    .finalizer = js_audioparam_finalizer,
};

static const JSCFunctionListEntry js_audioparam_funcs[] = {
    JS_CFUNC_MAGIC_DEF("value", 0, js_audioparam_methods, AUDIOPARAM_VALUE),
    JS_CFUNC_MAGIC_DEF("setValue", 1, js_audioparam_methods, AUDIOPARAM_SETVALUE),
    JS_CFUNC_MAGIC_DEF("setValueAtTime", 2, js_audioparam_methods, AUDIOPARAM_SETVALUEATTIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioParam", JS_PROP_CONFIGURABLE),
};

int
js_labsound_init(JSContext* ctx, JSModuleDef* m) {
  js_audiobuffer_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiobuffer_class_id, &js_audiobuffer_class);

  audiobuffer_ctor = JS_NewCFunction2(ctx, js_audiobuffer_constructor, "AudioBuffer", 1, JS_CFUNC_constructor, 0);
  audiobuffer_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiobuffer_proto, js_audiobuffer_funcs, countof(js_audiobuffer_funcs));

  JS_SetClassProto(ctx, js_audiobuffer_class_id, audiobuffer_proto);
  JS_SetConstructor(ctx, audiobuffer_ctor, audiobuffer_proto);

  js_audiocontext_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiocontext_class_id, &js_audiocontext_class);

  audiocontext_ctor = JS_NewCFunction2(ctx, js_audiocontext_constructor, "AudioContext", 1, JS_CFUNC_constructor, 0);
  audiocontext_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiocontext_proto, js_audiocontext_funcs, countof(js_audiocontext_funcs));

  JS_SetClassProto(ctx, js_audiocontext_class_id, audiocontext_proto);
  JS_SetConstructor(ctx, audiocontext_ctor, audiocontext_proto);

  js_audiolistener_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiolistener_class_id, &js_audiolistener_class);

  audiolistener_ctor = JS_NewCFunction2(ctx, js_audiolistener_constructor, "AudioListener", 1, JS_CFUNC_constructor, 0);
  audiolistener_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiolistener_proto, js_audiolistener_funcs, countof(js_audiolistener_funcs));

  JS_SetClassProto(ctx, js_audiolistener_class_id, audiolistener_proto);
  JS_SetConstructor(ctx, audiolistener_ctor, audiolistener_proto);

  js_audiodevice_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiodevice_class_id, &js_audiodevice_class);

  audiodevice_ctor = JS_NewCFunction2(ctx, js_audiodevice_constructor, "AudioDevice", 1, JS_CFUNC_constructor, 0);
  audiodevice_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiodevice_proto, js_audiodevice_funcs, countof(js_audiodevice_funcs));

  JS_SetClassProto(ctx, js_audiodevice_class_id, audiodevice_proto);
  JS_SetConstructor(ctx, audiodevice_ctor, audiodevice_proto);

  js_audionode_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audionode_class_id, &js_audionode_class);

  audionode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audionode_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audionode_proto, js_audionode_funcs, countof(js_audionode_funcs));

  JS_SetClassProto(ctx, js_audionode_class_id, audionode_proto);
  JS_SetConstructor(ctx, audionode_ctor, audionode_proto);

  js_audiodestinationnode_class_id.init();
  js_audiodestinationnode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiodestinationnode_class_id, &js_audiodestinationnode_class);

  audiodestinationnode_ctor = JS_NewCFunction2(ctx, js_audiodestinationnode_constructor, "AudioDestinationNode", 1, JS_CFUNC_constructor, 0);
  audiodestinationnode_proto = JS_NewObjectProto(ctx, audionode_proto);

  JS_SetPropertyFunctionList(ctx, audiodestinationnode_proto, js_audiodestinationnode_funcs, countof(js_audiodestinationnode_funcs));

  JS_SetClassProto(ctx, js_audiodestinationnode_class_id, audiodestinationnode_proto);
  JS_SetConstructor(ctx, audiodestinationnode_ctor, audiodestinationnode_proto);

  js_audioscheduledsourcenode_class_id.init();
  js_audioscheduledsourcenode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audioscheduledsourcenode_class_id, &js_audioscheduledsourcenode_class);

  audioscheduledsourcenode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audioscheduledsourcenode_proto = JS_NewObjectProto(ctx, audionode_proto);

  JS_SetPropertyFunctionList(ctx, audioscheduledsourcenode_proto, js_audioscheduledsourcenode_funcs, countof(js_audioscheduledsourcenode_funcs));

  JS_SetClassProto(ctx, js_audioscheduledsourcenode_class_id, audioscheduledsourcenode_proto);
  JS_SetConstructor(ctx, audioscheduledsourcenode_ctor, audioscheduledsourcenode_proto);

  js_oscillatornode_class_id.init();
  js_oscillatornode_class_id.inherit(js_audioscheduledsourcenode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_oscillatornode_class_id, &js_oscillatornode_class);

  oscillatornode_ctor = JS_NewCFunction2(ctx, js_oscillatornode_constructor, "OscillatorNode", 1, JS_CFUNC_constructor, 0);
  oscillatornode_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, oscillatornode_proto, js_oscillatornode_funcs, countof(js_oscillatornode_funcs));

  JS_SetClassProto(ctx, js_oscillatornode_class_id, oscillatornode_proto);

  js_audiosummingjunction_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiosummingjunction_class_id, &js_audiosummingjunction_class);

  audiosummingjunction_ctor = JS_NewCFunction2(ctx, js_audiosummingjunction_constructor, "AudioSummingJunction", 1, JS_CFUNC_constructor, 0);
  audiosummingjunction_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, audiosummingjunction_proto, js_audiosummingjunction_funcs, countof(js_audiosummingjunction_funcs));

  JS_SetClassProto(ctx, js_audiosummingjunction_class_id, audiosummingjunction_proto);

  js_audioparam_class_id.init();
  js_audioparam_class_id.inherit(js_audiosummingjunction_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audioparam_class_id, &js_audioparam_class);

  audioparam_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audioparam_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, audioparam_proto, js_audioparam_funcs, countof(js_audioparam_funcs));

  JS_SetClassProto(ctx, js_audioparam_class_id, audioparam_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "AudioBuffer", audiobuffer_ctor);
    JS_SetModuleExport(ctx, m, "AudioContext", audiocontext_ctor);
    JS_SetModuleExport(ctx, m, "AudioListener", audiolistener_ctor);
    JS_SetModuleExport(ctx, m, "AudioDevice", audiodevice_ctor);
    JS_SetModuleExport(ctx, m, "AudioNode", audionode_ctor);
    JS_SetModuleExport(ctx, m, "AudioDestinationNode", audiodestinationnode_ctor);
    JS_SetModuleExport(ctx, m, "AudioScheduledSourceNode", audioscheduledsourcenode_ctor);
    JS_SetModuleExport(ctx, m, "OscillatorNode", oscillatornode_ctor);
    JS_SetModuleExport(ctx, m, "AudioSummingJunction", audiosummingjunction_ctor);
    JS_SetModuleExport(ctx, m, "AudioParam", audioparam_ctor);
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_labsound(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "AudioBuffer");
  JS_AddModuleExport(ctx, m, "AudioContext");
  JS_AddModuleExport(ctx, m, "AudioListener");
  JS_AddModuleExport(ctx, m, "AudioDevice");
  JS_AddModuleExport(ctx, m, "AudioNode");
  JS_AddModuleExport(ctx, m, "AudioDestinationNode");
  JS_AddModuleExport(ctx, m, "AudioScheduledSourceNode");
  JS_AddModuleExport(ctx, m, "OscillatorNode");
  JS_AddModuleExport(ctx, m, "AudioSummingJunction");
  JS_AddModuleExport(ctx, m, "AudioParam");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_labsound_init)))
    js_init_module_labsound(ctx, m);

  return m;
}
