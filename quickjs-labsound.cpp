#include <cutils.h>
#include "defines.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "quickjs-labsound.hpp"
#include <array>
#include <map>
#include <vector>
#include <algorithm>
#include <type_traits>

using std::make_pair;
using std::make_shared;
using std::make_unique;
using std::map;
using std::min;
using std::pair;

ClassWrapper audiobuffer_class, audiocontext_class, audiolistener_class, audiodevice_class, audionodeinput_class, audionodeoutput_class, audionode_class,
    audiodestinationnode_class, audioscheduledsourcenode_class, oscillatornode_class, audiosummingjunction_class, audiobuffersourcenode_class, audioparam_class, audiosetting_class,
    periodicwave_class, audioprocessor_class;

template<> ClassObjectMap<lab::AudioContext>::map_type ClassObjectMap<lab::AudioContext>::object_map{};
template<> ClassObjectMap<lab::AudioParam>::map_type ClassObjectMap<lab::AudioParam>::object_map{};
template<> ClassObjectMap<lab::AudioSetting>::map_type ClassObjectMap<lab::AudioSetting>::object_map{};
template<> ClassObjectMap<lab::AudioBus>::map_type ClassObjectMap<lab::AudioBus>::object_map{};

ClassWrapper* ClassWrapper::wrappers{nullptr};
ClassWrapper** ClassWrapper::wrapper_ptr{&ClassWrapper::wrappers};

static map<weak_ptr<lab::AudioBus>, JSObjectArray> channel_map;

static JSObjectArray* js_audiobuffer_channels(JSContext*, const AudioBufferPtr&);
static JSValue js_audionode_wrap(JSContext*, const AudioNodePtr&);
static JSValue js_audiolistener_wrap(JSContext*, AudioListenerPtr&);
static JSValue js_audiodestinationnode_wrap(JSContext*, AudioDestinationNodePtr&);
static JSValue js_audiodevice_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
static JSValue js_audiodestinationnode_constructor(JSContext*, JSValueConst, int, JSValueConst[]);

shared_ptr<lab::AudioContext>
DestinationNode::getContext() const {
  AudioContextPtr* ac;

  audiocontext_class.opaque(get_value(_context), ac);

  return ac ? *ac : shared_ptr<lab::AudioContext>(nullptr);
}

extern "C" ClassWrapper*
get_class_id(JSClassID cid) {
  return ClassWrapper::get(cid);
}

static int
js_enum_value(JSContext* ctx, JSValueConst value, const char* values[], int offset = 1) {
  const char* str = JS_ToCString(ctx, value);

  for(int i = 0; values[i]; ++i)
    if(!strcasecmp(values[i], str))
      return i + offset;

  return -1;
}

static void
js_audiochannel_free(JSRuntime* rt, void* opaque, void* ptr) {
  AudioChannelPtr* ac = static_cast<AudioChannelPtr*>(opaque);

  ac->~AudioChannelPtr();

  js_free_rt(rt, ac);
}

static pair<float*, int>
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
  size_t count = 0;
  for(auto it = channel_map.begin(); it != channel_map.end();) {
    const bool del = it->first.expired();

    if(del) {
      for(JSObjectPtr ptr : it->second)
        if(ptr)
          JS_FreeValue(ctx, to_js(ptr));

      channel_map.erase(it);
      it = channel_map.begin();
      ++count;
      continue;
    }

    ++it;
  }

  /*size_t count = std::erase_if(channel_map, [ctx](const auto& item) {
    const auto& [key, value] = item;
    const bool del = key.expired();

    if(del)
      for(JSObjectPtr ptr : value)
        if(ptr)
          JS_FreeValue(ctx, to_js(ptr));

    return del;
  });*/

  if(count > 0)
    std::cerr << "Erased " << count << " expired references" << std::endl;

  JSObjectArray* obj;

  if(!(obj = js_audiobuffer_channels(ctx, ac))) {
    weak_ptr<lab::AudioBus> key(ac);

    channel_map.emplace(make_pair(key, JSObjectArray(ac->length(), nullptr)));

    obj = &channel_map[key];
  }

  return (*obj)[ac.value];
}

static JSValue
js_audiochannel_new(JSContext* ctx, const AudioChannelPtr& ac) {
  AudioChannelPtr* ptr;
  JSObjectPtr& obj = js_audiochannel_object(ctx, ac);

  if(obj)
    return JS_DupValue(ctx, to_js(obj));

  if(!js_alloc(ctx, ptr))
    return JS_ThrowOutOfMemory(ctx);

  new(ptr) AudioChannelPtr(ac);

  lab::AudioChannel& ch = *(*ptr)->channel(ptr->value);

  uint8_t* buf = reinterpret_cast<uint8_t*>(ch.mutableData());
  size_t len = ch.length() * sizeof(float);

  JSValue ret = js_float32array_new(ctx, buf, len, &js_audiochannel_free, ptr);

  /*JSValue f32arr = js_float32array_constructor(ctx);
    JSValue args[] = {
        JS_NewArrayBuffer(ctx, buf, len, &js_audiochannel_free, ptr, FALSE),
        JS_NewUint32(ctx, 0),
        JS_NewUint32(ctx, ch.length()),
    };

    ret = JS_CallConstructor(ctx, f32arr, countof(args), args);

    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    JS_FreeValue(ctx, f32arr);*/

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

  if(!js_alloc(ctx, ab))
    return JS_EXCEPTION;

  new(ab) AudioBufferPtr(ptr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audiobuffer_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ab);

  ClassObjectMap<lab::AudioBus>::set(*ab, from_js<JSObject*>(JS_DupValue(ctx, obj)));

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiobuffer_wrap(JSContext* ctx, AudioBufferPtr& ptr) {
  JSObject* obj;

  if((obj = ClassObjectMap<lab::AudioBus>::get(ptr)))
    return JS_DupValue(ctx, to_js(obj));

  return js_audiobuffer_wrap(ctx, audiobuffer_class.proto, ptr);
}

static JSValue
js_audiobuffer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioBufferPtr* ab;
  uint64_t length;
  uint32_t numberOfChannels;
  double sampleRate;

  if(!js_alloc(ctx, ab))
    return JS_EXCEPTION;

  if(argc > 0) {
    if(JS_IsObject(argv[0])) {
      length = from_js_property<uint64_t>(ctx, argv[0], "length");
      numberOfChannels = from_js_property<uint32_t>(ctx, argv[0], "numberOfChannels");
      sampleRate = from_js_property<double>(ctx, argv[0], "sampleRate");

      if(length == 0 && sampleRate > 0 && js_has_property(ctx, argv[0], "duration")) {
        double duration = from_js_property<double>(ctx, argv[0], "duration");

        length = duration * sampleRate;
      }

    } else {
      length = argc > 0 ? from_js<uint64_t>(ctx, argv[0]) : 0;
      numberOfChannels = argc > 1 ? from_js<uint32_t>(ctx, argv[1]) : 1;
      sampleRate = argc > 2 ? from_js<double>(ctx, argv[0]) : 0;
    }
  }

  new(ab) AudioBufferPtr(make_shared<lab::AudioBus>(numberOfChannels, length));

  if(sampleRate > 0)
    (*ab)->setSampleRate(sampleRate);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiobuffer_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiobuffer_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ab);

  ClassObjectMap<lab::AudioBus>::set(*ab, from_js<JSObject*>(JS_DupValue(ctx, obj)));

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

  if(!audiobuffer_class.opaque(ctx, this_val, ab))
    return JS_EXCEPTION;

  switch(magic) {
    case BUFFER_COPY_FROM_CHANNEL: {
      int ch = from_js<int32_t>(ctx, argv[1]);

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
      int ch = from_js<int32_t>(ctx, argv[1]);

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
      int ch = from_js<int32_t>(ctx, argv[0]);

      if(ch < 0 || ch >= (*ab)->numberOfChannels())
        return JS_ThrowRangeError(ctx, "channel number out of range 0 < ch < %d", (*ab)->numberOfChannels());

      ret = js_audiochannel_new(ctx, AudioChannelPtr(*ab, ch));
      break;
    }
    case BUFFER_TOPOLOGY_MATCHES: {
      AudioBufferPtr* other;

      if(!audiobuffer_class.opaque(ctx, this_val, other))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->topologyMatches(*other->get());
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
      lab::ChannelInterpretation interp = argc > 1 ? find_enumeration<lab::ChannelInterpretation>(ctx, argv[1]) : lab::ChannelInterpretation::Speakers;

      if(!audiobuffer_class.opaque(ctx, argv[0], source))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyFrom(*source->get(), interp);
      break;
    }
    case BUFFER_SUM_FROM: {
      AudioBufferPtr* source;
      lab::ChannelInterpretation interp = argc > 1 ? find_enumeration<lab::ChannelInterpretation>(ctx, argv[1]) : lab::ChannelInterpretation::Speakers;

      if(!audiobuffer_class.opaque(ctx, argv[0], source))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->sumFrom(*source->get(), interp);
      break;
    }
    case BUFFER_COPY_WITH_GAIN_FROM: {
      AudioBufferPtr* source;
      double targetGain = from_js<double>(ctx, argv[2]);
      float lastMixGain;

      if(!audiobuffer_class.opaque(ctx, argv[0], source))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyWithGainFrom(*source->get(), &lastMixGain, targetGain);
      break;
    }
    case BUFFER_COPY_WITH_SAMPLE_ACCURATE_GAIN_VALUES_FROM: {
      AudioBufferPtr* source;
      auto [ptr, len] = js_audiochannel_buffer(ctx, argv[1]);
      int numberOfGainValues = argc > 2 ? from_js<int>(ctx, argv[2]) : len;

      if(ptr == nullptr)
        return JS_ThrowTypeError(ctx, "argument 2 must be a Float32Array");

      if(!audiobuffer_class.opaque(ctx, argv[0], source))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

      (*ab)->copyWithSampleAccurateGainValuesFrom(*source->get(), ptr, min(numberOfGainValues, len));
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

  if(!audiobuffer_class.opaque(ctx, argv[0], source))
    return JS_ThrowTypeError(ctx, "argument 1 must be an AudioBuffer");

  switch(magic) {
    case BUFFER_CREATE_BUFFER_FROM_RANGE: {
      int start = from_js<int>(ctx, argv[1]), end = from_js<int>(ctx, argv[2]);

      ret = std::move(lab::AudioBus::createBufferFromRange(source->get(), start, end));
      break;
    }
    case BUFFER_CREATE_BY_SAMPLE_RATE_CONVERTING: {
      BOOL mixToMono = from_js<BOOL>(ctx, argv[1]);
      double newSampleRate = from_js<double>(ctx, argv[2]);

      ret = std::move(lab::AudioBus::createBySampleRateConverting(source->get(), mixToMono, newSampleRate));
      break;
    }
    case BUFFER_CREATE_BY_MIXING_TO_MONO: {
      ret = std::move(lab::AudioBus::createByMixingToMono(source->get()));
      break;
    }
    case BUFFER_CREATE_BY_CLONING: {
      ret = std::move(lab::AudioBus::createByCloning(source->get()));
      break;
    }
  }

  return js_audiobuffer_wrap(ctx, ret);
}

enum {
  BUFFER_LENGTH,
  BUFFER_DURATION,
  BUFFER_NUMBEROFCHANNELS,
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

  if(!audiobuffer_class.opaque(ctx, this_val, ab))
    return JS_EXCEPTION;

  switch(magic) {
    case BUFFER_LENGTH: {
      ret = JS_NewUint32(ctx, (*ab)->length());
      break;
    }
    case BUFFER_DURATION: {
      float rate = (*ab)->sampleRate();
      int len = (*ab)->length();

      if(rate >= std::numeric_limits<float>::epsilon())
        ret = JS_NewFloat64(ctx, double(len) / rate);
      else
        ret = (JSValue){.u = {.float64 = NAN}, .tag = JS_TAG_FLOAT64};

      break;
    }
    case BUFFER_NUMBEROFCHANNELS: {
      ret = JS_NewInt32(ctx, (*ab)->numberOfChannels());
      break;
    }
    case BUFFER_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*ab)->sampleRate());
      break;
    }
    case BUFFER_SILENT: {
      ret = JS_NewBool(ctx, !!(*ab)->isSilent());
      break;
    }
    case BUFFER_ZERO: {
      ret = JS_NewBool(ctx, !!(*ab)->isZero());
      break;
    }
    case BUFFER_MAX_ABS_VALUE: {
      ret = JS_NewFloat64(ctx, (*ab)->maxAbsValue());
      break;
    }
    case BUFFER_FIRST_TIME: {
      ret = JS_NewBool(ctx, !!(*ab)->isFirstTime());
      break;
    }
  }

  return ret;
}

static JSValue
js_audiobuffer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioBufferPtr* ab;
  JSValue ret = JS_UNDEFINED;

  if(!audiobuffer_class.opaque(ctx, this_val, ab))
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
    /*case BUFFER_NUMBEROFCHANNELS: {
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

  ClassObjectMap<lab::AudioBus>::remove(from_js<JSObject*>(this_val));

  if(audiobuffer_class.opaque(this_val, ab)) {
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
    JS_CGETSET_MAGIC_DEF("numberOfChannels", js_audiobuffer_get, 0, BUFFER_NUMBEROFCHANNELS),
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

static void
js_audioparam_descriptor_reset(JSContext* ctx, lab::AudioParamDescriptor* apd) {
  js_delete(ctx, *const_cast<char**>(&apd->name));
  js_delete(ctx, *const_cast<char**>(&apd->shortName));
}

static lab::AudioParamDescriptor
js_audioparam_descriptor_get(JSContext* ctx, JSValueConst obj) {
  return lab::AudioParamDescriptor{
      .name = from_js_property<char*>(ctx, obj, "name"),
      .shortName = from_js_property<char*>(ctx, obj, "shortName"),
      .defaultValue = from_js_property<double>(ctx, obj, "defaultValue"),
      .minValue = from_js_property<double>(ctx, obj, "minValue"),
      .maxValue = from_js_property<double>(ctx, obj, "maxValue"),
  };
}

static JSValue
js_audioparam_descriptor(JSContext* ctx, lab::AudioParamDescriptor* pd) {
  JSValue ret = JS_NewObjectProto(ctx, JS_NULL);
  to_js_property(ctx, ret, "name", pd->name);
  to_js_property(ctx, ret, "shortName", pd->shortName);
  to_js_property<double>(ctx, ret, "defaultValue", pd->defaultValue);
  to_js_property<double>(ctx, ret, "minValue", pd->minValue);
  to_js_property<double>(ctx, ret, "maxValue", pd->maxValue);
  return ret;
}

static lab::AudioParamDescriptor
js_audioparam_arguments(JSContext* ctx, int argc, JSValueConst argv[]) {
  return lab::AudioParamDescriptor{
      .name = from_js<char*>(ctx, argv[0]),
      .shortName = from_js<char*>(ctx, argv[1]),
      .defaultValue = from_js<double>(ctx, argv[2]),
      .minValue = from_js<double>(ctx, argv[3]),
      .maxValue = from_js<double>(ctx, argv[4]),
  };
}

static JSValue
js_audioparam_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioParamPtr* ap;

  if(!js_alloc(ctx, ap))
    return JS_EXCEPTION;

  lab::AudioParamDescriptor desc(argc >= 5 ? js_audioparam_arguments(ctx, argc, argv) : js_audioparam_descriptor_get(ctx, argv[0]));

  shared_ptr<lab::AudioParamDescriptor> descPtr(new lab::AudioParamDescriptor(desc), [ctx](lab::AudioParamDescriptor* apd) {
    js_audioparam_descriptor_reset(ctx, apd);
    delete apd;
  });

  new(ap) AudioParamPtr(make_shared<lab::AudioParam>(descPtr.get()), descPtr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audioparam_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audioparam_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ap);
  ClassObjectMap<lab::AudioParam>::set(*ap, from_js<JSObject*>(JS_DupValue(ctx, obj)));
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audioparam_wrap(JSContext* ctx, JSValueConst proto, AudioParamPtr& aparam) {
  AudioParamPtr* ap;

  if(!js_alloc(ctx, ap))
    return JS_EXCEPTION;

  new(ap) AudioParamPtr(aparam);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audioparam_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ap);
  ClassObjectMap<lab::AudioParam>::set(*ap, from_js<JSObject*>(JS_DupValue(ctx, obj)));
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audioparam_wrap(JSContext* ctx, AudioParamPtr& aparam) {
  JSObject* obj;

  if((obj = ClassObjectMap<lab::AudioParam>::get(aparam)))
    return JS_DupValue(ctx, to_js(obj));

  return js_audioparam_wrap(ctx, audioparam_class.proto, aparam);
}

enum {
  AUDIOPARAM_VALUEATTIME,
  AUDIOPARAM_LINRAMPTOVALUEATTIME,
  AUDIOPARAM_EXPRAMPTOVALUEATTIME,
  AUDIOPARAM_TARGETATTIME,
  AUDIOPARAM_VALUECURVEATTIME,
  AUDIOPARAM_CANCELSCHEDULED,
  AUDIOPARAM_TOPRIMITIVE,
};

static JSValue
js_audioparam_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioParamPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!audioparam_class.opaque(ctx, this_val, ap))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_VALUEATTIME: {
      double v, t;
      JS_ToFloat64(ctx, &v, argv[0]);
      JS_ToFloat64(ctx, &t, argv[1]);

      (*ap)->setValueAtTime(v, t);

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case AUDIOPARAM_LINRAMPTOVALUEATTIME: {
      (*ap)->linearRampToValueAtTime(from_js<double>(ctx, argv[0]), from_js<double>(ctx, argv[1]));

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case AUDIOPARAM_EXPRAMPTOVALUEATTIME: {
      (*ap)->exponentialRampToValueAtTime(from_js<double>(ctx, argv[0]), from_js<double>(ctx, argv[1]));

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case AUDIOPARAM_TARGETATTIME: {
      (*ap)->setTargetAtTime(from_js<double>(ctx, argv[0]), from_js<double>(ctx, argv[1]), from_js<double>(ctx, argv[2]));

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case AUDIOPARAM_VALUECURVEATTIME: {
      double t, d;
      vector<float> curve;
      vector<double> curve2 = from_js<vector, double>(ctx, argv[0]);
      std::transform(curve2.begin(), curve2.end(), std::back_inserter(curve), [](double d) -> float { return d; });
      from_js(ctx, argv[1], t);
      from_js(ctx, argv[2], d);

      (*ap)->setValueCurveAtTime(curve, t, d);

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case AUDIOPARAM_CANCELSCHEDULED: {
      double t;
      from_js(ctx, argv[0], t);

      (*ap)->cancelScheduledValues(t);

      ret = JS_DupValue(ctx, this_val);
      break;
    }
    case AUDIOPARAM_TOPRIMITIVE: {
      ret = to_js<double>(ctx, (*ap)->value());
      break;
    }
  }

  return ret;
}

enum {
  AUDIOPARAM_NAME,
  AUDIOPARAM_SHORTNAME,
  AUDIOPARAM_DEFAULTVALUE,
  AUDIOPARAM_MAXVALUE,
  AUDIOPARAM_MINVALUE,
  AUDIOPARAM_VALUE,
  AUDIOPARAM_SMOOTHED,
};

static JSValue
js_audioparam_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioParamPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!audioparam_class.opaque(ctx, this_val, ap))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_NAME: {
      ret = to_js<std::string>(ctx, (*ap)->name());
      break;
    }

    case AUDIOPARAM_SHORTNAME: {
      ret = to_js<std::string>(ctx, (*ap)->shortName());
      break;
    }
    case AUDIOPARAM_DEFAULTVALUE: {
      ret = to_js<double>(ctx, (*ap)->defaultValue());
      break;
    }
    case AUDIOPARAM_MAXVALUE: {
      ret = to_js<double>(ctx, (*ap)->maxValue());
      break;
    }
    case AUDIOPARAM_MINVALUE: {
      ret = to_js<double>(ctx, (*ap)->minValue());
      break;
    }
    case AUDIOPARAM_VALUE: {
      ret = to_js<double>(ctx, (*ap)->value());
      break;
    }
    case AUDIOPARAM_SMOOTHED: {
      ret = to_js<double>(ctx, (*ap)->smoothedValue());
      break;
    }
  }

  return ret;
}

static JSValue
js_audioparam_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioParamPtr* ap;

  if(!audioparam_class.opaque(ctx, this_val, ap))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOPARAM_VALUE: {
      double d;
      JS_ToFloat64(ctx, &d, value);
      (*ap)->setValue(d);
      break;
    }
  }

  return JS_UNDEFINED;
}

static void
js_audioparam_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioParamPtr* ap;

  ClassObjectMap<lab::AudioParam>::remove(from_js<JSObject*>(this_val));

  if(audioparam_class.opaque(this_val, ap)) {
    ap->~AudioParamPtr();
    js_free_rt(rt, ap);
  }
}

static JSClassDef js_audioparam_class = {
    .class_name = "AudioParam",
    .finalizer = js_audioparam_finalizer,
};

static const JSCFunctionListEntry js_audioparam_methods[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_audioparam_get, 0, AUDIOPARAM_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("value", js_audioparam_get, js_audioparam_set, AUDIOPARAM_VALUE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("shortName", js_audioparam_get, 0, AUDIOPARAM_SHORTNAME),
    JS_CGETSET_MAGIC_DEF("defaultValue", js_audioparam_get, 0, AUDIOPARAM_DEFAULTVALUE),
    JS_CGETSET_MAGIC_DEF("maxValue", js_audioparam_get, 0, AUDIOPARAM_MAXVALUE),
    JS_CGETSET_MAGIC_DEF("minValue", js_audioparam_get, 0, AUDIOPARAM_MINVALUE),
    JS_CGETSET_MAGIC_DEF("smoothedValue", js_audioparam_get, 0, AUDIOPARAM_SMOOTHED),
    JS_CFUNC_MAGIC_DEF("setValueAtTime", 2, js_audioparam_method, AUDIOPARAM_VALUEATTIME),
    JS_CFUNC_MAGIC_DEF("linearRampToValueAtTime", 2, js_audioparam_method, AUDIOPARAM_LINRAMPTOVALUEATTIME),
    JS_CFUNC_MAGIC_DEF("exponentialRampToValueAtTime", 2, js_audioparam_method, AUDIOPARAM_EXPRAMPTOVALUEATTIME),
    JS_CFUNC_MAGIC_DEF("setTargetAtTime", 3, js_audioparam_method, AUDIOPARAM_TARGETATTIME),
    JS_CFUNC_MAGIC_DEF("setValueCurveAtTime", 3, js_audioparam_method, AUDIOPARAM_VALUECURVEATTIME),
    JS_CFUNC_MAGIC_DEF("cancelScheduledValues", 1, js_audioparam_method, AUDIOPARAM_CANCELSCHEDULED),
    JS_CFUNC_MAGIC_DEF("[Symbol.toPrimitive]", 0, js_audioparam_method, AUDIOPARAM_TOPRIMITIVE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioParam", JS_PROP_CONFIGURABLE),
};

static const char* js_audiosetting_types[] = {
    "None",
    "Bool",
    "Integer",
    "Float",
    "Enum",
    "Bus",
    nullptr,
};

static lab::AudioSettingDescriptor
js_audiosetting_descriptor_reset(JSContext* ctx, lab::AudioSettingDescriptor* desc) {
  js_delete(ctx, *const_cast<char**>(&desc->name));
  js_delete(ctx, *const_cast<char**>(&desc->shortName));
  js_delete(ctx, *const_cast<char***>(&desc->enums));
}

static lab::AudioSettingDescriptor
js_audiosetting_descriptor(JSContext* ctx, JSValueConst obj) {
  return lab::AudioSettingDescriptor{
      .name = from_js_property<char*>(ctx, obj, "name"),
      .shortName = from_js_property<char*>(ctx, obj, "shortName"),
      .type = find_enumeration_free<lab::SettingType>(ctx, JS_GetPropertyStr(ctx, obj, "type")),
      .enums = from_js_property<char**>(ctx, obj, "enums"),
  };
}

static lab::AudioSettingDescriptor
js_audiosetting_arguments(JSContext* ctx, int argc, JSValueConst argv[]) {
  return lab::AudioSettingDescriptor{
      .name = from_js<char*>(ctx, argv[0]),
      .shortName = from_js<char*>(ctx, argv[1]),
      .type = find_enumeration<lab::SettingType>(ctx, argv[2]),
      .enums = argc > 3 ? from_js<char**>(ctx, argv[3]) : nullptr,
  };
}

static JSValue
js_audiosetting_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioSettingPtr* ap;

  if(!js_alloc(ctx, ap))
    return JS_EXCEPTION;

  lab::AudioSettingDescriptor desc(argc >= 3 ? js_audiosetting_arguments(ctx, argc, argv) : js_audiosetting_descriptor(ctx, argv[0]));

  shared_ptr<lab::AudioSettingDescriptor> descPtr(new lab::AudioSettingDescriptor(desc), [ctx](lab::AudioSettingDescriptor* dptr) {
    js_audiosetting_descriptor_reset(ctx, dptr);
    delete dptr;
  });

  if(descPtr->type == lab::SettingType::None)
    return JS_ThrowTypeError(ctx, "AudioSetting descriptor .type can't be SettingType::None");

  if(descPtr->type == lab::SettingType::Enum)
    if(descPtr->enums == nullptr)
      return JS_ThrowTypeError(ctx, "AudioSetting descriptor needs .enums array when type is SettingType::Enum");

  new(ap) AudioSettingPtr(make_shared<lab::AudioSetting>(descPtr.get()), descPtr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiosetting_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiosetting_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ap);
  ClassObjectMap<lab::AudioSetting>::set(*ap, from_js<JSObject*>(JS_DupValue(ctx, obj)));
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiosetting_wrap(JSContext* ctx, JSValueConst proto, AudioSettingPtr& setting) {
  AudioSettingPtr* as;

  if(!js_alloc(ctx, as))
    return JS_EXCEPTION;

  new(as) AudioSettingPtr(setting);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audiosetting_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, as);
  ClassObjectMap<lab::AudioSetting>::set(*as, from_js<JSObject*>(JS_DupValue(ctx, obj)));
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiosetting_wrap(JSContext* ctx, AudioSettingPtr& setting) {
  JSObject* obj;

  if((obj = ClassObjectMap<lab::AudioSetting>::get(setting)))
    return JS_DupValue(ctx, to_js(obj));

  return js_audiosetting_wrap(ctx, audiosetting_class.proto, setting);
}

static JSValue
js_audiosetting_setvalue(JSContext* ctx, AudioSettingPtr& as, JSValueConst value, bool notify = true) {
  switch(as->type()) {
    case lab::SettingType::None: {
      break;
    }
    case lab::SettingType::Bool: {
      as->setBool(from_js<BOOL>(ctx, value), notify);
      break;
    }
    case lab::SettingType::Integer: {
      as->setUint32(from_js<uint32_t>(ctx, value), notify);
      break;
    }
    case lab::SettingType::Float: {
      as->setFloat(from_js<double>(ctx, value), notify);
      break;
    }
    case lab::SettingType::Enum: {
      auto enums = range_from(as->enums());
      int size = range_size(enums);
      int32_t val;
      const char* str;

      if(JS_IsNumber(value)) {
        val = from_js<uint32_t>(ctx, value);
      } else if((str = JS_ToCString(ctx, value))) {
        val = find_enumeration(enums, str);
        JS_FreeCString(ctx, str);
      }

      if(val < 0 || val >= size)
        return JS_ThrowRangeError(ctx, "enumeration value not >= 0 and < %d", size);

      as->setEnumeration(val, notify);
      break;
    }
    case lab::SettingType::Bus: {
      AudioBufferPtr* ab;

      if(!audiobuffer_class.opaque(ctx, value, ab))
        return JS_ThrowTypeError(ctx, "must be an AudioBuffer");

      as->setBus(ab->get(), notify);
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_audiosetting_getvalue(JSContext* ctx, AudioSettingPtr& as) {
  JSValue ret = JS_UNDEFINED;

  switch(as->type()) {
    case lab::SettingType::None: {
      ret = JS_ThrowInternalError(ctx, "AudioSetting has type None");
      break;
    }
    case lab::SettingType::Bool: {
      ret = to_js<BOOL>(ctx, !!as->valueBool());
      break;
    }
    case lab::SettingType::Integer: {
      ret = to_js<uint32_t>(ctx, as->valueUint32());
      break;
    }
    case lab::SettingType::Float: {
      ret = to_js<double>(ctx, as->valueFloat());
      break;
    }
    case lab::SettingType::Enum: {
      auto enums = range_from(as->enums());
      uint32_t index = as->valueUint32();
      int size = range_size(enums);

      if(index >= size)
        ret = JS_ThrowRangeError(ctx, "enumeration value not < %d", size);
      else
        ret = to_js<const char*>(ctx, enums.first[index]);
      break;
    }
    case lab::SettingType::Bus: {
      AudioBufferPtr ab(as->valueBus());

      ret = !!ab ? js_audiobuffer_wrap(ctx, ab) : JS_NULL;
      break;
    }
  }

  return ret;
}

enum {
  AUDIOSETTING_TOPRIMITIVE,
  AUDIOSETTING_SETVALUE,
  AUDIOSETTING_SETVALUECHANGED,
  AUDIOSETTING_ENUMFROMNAME,
};

static JSValue
js_audiosetting_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioSettingPtr* as;
  JSValue ret = JS_UNDEFINED;

  if(!audiosetting_class.opaque(ctx, this_val, as))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOSETTING_TOPRIMITIVE: {
      auto hint = argc > 0 ? from_js<std::string>(ctx, argv[0]) : std::string();
      auto type = (*as)->type();

      if(hint != "number" && type == lab::SettingType::Enum) {
        ret = JS_NewString(ctx, (*as)->enums()[(*as)->valueUint32()]);
      } else {
        switch(type) {
          case lab::SettingType::Float: {
            ret = to_js<double>(ctx, (*as)->valueFloat());
            break;
          }
          case lab::SettingType::Integer:
          case lab::SettingType::Enum: {
            ret = to_js<uint32_t>(ctx, (*as)->valueUint32());
            break;
          }
          default: {
            ret = JS_ThrowTypeError(ctx, "AudioSetting has type %d", int((*as)->type()));
            break;
          }
        }
      }

      break;
    }

    case AUDIOSETTING_SETVALUE: {
      ret = js_audiosetting_setvalue(ctx, *as, argv[0], argc > 1 ? !!from_js<BOOL>(ctx, argv[1]) : true);
      break;
    }

    case AUDIOSETTING_SETVALUECHANGED: {
      std::function<void()> fn;

      if(argc > 0)
        fn = [ctx = JS_DupContext(ctx), callback = JS_DupValue(ctx, argv[0]), this_obj = JS_DupValue(ctx, this_val), as]() {
          JSValue args[] = {js_audiosetting_getvalue(ctx, *as)};
          JSValue ret = JS_Call(ctx, callback, this_obj, countof(args), args);
          JS_FreeValue(ctx, ret);
          JS_FreeValue(ctx, args[0]);
          /*JS_FreeValue(ctx, callback);
          JS_FreeContext(ctx);*/
        };
      else
        fn = []() {};

      (*as)->setValueChanged(fn);
      break;
    }

    case AUDIOSETTING_ENUMFROMNAME: {
      int32_t index = -1;
      const char* name;

      if((name = from_js<const char*>(ctx, argv[0]))) {
        index = (*as)->enumFromName(name);
        JS_FreeCString(ctx, name);
      }

      ret = to_js<int32_t>(ctx, index);
      break;
    }
  }

  return ret;
}

enum {
  AUDIOSETTING_NAME,
  AUDIOSETTING_SHORT_NAME,
  AUDIOSETTING_TYPE,
  AUDIOSETTING_VALUE,
  AUDIOSETTING_ENUMS,
};

static JSValue
js_audiosetting_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioSettingPtr* as;
  JSValue ret = JS_UNDEFINED;

  if(!audiosetting_class.opaque(ctx, this_val, as))
    return JS_EXCEPTION;

  int type = int((*as)->type());

  if(!(type >= 0 && type < countof(js_audiosetting_types) && js_audiosetting_types[type]))
    return JS_ThrowInternalError(ctx, "AudioSetting has invalid type %d", type);

  switch(magic) {
    case AUDIOSETTING_NAME: {
      ret = to_js<std::string>(ctx, (*as)->name());
      break;
    }
    case AUDIOSETTING_SHORT_NAME: {
      ret = to_js<std::string>(ctx, (*as)->shortName());
      break;
    }
    case AUDIOSETTING_TYPE: {
      ret = JS_NewString(ctx, js_audiosetting_types[type]);
      break;
    }
    case AUDIOSETTING_VALUE: {
      ret = js_audiosetting_getvalue(ctx, *as);
      break;
    }

    case AUDIOSETTING_ENUMS: {
      if(lab::SettingType(type) == lab::SettingType::Enum) {
        char const* const* enums;

        if(!(enums = (*as)->enums()))
          return JS_ThrowInternalError(ctx, "AudioSetting of type Enum has no enums()");

        auto range = range_from(enums);
        ret = to_js(ctx, range.first, range.second);
      }
      break;
    }
  }

  return ret;
}

static JSValue
js_audiosetting_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioSettingPtr* as;
  JSValue ret = JS_UNDEFINED;

  if(!audiosetting_class.opaque(ctx, this_val, as))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOSETTING_VALUE: {
      ret = js_audiosetting_setvalue(ctx, *as, value);
      break;
    }
  }

  return ret;
}

static void
js_audiosetting_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioSettingPtr* as;

  ClassObjectMap<lab::AudioSetting>::remove(from_js<JSObject*>(this_val));

  if(audiosetting_class.opaque(this_val, as)) {
    as->~AudioSettingPtr();
    js_free_rt(rt, as);
  }
}

static JSClassDef js_audiosetting_class = {
    .class_name = "AudioSetting",
    .finalizer = js_audiosetting_finalizer,
};

static const JSCFunctionListEntry js_audiosetting_methods[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_audiosetting_get, 0, AUDIOSETTING_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("shortName", js_audiosetting_get, 0, AUDIOSETTING_SHORT_NAME),
    JS_CGETSET_MAGIC_DEF("type", js_audiosetting_get, 0, AUDIOSETTING_TYPE),
    JS_CGETSET_MAGIC_FLAGS_DEF("value", js_audiosetting_get, js_audiosetting_set, AUDIOSETTING_VALUE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("enums", js_audiosetting_get, 0, AUDIOSETTING_ENUMS),
    JS_CFUNC_MAGIC_DEF("setValue", 1, js_audiosetting_method, AUDIOSETTING_SETVALUE),
    JS_CFUNC_MAGIC_DEF("setValueChanged", 1, js_audiosetting_method, AUDIOSETTING_SETVALUECHANGED),
    JS_CFUNC_MAGIC_DEF("enumFromName", 1, js_audiosetting_method, AUDIOSETTING_ENUMFROMNAME),
    JS_CFUNC_MAGIC_DEF("[Symbol.toPrimitive]", 0, js_audiosetting_method, AUDIOSETTING_TOPRIMITIVE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioSetting", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiocontext_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  BOOL offline = FALSE, autoDispatchEvents = TRUE;

  if(argc > 0) {
    if(JS_IsObject(argv[0])) {
      offline = from_js_property<BOOL>(ctx, argv[0], "offline");
      autoDispatchEvents = from_js_property<BOOL>(ctx, argv[0], "autoDispatchEvents");
    } else {
      offline = from_js<BOOL>(ctx, argv[0]);
    }
    if(argc > 1)
      autoDispatchEvents = from_js<BOOL>(ctx, argv[1]);
  }

  AudioContextPtr* ac;

  if(!js_alloc(ctx, ac))
    return JS_EXCEPTION;

  new(ac) AudioContextPtr(make_shared<lab::AudioContext>(offline, autoDispatchEvents));

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiocontext_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiocontext_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ac);

  ClassObjectMap<lab::AudioContext>::set(*ac, from_js<JSObject*>(JS_DupValue(ctx, obj)));

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiocontext_wrap(JSContext* ctx, JSValueConst proto, AudioContextPtr& context) {
  AudioContextPtr* ac;

  if(!js_alloc(ctx, ac))
    return JS_EXCEPTION;

  new(ac) AudioContextPtr(context);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audiocontext_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ac);

  ClassObjectMap<lab::AudioContext>::set(*ac, from_js<JSObject*>(JS_DupValue(ctx, obj)));

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiocontext_wrap(JSContext* ctx, AudioContextPtr& context) {
  JSObject* obj;

  if((obj = ClassObjectMap<lab::AudioContext>::get(context)))
    return JS_DupValue(ctx, to_js(obj));

  return js_audiocontext_wrap(ctx, audiocontext_class.proto, context);
}

enum {
  AUDIOCONTEXT_SUSPEND,
  AUDIOCONTEXT_RESUME,
  AUDIOCONTEXT_CLOSE,
  AUDIOCONTEXT_SYNCHRONIZECONNECTIONS,
  AUDIOCONTEXT_STARTOFFLINERENDERING,
  AUDIOCONTEXT_ENQUEUEEVENT,
  AUDIOCONTEXT_DISPATCHEVENTS,
  AUDIOCONTEXT_DECODEAUDIODATA,
  AUDIOCONTEXT_CREATEBUFFER,
  AUDIOCONTEXT_CREATEPERIODICWAVE,
};

static JSValue
js_audiocontext_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!audiocontext_class.opaque(ctx, this_val, ac))
    return JS_EXCEPTION;

  if(magic <= AUDIOCONTEXT_STARTOFFLINERENDERING) {
    if(!(*ac)->destinationNode())
      return JS_ThrowInternalError(ctx, "AudioContext has no destination node");

    if(!(*ac)->destinationNode()->device())
      return JS_ThrowInternalError(ctx, "AudioContext's AudioDestinationNode has no AudioDevice");
  }

  switch(magic) {

    case AUDIOCONTEXT_SUSPEND: {
      (*ac)->suspend();
      break;
    }
    case AUDIOCONTEXT_RESUME: {
      (*ac)->resume();
      break;
    }
    case AUDIOCONTEXT_CLOSE: {
      (*ac)->close();
      break;
    }

    case AUDIOCONTEXT_SYNCHRONIZECONNECTIONS: {
      (*ac)->synchronizeConnections(argc > 0 ? from_js<int32_t>(ctx, argv[0]) : 1000);
      break;
    }
    case AUDIOCONTEXT_STARTOFFLINERENDERING: {
      if(argc > 0) {
        if(!JS_IsFunction(ctx, argv[0]))
          return JS_ThrowTypeError(ctx, "argument 1 must be a function");

        std::function<void()> fn = [ctx = JS_DupContext(ctx), fn = JS_DupValue(ctx, argv[0]), this_val]() {
          JSValue ret = JS_Call(ctx, fn, this_val, 0, 0);
          JS_FreeValue(ctx, ret);
          JS_FreeValue(ctx, fn);
          JS_FreeContext(ctx);
        };
        (*ac)->offlineRenderCompleteCallback = fn;
      }

      (*ac)->startOfflineRendering();
      break;
    }
    case AUDIOCONTEXT_ENQUEUEEVENT: {
      std::function<void()> fn = [ctx = JS_DupContext(ctx), fn = JS_DupValue(ctx, argv[0]), this_val]() {
        JSValue ret = JS_Call(ctx, fn, this_val, 0, 0);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, fn);
        JS_FreeContext(ctx);
      };

      (*ac)->enqueueEvent(fn);
      break;
    }

    case AUDIOCONTEXT_DISPATCHEVENTS: {
      (*ac)->dispatchEvents();
      break;
    }

    case AUDIOCONTEXT_DECODEAUDIODATA: {
      AudioBufferPtr ab = lab::MakeBusFromFile(from_js<std::string>(ctx, argv[0]), argc > 1 ? from_js<BOOL>(ctx, argv[1]) : false);

      if(!bool(ab))
        return JS_ThrowInternalError(ctx, "Failed reading sample.");

      ret = js_audiobuffer_wrap(ctx, ab);
      break;
    }
    case AUDIOCONTEXT_CREATEBUFFER: {
      AudioBufferPtr ab = make_shared<lab::AudioBus>(from_js<int32_t>(ctx, argv[0]), from_js<int32_t>(ctx, argv[1]));

      ab->setSampleRate(argc > 2 ? from_js<double>(ctx, argv[2]) : (*ac)->sampleRate());

      ret = js_audiobuffer_wrap(ctx, ab);
      break;
    }
    case AUDIOCONTEXT_CREATEPERIODICWAVE: {
      break;
    }
  }

  return ret;
}

enum {
  AUDIOCONTEXT_CREATEANALYSER,
  AUDIOCONTEXT_CREATEBIQUADFILTER,
  AUDIOCONTEXT_CREATEBUFFERSOURCE,
  AUDIOCONTEXT_CREATECHANNELMERGER,
  AUDIOCONTEXT_CREATECHANNELSPLITTER,
  AUDIOCONTEXT_CREATECONSTANTSOURCE,
  AUDIOCONTEXT_CREATECONVOLVER,
  AUDIOCONTEXT_CREATEDELAY,
  AUDIOCONTEXT_CREATEDYNAMICSCOMPRESSOR,
  AUDIOCONTEXT_CREATEGAIN,
  AUDIOCONTEXT_CREATEOSCILLATOR,
  AUDIOCONTEXT_CREATEPANNER,
  AUDIOCONTEXT_CREATESTEREOPANNER,
  AUDIOCONTEXT_CREATEWAVESHAPER,
};
static JSValue
js_audiocontext_create(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioContextPtr* ac;
  shared_ptr<lab::AudioNode> node;

  if(!audiocontext_class.opaque(ctx, this_val, ac))
    return JS_EXCEPTION;

  lab::AudioContext& context = *ac->get();

  switch(magic) {
    case AUDIOCONTEXT_CREATEANALYSER: {
      node = make_shared<lab::AnalyserNode>(context, from_js<int32_t>(ctx, argv[0]));
      break;
    }
    case AUDIOCONTEXT_CREATEBIQUADFILTER: {
      node = make_shared<lab::BiquadFilterNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATEBUFFERSOURCE: {
      node = make_shared<lab::SampledAudioNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATECHANNELMERGER: {
      node = make_shared<lab::ChannelMergerNode>(*ac->get(), argc > 0 ? from_js<int32_t>(ctx, argv[0]) : 1);
      break;
    }
    case AUDIOCONTEXT_CREATECHANNELSPLITTER: {
      node = make_shared<lab::ChannelSplitterNode>(*ac->get(), argc > 0 ? from_js<int32_t>(ctx, argv[0]) : 1);
      break;
    }
    case AUDIOCONTEXT_CREATECONSTANTSOURCE: {
      node = make_shared<lab::ConstantSourceNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATECONVOLVER: {
      node = make_shared<lab::ConvolverNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATEDELAY: {
      node = make_shared<lab::DelayNode>(*ac->get(), argc > 0 ? from_js<double>(ctx, argv[0]) : 2.0);
      break;
    }
    case AUDIOCONTEXT_CREATEDYNAMICSCOMPRESSOR: {
      node = make_shared<lab::DynamicsCompressorNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATEGAIN: {
      node = make_shared<lab::GainNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATEOSCILLATOR: {
      node = make_shared<lab::OscillatorNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATEPANNER: {
      node = make_shared<lab::PannerNode>(*ac->get());
      break;
    }

    case AUDIOCONTEXT_CREATESTEREOPANNER: {
      node = make_shared<lab::StereoPannerNode>(*ac->get());
      break;
    }
    case AUDIOCONTEXT_CREATEWAVESHAPER: {
      node = make_shared<lab::WaveShaperNode>(*ac->get());
      break;
    }
  }

  if(bool(node)) {
    AudioNodePtr anodeptr(node, *ac);

    std::cout << "use_count = " << node.use_count() << std::endl;

    return js_audionode_wrap(ctx, anodeptr);
  }

  return JS_ThrowTypeError(ctx, "create");
}

enum {
  AUDIOCONTEXT_SAMPLEFRAME,
  AUDIOCONTEXT_PREDICTEDTIME,
  AUDIOCONTEXT_INITIALIZED,
  AUDIOCONTEXT_AUTODISPATCH,
  AUDIOCONTEXT_OFFLINE,
  AUDIOCONTEXT_SAMPLERATE,
  AUDIOCONTEXT_DESTINATION,
  AUDIOCONTEXT_LISTENER,
  AUDIOCONTEXT_CURRENTTIME,
};

static JSValue
js_audiocontext_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!audiocontext_class.opaque(ctx, this_val, ac))
    return JS_EXCEPTION;

  if(magic <= AUDIOCONTEXT_PREDICTEDTIME)
    if(!(*ac)->destinationNode())
      return JS_ThrowInternalError(ctx, "AudioContext has no destination node");

  switch(magic) {
    case AUDIOCONTEXT_SAMPLEFRAME: {
      if((*ac)->destinationNode())
        ret = JS_NewInt64(ctx, (*ac)->currentSampleFrame());
      break;
    }
    case AUDIOCONTEXT_PREDICTEDTIME: {
      if((*ac)->destinationNode())
        ret = JS_NewFloat64(ctx, (*ac)->predictedCurrentTime());
      break;
    }
    case AUDIOCONTEXT_INITIALIZED: {
      ret = JS_NewBool(ctx, !!(*ac)->isInitialized());
      break;
    }
    case AUDIOCONTEXT_AUTODISPATCH: {
      ret = JS_NewBool(ctx, !!(*ac)->isAutodispatchingEvents());
      break;
    }
    case AUDIOCONTEXT_OFFLINE: {
      ret = JS_NewBool(ctx, !!(*ac)->isOfflineContext());
      break;
    }
    case AUDIOCONTEXT_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*ac)->sampleRate());
      break;
    }
    case AUDIOCONTEXT_DESTINATION: {
      AudioDestinationNodePtr adn((*ac)->destinationNode(), *ac);
      ret = !!adn ? js_audiodestinationnode_wrap(ctx, adn) : JS_NULL;
      break;
    }
    case AUDIOCONTEXT_LISTENER: {
      AudioListenerPtr al = (*ac)->listener();
      ret = !!al ? js_audiolistener_wrap(ctx, al) : JS_NULL;
      break;
    }
    case AUDIOCONTEXT_CURRENTTIME: {
      ret = JS_NewFloat64(ctx, (*ac)->currentTime());
      break;
    }
  }

  return ret;
}

static JSValue
js_audiocontext_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!audiocontext_class.opaque(ctx, this_val, ac))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOCONTEXT_DESTINATION: {
      AudioDestinationNodePtr* sadn;

      if(!audiodestinationnode_class.opaque(ctx, value, sadn))
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

  ClassObjectMap<lab::AudioContext>::remove(from_js<JSObject*>(this_val));

  if(audiocontext_class.opaque(this_val, ac)) {
    ac->~AudioContextPtr();
    js_free_rt(rt, ac);
  }
}

static JSClassDef js_audiocontext_class = {
    .class_name = "AudioContext",
    .finalizer = js_audiocontext_finalizer,
};

static const JSCFunctionListEntry js_audiocontext_methods[] = {
    JS_CFUNC_MAGIC_DEF("suspend", 0, js_audiocontext_method, AUDIOCONTEXT_SUSPEND),
    JS_CFUNC_MAGIC_DEF("resume", 0, js_audiocontext_method, AUDIOCONTEXT_RESUME),
    JS_CFUNC_MAGIC_DEF("close", 0, js_audiocontext_method, AUDIOCONTEXT_CLOSE),
    JS_CFUNC_MAGIC_DEF("synchronizeConnections", 1, js_audiocontext_method, AUDIOCONTEXT_SYNCHRONIZECONNECTIONS),
    JS_CFUNC_MAGIC_DEF("startOfflineRendering", 0, js_audiocontext_method, AUDIOCONTEXT_STARTOFFLINERENDERING),
    JS_CFUNC_MAGIC_DEF("enqueueEvent", 1, js_audiocontext_method, AUDIOCONTEXT_ENQUEUEEVENT),
    JS_CFUNC_MAGIC_DEF("dispatchEvents", 0, js_audiocontext_method, AUDIOCONTEXT_DISPATCHEVENTS),
    JS_CFUNC_MAGIC_DEF("decodeAudioData", 1, js_audiocontext_method, AUDIOCONTEXT_DECODEAUDIODATA),
    JS_CGETSET_MAGIC_DEF("initialized", js_audiocontext_get, 0, AUDIOCONTEXT_INITIALIZED),
    JS_CGETSET_MAGIC_DEF("autoDispatchEvents", js_audiocontext_get, 0, AUDIOCONTEXT_AUTODISPATCH),
    JS_CGETSET_MAGIC_DEF("offline", js_audiocontext_get, 0, AUDIOCONTEXT_OFFLINE),
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiocontext_get, 0, AUDIOCONTEXT_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("destination", js_audiocontext_get, js_audiocontext_set, AUDIOCONTEXT_DESTINATION),
    JS_CGETSET_MAGIC_DEF("listener", js_audiocontext_get, 0, AUDIOCONTEXT_LISTENER),
    JS_CGETSET_MAGIC_DEF("currentTime", js_audiocontext_get, 0, AUDIOCONTEXT_CURRENTTIME),
    JS_CGETSET_MAGIC_DEF("currentSampleFrame", js_audiocontext_get, 0, AUDIOCONTEXT_SAMPLEFRAME),
    JS_CGETSET_MAGIC_DEF("predictedCurrentTime", js_audiocontext_get, 0, AUDIOCONTEXT_PREDICTEDTIME),
    JS_CFUNC_MAGIC_DEF("createBuffer", 0, js_audiocontext_method, AUDIOCONTEXT_CREATEBUFFER),
    JS_CFUNC_MAGIC_DEF("createPeriodicWave", 0, js_audiocontext_method, AUDIOCONTEXT_CREATEPERIODICWAVE),
    JS_CFUNC_MAGIC_DEF("createAnalyser", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEANALYSER),
    JS_CFUNC_MAGIC_DEF("createBiquadFilter", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEBIQUADFILTER),
    JS_CFUNC_MAGIC_DEF("createBufferSource", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEBUFFERSOURCE),
    JS_CFUNC_MAGIC_DEF("createChannelMerger", 0, js_audiocontext_create, AUDIOCONTEXT_CREATECHANNELMERGER),
    JS_CFUNC_MAGIC_DEF("createChannelSplitter", 0, js_audiocontext_create, AUDIOCONTEXT_CREATECHANNELSPLITTER),
    JS_CFUNC_MAGIC_DEF("createConstantSource", 0, js_audiocontext_create, AUDIOCONTEXT_CREATECONSTANTSOURCE),
    JS_CFUNC_MAGIC_DEF("createConvolver", 0, js_audiocontext_create, AUDIOCONTEXT_CREATECONVOLVER),
    JS_CFUNC_MAGIC_DEF("createDelay", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEDELAY),
    JS_CFUNC_MAGIC_DEF("createDynamicsCompressor", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEDYNAMICSCOMPRESSOR),
    JS_CFUNC_MAGIC_DEF("createGain", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEGAIN),
    JS_CFUNC_MAGIC_DEF("createOscillator", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEOSCILLATOR),
    JS_CFUNC_MAGIC_DEF("createPanner", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEPANNER),
    JS_CFUNC_MAGIC_DEF("createStereoPanner", 0, js_audiocontext_create, AUDIOCONTEXT_CREATESTEREOPANNER),
    JS_CFUNC_MAGIC_DEF("createWaveShaper", 0, js_audiocontext_create, AUDIOCONTEXT_CREATEWAVESHAPER),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioContext", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiolistener_wrap(JSContext* ctx, JSValueConst proto, AudioListenerPtr& listener) {
  AudioListenerPtr* al;

  if(!js_alloc(ctx, al))
    return JS_EXCEPTION;

  new(al) AudioListenerPtr(listener);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audiolistener_class);

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
  return js_audiolistener_wrap(ctx, audiolistener_class.proto, listener);
}

static JSValue
js_audiolistener_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioListenerPtr* al;

  if(!js_alloc(ctx, al))
    return JS_EXCEPTION;

  new(al) AudioListenerPtr(make_shared<lab::AudioListener>());

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiolistener_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiolistener_class);
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

  if(!audiolistener_class.opaque(ctx, this_val, al))
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

  if(!audiolistener_class.opaque(ctx, this_val, al))
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

  if(audiolistener_class.opaque(this_val, al)) {
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

static lab::AudioStreamConfig
js_audiostream_config(JSContext* ctx, JSValueConst obj) {
  return lab::AudioStreamConfig{
      .device_index = from_js_property<int32_t>(ctx, obj, "deviceIndex", -1),
      .desired_channels = from_js_property<uint32_t>(ctx, obj, "desiredChannels", 0),
      .desired_samplerate = static_cast<float>(from_js_property<double>(ctx, obj, "desiredSampleRate", 0)),
  };
}
struct AudioDeviceInfo {
  int32_t index{-1};
  std::string identifier;
  uint32_t num_output_channels{0};
  uint32_t num_input_channels{0};
  std::vector<float> supported_samplerates;
  float nominal_samplerate{0};
  bool is_default_output{false};
  bool is_default_input{false};
};

static JSValue
js_audiodevice_info(JSContext* ctx, lab::AudioDeviceInfo* adi) {
  JSValue ret = JS_NewObjectProto(ctx, JS_NULL);
  to_js_property(ctx, ret, "index", adi->index);
  to_js_property(ctx, ret, "identifier", adi->identifier.c_str());
  to_js_property(ctx, ret, "numOutputChannels", adi->num_output_channels);
  to_js_property(ctx, ret, "numInputChannels", adi->num_input_channels);

  std::vector<double> samplerates;
  std::transform(adi->supported_samplerates.begin(), adi->supported_samplerates.end(), std::back_inserter(samplerates), [](float f) -> double { return f; });

  to_js_property(ctx, ret, "supportedSamplerates", samplerates);
  to_js_property(ctx, ret, "nominalSamplerate", double(adi->nominal_samplerate));
  to_js_property(ctx, ret, "isDefaultOutput", BOOL(adi->is_default_output));
  to_js_property(ctx, ret, "isDefaultInput", double(adi->is_default_input));

  return ret;
}

static JSValue
js_audiodevice_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioDevicePtr* ad;

  if(!js_alloc(ctx, ad))
    return JS_EXCEPTION;

  new(ad) AudioDevicePtr(
      make_shared<lab::AudioDevice_RtAudio>(js_audiostream_config(ctx, argc > 0 ? argv[0] : JS_UNDEFINED), js_audiostream_config(ctx, argc > 1 ? argv[1] : JS_UNDEFINED)));

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiodevice_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiodevice_class);
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
  AUDIODEVICE_START,
  AUDIODEVICE_STOP,
  AUDIODEVICE_BACKENDREINITIALIZE,
};

static JSValue
js_audiodevice_wrap(JSContext* ctx, JSValueConst proto, AudioDevicePtr& device) {
  AudioDevicePtr* ad;

  if(!js_alloc(ctx, ad))
    return JS_EXCEPTION;

  new(ad) AudioDevicePtr(device);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audiodevice_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ad);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audiodevice_wrap(JSContext* ctx, AudioDevicePtr& device) {
  return js_audiodevice_wrap(ctx, audiodevice_class.proto, device);
}

static JSValue
js_audiodevice_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!audiodevice_class.opaque(ctx, this_val, ad))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIODEVICE_START: {
      (*ad)->start();
      break;
    }
    case AUDIODEVICE_STOP: {
      (*ad)->stop();
      break;
    }
    case AUDIODEVICE_BACKENDREINITIALIZE: {
      (*ad)->backendReinitialize();
      break;
    }
  }

  return ret;
}

enum {
  AUDIODEVICE_DEVICELIST,
};

static JSValue
js_audiodevice_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case AUDIODEVICE_DEVICELIST: {
      vector<lab::AudioDeviceInfo> audioDevices = lab::AudioDevice_RtAudio::MakeAudioDeviceList();

      ret = js_array_build<lab::AudioDeviceInfo>(ctx, audioDevices.begin(), audioDevices.end(), js_audiodevice_info);
      break;
    }
  }

  return ret;
}

enum {
  AUDIODEVICE_RUNNING,
  AUDIODEVICE_DESTINATION,
};

static JSValue
js_audiodevice_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!audiodevice_class.opaque(ctx, this_val, ad))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIODEVICE_RUNNING: {
      ret = JS_NewBool(ctx, !!(*ad)->isRunning());
      break;
    }
    case AUDIODEVICE_DESTINATION: {
      shared_ptr<lab::AudioDestinationNode> adn = reinterpret_cast<Device*>(ad->get())->destination();

      if(adn) {
        shared_ptr<lab::AudioContext> context = reinterpret_cast<DestinationNode*>(adn.get())->getContext();

        AudioDestinationNodePtr sadn(adn, context);

        ret = !!sadn ? js_audiodestinationnode_wrap(ctx, sadn) : JS_NULL;
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_audiodevice_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!audiodevice_class.opaque(ctx, this_val, ad))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIODEVICE_DESTINATION: {
      AudioDestinationNodePtr* sadn;

      if(!audiodestinationnode_class.opaque(ctx, value, sadn))
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

  if(audiodevice_class.opaque(this_val, ad)) {
    ad->~AudioDevicePtr();
    js_free_rt(rt, ad);
  }
}

static JSClassDef js_audiodevice_class = {
    .class_name = "AudioDevice",
    .finalizer = js_audiodevice_finalizer,
};

static const JSCFunctionListEntry js_audiodevice_methods[] = {
    JS_CFUNC_MAGIC_DEF("start", 0, js_audiodevice_method, AUDIODEVICE_START),
    JS_CFUNC_MAGIC_DEF("stop", 0, js_audiodevice_method, AUDIODEVICE_STOP),
    JS_CFUNC_MAGIC_DEF("backendReinitialize", 0, js_audiodevice_method, AUDIODEVICE_BACKENDREINITIALIZE),
    JS_CGETSET_MAGIC_DEF("running", js_audiodevice_get, 0, AUDIODEVICE_RUNNING),
    JS_CGETSET_MAGIC_DEF("destination", js_audiodevice_get, js_audiodevice_set, AUDIODEVICE_DESTINATION),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDevice", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_audiodevice_functions[] = {
    JS_CFUNC_MAGIC_DEF("deviceList", 0, js_audiodevice_function, AUDIODEVICE_DEVICELIST),
};

static JSValue
js_audionodeinput_wrap(JSContext* ctx, JSValueConst proto, AudioNodeInputPtr& nodeinput) {
  AudioNodeInputPtr* ani;

  if(!js_alloc(ctx, ani))
    return JS_EXCEPTION;

  new(ani) AudioNodeInputPtr(nodeinput);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audionodeinput_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ani);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audionodeinput_wrap(JSContext* ctx, AudioNodeInputPtr& nodeinput) {
  return js_audionodeinput_wrap(ctx, audionodeinput_class.proto, nodeinput);
}

static JSValue
js_audionodeinput_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioNodeInputPtr* ani;
  AudioNodePtr* an;
  uint32_t processingSizeInFrames = lab::AudioNode::ProcessingSizeInFrames;

  if(!js_alloc(ctx, ani))
    return JS_EXCEPTION;

  if(!audionode_class.opaque(ctx, argv[0], an))
    return JS_ThrowTypeError(ctx, "argument 1 must be an AudioNode");

  if(argc > 1)
    processingSizeInFrames = from_js<uint32_t>(ctx, argv[1]);

  new(ani) AudioNodeInputPtr(make_shared<lab::AudioNodeInput>(an->get(), processingSizeInFrames));

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audionodeinput_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audionodeinput_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ani);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AUDIONODEINPUT_NAME,
  AUDIONODEINPUT_NUMBEROFCHANNELS,
};

static JSValue
js_audionodeinput_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioNodeInputPtr* ani;
  JSValue ret = JS_UNDEFINED;

  if(!audionodeinput_class.opaque(ctx, this_val, ani))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODEINPUT_NAME: {
      ret = to_js<std::string>(ctx, (*ani)->name());
      break;
    }
    case AUDIONODEINPUT_NUMBEROFCHANNELS: {
      lab::ContextRenderLock lock(nullptr, "get numberOfChannels");

      ret = to_js<int32_t>(ctx, (*ani)->numberOfChannels(lock));
      break;
    }
  }

  return ret;
}

static JSValue
js_audionodeinput_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioNodeInputPtr* ani;
  JSValue ret = JS_UNDEFINED;

  if(!audionodeinput_class.opaque(ctx, this_val, ani))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODEINPUT_NAME: {
      (*ani)->setName(from_js<std::string>(ctx, value));
      break;
    }
  }

  return ret;
}

static void
js_audionodeinput_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioNodeInputPtr* ani;

  if(audionodeinput_class.opaque(this_val, ani)) {
    ani->~AudioNodeInputPtr();
    js_free_rt(rt, ani);
  }
}

static JSClassDef js_audionodeinput_class = {
    .class_name = "AudioNodeInput",
    .finalizer = js_audionodeinput_finalizer,
};

static const JSCFunctionListEntry js_audionodeinput_methods[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_audionodeinput_get, js_audionodeinput_set, AUDIONODEINPUT_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("numberOfChannels", js_audionodeinput_get, 0, AUDIONODEINPUT_NUMBEROFCHANNELS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioNodeInput", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audionodeoutput_wrap(JSContext* ctx, JSValueConst proto, AudioNodeOutputPtr& nodeoutput) {
  AudioNodeOutputPtr* ano;

  if(!js_alloc(ctx, ano))
    return JS_EXCEPTION;

  new(ano) AudioNodeOutputPtr(nodeoutput);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audionodeoutput_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ano);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audionodeoutput_wrap(JSContext* ctx, AudioNodeOutputPtr& nodeoutput) {
  return js_audionodeoutput_wrap(ctx, audionodeoutput_class.proto, nodeoutput);
}

static JSValue
js_audionodeoutput_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioNodeOutputPtr* ano;
  AudioNodePtr* an;
  const char* name = nullptr;
  uint32_t numberOfChannels = 0, processingSizeInFrames = lab::AudioNode::ProcessingSizeInFrames;

  if(!js_alloc(ctx, ano))
    return JS_EXCEPTION;

  if(!audionode_class.opaque(ctx, argv[0], an))
    return JS_ThrowTypeError(ctx, "argument 1 must be an AudioNode");

  if(argc > 1) {
    if(JS_IsString(argv[1])) {
      name = JS_ToCString(ctx, argv[1]);
      ++argv;
      --argc;
    }
  }

  if(argc > 1)
    numberOfChannels = from_js<uint32_t>(ctx, argv[1]);
  if(argc > 2)
    processingSizeInFrames = from_js<uint32_t>(ctx, argv[2]);

  if(name) {
    new(ano) AudioNodeOutputPtr(make_shared<lab::AudioNodeOutput>(an->get(), name, numberOfChannels, processingSizeInFrames));
    JS_FreeCString(ctx, name);
  } else
    new(ano) AudioNodeOutputPtr(make_shared<lab::AudioNodeOutput>(an->get(), numberOfChannels, processingSizeInFrames));

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audionodeoutput_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audionodeoutput_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ano);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AUDIONODEOUTPUT_NAME,
  AUDIONODEOUTPUT_NUMBEROFCHANNELS,
  AUDIONODEOUTPUT_CONNECTED,
  AUDIONODEOUTPUT_RENDERINGFANOUTCOUNT,
  AUDIONODEOUTPUT_RENDERINGPARAMFANOUTCOUNT,
};

static JSValue
js_audionodeoutput_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioNodeOutputPtr* ano;
  JSValue ret = JS_UNDEFINED;

  if(!audionodeoutput_class.opaque(ctx, this_val, ano))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODEOUTPUT_NAME: {
      ret = to_js<std::string>(ctx, (*ano)->name());
      break;
    }
    case AUDIONODEOUTPUT_NUMBEROFCHANNELS: {
      ret = to_js<int32_t>(ctx, (*ano)->numberOfChannels());
      break;
    }
    case AUDIONODEOUTPUT_CONNECTED: {
      ret = to_js<BOOL>(ctx, !!(*ano)->isConnected());
      break;
    }
    case AUDIONODEOUTPUT_RENDERINGFANOUTCOUNT: {
      ret = to_js<int32_t>(ctx, (*ano)->renderingFanOutCount());
      break;
    }
    case AUDIONODEOUTPUT_RENDERINGPARAMFANOUTCOUNT: {
      ret = to_js<int32_t>(ctx, (*ano)->renderingParamFanOutCount());
      break;
    }
  }

  return ret;
}

static JSValue
js_audionodeoutput_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioNodeOutputPtr* ano;
  JSValue ret = JS_UNDEFINED;

  if(!audionodeoutput_class.opaque(ctx, this_val, ano))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_audionodeoutput_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioNodeOutputPtr* ano;

  if(audionodeoutput_class.opaque(this_val, ano)) {
    ano->~AudioNodeOutputPtr();
    js_free_rt(rt, ano);
  }
}

static JSClassDef js_audionodeoutput_class = {
    .class_name = "AudioNodeOutput",
    .finalizer = js_audionodeoutput_finalizer,
};

static const JSCFunctionListEntry js_audionodeoutput_methods[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_audionodeoutput_get, 0, AUDIONODEOUTPUT_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("numberOfChannels", js_audionodeoutput_get, 0, AUDIONODEOUTPUT_NUMBEROFCHANNELS),
    JS_CGETSET_MAGIC_DEF("connected", js_audionodeoutput_get, 0, AUDIONODEOUTPUT_CONNECTED),
    JS_CGETSET_MAGIC_DEF("renderingFanOutCount", js_audionodeoutput_get, 0, AUDIONODEOUTPUT_RENDERINGFANOUTCOUNT),
    JS_CGETSET_MAGIC_DEF("renderingParamFanOutCount", js_audionodeoutput_get, 0, AUDIONODEOUTPUT_RENDERINGPARAMFANOUTCOUNT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioNodeOutput", JS_PROP_CONFIGURABLE),
};

static lab::AudioNodeDescriptor
js_audionode_descriptor2(JSContext* ctx, JSValueConst obj) {
  lab::AudioParamDescriptor* params = js_array_get(ctx, JS_GetPropertyStr(ctx, obj, "params"), js_audioparam_descriptor_get, true);
  lab::AudioSettingDescriptor* settings = js_array_get(ctx, JS_GetPropertyStr(ctx, obj, "settings"), js_audiosetting_descriptor, true);
  int initialChannelCount = from_js_property<int32_t>(ctx, obj, "initialChannelCount");

  return lab::AudioNodeDescriptor{params, settings, initialChannelCount};
}

static JSValue
js_audionode_descriptor(JSContext* ctx, lab::AudioNodeDescriptor* nd) {
  JSValue ret = JS_NewObjectProto(ctx, JS_NULL);

  // auto r = range_from(const_cast<lab::AudioParamDescriptor* >(nd->params));
  //
  //
  JS_SetPropertyStr(ctx, ret, "params", js_array_build<lab::AudioParamDescriptor>(ctx, nd->params, nd->params + size(nd->params), js_audioparam_descriptor));

  to_js_property<int32_t>(ctx, ret, "initialChannelCount", nd->initialChannelCount);

  return ret;
}

static JSValue
js_audionode_wrap(JSContext* ctx, JSValueConst proto, const AudioNodePtr& anode) {
  AudioNodePtr* an;

  if(!js_alloc(ctx, an))
    return JS_EXCEPTION;

  new(an) AudioNodePtr(anode, anode.value);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audionode_class);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, an);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_audionode_wrap(JSContext* ctx, const AudioNodePtr& anode) {
  return js_audionode_wrap(ctx, audionode_class.proto, anode);
}

enum {
  AUDIONODE_CONNECT,
  AUDIONODE_DISCONNECT,
  AUDIONODE_CONNECTPARAM,
  AUDIONODE_DISCONNECTPARAM,
  AUDIONODE_ISCONNECTED,
  AUDIONODE_ISSCHEDULEDNODE,
  AUDIONODE_INITIALIZE,
  AUDIONODE_UNINITIALIZE,
  AUDIONODE_PARAMINDEX,
  AUDIONODE_PARAM,
  AUDIONODE_SETTINGINDEX,
  AUDIONODE_SETTING,
  AUDIONODE_INPUT,
  AUDIONODE_OUTPUT,

};

static JSValue
js_audionode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!audionode_class.opaque(ctx, this_val, an))
    return JS_EXCEPTION;

  shared_ptr<lab::AudioContext> ac(an->value);

  switch(magic) {
    case AUDIONODE_CONNECT: {
      AudioNodePtr* destination;

      if(!audionode_class.opaque(ctx, argv[0], destination))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioNode");

      ac->connect(*destination, *an, argc > 1 ? from_js<int32_t>(ctx, argv[1]) : 0, argc > 2 ? from_js<int32_t>(ctx, argv[2]) : 0);
      break;
    }

    case AUDIONODE_DISCONNECT: {
      AudioNodePtr* destination;

      if(argc == 0 || JS_IsNumber(argv[0])) {
        ac->disconnect(*an, argc > 0 ? from_js<int32_t>(ctx, argv[0]) : 0);
      } else if(audionode_class.opaque(ctx, argv[0], destination)) {
        ac->disconnect(*destination, *an, argc > 1 ? from_js<int32_t>(ctx, argv[1]) : 0, argc > 2 ? from_js<int32_t>(ctx, argv[2]) : 0);
      } else {
        return JS_ThrowTypeError(ctx, "argument 1 must be a Number or an AudioNode");
      }

      break;
    }
    case AUDIONODE_ISCONNECTED: {
      AudioNodePtr* destination;

      if(!audionode_class.opaque(ctx, argv[0], destination))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioNode");

      ret = JS_NewBool(ctx, !!ac->isConnected(*destination, *an));
      break;
    }
    case AUDIONODE_CONNECTPARAM: {
      AudioParamPtr* param;
      AudioNodePtr* dest;

      if(audioparam_class.opaque(ctx, argv[0], param)) {
        ac->connectParam(*param, *an, argc > 1 ? from_js<int32_t>(ctx, argv[1]) : 0);
      } else if(audionode_class.opaque(ctx, argv[0], dest)) {
        ac->connectParam(*dest, from_js<std::string>(ctx, argv[1]).c_str(), *an, from_js<int32_t>(ctx, argv[2]));
      } else {
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioParam or an AudioNode");
      }

      break;
    }
    case AUDIONODE_DISCONNECTPARAM: {
      AudioParamPtr* param;
      AudioNodePtr* driver;

      if(!audioparam_class.opaque(ctx, argv[0], param))
        return JS_ThrowTypeError(ctx, "argument 1 must be an AudioParam");

      if(!audionode_class.opaque(ctx, argv[1], driver))
        return JS_ThrowTypeError(ctx, "argument 2 must be an AudioNode");

      ac->disconnectParam(*param, *driver, from_js<int32_t>(ctx, argv[2]));
      break;
    }
    case AUDIONODE_ISSCHEDULEDNODE: {
      ret = JS_NewBool(ctx, !!(*an)->isScheduledNode());
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
    case AUDIONODE_PARAMINDEX: {
      const char* str;

      if((str = JS_ToCString(ctx, argv[0]))) {
        ret = to_js<int32_t>(ctx, (*an)->param_index(str));
        JS_FreeCString(ctx, str);
      }

      break;
    }
    case AUDIONODE_PARAM: {
      AudioParamPtr ap;
      const char* str;

      if(JS_IsNumber(argv[0])) {
        ap = AudioParamPtr((*an)->param(from_js<int32_t>(ctx, argv[0])), nullptr);
      } else if((str = JS_ToCString(ctx, argv[0]))) {
        ap = AudioParamPtr((*an)->param(str), nullptr);
        JS_FreeCString(ctx, str);
      }

      ret = !!ap ? js_audioparam_wrap(ctx, ap) : JS_NULL;
      break;
    }
    case AUDIONODE_SETTINGINDEX: {
      const char* str;

      if((str = JS_ToCString(ctx, argv[0]))) {
        ret = to_js<int32_t>(ctx, (*an)->setting_index(str));
        JS_FreeCString(ctx, str);
      }

      break;
    }
    case AUDIONODE_SETTING: {
      AudioSettingPtr as;
      const char* str;

      if(JS_IsNumber(argv[0])) {
        as = AudioSettingPtr((*an)->setting(from_js<int32_t>(ctx, argv[0])), nullptr);
      } else if((str = JS_ToCString(ctx, argv[0]))) {
        as = AudioSettingPtr((*an)->setting(str), nullptr);
        JS_FreeCString(ctx, str);
      }

      ret = !!as ? js_audiosetting_wrap(ctx, as) : JS_NULL;
      break;
    }
    case AUDIONODE_INPUT: {
      AudioNodeInputPtr ani;
      const char* str;

      if(JS_IsNumber(argv[0])) {
        ani = (*an)->input(from_js<int32_t>(ctx, argv[0]));
      } else if((str = JS_ToCString(ctx, argv[0]))) {
        ani = (*an)->input(str);
        JS_FreeCString(ctx, str);
      }

      ret = !!ani ? js_audionodeinput_wrap(ctx, ani) : JS_NULL;
      break;
    }
    case AUDIONODE_OUTPUT: {
      AudioNodeOutputPtr ano;
      const char* str;

      if(JS_IsNumber(argv[0])) {
        ano = (*an)->output(from_js<int32_t>(ctx, argv[0]));
      } else if((str = JS_ToCString(ctx, argv[0]))) {
        ano = (*an)->output(str);
        JS_FreeCString(ctx, str);
      }

      ret = !!ano ? js_audionodeoutput_wrap(ctx, ano) : JS_NULL;
      break;
    }
  }

  return ret;
}

enum {
  AUDIONODE_PRINTGRAPH,
};

static JSValue
js_audionode_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!audionode_class.opaque(ctx, argv[0], an))
    return JS_ThrowTypeError(ctx, "argument 1 must be AudioNode");

  switch(magic) {
    case AUDIONODE_PRINTGRAPH: {
      lab::AudioNode::printGraph(an->get(), [ctx, argv, &this_val](const char* str) {
        JSValueConst args[] = {
            JS_NewString(ctx, str),
        };
        JSValue ret = JS_Call(ctx, argv[1], this_val, countof(args), args);
        JS_FreeValue(ctx, ret);
      });

      break;
    }
  }

  return ret;
}

enum {
  AUDIONODE_NAME,
  AUDIONODE_DESC,
  AUDIONODE_CHANNELCOUNT,
  AUDIONODE_CHANNELCOUNTMODE,
  AUDIONODE_CHANNELINTERPRETATION,
  AUDIONODE_CONTEXT,
  AUDIONODE_INITIALIZED,
  AUDIONODE_NUMBEROFINPUTS,
  AUDIONODE_NUMBEROFOUTPUTS,
  AUDIONODE_PARAMNAMES,
  AUDIONODE_PARAMSHORTNAMES,
  AUDIONODE_SETTINGNAMES,
  AUDIONODE_SETTINGSHORTNAMES,
};

static const char* js_audionode_channelcountmodes[] = {
    "max",
    "clamped-max",
    "explicit",
    nullptr,
};

static const char* js_audionode_channelinterpretations[] = {
    "speakers",
    "discrete",
    nullptr,
};

static JSValue
js_audionode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!audionode_class.opaque(ctx, this_val, an))
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
    case AUDIONODE_CHANNELCOUNTMODE: {
      ret = JS_NewString(ctx, js_audionode_channelcountmodes[int32_t((*an)->channelCountMode())]);
      break;
    }
    case AUDIONODE_CHANNELINTERPRETATION: {
      int32_t index = int32_t((*an)->channelInterpretation());
      ret = JS_NewString(ctx, js_audionode_channelinterpretations[index]);
      break;
    }
    case AUDIONODE_CONTEXT: {
      AudioContextPtr ac(an->value);

      ret = !!ac ? js_audiocontext_wrap(ctx, ac) : JS_NULL;
      break;
    }
    case AUDIONODE_INITIALIZED: {
      ret = JS_NewBool(ctx, !!(*an)->isInitialized());
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
      vector<std::string> names((*an)->paramNames());

      ret = to_js<vector, std::string>(ctx, names);
      break;
    }
    case AUDIONODE_PARAMSHORTNAMES: {
      vector<std::string> names((*an)->paramShortNames());

      ret = to_js<vector, std::string>(ctx, names);
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
  }

  return ret;
}

static JSValue
js_audionode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;
  double d;

  if(!audionode_class.opaque(ctx, this_val, an))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &d, value);

  switch(magic) {
    case AUDIONODE_CHANNELCOUNT: {
      lab::ContextGraphLock g(an->value.get(), __func__);
      (*an)->setChannelCount(g, from_js<int32_t>(ctx, value));
      break;
    }
    case AUDIONODE_CHANNELCOUNTMODE: {
      int32_t ccm = -1;

      if(JS_IsNumber(value))
        JS_ToInt32(ctx, &ccm, value);
      else
        ccm = js_enum_value(ctx, value, js_audionode_channelcountmodes, 0);

      lab::ContextGraphLock g(an->value.get(), __func__);
      (*an)->setChannelCountMode(g, lab::ChannelCountMode(ccm));
      break;
    }
    case AUDIONODE_CHANNELINTERPRETATION: {
      int32_t ci = -1;

      if(JS_IsNumber(value))
        JS_ToInt32(ctx, &ci, value);
      else
        ci = js_enum_value(ctx, value, js_audionode_channelinterpretations, 0);

      (*an)->setChannelInterpretation(lab::ChannelInterpretation(ci));
      break;
    }
  }

  return ret;
}

static void
js_audionode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioNodePtr* an;

  if(!audionode_class.opaque(this_val, an)) {
    an->~AudioNodePtr();

    js_free_rt(rt, an);
  }
}

static int
js_audionode_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  AudioNodePtr* an;

  if(!audionode_class.opaque(ctx, obj, an))
    return 0;

  const char* key;
  bool ret;

  if((key = JS_AtomToCString(ctx, prop))) {
    if((*an)->param_index(key) >= 0 || (*an)->setting_index(key) >= 0)
      ret = true;

    JS_FreeCString(ctx, key);
  }

  return ret;
}

static int
js_audionode_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  AudioNodePtr* an;

  if(!audionode_class.opaque(ctx, obj, an))
    return 0;

  const char* key;
  bool ret;
  JSValue value = JS_UNDEFINED, proto = JS_GetPrototype(ctx, obj);

  if(JS_HasProperty(ctx, proto, prop)) {
    ret = false;

  } else if((key = JS_AtomToCString(ctx, prop))) {
    int index;

    if((index = (*an)->param_index(key)) >= 0) {
      AudioParamPtr ap((*an)->param(index), nullptr);

      if((ret = bool(ap)))
        value = js_audioparam_wrap(ctx, ap);

    } else if((index = (*an)->setting_index(key)) >= 0) {
      AudioSettingPtr as((*an)->setting(index), nullptr);

      if((ret = bool(as)))
        value = js_audiosetting_wrap(ctx, as);
    }

    JS_FreeCString(ctx, key);
  }

  if(pdesc) {
    pdesc->flags = JS_PROP_ENUMERABLE;
    pdesc->value = value;
    pdesc->getter = JS_UNDEFINED;
    pdesc->setter = JS_UNDEFINED;
  }

  JS_FreeValue(ctx, proto);
  return ret;
}

static int
js_audionode_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  AudioNodePtr* an;

  if(!audionode_class.opaque(ctx, obj, an))
    return -1;

  const auto params = (*an)->paramNames(), settings = (*an)->settingNames();
  uint32_t i = 0, len = params.size() + settings.size();
  JSPropertyEnum* props;

  if(!(props = static_cast<JSPropertyEnum*>(js_malloc(ctx, sizeof(JSPropertyEnum) * len))))
    return -1;

  for(const auto str : params) {
    props[i].is_enumerable = TRUE;
    props[i].atom = JS_NewAtom(ctx, str.c_str());
    ++i;
  }

  for(const auto str : settings) {
    props[i].is_enumerable = TRUE;
    props[i].atom = JS_NewAtom(ctx, str.c_str());
    ++i;
  }

  *ptab = props;
  *plen = i;
  return 0;
}

static int
js_audionode_set_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  AudioNodePtr* an;

  if(!audionode_class.opaque(ctx, obj, an))
    return FALSE;

  const char* key;
  bool ret;
  JSValue proto = JS_GetPrototype(ctx, obj);

  if(JS_HasProperty(ctx, proto, prop)) {
    ret = false;
  } else if(js_audionode_has_property(ctx, obj, prop)) {
    JSValue param = JS_GetProperty(ctx, obj, prop);
    JS_SetPropertyStr(ctx, param, "value", JS_DupValue(ctx, value));
    JS_FreeValue(ctx, param);
    ret = true;
  }

  JS_FreeValue(ctx, proto);
  return ret;
}

static JSClassExoticMethods js_audionode_exotic_methods = {
    .get_own_property = js_audionode_get_own_property,
    .get_own_property_names = js_audionode_get_own_property_names,
    .has_property = js_audionode_has_property,
    .set_property = js_audionode_set_property,
};

static JSClassDef js_audionode_class = {
    .class_name = "AudioNode",
    .finalizer = js_audionode_finalizer,
    .exotic = &js_audionode_exotic_methods,

};

static const JSCFunctionListEntry js_audionode_methods[] = {
    JS_CFUNC_MAGIC_DEF("connect", 1, js_audionode_method, AUDIONODE_CONNECT),
    JS_CFUNC_MAGIC_DEF("disconnect", 1, js_audionode_method, AUDIONODE_DISCONNECT),
    JS_CFUNC_MAGIC_DEF("isConnected", 1, js_audionode_method, AUDIONODE_ISCONNECTED),
    JS_CFUNC_MAGIC_DEF("connectParam", 3, js_audionode_method, AUDIONODE_CONNECTPARAM),
    JS_CFUNC_MAGIC_DEF("disconnectParam", 3, js_audionode_method, AUDIONODE_DISCONNECTPARAM),
    JS_CFUNC_MAGIC_DEF("isScheduledNode", 0, js_audionode_method, AUDIONODE_ISSCHEDULEDNODE),
    JS_CFUNC_MAGIC_DEF("initialize", 0, js_audionode_method, AUDIONODE_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("uninitialize", 0, js_audionode_method, AUDIONODE_UNINITIALIZE),
    JS_CGETSET_MAGIC_DEF("context", js_audionode_get, 0, AUDIONODE_CONTEXT),
    JS_CGETSET_MAGIC_DEF("initialized", js_audionode_get, 0, AUDIONODE_INITIALIZED),
    JS_CGETSET_MAGIC_DEF("numberOfInputs", js_audionode_get, 0, AUDIONODE_NUMBEROFINPUTS),
    JS_CGETSET_MAGIC_DEF("numberOfOutputs", js_audionode_get, 0, AUDIONODE_NUMBEROFOUTPUTS),
    JS_CGETSET_MAGIC_DEF("paramNames", js_audionode_get, 0, AUDIONODE_PARAMNAMES),
    JS_CGETSET_MAGIC_DEF("paramShortNames", js_audionode_get, 0, AUDIONODE_PARAMSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("param_index", 1, js_audionode_method, AUDIONODE_PARAMINDEX),
    JS_CFUNC_MAGIC_DEF("param", 1, js_audionode_method, AUDIONODE_PARAM),
    JS_CGETSET_MAGIC_DEF("settingNames", js_audionode_get, 0, AUDIONODE_SETTINGNAMES),
    JS_CGETSET_MAGIC_DEF("settingShortNames", js_audionode_get, 0, AUDIONODE_SETTINGSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("setting_index", 1, js_audionode_method, AUDIONODE_SETTINGINDEX),
    JS_CFUNC_MAGIC_DEF("setting", 1, js_audionode_method, AUDIONODE_SETTING),
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_audionode_get, 0, AUDIONODE_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("channelCount", js_audionode_get, js_audionode_set, AUDIONODE_CHANNELCOUNT),
    JS_CGETSET_MAGIC_DEF("channelCountMode", js_audionode_get, js_audionode_set, AUDIONODE_CHANNELCOUNTMODE),
    JS_CGETSET_MAGIC_DEF("channelInterpretation", js_audionode_get, js_audionode_set, AUDIONODE_CHANNELINTERPRETATION),
    JS_CFUNC_MAGIC_DEF("input", 1, js_audionode_method, AUDIONODE_INPUT),
    JS_CFUNC_MAGIC_DEF("output", 1, js_audionode_method, AUDIONODE_OUTPUT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioNode", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_audionode_functions[] = {
    JS_CFUNC_MAGIC_DEF("printGraph", 2, js_audionode_function, AUDIONODE_PRINTGRAPH),
};

static JSValue
js_audiodestinationnode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioDestinationNodePtr* adn;
  AudioContextPtr* ac;
  AudioDevicePtr* ad;

  if(!audiocontext_class.opaque(ctx, argv[0], ac))
    return JS_ThrowTypeError(ctx, "argument 1 must be AudioContext");

  if(!audiodevice_class.opaque(ctx, argv[1], ad))
    return JS_ThrowTypeError(ctx, "argument 2 must be AudioDevice");

  if(!js_alloc(ctx, adn))
    return JS_EXCEPTION;

  new(adn) AudioDestinationNodePtr(make_shared<lab::AudioDestinationNode>(*ac->get(), *ad), *ac);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiodestinationnode_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiodestinationnode_class);
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
js_audiodestinationnode_wrap(JSContext* ctx, JSValueConst proto, AudioDestinationNodePtr& adnptr) {
  AudioDestinationNodePtr* adn;

  if(!js_alloc(ctx, adn))
    return JS_EXCEPTION;

  new(adn) AudioDestinationNodePtr(adnptr, adnptr.value);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audiodestinationnode_class);

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
  return js_audiodestinationnode_wrap(ctx, audiodestinationnode_class.proto, adnptr);
}

enum {
  AUDIODESTINATIONNODE_DEVICE,
};

static JSValue
js_audiodestinationnode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!audiodestinationnode_class.opaque(ctx, this_val, adn))
    return JS_EXCEPTION;

  if(adn->get() == nullptr)
    return JS_UNDEFINED;

  switch(magic) {

    case AUDIODESTINATIONNODE_DEVICE: {
      AudioDevicePtr device;

      if((device = reinterpret_cast<DestinationNode*>(adn->get())->platformAudioDevice()))
        ret = js_audiodevice_wrap(ctx, device);

      break;
    }
  }

  return ret;
}

enum {
  AUDIODESTINATIONNODE_RESET,
};

static JSValue
js_audiodestinationnode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!audiodestinationnode_class.opaque(ctx, this_val, adn))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIODESTINATIONNODE_RESET: {
      adn->reset();
      break;
    }
  }

  return ret;
}

static void
js_audiodestinationnode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioDestinationNodePtr* adn;

  if(audiodestinationnode_class.opaque(this_val, adn)) {
    adn->~AudioDestinationNodePtr();
    js_free_rt(rt, adn);
  }
}

static JSClassDef js_audiodestinationnode_class = {
    .class_name = "AudioDestinationNode",
    .finalizer = js_audiodestinationnode_finalizer,
};

static const JSCFunctionListEntry js_audiodestinationnode_methods[] = {
    JS_CGETSET_MAGIC_DEF("device", js_audiodestinationnode_get, 0, AUDIODESTINATIONNODE_DEVICE),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_audiodestinationnode_method, AUDIODESTINATIONNODE_RESET),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDestinationNode", JS_PROP_CONFIGURABLE),
};

static const char* js_audioscheduledsourcenode_states[] = {
    "unscheduled",
    "scheduled",
    "fade-in",
    "playing",
    "stopping",
    "resetting",
    "finishing",
    "finished",
    nullptr,
};

static JSValue
js_audioscheduledsourcenode_wrap(JSContext* ctx, JSValueConst proto, AudioScheduledSourceNodePtr& anode) {
  AudioScheduledSourceNodePtr* assn;

  if(!js_alloc(ctx, assn))
    return JS_EXCEPTION;

  new(assn) AudioScheduledSourceNodePtr(anode, anode.value);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, audioscheduledsourcenode_class);

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
  return js_audioscheduledsourcenode_wrap(ctx, audioscheduledsourcenode_class.proto, anode);
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

  if(!audioscheduledsourcenode_class.opaque(ctx, this_val, assn))
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
      ret = JS_NewBool(ctx, !!(*assn)->isPlayingOrScheduled());
      break;
    }
  }

  return ret;
}

enum {
  AUDIOSCHEDULEDSOURCENODE_PLAYBACKSTATE,
};

static JSValue
js_audioscheduledsourcenode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioScheduledSourceNodePtr* assn;
  JSValue ret = JS_UNDEFINED;

  if(!audioscheduledsourcenode_class.opaque(ctx, this_val, assn))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOSCHEDULEDSOURCENODE_PLAYBACKSTATE: {
      const auto state = int32_t((*assn)->playbackState());
      ret = to_js<std::string>(ctx, js_audioscheduledsourcenode_states[state]);
      break;
    }
  }

  return ret;
}

static void
js_audioscheduledsourcenode_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioScheduledSourceNodePtr* assn;

  if(audioscheduledsourcenode_class.opaque(this_val, assn)) {
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
    JS_CGETSET_MAGIC_DEF("playbackState", js_audioscheduledsourcenode_get, 0, AUDIOSCHEDULEDSOURCENODE_PLAYBACKSTATE),
    JS_CFUNC_MAGIC_DEF("isPlayingOrScheduled", 0, js_audioscheduledsourcenode_method, AUDIOSCHEDULEDSOURCENODE_IS_PLAYING_OR_SCHEDULED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioScheduledSourceNode", JS_PROP_CONFIGURABLE),
};

static const char* oscillator_types[] = {
    "oscillator_none",
    "sine",
    "fast-sine",
    "square",
    "sawtooth",
    "falling_sawtooth",
    "triangle",
    "custom",
    nullptr,
};

static JSValue
js_oscillatornode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioContextPtr* acptr;

  if(!audiocontext_class.opaque(ctx, argv[0], acptr))
    return JS_ThrowTypeError(ctx, "argument 1 must be AudioContext");

  lab::AudioContext& ac = *acptr->get();

  OscillatorNodePtr* on;

  if(!js_alloc(ctx, on))
    return JS_EXCEPTION;

  new(on) OscillatorNodePtr(make_shared<lab::OscillatorNode>(ac), *acptr);

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, oscillatornode_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, oscillatornode_class);
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

  if(!oscillatornode_class.opaque(ctx, this_val, on))
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
  }

  return ret;
}

enum {
  /*  OSCILLATORNODE_TYPE,
    OSCILLATORNODE_AMPLITUDE,
    OSCILLATORNODE_FREQUENCY,
    OSCILLATORNODE_DETUNE,
    OSCILLATORNODE_BIAS,*/
};

static JSValue
js_oscillatornode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!oscillatornode_class.opaque(ctx, this_val, on))
    return JS_EXCEPTION;

  switch(magic) {
    /*case OSCILLATORNODE_TYPE: {
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
    }*/
  }

  return ret;
}

static JSValue
js_oscillatornode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;
  double d;

  if(!oscillatornode_class.opaque(ctx, this_val, on))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &d, value);

  switch(magic) {
    /*case OSCILLATORNODE_TYPE: {
      int32_t type = -1;
      const char* arg;

      if(JS_IsString(value) && (arg = JS_ToCString(ctx, value))) {

        const auto it = std::find_if(oscillator_types.begin(), oscillator_types.end(), [arg](const
    char* str) -> bool { return !strcasecmp(arg, str); });

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
    }*/
  }

  return ret;
}

static void
js_oscillatornode_finalizer(JSRuntime* rt, JSValue this_val) {
  OscillatorNodePtr* on;

  if(oscillatornode_class.opaque(this_val, on)) {
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
    /*JS_CGETSET_MAGIC_FLAGS_DEF("amplitude", js_oscillatornode_get, js_oscillatornode_set,
    OSCILLATORNODE_AMPLITUDE, JS_PROP_ENUMERABLE), JS_CGETSET_MAGIC_FLAGS_DEF("frequency",
    js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_FREQUENCY, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("detune", js_oscillatornode_get, js_oscillatornode_set,
    OSCILLATORNODE_DETUNE, JS_PROP_ENUMERABLE), JS_CGETSET_MAGIC_FLAGS_DEF("bias",
    js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_BIAS, JS_PROP_ENUMERABLE),*/
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "OscillatorNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiosummingjunction_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {

  AudioSummingJunctionPtr* asj;

  if(!js_alloc(ctx, asj))
    return JS_EXCEPTION;

  new(asj) AudioSummingJunctionPtr(make_shared<lab::AudioSummingJunction>());

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiosummingjunction_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiosummingjunction_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, asj);
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

  if(!audiosummingjunction_class.opaque(ctx, this_val, asj))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

enum {
  AUDIOSUMMINGJUNCTION_NUMBEROFCONNECTIONS,
  AUDIOSUMMINGJUNCTION_CONNECTED,
};

static JSValue
js_audiosummingjunction_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioSummingJunctionPtr* asj;
  JSValue ret = JS_UNDEFINED;

  if(!audiosummingjunction_class.opaque(ctx, this_val, asj))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOSUMMINGJUNCTION_NUMBEROFCONNECTIONS: {
      ret = to_js<int32_t>(ctx, (*asj)->numberOfConnections());
      break;
    }
    case AUDIOSUMMINGJUNCTION_CONNECTED: {
      ret = to_js<BOOL>(ctx, !!(*asj)->isConnected());
      break;
    }
  }

  return ret;
}

static JSValue
js_audiosummingjunction_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioSummingJunctionPtr* asj;
  JSValue ret = JS_UNDEFINED;

  if(!audiosummingjunction_class.opaque(ctx, this_val, asj))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_audiosummingjunction_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioSummingJunctionPtr* asj;

  if(audiosummingjunction_class.opaque(this_val, asj)) {
    asj->~AudioSummingJunctionPtr();
    js_free_rt(rt, asj);
  }
}

static JSClassDef js_audiosummingjunction_class = {
    .class_name = "AudioSummingJunction",
    .finalizer = js_audiosummingjunction_finalizer,
};

static const JSCFunctionListEntry js_audiosummingjunction_methods[] = {
    JS_CGETSET_MAGIC_DEF("numberOfConnections", js_audiosummingjunction_get, 0, AUDIOSUMMINGJUNCTION_NUMBEROFCONNECTIONS),
    JS_CGETSET_MAGIC_DEF("connected", js_audiosummingjunction_get, 0, AUDIOSUMMINGJUNCTION_CONNECTED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioSummingJunction", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiobuffersourcenode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioContextPtr* acptr;

  if(!audiocontext_class.opaque(ctx, argv[0], acptr))
    return JS_ThrowTypeError(ctx, "argument 1 must be AudioContext");

  lab::AudioContext& ac = *acptr->get();
  AudioBufferSourceNodePtr* absn;

  if(!js_alloc(ctx, absn))
    return JS_EXCEPTION;

  new(absn) AudioBufferSourceNodePtr(make_shared<lab::SampledAudioNode>(ac));

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audiobuffersourcenode_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audiobuffersourcenode_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, absn);

  if(argc > 1)
    js_copy(ctx, obj, argv[1]);

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

  if(!audiobuffersourcenode_class.opaque(ctx, this_val, absn))
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

  if(!audiobuffersourcenode_class.opaque(ctx, this_val, absn))
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

  if(!audiobuffersourcenode_class.opaque(ctx, this_val, absn))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIOBUFFERSOURCENODE_BUFFER: {
      AudioBufferPtr* ab;

      if(!JS_IsObject(value)) {
        (*absn)->setBus(AudioBufferPtr());

      } else {
        if(!audiobuffer_class.opaque(ctx, value, ab))
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

  if(audiobuffersourcenode_class.opaque(this_val, absn)) {
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

static JSValue
js_periodicwave_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  PeriodicWavePtr* pw;

  if(!js_alloc(ctx, pw))
    return JS_EXCEPTION;

  double sampleRate = from_js<double>(ctx, argv[0]);
  auto oscillatorType = find_enumeration<lab::OscillatorType>(ctx, argv[1]);

  if(argc >= 4) {
    /*    auto re = from_js<std::vector<double>>(ctx, argv[2]);
        auto im = from_js<std::vector<double>>(ctx, argv[3]);
        vector<float> re_f, im_f;

        std::copy(re.begin(), re.end(), std::back_inserter(re_f));
        std::copy(im.begin(), im.end(), std::back_inserter(im_f));

        new(pw) PeriodicWavePtr(make_shared<lab::PeriodicWave>(sampleRate, oscillatorType, re_f,
       im_f));*/
  } else {
    new(pw) PeriodicWavePtr(make_shared<lab::PeriodicWave>(sampleRate, oscillatorType));
  }
  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, periodicwave_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, periodicwave_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, pw);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PERIODICWAVE_WAVEDATAFORFUNDAMENTALFREQUENCY,
};

static JSValue
js_periodicwave_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  PeriodicWavePtr* pw;
  JSValue ret = JS_UNDEFINED;

  if(!periodicwave_class.opaque(ctx, this_val, pw))
    return JS_EXCEPTION;

  switch(magic) {
    case PERIODICWAVE_WAVEDATAFORFUNDAMENTALFREQUENCY: {
      float *lower, *higher, tableInterpolationFactor;

      (*pw)->waveDataForFundamentalFrequency(from_js<double>(ctx, argv[0]), lower, higher, tableInterpolationFactor);

      ret = JS_NewObjectProto(ctx, JS_NULL);

      // JS_SetPropertyStr(ctx, ret, "lowerWaveData", )
      break;
    }
  }

  return ret;
}

enum {
  PERIODICWAVE_SIZE,
  PERIODICWAVE_RATESCALE,
};

static JSValue
js_periodicwave_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PeriodicWavePtr* pw;
  JSValue ret = JS_UNDEFINED;

  if(!periodicwave_class.opaque(ctx, this_val, pw))
    return JS_EXCEPTION;

  switch(magic) {
    case PERIODICWAVE_SIZE: {
      ret = to_js<uint32_t>(ctx, (*pw)->periodicWaveSize());
      break;
    }
    case PERIODICWAVE_RATESCALE: {
      ret = to_js<double>(ctx, (*pw)->rateScale());
      break;
    }
  }

  return ret;
}

static JSValue
js_periodicwave_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  PeriodicWavePtr* pw;
  JSValue ret = JS_UNDEFINED;

  if(!periodicwave_class.opaque(ctx, this_val, pw))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_periodicwave_finalizer(JSRuntime* rt, JSValue this_val) {
  PeriodicWavePtr* pw;

  if(periodicwave_class.opaque(this_val, pw)) {
    pw->~PeriodicWavePtr();
    js_free_rt(rt, pw);
  }
}

static JSClassDef js_periodicwave_class = {
    .class_name = "PeriodicWave",
    .finalizer = js_periodicwave_finalizer,
};

static const JSCFunctionListEntry js_periodicwave_methods[] = {
    JS_CGETSET_MAGIC_DEF("size", js_periodicwave_get, 0, PERIODICWAVE_SIZE),
    JS_CGETSET_MAGIC_DEF("rateScale", js_periodicwave_get, 0, PERIODICWAVE_RATESCALE),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PeriodicWave", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audioprocessor_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  AudioProcessorPtr* ap;

  if(!js_alloc(ctx, ap))
    return JS_EXCEPTION;

  new(ap) AudioProcessorPtr(make_shared<AudioProcessor>(ctx, argv[0]));

  /* using new_target to get the prototype is necessary when the class is extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, audioprocessor_class.proto);

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, audioprocessor_class);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ap);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {};

static JSValue
js_audioprocessor_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioProcessorPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!audioprocessor_class.opaque(ctx, this_val, ap))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

enum {};

static JSValue
js_audioprocessor_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioProcessorPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!audioprocessor_class.opaque(ctx, this_val, ap))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audioprocessor_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioProcessorPtr* ap;
  JSValue ret = JS_UNDEFINED;

  if(!audioprocessor_class.opaque(ctx, this_val, ap))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_audioprocessor_finalizer(JSRuntime* rt, JSValue this_val) {
  AudioProcessorPtr* ap;

  if(audioprocessor_class.opaque(this_val, ap)) {
    ap->~AudioProcessorPtr();
    js_free_rt(rt, ap);
  }
}

static JSClassDef js_audioprocessor_class = {
    .class_name = "AudioProcessor",
    .finalizer = js_audioprocessor_finalizer,
};

static const JSCFunctionListEntry js_audioprocessor_methods[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioProcessor", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_settingtype_values[] = {
    JS_PROP_INT32_DEF("None", int32_t(lab::SettingType::None), JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Bool", int32_t(lab::SettingType::Bool), JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Integer", int32_t(lab::SettingType::Integer), JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Float", int32_t(lab::SettingType::Float), JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Enum", int32_t(lab::SettingType::Enum), JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Bus", int32_t(lab::SettingType::Bus), JS_PROP_ENUMERABLE),
};

int
js_labsound_init(JSContext* ctx, JSModuleDef* m) {
  audiobuffer_class.init(ctx, &js_audiobuffer_class);

  audiobuffer_class.constructor(ctx, js_audiobuffer_constructor, 1, 0);
  audiobuffer_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiobuffer_class.proto, js_audiobuffer_methods, countof(js_audiobuffer_methods));
  JS_SetPropertyFunctionList(ctx, audiobuffer_class.ctor, js_audiobuffer_functions, countof(js_audiobuffer_functions));

  audiobuffer_class.setProtoConstructor(ctx);
  /*JS_SetClassProto(ctx, audiobuffer_class, audiobuffer_class.proto);
  JS_SetConstructor(ctx, audiobuffer_class.ctor, audiobuffer_class.proto);*/

  audiocontext_class.init(ctx, &js_audiocontext_class);

  audiocontext_class.constructor(ctx, js_audiocontext_constructor, 1, 0);
  audiocontext_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiocontext_class.proto, js_audiocontext_methods, countof(js_audiocontext_methods));

  JS_SetClassProto(ctx, audiocontext_class, audiocontext_class.proto);
  JS_SetConstructor(ctx, audiocontext_class.ctor, audiocontext_class.proto);

  audiolistener_class.init(ctx, &js_audiolistener_class);

  audiolistener_class.constructor(ctx, js_audiolistener_constructor, 1, 0);
  audiolistener_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiolistener_class.proto, js_audiolistener_methods, countof(js_audiolistener_methods));

  JS_SetClassProto(ctx, audiolistener_class, audiolistener_class.proto);
  JS_SetConstructor(ctx, audiolistener_class.ctor, audiolistener_class.proto);

  audiodevice_class.init(ctx, &js_audiodevice_class);

  audiodevice_class.constructor(ctx, js_audiodevice_constructor, 1, 0);
  audiodevice_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiodevice_class.proto, js_audiodevice_methods, countof(js_audiodevice_methods));
  JS_SetPropertyFunctionList(ctx, audiodevice_class.ctor, js_audiodevice_functions, countof(js_audiodevice_functions));

  JS_SetClassProto(ctx, audiodevice_class, audiodevice_class.proto);
  JS_SetConstructor(ctx, audiodevice_class.ctor, audiodevice_class.proto);

  audionodeinput_class.init(ctx, &js_audionodeinput_class);

  audionodeinput_class.constructor(ctx, js_audionodeinput_constructor, 1, 0);
  audionodeinput_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audionodeinput_class.proto, js_audionodeinput_methods, countof(js_audionodeinput_methods));

  JS_SetClassProto(ctx, audionodeinput_class, audionodeinput_class.proto);
  JS_SetConstructor(ctx, audionodeinput_class.ctor, audionodeinput_class.proto);

  audionodeoutput_class.init(ctx, &js_audionodeoutput_class);

  audionodeoutput_class.constructor(ctx, js_audionodeoutput_constructor, 1, 0);
  audionodeoutput_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audionodeoutput_class.proto, js_audionodeoutput_methods, countof(js_audionodeoutput_methods));

  JS_SetClassProto(ctx, audionodeoutput_class, audionodeoutput_class.proto);
  JS_SetConstructor(ctx, audionodeoutput_class.ctor, audionodeoutput_class.proto);

  audionode_class.init(ctx, &js_audionode_class);

  audionode_class.ctor = JS_NewObjectProto(ctx, JS_NULL);
  audionode_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audionode_class.proto, js_audionode_methods, countof(js_audionode_methods));
  JS_SetPropertyFunctionList(ctx, audionode_class.ctor, js_audionode_functions, countof(js_audionode_functions));

  JS_SetClassProto(ctx, audionode_class, audionode_class.proto);
  JS_SetConstructor(ctx, audionode_class.ctor, audionode_class.proto);

  audiodestinationnode_class.init(ctx, &js_audiodestinationnode_class);
  audiodestinationnode_class.inherit(audionode_class);

  audiodestinationnode_class.constructor(ctx, js_audiodestinationnode_constructor, 2, 0);
  audiodestinationnode_class.proto = JS_NewObjectProto(ctx, audionode_class.proto);

  JS_SetPropertyFunctionList(ctx, audiodestinationnode_class.proto, js_audiodestinationnode_methods, countof(js_audiodestinationnode_methods));

  JS_SetClassProto(ctx, audiodestinationnode_class, audiodestinationnode_class.proto);
  JS_SetConstructor(ctx, audiodestinationnode_class.ctor, audiodestinationnode_class.proto);

  audioscheduledsourcenode_class.init(ctx, &js_audioscheduledsourcenode_class);
  audioscheduledsourcenode_class.inherit(audionode_class);

  audioscheduledsourcenode_class.ctor = JS_NewObjectProto(ctx, JS_NULL);
  audioscheduledsourcenode_class.proto = JS_NewObjectProto(ctx, audionode_class.proto);

  JS_SetPropertyFunctionList(ctx, audioscheduledsourcenode_class.proto, js_audioscheduledsourcenode_methods, countof(js_audioscheduledsourcenode_methods));

  JS_SetClassProto(ctx, audioscheduledsourcenode_class, audioscheduledsourcenode_class.proto);
  JS_SetConstructor(ctx, audioscheduledsourcenode_class.ctor, audioscheduledsourcenode_class.proto);

  oscillatornode_class.init(ctx, &js_oscillatornode_class);
  oscillatornode_class.inherit(audioscheduledsourcenode_class);

  oscillatornode_class.constructor(ctx, js_oscillatornode_constructor, 1, 0);
  oscillatornode_class.proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_class.proto);

  JS_SetPropertyFunctionList(ctx, oscillatornode_class.proto, js_oscillatornode_methods, countof(js_oscillatornode_methods));

  JS_SetClassProto(ctx, oscillatornode_class, oscillatornode_class.proto);

  audiosummingjunction_class.init(ctx, &js_audiosummingjunction_class);

  audiosummingjunction_class.constructor(ctx, js_audiosummingjunction_constructor, 1, 0);
  audiosummingjunction_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiosummingjunction_class.proto, js_audiosummingjunction_methods, countof(js_audiosummingjunction_methods));

  JS_SetClassProto(ctx, audiosummingjunction_class, audiosummingjunction_class.proto);

  audiobuffersourcenode_class.init(ctx, &js_audiobuffersourcenode_class);
  audiobuffersourcenode_class.inherit(audioscheduledsourcenode_class);

  audiobuffersourcenode_class.constructor(ctx, js_audiobuffersourcenode_constructor, 1, 0);
  audiobuffersourcenode_class.proto = JS_NewObjectProto(ctx, audioscheduledsourcenode_class.proto);

  JS_SetPropertyFunctionList(ctx, audiobuffersourcenode_class.proto, js_audiobuffersourcenode_methods, countof(js_audiobuffersourcenode_methods));

  JS_SetClassProto(ctx, audiobuffersourcenode_class, audiobuffersourcenode_class.proto);

  audioparam_class.init(ctx, &js_audioparam_class);
  audioparam_class.inherit(audiosummingjunction_class);
  audioparam_class.constructor(ctx, js_audioparam_constructor, 1, 0);

  audioparam_class.proto = JS_NewObjectProto(ctx, audiosummingjunction_class.proto);

  JS_SetPropertyFunctionList(ctx, audioparam_class.proto, js_audioparam_methods, countof(js_audioparam_methods));

  JS_SetClassProto(ctx, audioparam_class, audioparam_class.proto);

  audiosetting_class.init(ctx, &js_audiosetting_class);

  audiosetting_class.constructor(ctx, js_audiosetting_constructor, 1, 0);
  audiosetting_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audiosetting_class.proto, js_audiosetting_methods, countof(js_audiosetting_methods));

  JS_SetClassProto(ctx, audiosetting_class, audiosetting_class.proto);

  JSValue settingtype_obj = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, settingtype_obj, js_settingtype_values, countof(js_settingtype_values));

  periodicwave_class.init(ctx, &js_periodicwave_class);

  periodicwave_class.constructor(ctx, js_periodicwave_constructor, 1, 0);
  periodicwave_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, periodicwave_class.proto, js_periodicwave_methods, countof(js_periodicwave_methods));

  JS_SetClassProto(ctx, periodicwave_class, periodicwave_class.proto);

  audioprocessor_class.init(ctx, &js_audioprocessor_class);

  audioprocessor_class.constructor(ctx, js_audioprocessor_constructor, 1, 0);
  audioprocessor_class.proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, audioprocessor_class.proto, js_audioprocessor_methods, countof(js_audioprocessor_methods));

  JS_SetClassProto(ctx, audioprocessor_class, audioprocessor_class.proto);

  if(m) {
    audiobuffer_class.setModuleExport(ctx, m);
    JS_SetModuleExport(ctx, m, "AudioContext", audiocontext_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioListener", audiolistener_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioDevice", audiodevice_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioNodeInput", audionodeinput_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioNodeOutput", audionodeoutput_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioNode", audionode_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioDestinationNode", audiodestinationnode_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioScheduledSourceNode", audioscheduledsourcenode_class.ctor);
    JS_SetModuleExport(ctx, m, "OscillatorNode", oscillatornode_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioSummingJunction", audiosummingjunction_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioBufferSourceNode", audiobuffersourcenode_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioParam", audioparam_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioSetting", audiosetting_class.ctor);
    JS_SetModuleExport(ctx, m, "SettingType", settingtype_obj);
    JS_SetModuleExport(ctx, m, "PeriodicWave", periodicwave_class.ctor);
    JS_SetModuleExport(ctx, m, "AudioProcessor", audioprocessor_class.ctor);
  }

  get_class_id(audionode_class);

  return 0;
}

extern "C" VISIBLE void
js_init_module_labsound(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "AudioBuffer");
  JS_AddModuleExport(ctx, m, "AudioContext");
  JS_AddModuleExport(ctx, m, "AudioListener");
  JS_AddModuleExport(ctx, m, "AudioDevice");
  JS_AddModuleExport(ctx, m, "AudioNodeInput");
  JS_AddModuleExport(ctx, m, "AudioNodeOutput");
  JS_AddModuleExport(ctx, m, "AudioNode");
  JS_AddModuleExport(ctx, m, "AudioDestinationNode");
  JS_AddModuleExport(ctx, m, "AudioScheduledSourceNode");
  JS_AddModuleExport(ctx, m, "OscillatorNode");
  JS_AddModuleExport(ctx, m, "AudioSummingJunction");
  JS_AddModuleExport(ctx, m, "AudioBufferSourceNode");
  JS_AddModuleExport(ctx, m, "AudioParam");
  JS_AddModuleExport(ctx, m, "AudioSetting");
  JS_AddModuleExport(ctx, m, "SettingType");
  JS_AddModuleExport(ctx, m, "PeriodicWave");
  JS_AddModuleExport(ctx, m, "AudioProcessor");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_labsound_init)))
    js_init_module_labsound(ctx, m);

  return m;
}
