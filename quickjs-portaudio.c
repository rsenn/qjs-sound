#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include <portaudio.h>

static JSClassID js_pastream_class_id;
static JSValue pastream_proto, pastream_ctor;

static int
js_pastreamcallback(const void* in,
                    void* out,
                    unsigned long nframes,
                    const PaStreamCallbackTimeInfo* ti,
                    PaStreamCallbackFlags sf,
                    void* u) {
}

enum {
  FUNC_INITIALIZE = 0,
  FUNC_TERMINATE,
  FUNC_SLEEP,
  FUNC_GETSAMPLESIZE,
};

static JSValue
js_portaudio_error(JSContext* ctx, PaError err) {
  return err < 0    ? JS_ThrowInternalError(ctx, "PaError: %s", Pa_GetErrorText(err))
         : err == 0 ? JS_UNDEFINED
                    : JS_NewInt32(ctx, err);
}

static JSValue
js_portaudio_function(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case FUNC_INITIALIZE: {
      ret = js_portaudio_error(ctx, Pa_Initialize());
      break;
    }
    case FUNC_TERMINATE: {
      ret = js_portaudio_error(ctx, Pa_Terminate());
      break;
    }
    case FUNC_SLEEP: {
      int64_t msec = -1;
      if(argc > 0)
        JS_ToInt64(ctx, &msec, argv[0]);
      Pa_Sleep(msec);
      break;
    }

    case FUNC_GETSAMPLESIZE: {
      uint32_t u = 0;

      if(argc > 0)
        JS_ToUint32(ctx, &u, argv[0]);

      PaError r = Pa_GetSampleSize(u);

      ret = js_portaudio_error(ctx, r);
      break;
    }
  }

  return ret;
}

static JSValue
js_pastream_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  PaStream* st = NULL;

  int32_t numInputChannels, numOutputChannels;
  uint32_t sampleFormat;
  double sampleRate;
  uint32_t framesPerBuffer;
  void* userData = NULL;

  if(argc > 0)
    JS_ToInt32(ctx, &numInputChannels, argv[0]);
  if(argc > 1)
    JS_ToInt32(ctx, &numOutputChannels, argv[1]);
  if(argc > 2)
    JS_ToUint32(ctx, &sampleFormat, argv[2]);
  if(argc > 3)
    JS_ToFloat64(ctx, &sampleRate, argv[3]);
  if(argc > 4)
    JS_ToUint32(ctx, &framesPerBuffer, argv[4]);

  if(argc > 5) {}

  PaError r = Pa_OpenDefaultStream(&st,
                                   numInputChannels,
                                   numOutputChannels,
                                   sampleFormat,
                                   sampleRate,
                                   framesPerBuffer,
                                   js_pastreamcallback,
                                   userData);

  if(r < 0) {
    JS_ThrowInternalError(ctx, "PortAudio error: %s", Pa_GetErrorText(r));
    goto fail;
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = pastream_proto;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_pastream_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, st);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_ACTIVE = 0,
  PROP_STOPPED,
  PROP_INPUTLATENCY,
  PROP_OUTPUTLATENCY,
  PROP_SAMPLERATE,
  PROP_TIME,
  PROP_CPULOAD,
  PROP_READAVAILABLE,
  PROP_WRITEAVAILABLE,
  PROP_HOSTAPITYPE,
};

static JSValue
js_pastream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PaStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ACTIVE: {
      ret = JS_NewBool(ctx, Pa_IsStreamActive(st));
      break;
    }
    case PROP_STOPPED: {
      ret = JS_NewBool(ctx, Pa_IsStreamStopped(st));
      break;
    }
    case PROP_INPUTLATENCY: {
      const PaStreamInfo* si;
      if((si = Pa_GetStreamInfo(st)))
        ret = JS_NewFloat64(ctx, si->inputLatency);
      break;
    }
    case PROP_OUTPUTLATENCY: {
      const PaStreamInfo* si;
      if((si = Pa_GetStreamInfo(st)))
        ret = JS_NewFloat64(ctx, si->outputLatency);
      break;
    }
    case PROP_SAMPLERATE: {
      const PaStreamInfo* si;
      if((si = Pa_GetStreamInfo(st)))
        ret = JS_NewFloat64(ctx, si->sampleRate);
      break;
    }
    case PROP_TIME: {
      ret = JS_NewFloat64(ctx, Pa_GetStreamTime(st));
      break;
    }
    case PROP_CPULOAD: {
      ret = JS_NewFloat64(ctx, Pa_GetStreamCpuLoad(st));
      break;
    }
    case PROP_READAVAILABLE: {
      ret = JS_NewInt64(ctx, Pa_GetStreamReadAvailable(st));
      break;
    }
    case PROP_WRITEAVAILABLE: {
      ret = JS_NewInt64(ctx, Pa_GetStreamWriteAvailable(st));
      break;
    }
#ifdef HAVE_GETSTREAMHOSTAPITYPE
    case PROP_HOSTAPITYPE: {
      enum PaHostApiTypeId id = Pa_GetStreamHostApiType(st);
      const char* str = 0;

      switch(id) {
        case paInDevelopment: str = "InDevelopment"; break;
        case paDirectSound: str = "DirectSound"; break;
        case paMME: str = "MME"; break;
        case paASIO: str = "ASIO"; break;
        case paSoundManager: str = "SoundManager"; break;
        case paCoreAudio: str = "CoreAudio"; break;
        case paOSS: str = "OSS"; break;
        case paALSA: str = "ALSA"; break;
        case paAL: str = "AL"; break;
        case paBeOS: str = "BeOS"; break;
        case paWDMKS: str = "WDMKS"; break;
        case paJACK: str = "JACK"; break;
        case paWASAPI: str = "WASAPI"; break;
        case paAudioScienceHPI: str = "AudioScienceHPI"; break;
      }

      if(str)
        ret = JS_NewString(ctx, str);

      break;
    }
#endif
  }

  return ret;
}

static JSValue
js_pastream_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  PaStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

enum {
  METHOD_READ = 0,
  METHOD_START,
  METHOD_STOP,
  METHOD_ABORT,
  METHOD_CLOSE,
};

static JSValue
js_pastream_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  PaStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_READ: {
      size_t len;
      uint8_t* ptr;
      uint32_t frames = 0;

      if(!(ptr = JS_GetArrayBuffer(ctx, &len, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer");

      if(argc > 1)
        JS_ToUint32(ctx, &frames, argv[1]);

      break;
    }

    case METHOD_START: {
      ret = JS_NewInt32(ctx, Pa_StartStream(st));
      break;
    }
    case METHOD_STOP: {
      ret = JS_NewInt32(ctx, Pa_StopStream(st));
      break;
    }
    case METHOD_ABORT: {
      ret = JS_NewInt32(ctx, Pa_AbortStream(st));
      break;
    }
    case METHOD_CLOSE: {
      ret = JS_NewInt32(ctx, Pa_CloseStream(st));
      break;
    }
  }

  return ret;
}

static void
js_pastream_finalizer(JSRuntime* rt, JSValue val) {
  PaStream* st;

  if((st = JS_GetOpaque(val, js_pastream_class_id))) {
    js_free_rt(rt, st);
  }
}

static JSClassDef js_pastream_class = {
    .class_name = "PaStream",
    .finalizer = js_pastream_finalizer,
};

static const JSCFunctionListEntry js_pastream_funcs[] = {
    JS_CGETSET_MAGIC_DEF("active", js_pastream_get, 0, PROP_ACTIVE),
    JS_CGETSET_MAGIC_DEF("inputLatency", js_pastream_get, 0, PROP_INPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("outputLatency", js_pastream_get, 0, PROP_OUTPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("sampleRate", js_pastream_get, 0, PROP_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("time", js_pastream_get, 0, PROP_TIME),
    JS_CGETSET_MAGIC_DEF("cpuLoad", js_pastream_get, 0, PROP_CPULOAD),
    JS_CGETSET_MAGIC_DEF("readAvailable", js_pastream_get, 0, PROP_READAVAILABLE),
    JS_CGETSET_MAGIC_DEF("writeAvailable", js_pastream_get, 0, PROP_WRITEAVAILABLE),
#ifdef HAVE_GETSTREAMHOSTAPITYPE
    JS_CGETSET_MAGIC_DEF("hostApiType", js_pastream_get, 0, PROP_HOSTAPITYPE),
#endif
    JS_CFUNC_MAGIC_DEF("start", 0, js_pastream_method, METHOD_START),
    JS_CFUNC_MAGIC_DEF("stop", 0, js_pastream_method, METHOD_STOP),
    JS_CFUNC_MAGIC_DEF("abort", 0, js_pastream_method, METHOD_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_pastream_method, METHOD_CLOSE),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaStream", JS_PROP_CONFIGURABLE),
};

static JSClassID js_padeviceinfo_class_id;
static JSValue padeviceinfo_proto, padeviceinfo_ctor;

static JSValue
js_padeviceinfo_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  PaDeviceInfo* di = js_mallocz(ctx, sizeof(PaDeviceInfo));

  if(argc > 0) {
    int32_t index = -1;
    JS_ToInt32(ctx, &index, argv[0]);
    const PaDeviceInfo* info;

    if((info = Pa_GetDeviceInfo(index)))
      *di = *info;
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = padeviceinfo_proto;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_padeviceinfo_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, di);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_padeviceinfo_wrap(JSContext* ctx, JSValueConst proto, PaDeviceInfo info) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_padeviceinfo_class_id);

  PaDeviceInfo* di;

  if(!(di = js_mallocz(ctx, sizeof(PaDeviceInfo))))
    goto fail;

  if(info.name)
    info.name = js_strdup(ctx, info.name);

  *di = info;

  JS_SetOpaque(obj, di);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_STRUCTVERSION,
  PROP_NAME,
  PROP_HOSTAPI,
  PROP_MAXINPUTCHANNELS,
  PROP_MAXOUTPUTCHANNELS,
  PROP_DEFAULTLOWINPUTLATENCY,
  PROP_DEFAULTLOWOUTPUTLATENCY,
  PROP_DEFAULTHIGHINPUTLATENCY,
  PROP_DEFAULTHIGHOUTPUTLATENCY,
  PROP_DEFAULTSAMPLERATE,

};

static JSValue
js_padeviceinfo_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PaDeviceInfo* di;
  JSValue ret = JS_UNDEFINED;

  if(!(di = JS_GetOpaque2(ctx, this_val, js_padeviceinfo_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_STRUCTVERSION: {
      ret = JS_NewInt32(ctx, di->structVersion);
      break;
    }
    case PROP_NAME: {
      ret = di->name ? JS_NewString(ctx, di->name) : JS_NULL;
      break;
    }
    case PROP_HOSTAPI: {
      ret = JS_NewInt32(ctx, di->hostApi);
      break;
    }
    case PROP_MAXINPUTCHANNELS: {
      ret = JS_NewInt32(ctx, di->maxInputChannels);
      break;
    }
    case PROP_MAXOUTPUTCHANNELS: {
      ret = JS_NewInt32(ctx, di->maxOutputChannels);
      break;
    }
    case PROP_DEFAULTLOWINPUTLATENCY: {
      ret = JS_NewFloat64(ctx, di->defaultLowInputLatency);
      break;
    }
    case PROP_DEFAULTLOWOUTPUTLATENCY: {
      ret = JS_NewFloat64(ctx, di->defaultLowOutputLatency);
      break;
    }
    case PROP_DEFAULTHIGHINPUTLATENCY: {
      ret = JS_NewFloat64(ctx, di->defaultHighInputLatency);
      break;
    }
    case PROP_DEFAULTHIGHOUTPUTLATENCY: {
      ret = JS_NewFloat64(ctx, di->defaultHighOutputLatency);
      break;
    }
    case PROP_DEFAULTSAMPLERATE: {
      ret = JS_NewFloat64(ctx, di->defaultSampleRate);
      break;
    }
  }

  return ret;
}

static JSValue
js_padeviceinfo_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  PaDeviceInfo* di;
  JSValue ret = JS_UNDEFINED;

  if(!(di = JS_GetOpaque2(ctx, this_val, js_padeviceinfo_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_STRUCTVERSION: {
      break;
    }
    case PROP_NAME: {
      const char* str;
      if((str = JS_ToCString(ctx, value))) {
        if(di->name)
          js_free(ctx, di->name);
        di->name = js_strdup(ctx, str);
      }
      break;
    }
    case PROP_HOSTAPI: {
      int32_t n;
      if(!JS_ToInt32(ctx, &n, value))
        di->hostApi = n;
      break;
    }
    case PROP_MAXINPUTCHANNELS: {
      int32_t n;
      if(!JS_ToInt32(ctx, &n, value))
        di->maxInputChannels = n;

      break;
    }
    case PROP_MAXOUTPUTCHANNELS: {
      int32_t n;
      if(!JS_ToInt32(ctx, &n, value))
        di->maxOutputChannels = n;
      break;
    }
    case PROP_DEFAULTLOWINPUTLATENCY: {
      double d;
      if(!JS_ToFloat64(ctx, &d, value))
        di->defaultLowInputLatency = d;
      break;
    }
    case PROP_DEFAULTLOWOUTPUTLATENCY: {
      double d;
      if(!JS_ToFloat64(ctx, &d, value))
        di->defaultLowOutputLatency = d;
      break;
    }
    case PROP_DEFAULTHIGHINPUTLATENCY: {
      double d;
      if(!JS_ToFloat64(ctx, &d, value))
        di->defaultHighInputLatency = d;
      break;
    }
    case PROP_DEFAULTHIGHOUTPUTLATENCY: {
      double d;
      if(!JS_ToFloat64(ctx, &d, value))
        di->defaultHighOutputLatency = d;
      break;
    }
    case PROP_DEFAULTSAMPLERATE: {
      double d;
      if(!JS_ToFloat64(ctx, &d, value))
        di->defaultSampleRate = d;
      break;
    }
  }

  return ret;
}

static void
js_padeviceinfo_finalizer(JSRuntime* rt, JSValue val) {
  PaDeviceInfo* di;

  if((di = JS_GetOpaque(val, js_padeviceinfo_class_id))) {
    js_free_rt(rt, di->name);
    js_free_rt(rt, di);
  }
}

static JSClassDef js_padeviceinfo_class = {
    .class_name = "PaDeviceInfo",
    .finalizer = js_padeviceinfo_finalizer,
};

static const JSCFunctionListEntry js_padeviceinfo_funcs[] = {
    JS_CGETSET_MAGIC_DEF("structVersion", js_padeviceinfo_get, js_padeviceinfo_set, PROP_STRUCTVERSION),
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_padeviceinfo_get, js_padeviceinfo_set, PROP_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("hostApi", js_padeviceinfo_get, js_padeviceinfo_set, PROP_HOSTAPI),
    JS_CGETSET_MAGIC_DEF("maxInputChannels", js_padeviceinfo_get, js_padeviceinfo_set, PROP_MAXINPUTCHANNELS),
    JS_CGETSET_MAGIC_DEF("maxOutputChannels", js_padeviceinfo_get, js_padeviceinfo_set, PROP_MAXOUTPUTCHANNELS),
    JS_CGETSET_MAGIC_DEF(
        "defaultLowInputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTLOWINPUTLATENCY),
    JS_CGETSET_MAGIC_DEF(
        "defaultLowOutputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTLOWOUTPUTLATENCY),
    JS_CGETSET_MAGIC_DEF(
        "defaultHighInputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTHIGHINPUTLATENCY),
    JS_CGETSET_MAGIC_DEF(
        "defaultHighOutputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTHIGHOUTPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("defaultSampleRate", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTSAMPLERATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaDeviceInfo", JS_PROP_CONFIGURABLE),
};

static JSClassID js_pastreamparameters_class_id;
static JSValue pastreamparameters_proto, pastreamparameters_ctor;

static JSValue
js_pastreamparameters_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  PaStreamParameters* sp = js_mallocz(ctx, sizeof(PaStreamParameters));

  *sp = (PaStreamParameters){-1, 2, 1, 0.001};

  if(argc > 0) {
    int32_t n = -1;
    JS_ToInt32(ctx, &n, argv[0]);
    sp->device = n;
  }
  if(argc > 1) {
    int32_t n = -1;
    JS_ToInt32(ctx, &n, argv[1]);
    sp->channelCount = n;
  }
  if(argc > 2) {
    uint32_t u;
    JS_ToUint32(ctx, &u, argv[2]);
    sp->sampleFormat = u;
  }
  if(argc > 3) {
    double d;
    JS_ToFloat64(ctx, &d, argv[3]);
    sp->suggestedLatency = d;
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = pastreamparameters_proto;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_pastreamparameters_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, sp);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_DEVICE,
  PROP_CHANNELCOUNT,
  PROP_SAMPLEFORMAT,
  PROP_SUGGESTEDLATENCY,
  PROP_HOSTAPISPECIFICSTREAMINFO,
};

static JSValue
js_pastreamparameters_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PaStreamParameters* sp;
  JSValue ret = JS_UNDEFINED;

  if(!(sp = JS_GetOpaque2(ctx, this_val, js_pastreamparameters_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_DEVICE: {
      ret = JS_NewInt32(ctx, sp->device);
      break;
    }
    case PROP_CHANNELCOUNT: {
      ret = JS_NewInt32(ctx, sp->channelCount);
      break;
    }
    case PROP_SAMPLEFORMAT: {
      ret = JS_NewUint32(ctx, sp->sampleFormat);
      break;
    }
    case PROP_SUGGESTEDLATENCY: {
      ret = JS_NewFloat64(ctx, sp->suggestedLatency);
      break;
    }
    case PROP_HOSTAPISPECIFICSTREAMINFO: {
      if(sp->hostApiSpecificStreamInfo) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%p", sp->hostApiSpecificStreamInfo);
        ret = JS_NewString(ctx, buf);
      } else {
        ret = JS_NULL;
      }
      break;
    }
  }

  return ret;
}

static JSValue
js_pastreamparameters_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  PaStreamParameters* sp;
  JSValue ret = JS_UNDEFINED;

  if(!(sp = JS_GetOpaque2(ctx, this_val, js_pastreamparameters_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_DEVICE: {
      int32_t n;
      if(!JS_ToInt32(ctx, &n, value))
        sp->device = n;
      break;
    }
    case PROP_CHANNELCOUNT: {
      int32_t n;
      if(!JS_ToInt32(ctx, &n, value))
        sp->channelCount = n;
      break;
      break;
    }
    case PROP_SAMPLEFORMAT: {
      uint32_t u;
      if(!JS_ToUint32(ctx, &u, value))
        sp->sampleFormat = u;
      break;
      break;
    }
    case PROP_SUGGESTEDLATENCY: {
      double d;
      if(!JS_ToFloat64(ctx, &d, value))
        sp->suggestedLatency = d;
      break;

      break;
    }
    case PROP_HOSTAPISPECIFICSTREAMINFO: {
      break;
    }
  }

  return ret;
}

static void
js_pastreamparameters_finalizer(JSRuntime* rt, JSValue val) {
  PaStreamParameters* sp;

  if((sp = JS_GetOpaque(val, js_pastreamparameters_class_id))) {
    js_free_rt(rt, sp);
  }
}

static JSClassDef js_pastreamparameters_class = {
    .class_name = "PaStreamParameters",
    .finalizer = js_pastreamparameters_finalizer,
};

static const JSCFunctionListEntry js_pastreamparameters_funcs[] = {
    JS_CGETSET_MAGIC_DEF("device", js_pastreamparameters_get, js_pastreamparameters_set, PROP_DEVICE),
    JS_CGETSET_MAGIC_DEF("channelCount", js_pastreamparameters_get, js_pastreamparameters_set, PROP_CHANNELCOUNT),
    JS_CGETSET_MAGIC_DEF("sampleFormat", js_pastreamparameters_get, js_pastreamparameters_set, PROP_SAMPLEFORMAT),
    JS_CGETSET_MAGIC_DEF(
        "suggestedLatency", js_pastreamparameters_get, js_pastreamparameters_set, PROP_SUGGESTEDLATENCY),
    JS_CGETSET_MAGIC_DEF("hostApiSpecificStreamInfo", js_pastreamparameters_get, 0, PROP_HOSTAPISPECIFICSTREAMINFO),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaStreamParameters", JS_PROP_CONFIGURABLE),
};

static JSClassID js_padevices_class_id;
static JSValue padevices_proto, padevices_ctor, padevices_obj;

js_padevices_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {

  if(prop & (1 << 31)) {
    int32_t index;

    if((index = prop & (~(1 << 31))) >= 0) {
      PaDeviceInfo* info = Pa_GetDeviceInfo(index);

      if(info)
        if(pdesc) {
          pdesc->flags = JS_PROP_ENUMERABLE;
          pdesc->value = js_padeviceinfo_wrap(ctx, padeviceinfo_proto, *info);
          pdesc->getter = JS_UNDEFINED;
          pdesc->setter = JS_UNDEFINED;
        }

      return TRUE;
    }
  }

  const char* key;
  BOOL ret = FALSE;

  if((key = JS_AtomToCString(ctx, prop))) {
    if(!strcmp(key, "length")) {
      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_NewUint32(ctx, Pa_GetDeviceCount());
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }

      ret = TRUE;
    }

    JS_FreeCString(ctx, key);
  }

  return ret;
}

static int
js_padevices_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  uint32_t i, len = Pa_GetDeviceCount();
  JSPropertyEnum* props;

  if((props = js_malloc(ctx, sizeof(JSPropertyEnum) * len))) {
    for(i = 0; i < len; i++) {
      props[i].is_enumerable = TRUE;
      props[i].atom = JS_NewAtomUInt32(ctx, i);
    }

    *ptab = props;
    *plen = len;
  }

  return 0;
}

static void
js_padevices_finalizer(JSRuntime* rt, JSValue val) {
  /*PaDevices* sp;

  if((sp = JS_GetOpaque(val, js_padevices_class_id))) {
    js_free_rt(rt, sp);
  }*/
}

static JSClassExoticMethods js_padevices_exotic_methods = {
    .get_own_property = js_padevices_get_own_property,
    .get_own_property_names = js_padevices_get_own_property_names,
};

static JSClassDef js_padevices_class = {
    .class_name = "PaDevices",
    .finalizer = js_padevices_finalizer,
    .exotic = &js_padevices_exotic_methods,
};

static const JSCFunctionListEntry js_padevices_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaDevices", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_portaudio_funcs[] = {
    JS_CFUNC_MAGIC_DEF("Initialize", 0, js_portaudio_function, FUNC_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("Terminate", 0, js_portaudio_function, FUNC_TERMINATE),
    JS_CFUNC_MAGIC_DEF("Sleep", 1, js_portaudio_function, FUNC_SLEEP),
    JS_CFUNC_MAGIC_DEF("GetSampleSize", 1, js_portaudio_function, FUNC_GETSAMPLESIZE),
    JS_PROP_INT32_DEF("NoDevice", paNoDevice, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("UseHostApiSpecificDeviceSpecification",
                      paUseHostApiSpecificDeviceSpecification,
                      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Continue", paContinue, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Complete", paComplete, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Abort", paAbort, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("InputUnderflow", paInputUnderflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("InputOverflow", paInputOverflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OutputUnderflow", paOutputUnderflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OutputOverflow", paOutputOverflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PrimingOutput", paPrimingOutput, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("NoFlag", paNoFlag, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ClipOff", paClipOff, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DitherOff", paDitherOff, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NeverDropInput", paNeverDropInput, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PrimeOutputBuffersUsingStreamCallback",
                      paPrimeOutputBuffersUsingStreamCallback,
                      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PlatformSpecificFlags", paPlatformSpecificFlags, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("Float32", paFloat32, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Int32", paInt32, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Int24", paInt24, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Int16", paInt16, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("Int8", paInt8, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("UInt8", paUInt8, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CustomFormat", paCustomFormat, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NonInterleaved", paNonInterleaved, JS_PROP_CONFIGURABLE),

    // JS_OBJECT_DEF("devices", js_portaudio_devices, countof(js_portaudio_devices), JS_PROP_CONFIGURABLE),
};

int
js_portaudio_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_pastream_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pastream_class_id, &js_pastream_class);

  pastream_ctor = JS_NewCFunction2(ctx, js_pastream_constructor, "PaStream", 1, JS_CFUNC_constructor, 0);
  pastream_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, pastream_proto, js_pastream_funcs, countof(js_pastream_funcs));

  JS_SetClassProto(ctx, js_pastream_class_id, pastream_proto);

  JS_NewClassID(&js_padeviceinfo_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_padeviceinfo_class_id, &js_padeviceinfo_class);

  padeviceinfo_ctor = JS_NewCFunction2(ctx, js_padeviceinfo_constructor, "PaDeviceInfo", 1, JS_CFUNC_constructor, 0);
  padeviceinfo_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, padeviceinfo_proto, js_padeviceinfo_funcs, countof(js_padeviceinfo_funcs));

  JS_SetClassProto(ctx, js_padeviceinfo_class_id, padeviceinfo_proto);

  JS_NewClassID(&js_pastreamparameters_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pastreamparameters_class_id, &js_pastreamparameters_class);

  pastreamparameters_ctor =
      JS_NewCFunction2(ctx, js_pastreamparameters_constructor, "PaStreamParameters", 1, JS_CFUNC_constructor, 0);
  pastreamparameters_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx,
                             pastreamparameters_proto,
                             js_pastreamparameters_funcs,
                             countof(js_pastreamparameters_funcs));

  JS_SetClassProto(ctx, js_pastreamparameters_class_id, pastreamparameters_proto);

  JS_NewClassID(&js_padevices_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_padevices_class_id, &js_padevices_class);

  padevices_ctor = JS_NewObjectProto(ctx, JS_NULL);
  padevices_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, padevices_proto, js_padevices_funcs, countof(js_padevices_funcs));

  JS_SetClassProto(ctx, js_padevices_class_id, padevices_proto);

  padevices_obj = JS_NewObjectProtoClass(ctx, padevices_proto, js_padevices_class_id);

  if(m) {
    JS_SetModuleExport(ctx, m, "Stream", pastream_ctor);
    JS_SetModuleExport(ctx, m, "DeviceInfo", padeviceinfo_ctor);
    JS_SetModuleExport(ctx, m, "StreamParameters", pastreamparameters_ctor);
    JS_SetModuleExport(ctx, m, "devices", padevices_obj);
    JS_SetModuleExportList(ctx, m, js_portaudio_funcs, countof(js_portaudio_funcs));
  }

  return 0;
}

VISIBLE void
js_init_module_portaudio(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "Stream");
  JS_AddModuleExport(ctx, m, "DeviceInfo");
  JS_AddModuleExport(ctx, m, "StreamParameters");
  JS_AddModuleExport(ctx, m, "devices");
  JS_AddModuleExportList(ctx, m, js_portaudio_funcs, countof(js_portaudio_funcs));
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_portaudio_init))) {
    js_init_module_portaudio(ctx, m);
  }

  return m;
}
