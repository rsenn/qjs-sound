#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "cpputils.h"
#include <array>
#include <map>
#include <vector>
#include <algorithm>

using std::make_pair;
using std::make_shared;
using std::map;
using std::min;
using std::shared_ptr;
using std::vector;
using std::weak_ptr;

static ClassId js_audiobuffer_class_id, js_audiocontext_class_id, js_audiolistener_class_id, js_audiodevice_class_id, js_audionode_class_id, js_audiodestinationnode_class_id,
    js_audioscheduledsourcenode_class_id, js_oscillatornode_class_id, js_audiosummingjunction_class_id, js_audiobuffersourcenode_class_id, js_audioparam_class_id;
static JSValue audiobuffer_proto, audiobuffer_ctor, audiocontext_proto, audiocontext_ctor, audiolistener_proto, audiolistener_ctor, audiodevice_proto, audiodevice_ctor, audionode_proto,
    audionode_ctor, audiodestinationnode_proto, audiodestinationnode_ctor, audioscheduledsourcenode_proto, audioscheduledsourcenode_ctor, oscillatornode_proto, oscillatornode_ctor,
    audiosummingjunction_proto, audiosummingjunction_ctor, audiobuffersourcenode_proto, audiobuffersourcenode_ctor, audioparam_proto, audioparam_ctor;

typedef shared_ptr<lab::AudioBus> AudioBufferPtr;
typedef shared_ptr<lab::AudioContext> AudioContextPtr;
typedef shared_ptr<lab::AudioDestinationNode> AudioDestinationNodePtr;
typedef shared_ptr<lab::AudioListener> AudioListenerPtr;
typedef shared_ptr<lab::AudioDevice> AudioDevicePtr;
typedef shared_ptr<lab::AudioParam> AudioParamPtr;
typedef shared_ptr<lab::AudioSummingJunction> AudioSummingJunctionPtr;
typedef ClassPtr<lab::AudioNode> AudioNodePtr;
typedef ClassPtr<lab::AudioScheduledSourceNode> AudioScheduledSourceNodePtr;
typedef ClassPtr<lab::OscillatorNode> OscillatorNodePtr;
typedef shared_ptr<lab::SampledAudioNode> AudioBufferSourceNodePtr;

typedef ClassPtr<lab::AudioBus, int> AudioChannelPtr;
typedef weak_ptr<lab::AudioBus> AudioBufferIndex;

typedef JSObject* JSObjectPtr;
typedef vector<JSObjectPtr> JSObjectArray;

static JSObjectArray* js_audiobuffer_channels(JSContext*, const AudioBufferPtr&);

static JSValue js_audiolistener_wrap(JSContext*, AudioListenerPtr&);
static JSValue js_audiodestinationnode_wrap(JSContext*, AudioDestinationNodePtr&);

template<> struct std::less<AudioBufferIndex> {
  bool
  operator()(const AudioBufferIndex& p1, const AudioBufferIndex& p2) const {
    AudioBufferPtr b1(p1);
    AudioBufferPtr b2(p2);

    return b1.get() < b2.get();
  }
};

typedef map<AudioBufferIndex, JSObjectArray> ChannelMap;

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

static std::pair<float*, int>
js_audiochannel_buffer(JSContext* ctx, JSValueConst value) {
  size_t size, byte_offset, byte_length, bytes_per_element;
  JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length, &bytes_per_element);
  uint8_t* buf = JS_GetArrayBuffer(ctx, &size, buffer);
  JS_FreeValue(ctx, buffer);

  if(!buf)
    return make_pair(nullptr, 0);

  return make_pair(reinterpret_cast<float*>(buf + byte_offset), int(byte_length / bytes_per_element));
}

static JSObjectPtr&
js_audiochannel_object(JSContext* ctx, const AudioChannelPtr& ac) {
  size_t count = std::erase_if(channel_map, [ctx](const auto& item) {
    const auto& [key, value] = item;
    const bool del = key.expired();

    if(del)
      for(JSObjectPtr ptr : value)
        if(ptr)
          JS_FreeValue(ctx, to_js(ptr));

    return del;
  });

  if(count > 0)
    std::cerr << "Erased " << count << " expired references" << std::endl;

  JSObjectArray* obj;

  if(!(obj = js_audiobuffer_channels(ctx, ac))) {
    AudioBufferIndex key(ac);

    channel_map.emplace(make_pair(key, JSObjectArray(ac->length(), nullptr)));

    obj = &channel_map[key];
  }

  return (*obj)[ac.value];
}

static JSValue
js_audiochannel_new(JSContext* ctx, const AudioChannelPtr& ac) {
  AudioChannelPtr* ptr;
  JSValue ret = JS_UNDEFINED;
  JSObjectPtr& obj = js_audiochannel_object(ctx, ac);

  if(obj)
    return JS_DupValue(ctx, to_js(obj));

  if(!(ptr = js_malloc<AudioChannelPtr>(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  new(ptr) AudioChannelPtr(ac);

  lab::AudioChannel& ch = *(*ptr)->channel(ptr->value);

  JSValue f32arr = js_float32array_ctor(ctx);
  JSValue args[] = {
      JS_NewArrayBuffer(ctx, (uint8_t*)ch.mutableData(), ch.length() * sizeof(float), &js_audiochannel_free, ptr, FALSE),
      JS_NewUint32(ctx, 0),
      JS_NewUint32(ctx, ch.length()),
  };

  ret = JS_CallConstructor(ctx, f32arr, countof(args), args);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
  JS_FreeValue(ctx, f32arr);

  obj = from_js<JSObjectPtr>(JS_DupValue(ctx, ret));

  return ret;
}

static JSObjectArray*
js_audiobuffer_channels(JSContext* ctx, const AudioBufferPtr& ab) {
  for(auto& [k, v] : channel_map) {
    AudioBufferPtr ab2(k);

    if(ab2.get() == ab.get()) {
      if(v.size() < ab->numberOfChannels())
        v.resize(ab->numberOfChannels());

      return &v;
    }
  }

  return nullptr;
}

static JSValue
js_audiobuffer_wrap(JSContext* ctx, JSValueConst proto, AudioBufferPtr& ptr) {
  AudioBufferPtr* ab;

  if(!(ab = js_malloc<AudioBufferPtr>(ctx)))
    return JS_EXCEPTION;

  *ab = std::move(ptr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_audiobuffer_class_id);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ab);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiobuffer_wrap(JSContext* ctx, AudioBufferPtr& ptr) {
  return js_audiobuffer_wrap(ctx, audiobuffer_proto, ptr);
}

static JSValue
js_audiobuffer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioBufferPtr* ab;
  JSValue proto, obj = JS_UNDEFINED;
  uint64_t length = 0, numberOfChannels = 1;
  double sampleRate = 44100;

  if(!(ab = js_malloc<AudioBufferPtr>(ctx)))
    return JS_EXCEPTION;

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

  new(ab) AudioBufferPtr(make_shared<lab::AudioBus>(numberOfChannels, length));

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
  BUFFER_COPY_WITH_GAIN_FROM,
  BUFFER_COPY_WITH_SAMPLE_ACCURATE_GAIN_VALUES_FROM,
  BUFFER_NORMALIZE,
};

static JSValue
js_audiobuffer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioBufferPtr* ab;
  JSValue ret = JS_UNDEFINED;

  if(!(ab = js_audiobuffer_class_id.opaque<AudioBufferPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case BUFFER_COPY_FROM_CHANNEL: {
      int ch = static_cast<int>(from_js<lab::Channel>(ctx, argv[1]));

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number not in range 0 - %d", (*ab)->numberOfChannels());

      lab::AudioChannel& src = *(*ab)->channel(ch);

      auto [buf, len] = js_audiochannel_buffer(ctx, argv[0]);

      if(!buf)
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");

      int start = argc > 2 ? from_js<int32_t>(ctx, argv[2]) : 0;

      if(start < 0 || start >= src.length())
        return JS_ThrowRangeError(ctx, "startInChannel %d not in range 0 - %d (source size)", start, src.length());

      const int frames = min(len, src.length() - start);

      /*lab::AudioChannel dest(buf, len);
      dest.copyFromRange(&src, start, start + frames);*/

      memcpy(buf, src.data() + start, frames * sizeof(float));
      break;
    }
    case BUFFER_COPY_TO_CHANNEL: {
      int ch = static_cast<int>(from_js<lab::Channel>(ctx, argv[1]));

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number not in range 0 - %d", (*ab)->numberOfChannels());

      lab::AudioChannel& dest = *(*ab)->channel(ch);

      auto [buf, len] = js_audiochannel_buffer(ctx, argv[0]);

      if(!buf)
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");

      int start = argc > 2 ? from_js<int32_t>(ctx, argv[2]) : 0;

      if(start < 0 || start >= dest.length())
        return JS_ThrowRangeError(ctx, "startInChannel %d not in range 0 - %d (destination size)", start, dest.length());

      // lab::AudioChannel src(buf, len);

      const int frames = min(len, dest.length() - start);
      memcpy(dest.mutableData() + start, buf, frames * sizeof(float));
      break;
    }
    case BUFFER_GET_CHANNEL_DATA: {
      int ch = static_cast<int>(from_js<lab::Channel>(ctx, argv[0]));

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number out of range 0 < ch < %d", (*ab)->numberOfChannels());

      ret = js_audiochannel_new(ctx, AudioChannelPtr(*ab, ch));
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
      AudioBufferPtr* source;
      lab::ChannelInterpretation interp = argc > 1 ? from_js<lab::ChannelInterpretation>(ctx, argv[1]) : lab::ChannelInterpretation::Speakers;

      if(!(source = js_audiobuffer_class_id.opaque<AudioBufferPtr>(argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyFrom(*(*source), interp);
      break;
    }
    case BUFFER_SUM_FROM: {
      AudioBufferPtr* source;
      lab::ChannelInterpretation interp = argc > 1 ? from_js<lab::ChannelInterpretation>(ctx, argv[1]) : lab::ChannelInterpretation::Speakers;

      if(!(source = js_audiobuffer_class_id.opaque<AudioBufferPtr>(argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->sumFrom(*(*source), interp);
      break;
    }
    case BUFFER_COPY_WITH_GAIN_FROM: {
      AudioBufferPtr* source;
      double targetGain = from_js<double>(ctx, argv[2]);
      float lastMixGain;

      if(!(source = js_audiobuffer_class_id.opaque<AudioBufferPtr>(argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyWithGainFrom(*(*source), &lastMixGain, targetGain);
      break;
    }
    case BUFFER_COPY_WITH_SAMPLE_ACCURATE_GAIN_VALUES_FROM: {
      AudioBufferPtr* source;
      auto [ptr, len] = js_audiochannel_buffer(ctx, argv[1]);
      int numberOfGainValues = argc > 2 ? from_js<int>(ctx, argv[2]) : len;

      if(ptr == nullptr)
        return JS_ThrowTypeError(ctx, "argument 2 must be a Float32Array");

      if(!(source = js_audiobuffer_class_id.opaque<AudioBufferPtr>(argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyWithSampleAccurateGainValuesFrom(*(*source), ptr, std::min(numberOfGainValues, len));
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
  BUFFER_CREATE_BUFFER_FROM_RANGE,
  BUFFER_CREATE_BY_SAMPLE_RATE_CONVERTING,
  BUFFER_CREATE_BY_MIXING_TO_MONO,
  BUFFER_CREATE_BY_CLONING,
};

static JSValue
js_audiobuffer_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioBufferPtr *source, ret;

  if(!(source = js_audiobuffer_class_id.opaque<AudioBufferPtr>(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

  switch(magic) {
    case BUFFER_CREATE_BUFFER_FROM_RANGE: {
      int start = from_js<int>(ctx, argv[1]), end = from_js<int>(ctx, argv[2]);

      ret = lab::AudioBus::createBufferFromRange(source->get(), start, end);
      break;
    }
    case BUFFER_CREATE_BY_SAMPLE_RATE_CONVERTING: {
      BOOL mixToMono = from_js<BOOL>(ctx, argv[1]);
      float newSampleRate = from_js<float>(ctx, argv[2]);

      ret = lab::AudioBus::createBySampleRateConverting(source->get(), mixToMono, newSampleRate);
      break;
    }
    case BUFFER_CREATE_BY_MIXING_TO_MONO: {
      ret = lab::AudioBus::createByMixingToMono(source->get());
      break;
    }
    case BUFFER_CREATE_BY_CLONING: {
      ret = lab::AudioBus::createByCloning(source->get());
      break;
    }
  }

  return js_audiobuffer_wrap(ctx, ret);
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

  if(!(ab = js_audiobuffer_class_id.opaque<AudioBufferPtr>(ctx, this_val)))
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

  if(!(ab = js_audiobuffer_class_id.opaque<AudioBufferPtr>(ctx, this_val)))
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
js_audiobuffer_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioBufferPtr* ab;

  if(!(ab = js_audiobuffer_class_id.opaque<AudioBufferPtr>(this_val))) {
    ab->~AudioBufferPtr();
    js_free_rt(rt, ab);
  }
}

static JSClassDef js_audiobuffer_class = {
    .class_name = "AudioBuffer",
    .finalizer = js_audiobuffer_finalizer,
};

static const JSCFunctionListEntry js_audiobuffer_methods[] = {
    JS_CFUNC_MAGIC_DEF("topologyMatches", 1, js_audiobuffer_method, BUFFER_TOPOLOGY_MATCHES),
    JS_CFUNC_MAGIC_DEF("scale", 1, js_audiobuffer_method, BUFFER_SCALE),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_audiobuffer_method, BUFFER_RESET),
    JS_CFUNC_MAGIC_DEF("copyFrom", 1, js_audiobuffer_method, BUFFER_COPY_FROM),
    JS_CFUNC_MAGIC_DEF("sumFrom", 1, js_audiobuffer_method, BUFFER_SUM_FROM),
    JS_CFUNC_MAGIC_DEF("copyWithGainFrom", 3, js_audiobuffer_method, BUFFER_COPY_WITH_GAIN_FROM),
    JS_CFUNC_MAGIC_DEF("copyWithSampleAccurateGainValuesFrom", 3, js_audiobuffer_method, BUFFER_COPY_WITH_SAMPLE_ACCURATE_GAIN_VALUES_FROM),
    JS_CFUNC_MAGIC_DEF("normalize", 0, js_audiobuffer_method, BUFFER_NORMALIZE),
    JS_CFUNC_MAGIC_DEF("copyFromChannel", 2, js_audiobuffer_method, BUFFER_COPY_FROM_CHANNEL),
    JS_CFUNC_MAGIC_DEF("copyToChannel", 2, js_audiobuffer_method, BUFFER_COPY_TO_CHANNEL),
    JS_CFUNC_MAGIC_DEF("getChannelData", 1, js_audiobuffer_method, BUFFER_GET_CHANNEL_DATA),
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

static const JSCFunctionListEntry js_audiobuffer_functions[] = {
    JS_CFUNC_MAGIC_DEF("createBufferFromRange", 3, js_audiobuffer_function, BUFFER_CREATE_BUFFER_FROM_RANGE),
    JS_CFUNC_MAGIC_DEF("createBySampleRateConverting", 3, js_audiobuffer_function, BUFFER_CREATE_BY_SAMPLE_RATE_CONVERTING),
    JS_CFUNC_MAGIC_DEF("createByMixingToMono", 1, js_audiobuffer_function, BUFFER_CREATE_BY_MIXING_TO_MONO),
    JS_CFUNC_MAGIC_DEF("createByCloning", 1, js_audiobuffer_function, BUFFER_CREATE_BY_CLONING),
};

static JSValue
js_audioparam_wrap(JSContext* ctx, JSValueConst new_target, AudioParamPtr& aparam) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioParamPtr* ap = js_malloc<AudioParamPtr>(ctx);

  new(ap) AudioParamPtr(aparam);

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

  JS_SetOpaque(obj, ap);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audioparam_wrap(JSContext* ctx,  AudioParamPtr& aparam) {
  return js_audioparam_wrap(ctx, audioparam_proto, aparam);
}

enum {
  AUDIOPARAM_SET_VALUE_AT_TIME,
};

static JSValue
js_audioparam_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioParamPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!(ap = js_audioparam_class_id.opaque<AudioParamPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_SET_VALUE_AT_TIME: {
      double value, time;
      JS_ToFloat64(ctx, &value, argv[0]);
      JS_ToFloat64(ctx, &time, argv[1]);

      (*ap)->setValueAtTime(value, time);

      ret = JS_DupValue(ctx, this_val);
      break;
    }
  }

  return ret;
}

enum {
  AUDIOPARAM_DEFAULT_VALUE,
  AUDIOPARAM_MAX_VALUE,
  AUDIOPARAM_MIN_VALUE,
  AUDIOPARAM_VALUE,
  AUDIOPARAM_SMOOTHED_VALUE,
};

static JSValue
js_audioparam_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioParamPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!(ap = js_audioparam_class_id.opaque<AudioParamPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_DEFAULT_VALUE: {
      ret = JS_NewFloat64(ctx, (*ap)->defaultValue());
      break;
    }
    case AUDIOPARAM_MAX_VALUE: {
      ret = JS_NewFloat64(ctx, (*ap)->maxValue());
      break;
    }
    case AUDIOPARAM_MIN_VALUE: {
      ret = JS_NewFloat64(ctx, (*ap)->minValue());
      break;
    }
    case AUDIOPARAM_VALUE: {
      ret = JS_NewFloat64(ctx, (*ap)->value());
      break;
    }
    case AUDIOPARAM_SMOOTHED_VALUE: {
      ret = JS_NewFloat64(ctx, (*ap)->smoothedValue());
      break;
    }
  }

  return ret;
}

static JSValue
js_audioparam_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioParamPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!(ap = js_audioparam_class_id.opaque<AudioParamPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_VALUE: {
      double d;
      JS_ToFloat64(ctx, &d, value);
      (*ap)->setValue(d);
      break;
    }
  }

  return ret;
}

static void
js_audioparam_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioParamPtr* ap;

  if(!(ap = js_audioparam_class_id.opaque<AudioParamPtr>(this_val))) {
    ap->~AudioParamPtr();
    js_free_rt(rt, ap);
  }
}

static JSClassDef js_audioparam_class = {
    .class_name = "AudioParam",
    .finalizer = js_audioparam_finalizer,
};

static const JSCFunctionListEntry js_audioparam_methods[] = {
    JS_CGETSET_MAGIC_DEF("defaultValue", js_audioparam_get, 0, AUDIOPARAM_DEFAULT_VALUE),
    JS_CGETSET_MAGIC_DEF("maxValue", js_audioparam_get, 0, AUDIOPARAM_MAX_VALUE),
    JS_CGETSET_MAGIC_DEF("minValue", js_audioparam_get, 0, AUDIOPARAM_MIN_VALUE),
    JS_CGETSET_MAGIC_DEF("value", js_audioparam_get, js_audioparam_set, AUDIOPARAM_VALUE),
    JS_CGETSET_MAGIC_DEF("smoothedValue", js_audioparam_get, 0, AUDIOPARAM_SMOOTHED_VALUE),
    JS_CFUNC_MAGIC_DEF("setValueAtTime", 2, js_audioparam_method, AUDIOPARAM_SET_VALUE_AT_TIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioParam", JS_PROP_CONFIGURABLE),
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

  new(ac) AudioContextPtr(make_shared<lab::AudioContext>(isOffline, autoDispatchEvents));

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

  if(!(ac = js_audiocontext_class_id.opaque<AudioContextPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CONTEXT_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*ac)->sampleRate());
      break;
    }
    case CONTEXT_DESTINATION: {
      AudioDestinationNodePtr adnptr = (*ac)->destinationNode();
      ret = js_audiodestinationnode_wrap(ctx, adnptr);
      break;
    }
    case CONTEXT_LISTENER: {
      AudioListenerPtr alptr = (*ac)->listener();
      ret = js_audiolistener_wrap(ctx, alptr);
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

  if(!(ac = js_audiocontext_class_id.opaque<AudioContextPtr>(ctx, this_val)))
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
js_audiocontext_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioContextPtr* ac;

  if(!(ac = js_audiocontext_class_id.opaque<AudioContextPtr>(this_val))) {
    ac->~AudioContextPtr();
    js_free_rt(rt, ac);
  }
}

static JSClassDef js_audiocontext_class = {
    .class_name = "AudioContext",
    .finalizer = js_audiocontext_finalizer,
};

static const JSCFunctionListEntry js_audiocontext_methods[] = {
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiocontext_get, 0, CONTEXT_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("destination", js_audiocontext_get, js_audiocontext_set, CONTEXT_DESTINATION),
    JS_CGETSET_MAGIC_DEF("listener", js_audiocontext_get, 0, CONTEXT_LISTENER),
    JS_CGETSET_MAGIC_DEF("currentTime", js_audiocontext_get, 0, CONTEXT_CURRENTTIME),
    JS_CGETSET_MAGIC_DEF("currentSampleFrame", js_audiocontext_get, 0, CONTEXT_CURRENTSAMPLEFRAME),
    JS_CGETSET_MAGIC_DEF("predictedCurrentTime", js_audiocontext_get, 0, CONTEXT_PREDICTED_CURRENTTIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioContext", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiolistener_wrap(JSContext* ctx, JSValueConst proto, AudioListenerPtr& listener) {
  AudioListenerPtr* al;

  if(!(al = js_malloc<AudioListenerPtr>(ctx)))
    return JS_EXCEPTION;

  *al = std::move(listener);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_audiolistener_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, al);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiolistener_wrap(JSContext* ctx, AudioListenerPtr& listener) {
  return js_audiolistener_wrap(ctx, audiolistener_proto, listener);
}

static JSValue
js_audiolistener_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  AudioListenerPtr* al = js_malloc<AudioListenerPtr>(ctx);

  new(al) AudioListenerPtr(make_shared<lab::AudioListener>());

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

  if(!(al = js_audiolistener_class_id.opaque<AudioListenerPtr>(ctx, this_val)))
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

  if(!(al = js_audiolistener_class_id.opaque<AudioListenerPtr>(ctx, this_val)))
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
js_audiolistener_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioListenerPtr* al;

  if(!(al = js_audiolistener_class_id.opaque<AudioListenerPtr>(this_val))) {
    al->~AudioListenerPtr();
    js_free_rt(rt, al);
  }
}

static JSClassDef js_audiolistener_class = {
    .class_name = "AudioListener",
    .finalizer = js_audiolistener_finalizer,
};

static const JSCFunctionListEntry js_audiolistener_methods[] = {
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

  new(ad) AudioDevicePtr(make_shared<lab::AudioDevice_RtAudio>(in_config, out_config));

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

  if(!(ad = js_audiodevice_class_id.opaque<AudioDevicePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audiodevice_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!(ad = js_audiodevice_class_id.opaque<AudioDevicePtr>(ctx, this_val)))
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
js_audiodevice_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioDevicePtr* ad;

  if(!(ad = js_audiodevice_class_id.opaque<AudioDevicePtr>(this_val))) {
    ad->~AudioDevicePtr();
    js_free_rt(rt, ad);
  }
}

static JSClassDef js_audiodevice_class = {
    .class_name = "AudioDevice",
    .finalizer = js_audiodevice_finalizer,
};

static const JSCFunctionListEntry js_audiodevice_methods[] = {
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
js_audionode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
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
js_audionode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioNodePtr* an;

  if((an = js_audionode_class_id.opaque<AudioNodePtr>(this_val))) {
    an->~AudioNodePtr();
    js_free_rt(rt, an);
  }
}

static JSClassDef js_audionode_class = {
    .class_name = "AudioNode",
    .finalizer = js_audionode_finalizer,
};

static const JSCFunctionListEntry js_audionode_methods[] = {
    JS_CFUNC_MAGIC_DEF("isScheduledNode", 0, js_audionode_method, AUDIONODE_ISSCHEDULEDNODE),
    JS_CFUNC_MAGIC_DEF("initialize", 0, js_audionode_method, AUDIONODE_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("uninitialize", 0, js_audionode_method, AUDIONODE_UNINITIALIZE),
    JS_CFUNC_MAGIC_DEF("isInitialized", 0, js_audionode_method, AUDIONODE_ISINITIALIZED),
    JS_CFUNC_MAGIC_DEF("numberOfInputs", 0, js_audionode_method, AUDIONODE_NUMBEROFINPUTS),
    JS_CFUNC_MAGIC_DEF("numberOfOutputs", 0, js_audionode_method, AUDIONODE_NUMBEROFOUTPUTS),
    JS_CFUNC_MAGIC_DEF("paramNames", 0, js_audionode_method, AUDIONODE_PARAMNAMES),
    JS_CFUNC_MAGIC_DEF("paramShortNames", 0, js_audionode_method, AUDIONODE_PARAMSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("param_index", 1, js_audionode_method, AUDIONODE_PARAMINDEX),
    JS_CFUNC_MAGIC_DEF("settingNames", 0, js_audionode_method, AUDIONODE_SETTINGNAMES),
    JS_CFUNC_MAGIC_DEF("settingShortNames", 0, js_audionode_method, AUDIONODE_SETTINGSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("setting_index", 1, js_audionode_method, AUDIONODE_SETTINGINDEX),
    JS_CGETSET_MAGIC_DEF("name", js_audionode_get, 0, AUDIONODE_NAME),
    JS_CGETSET_MAGIC_FLAGS_DEF("channelCount", js_audionode_get, js_audionode_set, AUDIONODE_CHANNELCOUNT, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiodestinationnode_wrap(JSContext* ctx, JSValueConst proto, AudioDestinationNodePtr& adnptr) {
  AudioDestinationNodePtr* adn;

  if(!(adn = js_malloc<AudioDestinationNodePtr>(ctx)))
    return JS_EXCEPTION;

  *adn = std::move(adnptr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_audiodestinationnode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, adn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiodestinationnode_wrap(JSContext* ctx, AudioDestinationNodePtr& adnptr) {
  return js_audiodestinationnode_wrap(ctx, audiodestinationnode_proto, adnptr);
}

enum {
  AUDIODESTINATIONNODE_NAME,
};

static JSValue
js_audiodestinationnode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!(adn = js_audiodestinationnode_class_id.opaque<AudioDestinationNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  if(adn->get() == nullptr)
    return JS_UNDEFINED;

  switch(magic) {
    case AUDIODESTINATIONNODE_NAME: {
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

  if(!(adn = js_audiodestinationnode_class_id.opaque<AudioDestinationNodePtr>(ctx, this_val)))
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
js_audiodestinationnode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioDestinationNodePtr* adn;

  if(!(adn = js_audiodestinationnode_class_id.opaque<AudioDestinationNodePtr>(this_val))) {
    adn->~AudioDestinationNodePtr();
    js_free_rt(rt, adn);
  }
}

static JSClassDef js_audiodestinationnode_class = {
    .class_name = "AudioDestinationNode",
    .finalizer = js_audiodestinationnode_finalizer,
};

static const JSCFunctionListEntry js_audiodestinationnode_methods[] = {
    JS_CGETSET_MAGIC_DEF("name", js_audiodestinationnode_get, 0, AUDIODESTINATIONNODE_NAME),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_audiodestinationnode_method, AUDIODESTINATION_RESET),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDestinationNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audioscheduledsourcenode_wrap(JSContext* ctx, JSValueConst proto, AudioScheduledSourceNodePtr& anode) {
  AudioScheduledSourceNodePtr* assn = js_malloc<AudioScheduledSourceNodePtr>(ctx);

  new(assn) AudioScheduledSourceNodePtr(anode, anode.value);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_audioscheduledsourcenode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, assn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audioscheduledsourcenode_wrap(JSContext* ctx, AudioScheduledSourceNodePtr& anode) {
  return js_audioscheduledsourcenode_wrap(ctx, audioscheduledsourcenode_proto, anode);
}

enum {
  AUDIOSCHEDULEDSOURCENODE_START,
  AUDIOSCHEDULEDSOURCENODE_STOP,
  AUDIOSCHEDULEDSOURCENODE_IS_PLAYING_OR_SCHEDULED,
};

static JSValue
js_audioscheduledsourcenode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioScheduledSourceNodePtr* assn;
  JSValue ret = JS_UNDEFINED;

  if(!(assn = js_audioscheduledsourcenode_class_id.opaque<AudioScheduledSourceNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOSCHEDULEDSOURCENODE_START: {
      float when = argc > 0 ? from_js<double>(ctx, argv[0]) : 0;

      (*assn)->start(when);
      break;
    }
    case AUDIOSCHEDULEDSOURCENODE_STOP: {
      float when = argc > 0 ? from_js<double>(ctx, argv[0]) : 0;

      (*assn)->stop(when);
      break;
    }
    case AUDIOSCHEDULEDSOURCENODE_IS_PLAYING_OR_SCHEDULED: {
      ret = JS_NewBool(ctx, (*assn)->isPlayingOrScheduled());
      break;
    }
  }

  return ret;
}

static void
js_audioscheduledsourcenode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioScheduledSourceNodePtr* assn;

  if(!(assn = js_audioscheduledsourcenode_class_id.opaque<AudioScheduledSourceNodePtr>(this_val))) {
    assn->~AudioScheduledSourceNodePtr();
    js_free_rt(rt, assn);
  }
}

static JSClassDef js_audioscheduledsourcenode_class = {
    .class_name = "AudioScheduledSourceNode",
    .finalizer = js_audioscheduledsourcenode_finalizer,
};

static const JSCFunctionListEntry js_audioscheduledsourcenode_methods[] = {
    JS_CFUNC_MAGIC_DEF("start", 0, js_audioscheduledsourcenode_method, AUDIOSCHEDULEDSOURCENODE_START),
    JS_CFUNC_MAGIC_DEF("stop", 0, js_audioscheduledsourcenode_method, AUDIOSCHEDULEDSOURCENODE_STOP),
    JS_CFUNC_MAGIC_DEF("isPlayingOrScheduled", 0, js_audioscheduledsourcenode_method, AUDIOSCHEDULEDSOURCENODE_IS_PLAYING_OR_SCHEDULED),
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

  new(on) OscillatorNodePtr(make_shared<lab::OscillatorNode>(ac), *acptr);

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
js_oscillatornode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = js_oscillatornode_class_id.opaque<OscillatorNodePtr>(ctx, this_val)))
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

  if(!(on = js_oscillatornode_class_id.opaque<OscillatorNodePtr>(ctx, this_val)))
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

  if(!(on = js_oscillatornode_class_id.opaque<OscillatorNodePtr>(ctx, this_val)))
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
js_oscillatornode_finalizer(JSRuntime* rt, JSValue this_val) {
  OscillatorNodePtr* on;

  if(!(on = js_oscillatornode_class_id.opaque<OscillatorNodePtr>(this_val))) {
    on->~OscillatorNodePtr();
    js_free_rt(rt, on);
  }
}

static JSClassDef js_oscillatornode_class = {
    .class_name = "OscillatorNode",
    .finalizer = js_oscillatornode_finalizer,
};

static const JSCFunctionListEntry js_oscillatornode_methods[] = {
    JS_CFUNC_MAGIC_DEF("start", 1, js_oscillatornode_method, OSCILLATORNODE_START),
    JS_CFUNC_MAGIC_DEF("startWhen", 0, js_oscillatornode_method, OSCILLATORNODE_STARTWHEN),
    JS_CFUNC_MAGIC_DEF("stop", 1, js_oscillatornode_method, OSCILLATORNODE_STOP),

    JS_CFUNC_MAGIC_DEF("connect", 1, js_oscillatornode_method, OSCILLATORNODE_CONNECT),
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

  new(on) AudioSummingJunctionPtr(make_shared<lab::AudioSummingJunction>());

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
js_audiosummingjunction_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioSummingJunctionPtr* asj;
  JSValue ret = JS_UNDEFINED;

  if(!(asj = js_audiosummingjunction_class_id.opaque<AudioSummingJunctionPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

enum {

};

static JSValue
js_audiosummingjunction_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioSummingJunctionPtr* asj;
  JSValue ret = JS_UNDEFINED;

  if(!(asj = js_audiosummingjunction_class_id.opaque<AudioSummingJunctionPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audiosummingjunction_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioSummingJunctionPtr* asj;
  JSValue ret = JS_UNDEFINED;

  if(!(asj = js_audiosummingjunction_class_id.opaque<AudioSummingJunctionPtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_audiosummingjunction_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioSummingJunctionPtr* asj;

  if(!(asj = js_audiosummingjunction_class_id.opaque<AudioSummingJunctionPtr>(this_val))) {
    asj->~AudioSummingJunctionPtr();
    js_free_rt(rt, asj);
  }
}

static JSClassDef js_audiosummingjunction_class = {
    .class_name = "AudioSummingJunction",
    .finalizer = js_audiosummingjunction_finalizer,
};

static const JSCFunctionListEntry js_audiosummingjunction_methods[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioSummingJunction", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiobuffersourcenode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioContextPtr* acptr;

  if(!(acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id))))
    return JS_EXCEPTION;

  lab::AudioContext& ac = *acptr->get();

  AudioBufferSourceNodePtr* absn = js_malloc<AudioBufferSourceNodePtr>(ctx);

  new(absn) AudioBufferSourceNodePtr(make_shared<lab::SampledAudioNode>(ac));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiobuffersourcenode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiobuffersourcenode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, absn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AUDIOBUFFERSOURCENODE_START,
};

static JSValue
js_audiobuffersourcenode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioBufferSourceNodePtr* absn;
  JSValue ret = JS_UNDEFINED;

  if(!(absn = js_audiobuffersourcenode_class_id.opaque<AudioBufferSourceNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOBUFFERSOURCENODE_START: {
      double when = from_js<double>(ctx, argv[0]);

      if(argc > 2)
        (*absn)->start(when, from_js<double>(ctx, argv[1]), from_js<double>(ctx, argv[2]));
      else if(argc > 1)
        (*absn)->start(when, from_js<double>(ctx, argv[1]));
      else
        (*absn)->start(when);

      break;
    }
  }

  return ret;
}

enum {
  AUDIOBUFFERSOURCENODE_BUFFER,
  AUDIOBUFFERSOURCENODE_DETUNE,
  AUDIOBUFFERSOURCENODE_LOOP,
  AUDIOBUFFERSOURCENODE_LOOP_END,
  AUDIOBUFFERSOURCENODE_LOOP_START,
  AUDIOBUFFERSOURCENODE_PLAYBACK_RATE,
};

static JSValue
js_audiobuffersourcenode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioBufferSourceNodePtr* absn;
  JSValue ret = JS_UNDEFINED;

  if(!(absn = js_audiobuffersourcenode_class_id.opaque<AudioBufferSourceNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOBUFFERSOURCENODE_BUFFER: {
      AudioBufferPtr ab = (*absn)->getBus();

      ret = bool(ab) ? js_audiobuffer_wrap(ctx, ab) : JS_NULL;
      break;
    }
    case AUDIOBUFFERSOURCENODE_DETUNE: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_LOOP: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_LOOP_END: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_LOOP_START: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_PLAYBACK_RATE: {
      break;
    }
  }

  return ret;
}

static JSValue
js_audiobuffersourcenode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioBufferSourceNodePtr* absn;
  JSValue ret = JS_UNDEFINED;

  if(!(absn = js_audiobuffersourcenode_class_id.opaque<AudioBufferSourceNodePtr>(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOBUFFERSOURCENODE_BUFFER: {
      AudioBufferPtr* ab;

      if(!JS_IsObject(value)) {
        (*absn)->setBus(AudioBufferPtr());

      } else {
        if(!(ab = js_audiobuffer_class_id.opaque<AudioBufferPtr>(value)))
          return JS_ThrowTypeError(ctx, "property .buffer must be an AudioBuffer");

        (*absn)->setBus(*ab);
      }

      break;
    }
    case AUDIOBUFFERSOURCENODE_DETUNE: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_LOOP: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_LOOP_END: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_LOOP_START: {
      break;
    }
    case AUDIOBUFFERSOURCENODE_PLAYBACK_RATE: {
      break;
    }
  }

  return ret;
}

static void
js_audiobuffersourcenode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioBufferSourceNodePtr* absn;

  if(!(absn = js_audiobuffersourcenode_class_id.opaque<AudioBufferSourceNodePtr>(this_val))) {
    absn->~AudioBufferSourceNodePtr();
    js_free_rt(rt, absn);
  }
}

static JSClassDef js_audiobuffersourcenode_class = {
    .class_name = "AudioBufferSourceNode",
    .finalizer = js_audiobuffersourcenode_finalizer,
};

static const JSCFunctionListEntry js_audiobuffersourcenode_methods[] = {
    JS_CFUNC_MAGIC_DEF("start", 1, js_audiobuffersourcenode_method, AUDIOBUFFERSOURCENODE_START),
    JS_CGETSET_MAGIC_DEF("buffer", js_audiobuffersourcenode_get, js_audiobuffersourcenode_set, AUDIOBUFFERSOURCENODE_BUFFER),
    JS_CGETSET_MAGIC_DEF("detune", js_audiobuffersourcenode_get, 0, AUDIOBUFFERSOURCENODE_DETUNE),
    JS_CGETSET_MAGIC_DEF("loop", js_audiobuffersourcenode_get, 0, AUDIOBUFFERSOURCENODE_LOOP),
    JS_CGETSET_MAGIC_DEF("loopEnd", js_audiobuffersourcenode_get, 0, AUDIOBUFFERSOURCENODE_LOOP_END),
    JS_CGETSET_MAGIC_DEF("loopStart", js_audiobuffersourcenode_get, 0, AUDIOBUFFERSOURCENODE_LOOP_START),
    JS_CGETSET_MAGIC_DEF("playbackRate", js_audiobuffersourcenode_get, 0, AUDIOBUFFERSOURCENODE_PLAYBACK_RATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioBufferSourceNode", JS_PROP_CONFIGURABLE),
};


int
js_labsound_init(JSContext* ctx, JSModuleDef* m) {
  js_audiobuffer_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiobuffer_class_id, &js_audiobuffer_class);

  audiobuffer_ctor = JS_NewCFunction2(ctx, js_audiobuffer_constructor, "AudioBuffer", 1, JS_CFUNC_constructor, 0);
  audiobuffer_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiobuffer_proto, js_audiobuffer_methods, countof(js_audiobuffer_methods));
  JS_SetPropertyFunctionList(ctx, audiobuffer_ctor, js_audiobuffer_functions, countof(js_audiobuffer_functions));

  JS_SetClassProto(ctx, js_audiobuffer_class_id, audiobuffer_proto);
  JS_SetConstructor(ctx, audiobuffer_ctor, audiobuffer_proto);

  js_audiocontext_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiocontext_class_id, &js_audiocontext_class);

  audiocontext_ctor = JS_NewCFunction2(ctx, js_audiocontext_constructor, "AudioContext", 1, JS_CFUNC_constructor, 0);
  audiocontext_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiocontext_proto, js_audiocontext_methods, countof(js_audiocontext_methods));

  JS_SetClassProto(ctx, js_audiocontext_class_id, audiocontext_proto);
  JS_SetConstructor(ctx, audiocontext_ctor, audiocontext_proto);

  js_audiolistener_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiolistener_class_id, &js_audiolistener_class);

  audiolistener_ctor = JS_NewCFunction2(ctx, js_audiolistener_constructor, "AudioListener", 1, JS_CFUNC_constructor, 0);
  audiolistener_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiolistener_proto, js_audiolistener_methods, countof(js_audiolistener_methods));

  JS_SetClassProto(ctx, js_audiolistener_class_id, audiolistener_proto);
  JS_SetConstructor(ctx, audiolistener_ctor, audiolistener_proto);

  js_audiodevice_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiodevice_class_id, &js_audiodevice_class);

  audiodevice_ctor = JS_NewCFunction2(ctx, js_audiodevice_constructor, "AudioDevice", 1, JS_CFUNC_constructor, 0);
  audiodevice_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiodevice_proto, js_audiodevice_methods, countof(js_audiodevice_methods));

  JS_SetClassProto(ctx, js_audiodevice_class_id, audiodevice_proto);
  JS_SetConstructor(ctx, audiodevice_ctor, audiodevice_proto);

  js_audionode_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audionode_class_id, &js_audionode_class);

  audionode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audionode_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audionode_proto, js_audionode_methods, countof(js_audionode_methods));

  JS_SetClassProto(ctx, js_audionode_class_id, audionode_proto);
  JS_SetConstructor(ctx, audionode_ctor, audionode_proto);

  js_audiodestinationnode_class_id.init();
  js_audiodestinationnode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiodestinationnode_class_id, &js_audiodestinationnode_class);

  audiodestinationnode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audiodestinationnode_proto = JS_NewObjectProto(ctx, audionode_proto);

  JS_SetPropertyFunctionList(ctx, audiodestinationnode_proto, js_audiodestinationnode_methods, countof(js_audiodestinationnode_methods));

  JS_SetClassProto(ctx, js_audiodestinationnode_class_id, audiodestinationnode_proto);
  JS_SetConstructor(ctx, audiodestinationnode_ctor, audiodestinationnode_proto);

  js_audioscheduledsourcenode_class_id.init();
  js_audioscheduledsourcenode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audioscheduledsourcenode_class_id, &js_audioscheduledsourcenode_class);

  audioscheduledsourcenode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audioscheduledsourcenode_proto = JS_NewObjectProto(ctx, audionode_proto);

  JS_SetPropertyFunctionList(ctx, audioscheduledsourcenode_proto, js_audioscheduledsourcenode_methods, countof(js_audioscheduledsourcenode_methods));

  JS_SetClassProto(ctx, js_audioscheduledsourcenode_class_id, audioscheduledsourcenode_proto);
  JS_SetConstructor(ctx, audioscheduledsourcenode_ctor, audioscheduledsourcenode_proto);

  js_oscillatornode_class_id.init();
  js_oscillatornode_class_id.inherit(js_audioscheduledsourcenode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_oscillatornode_class_id, &js_oscillatornode_class);

  oscillatornode_ctor = JS_NewCFunction2(ctx, js_oscillatornode_constructor, "OscillatorNode", 1, JS_CFUNC_constructor, 0);
  oscillatornode_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, oscillatornode_proto, js_oscillatornode_methods, countof(js_oscillatornode_methods));

  JS_SetClassProto(ctx, js_oscillatornode_class_id, oscillatornode_proto);

  js_audiosummingjunction_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiosummingjunction_class_id, &js_audiosummingjunction_class);

  audiosummingjunction_ctor = JS_NewCFunction2(ctx, js_audiosummingjunction_constructor, "AudioSummingJunction", 1, JS_CFUNC_constructor, 0);
  audiosummingjunction_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, audiosummingjunction_proto, js_audiosummingjunction_methods, countof(js_audiosummingjunction_methods));

  JS_SetClassProto(ctx, js_audiosummingjunction_class_id, audiosummingjunction_proto);

  js_audiobuffersourcenode_class_id.init();
  js_audiobuffersourcenode_class_id.inherit(js_audioscheduledsourcenode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiobuffersourcenode_class_id, &js_audiobuffersourcenode_class);

  audiobuffersourcenode_ctor = JS_NewCFunction2(ctx, js_audiobuffersourcenode_constructor, "AudioBufferSourceNode", 1, JS_CFUNC_constructor, 0);
  audiobuffersourcenode_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, audiobuffersourcenode_proto, js_audiobuffersourcenode_methods, countof(js_audiobuffersourcenode_methods));

  JS_SetClassProto(ctx, js_audiobuffersourcenode_class_id, audiobuffersourcenode_proto);

  js_audioparam_class_id.init();
  js_audioparam_class_id.inherit(js_audiosummingjunction_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audioparam_class_id, &js_audioparam_class);

  audioparam_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audioparam_proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_proto);

  JS_SetPropertyFunctionList(ctx, audioparam_proto, js_audioparam_methods, countof(js_audioparam_methods));

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
    JS_SetModuleExport(ctx, m, "AudioBufferSourceNode", audiobuffersourcenode_ctor);
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
  JS_AddModuleExport(ctx, m, "AudioBufferSourceNode");
  JS_AddModuleExport(ctx, m, "AudioParam");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_labsound_init)))
    js_init_module_labsound(ctx, m);

  return m;
}
