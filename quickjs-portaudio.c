#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include "defines.h"
#include <portaudio.h>

static JSClassID js_pastream_class_id;
static JSValue pastream_proto, pastream_ctor;

/* Opened with a NULL stream callback (blocking I/O) so JS is only ever
 * invoked from the interpreter thread, driving the stream via read()/
 * write() -- calling back into QuickJS from PortAudio's own realtime
 * audio thread would not be thread-safe. */
typedef struct {
  PaStream* stream;
  int32_t numInputChannels;
  int32_t numOutputChannels;
  PaSampleFormat sampleFormat;
} JSPaStream;

static uint8_t*
js_pastream_get_buffer(JSContext* ctx, JSValueConst val, size_t* plen) {
  size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);

  if(!JS_IsException(buf)) {
    size_t ab_size = 0;
    uint8_t* ab_data = JS_GetArrayBuffer(ctx, &ab_size, buf);
    JS_FreeValue(ctx, buf);
    if(!ab_data)
      return NULL;
    *plen = byte_length;
    return ab_data + byte_offset;
  }

  return JS_GetArrayBuffer(ctx, plen, val);
}

enum {
  FUNC_INITIALIZE = 0,
  FUNC_TERMINATE,
  FUNC_SLEEP,
  FUNC_GETSAMPLESIZE,
  FUNC_GETVERSION,
  FUNC_GETVERSIONTEXT,
  FUNC_GETLASTHOSTERRORINFO,
};

static JSValue
js_portaudio_error(JSContext* ctx, PaError err) {
  return err < 0 ? JS_ThrowInternalError(ctx, "PaError: %s", Pa_GetErrorText(err)) : err == 0 ? JS_UNDEFINED : JS_NewInt32(ctx, err);
}

static const char*
pa_hostapitype_name(PaHostApiTypeId id) {
  switch(id) {
    case paInDevelopment: return "InDevelopment";
    case paDirectSound: return "DirectSound";
    case paMME: return "MME";
    case paASIO: return "ASIO";
    case paSoundManager: return "SoundManager";
    case paCoreAudio: return "CoreAudio";
    case paOSS: return "OSS";
    case paALSA: return "ALSA";
    case paAL: return "AL";
    case paBeOS: return "BeOS";
    case paWDMKS: return "WDMKS";
    case paJACK: return "JACK";
    case paWASAPI: return "WASAPI";
    case paAudioScienceHPI: return "AudioScienceHPI";
  }
  return NULL;
}

/* Parses a plain {device, channelCount, sampleFormat, suggestedLatency}
 * object into a stack PaStreamParameters - used only at Pa_OpenStream()/
 * Pa_IsFormatSupported() call time, so this is a duck-typed value
 * conversion (skill section 6), not a resource class. Returns FALSE (obj
 * is not an object, e.g. null/undefined) to mean "pass NULL for this
 * side", matching Pa_OpenStream()'s own NULL-parameter convention. */
static BOOL
js_pastreamparameters_fromobj(JSContext* ctx, JSValueConst obj, PaStreamParameters* out) {
  JSValue v;

  if(!JS_IsObject(obj))
    return FALSE;

  *out = (PaStreamParameters){-1, 2, paFloat32, 0.001, NULL};

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "device"))) {
    int32_t n;
    if(!JS_ToInt32(ctx, &n, v))
      out->device = n;
  }
  JS_FreeValue(ctx, v);

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "channelCount"))) {
    int32_t n;
    if(!JS_ToInt32(ctx, &n, v))
      out->channelCount = n;
  }
  JS_FreeValue(ctx, v);

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "sampleFormat"))) {
    uint32_t u;
    if(!JS_ToUint32(ctx, &u, v))
      out->sampleFormat = u;
  }
  JS_FreeValue(ctx, v);

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "suggestedLatency"))) {
    double d;
    if(!JS_ToFloat64(ctx, &d, v))
      out->suggestedLatency = d;
  }
  JS_FreeValue(ctx, v);

  return TRUE;
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

    case FUNC_GETVERSION: {
      ret = JS_NewInt32(ctx, Pa_GetVersion());
      break;
    }

    case FUNC_GETVERSIONTEXT: {
      ret = JS_NewString(ctx, Pa_GetVersionText());
      break;
    }

    case FUNC_GETLASTHOSTERRORINFO: {
      const PaHostErrorInfo* info = Pa_GetLastHostErrorInfo();

      ret = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, ret, "hostApiType", JS_NewInt32(ctx, info->hostApiType));
      JS_SetPropertyStr(ctx, ret, "errorCode", JS_NewInt64(ctx, info->errorCode));
      JS_SetPropertyStr(ctx, ret, "errorText", JS_NewString(ctx, info->errorText));
      break;
    }
  }

  return ret;
}

static JSValue
js_pastream_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  PaStream* st = NULL;
  JSPaStream* w = NULL;
  PaError r;

  int32_t numInputChannels = 0, numOutputChannels = 2;
  uint32_t sampleFormat = paFloat32;

  if(argc > 0 && JS_IsObject(argv[0])) {
    PaStreamParameters inParams, outParams;
    JSValue inVal = JS_GetPropertyStr(ctx, argv[0], "input");
    JSValue outVal = JS_GetPropertyStr(ctx, argv[0], "output");
    BOOL hasIn = js_pastreamparameters_fromobj(ctx, inVal, &inParams);
    BOOL hasOut = js_pastreamparameters_fromobj(ctx, outVal, &outParams);
    JS_FreeValue(ctx, inVal);
    JS_FreeValue(ctx, outVal);

    double sampleRate = 44100;
    uint32_t framesPerBuffer = paFramesPerBufferUnspecified;
    uint32_t flags = paNoFlag;
    JSValue v;

    if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, argv[0], "sampleRate")))
      JS_ToFloat64(ctx, &sampleRate, v);
    JS_FreeValue(ctx, v);

    if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, argv[0], "framesPerBuffer")))
      JS_ToUint32(ctx, &framesPerBuffer, v);
    JS_FreeValue(ctx, v);

    if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, argv[0], "flags")))
      JS_ToUint32(ctx, &flags, v);
    JS_FreeValue(ctx, v);

    r = Pa_OpenStream(&st, hasIn ? &inParams : NULL, hasOut ? &outParams : NULL, sampleRate, framesPerBuffer, flags, NULL, NULL);

    numInputChannels = hasIn ? inParams.channelCount : 0;
    numOutputChannels = hasOut ? outParams.channelCount : 0;
    sampleFormat = hasIn ? inParams.sampleFormat : hasOut ? outParams.sampleFormat : paFloat32;
  } else {
    double sampleRate = 44100;
    uint32_t framesPerBuffer = paFramesPerBufferUnspecified;

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

    r = Pa_OpenDefaultStream(&st, numInputChannels, numOutputChannels, sampleFormat, sampleRate, framesPerBuffer, NULL, NULL);
  }

  if(r != paNoError) {
    JS_ThrowInternalError(ctx, "PortAudio error: %s", Pa_GetErrorText(r));
    goto fail;
  }

  if(!(w = js_mallocz(ctx, sizeof(JSPaStream)))) {
    Pa_CloseStream(st);
    goto fail;
  }

  w->stream = st;
  w->numInputChannels = numInputChannels;
  w->numOutputChannels = numOutputChannels;
  w->sampleFormat = sampleFormat;

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

  JS_SetOpaque(obj, w);
  return obj;

fail:
  if(w) {
    Pa_CloseStream(w->stream);
    js_free(ctx, w);
  }
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pastream_isformatsupported(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  PaStreamParameters inParams, outParams;
  BOOL hasIn = argc > 0 ? js_pastreamparameters_fromobj(ctx, argv[0], &inParams) : FALSE;
  BOOL hasOut = argc > 1 ? js_pastreamparameters_fromobj(ctx, argv[1], &outParams) : FALSE;
  double sampleRate = 44100;

  if(argc > 2)
    JS_ToFloat64(ctx, &sampleRate, argv[2]);

  return js_portaudio_error(ctx, Pa_IsFormatSupported(hasIn ? &inParams : NULL, hasOut ? &outParams : NULL, sampleRate));
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
  JSPaStream* w;
  PaStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  st = w->stream;

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
      const char* str = pa_hostapitype_name(Pa_GetStreamHostApiType(st));

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
  JSPaStream* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

enum {
  METHOD_READ = 0,
  METHOD_WRITE,
  METHOD_START,
  METHOD_STOP,
  METHOD_ABORT,
  METHOD_CLOSE,
};

static JSValue
js_pastream_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSPaStream* w;
  PaStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  st = w->stream;

  if(!st)
    return magic == METHOD_CLOSE ? JS_UNDEFINED : JS_ThrowTypeError(ctx, "PaStream is closed");

  switch(magic) {
    case METHOD_READ: {
      size_t len;
      uint8_t* ptr;
      uint32_t frames = 0;

      if(argc < 1 || !(ptr = js_pastream_get_buffer(ctx, argv[0], &len)))
        return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer or TypedArray");

      if(argc > 1) {
        JS_ToUint32(ctx, &frames, argv[1]);
      } else {
        int32_t channels = w->numInputChannels > 0 ? w->numInputChannels : 1;
        PaError sampleSize = Pa_GetSampleSize(w->sampleFormat);
        if(sampleSize > 0)
          frames = len / (channels * sampleSize);
      }

      ret = js_portaudio_error(ctx, Pa_ReadStream(st, ptr, frames));
      break;
    }

    case METHOD_WRITE: {
      size_t len;
      uint8_t* ptr;
      uint32_t frames = 0;

      if(argc < 1 || !(ptr = js_pastream_get_buffer(ctx, argv[0], &len)))
        return JS_ThrowTypeError(ctx, "argument 1 must be an ArrayBuffer or TypedArray");

      if(argc > 1) {
        JS_ToUint32(ctx, &frames, argv[1]);
      } else {
        int32_t channels = w->numOutputChannels > 0 ? w->numOutputChannels : 1;
        PaError sampleSize = Pa_GetSampleSize(w->sampleFormat);
        if(sampleSize > 0)
          frames = len / (channels * sampleSize);
      }

      ret = js_portaudio_error(ctx, Pa_WriteStream(st, ptr, frames));
      break;
    }

    case METHOD_START: {
      ret = js_portaudio_error(ctx, Pa_StartStream(st));
      break;
    }
    case METHOD_STOP: {
      ret = js_portaudio_error(ctx, Pa_StopStream(st));
      break;
    }
    case METHOD_ABORT: {
      ret = js_portaudio_error(ctx, Pa_AbortStream(st));
      break;
    }
    case METHOD_CLOSE: {
      ret = js_portaudio_error(ctx, Pa_CloseStream(st));
      w->stream = NULL;
      break;
    }
  }

  return ret;
}

static void
js_pastream_finalizer(JSRuntime* rt, JSValue val) {
  JSPaStream* w;

  if((w = JS_GetOpaque(val, js_pastream_class_id))) {
    if(w->stream)
      Pa_CloseStream(w->stream);
    js_free_rt(rt, w);
  }
}

static JSClassDef js_pastream_class = {
    .class_name = "PaStream",
    .finalizer = js_pastream_finalizer,
};

static const JSCFunctionListEntry js_pastream_funcs[] = {
    JS_CGETSET_MAGIC_DEF("active", js_pastream_get, 0, PROP_ACTIVE),
    JS_CGETSET_MAGIC_DEF("stopped", js_pastream_get, 0, PROP_STOPPED),
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
    JS_CFUNC_MAGIC_DEF("read", 1, js_pastream_method, METHOD_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_pastream_method, METHOD_WRITE),
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
    JS_CGETSET_MAGIC_DEF("defaultLowInputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTLOWINPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("defaultLowOutputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTLOWOUTPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("defaultHighInputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTHIGHINPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("defaultHighOutputLatency", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTHIGHOUTPUTLATENCY),
    JS_CGETSET_MAGIC_DEF("defaultSampleRate", js_padeviceinfo_get, js_padeviceinfo_set, PROP_DEFAULTSAMPLERATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaDeviceInfo", JS_PROP_CONFIGURABLE),
};

static JSClassID js_hostapiinfo_class_id;
static JSValue hostapiinfo_proto, hostapiinfo_ctor;

static JSValue
js_hostapiinfo_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  PaHostApiInfo* hi = js_mallocz(ctx, sizeof(PaHostApiInfo));

  if(argc > 0) {
    int32_t index = -1;
    JS_ToInt32(ctx, &index, argv[0]);
    const PaHostApiInfo* info;

    if((info = Pa_GetHostApiInfo(index))) {
      *hi = *info;
      if(hi->name)
        hi->name = js_strdup(ctx, hi->name);
    }
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = hostapiinfo_proto;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_hostapiinfo_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, hi);
  return obj;

fail:
  js_free(ctx, hi);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_hostapiinfo_wrap(JSContext* ctx, JSValueConst proto, PaHostApiInfo info) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_hostapiinfo_class_id);

  PaHostApiInfo* hi;

  if(!(hi = js_mallocz(ctx, sizeof(PaHostApiInfo))))
    goto fail;

  if(info.name)
    info.name = js_strdup(ctx, info.name);

  *hi = info;

  JS_SetOpaque(obj, hi);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  HAPROP_STRUCTVERSION = 0,
  HAPROP_TYPE,
  HAPROP_TYPENAME,
  HAPROP_NAME,
  HAPROP_DEVICECOUNT,
  HAPROP_DEFAULTINPUTDEVICE,
  HAPROP_DEFAULTOUTPUTDEVICE,
};

static JSValue
js_hostapiinfo_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PaHostApiInfo* hi;
  JSValue ret = JS_UNDEFINED;

  if(!(hi = JS_GetOpaque2(ctx, this_val, js_hostapiinfo_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case HAPROP_STRUCTVERSION: {
      ret = JS_NewInt32(ctx, hi->structVersion);
      break;
    }
    case HAPROP_TYPE: {
      ret = JS_NewInt32(ctx, hi->type);
      break;
    }
    case HAPROP_TYPENAME: {
      const char* str = pa_hostapitype_name(hi->type);
      ret = str ? JS_NewString(ctx, str) : JS_NULL;
      break;
    }
    case HAPROP_NAME: {
      ret = hi->name ? JS_NewString(ctx, hi->name) : JS_NULL;
      break;
    }
    case HAPROP_DEVICECOUNT: {
      ret = JS_NewInt32(ctx, hi->deviceCount);
      break;
    }
    case HAPROP_DEFAULTINPUTDEVICE: {
      ret = JS_NewInt32(ctx, hi->defaultInputDevice);
      break;
    }
    case HAPROP_DEFAULTOUTPUTDEVICE: {
      ret = JS_NewInt32(ctx, hi->defaultOutputDevice);
      break;
    }
  }

  return ret;
}

static void
js_hostapiinfo_finalizer(JSRuntime* rt, JSValue val) {
  PaHostApiInfo* hi;

  if((hi = JS_GetOpaque(val, js_hostapiinfo_class_id))) {
    js_free_rt(rt, (void*)hi->name);
    js_free_rt(rt, hi);
  }
}

static JSClassDef js_hostapiinfo_class = {
    .class_name = "HostApiInfo",
    .finalizer = js_hostapiinfo_finalizer,
};

static const JSCFunctionListEntry js_hostapiinfo_funcs[] = {
    JS_CGETSET_MAGIC_DEF("structVersion", js_hostapiinfo_get, 0, HAPROP_STRUCTVERSION),
    JS_CGETSET_MAGIC_DEF("type", js_hostapiinfo_get, 0, HAPROP_TYPE),
    JS_CGETSET_MAGIC_DEF("typeName", js_hostapiinfo_get, 0, HAPROP_TYPENAME),
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_hostapiinfo_get, 0, HAPROP_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("deviceCount", js_hostapiinfo_get, 0, HAPROP_DEVICECOUNT),
    JS_CGETSET_MAGIC_DEF("defaultInputDevice", js_hostapiinfo_get, 0, HAPROP_DEFAULTINPUTDEVICE),
    JS_CGETSET_MAGIC_DEF("defaultOutputDevice", js_hostapiinfo_get, 0, HAPROP_DEFAULTOUTPUTDEVICE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HostApiInfo", JS_PROP_CONFIGURABLE),
};

static JSClassID js_padevices_class_id;
static JSValue padevices_proto, padevices_ctor, padevices_obj;

static BOOL
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
    } else if(!strcmp(key, "defaultInput")) {
      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_NewInt32(ctx, Pa_GetDefaultInputDevice());
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }

      ret = TRUE;
    } else if(!strcmp(key, "defaultOutput")) {
      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_NewInt32(ctx, Pa_GetDefaultOutputDevice());
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

static JSClassID js_pahostapis_class_id;
static JSValue pahostapis_proto, pahostapis_obj;

static BOOL
js_pahostapis_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {

  if(prop & (1 << 31)) {
    int32_t index;

    if((index = prop & (~(1 << 31))) >= 0) {
      PaHostApiInfo* info = Pa_GetHostApiInfo(index);

      if(info)
        if(pdesc) {
          pdesc->flags = JS_PROP_ENUMERABLE;
          pdesc->value = js_hostapiinfo_wrap(ctx, hostapiinfo_proto, *info);
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
        pdesc->value = JS_NewUint32(ctx, Pa_GetHostApiCount());
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }

      ret = TRUE;
    } else if(!strcmp(key, "default")) {
      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_NewInt32(ctx, Pa_GetDefaultHostApi());
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
js_pahostapis_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  uint32_t i, len = Pa_GetHostApiCount();
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
js_pahostapis_finalizer(JSRuntime* rt, JSValue val) {
}

static JSClassExoticMethods js_pahostapis_exotic_methods = {
    .get_own_property = js_pahostapis_get_own_property,
    .get_own_property_names = js_pahostapis_get_own_property_names,
};

static JSClassDef js_pahostapis_class = {
    .class_name = "PaHostApis",
    .finalizer = js_pahostapis_finalizer,
    .exotic = &js_pahostapis_exotic_methods,
};

static const JSCFunctionListEntry js_pahostapis_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaHostApis", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_portaudio_funcs[] = {
    JS_CFUNC_MAGIC_DEF("Pa_Initialize", 0, js_portaudio_function, FUNC_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("Pa_Terminate", 0, js_portaudio_function, FUNC_TERMINATE),
    JS_CFUNC_MAGIC_DEF("Pa_Sleep", 1, js_portaudio_function, FUNC_SLEEP),
    JS_CFUNC_MAGIC_DEF("Pa_GetSampleSize", 1, js_portaudio_function, FUNC_GETSAMPLESIZE),
    JS_CFUNC_MAGIC_DEF("Pa_GetVersion", 0, js_portaudio_function, FUNC_GETVERSION),
    JS_CFUNC_MAGIC_DEF("Pa_GetVersionText", 0, js_portaudio_function, FUNC_GETVERSIONTEXT),
    JS_CFUNC_MAGIC_DEF("Pa_GetLastHostErrorInfo", 0, js_portaudio_function, FUNC_GETLASTHOSTERRORINFO),

    JS_PROP_INT32_DEF("paNoDevice", paNoDevice, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paUseHostApiSpecificDeviceSpecification", paUseHostApiSpecificDeviceSpecification, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paContinue", paContinue, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paComplete", paComplete, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paAbort", paAbort, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("paInputUnderflow", paInputUnderflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paInputOverflow", paInputOverflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paOutputUnderflow", paOutputUnderflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paOutputOverflow", paOutputOverflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paPrimingOutput", paPrimingOutput, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("paNoFlag", paNoFlag, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paClipOff", paClipOff, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paDitherOff", paDitherOff, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paNeverDropInput", paNeverDropInput, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paPrimeOutputBuffersUsingStreamCallback", paPrimeOutputBuffersUsingStreamCallback, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paPlatformSpecificFlags", paPlatformSpecificFlags, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("paFloat32", paFloat32, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paInt32", paInt32, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paInt24", paInt24, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paInt16", paInt16, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paInt8", paInt8, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paUInt8", paUInt8, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paCustomFormat", paCustomFormat, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paNonInterleaved", paNonInterleaved, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("paFramesPerBufferUnspecified", paFramesPerBufferUnspecified, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("paInDevelopment", paInDevelopment, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paDirectSound", paDirectSound, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paMME", paMME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paASIO", paASIO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paSoundManager", paSoundManager, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paCoreAudio", paCoreAudio, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paOSS", paOSS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paALSA", paALSA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paAL", paAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paBeOS", paBeOS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paWDMKS", paWDMKS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paJACK", paJACK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paWASAPI", paWASAPI, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("paAudioScienceHPI", paAudioScienceHPI, JS_PROP_CONFIGURABLE),
};

int
js_portaudio_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_pastream_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pastream_class_id, &js_pastream_class);

  pastream_ctor = JS_NewCFunction2(ctx, js_pastream_constructor, "PaStream", 1, JS_CFUNC_constructor, 0);
  pastream_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, pastream_proto, js_pastream_funcs, countof(js_pastream_funcs));

  JS_SetClassProto(ctx, js_pastream_class_id, pastream_proto);

  JS_SetPropertyStr(ctx, pastream_ctor, "isFormatSupported", JS_NewCFunction(ctx, js_pastream_isformatsupported, "isFormatSupported", 3));

  JS_NewClassID(&js_padeviceinfo_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_padeviceinfo_class_id, &js_padeviceinfo_class);

  padeviceinfo_ctor = JS_NewCFunction2(ctx, js_padeviceinfo_constructor, "PaDeviceInfo", 1, JS_CFUNC_constructor, 0);
  padeviceinfo_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, padeviceinfo_proto, js_padeviceinfo_funcs, countof(js_padeviceinfo_funcs));

  JS_SetClassProto(ctx, js_padeviceinfo_class_id, padeviceinfo_proto);

  JS_NewClassID(&js_hostapiinfo_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_hostapiinfo_class_id, &js_hostapiinfo_class);

  hostapiinfo_ctor = JS_NewCFunction2(ctx, js_hostapiinfo_constructor, "HostApiInfo", 1, JS_CFUNC_constructor, 0);
  hostapiinfo_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, hostapiinfo_proto, js_hostapiinfo_funcs, countof(js_hostapiinfo_funcs));

  JS_SetClassProto(ctx, js_hostapiinfo_class_id, hostapiinfo_proto);

  JS_NewClassID(&js_padevices_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_padevices_class_id, &js_padevices_class);

  padevices_ctor = JS_NewObjectProto(ctx, JS_NULL);
  padevices_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, padevices_proto, js_padevices_funcs, countof(js_padevices_funcs));

  JS_SetClassProto(ctx, js_padevices_class_id, padevices_proto);

  padevices_obj = JS_NewObjectProtoClass(ctx, padevices_proto, js_padevices_class_id);

  JS_NewClassID(&js_pahostapis_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pahostapis_class_id, &js_pahostapis_class);

  pahostapis_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, pahostapis_proto, js_pahostapis_funcs, countof(js_pahostapis_funcs));

  JS_SetClassProto(ctx, js_pahostapis_class_id, pahostapis_proto);

  pahostapis_obj = JS_NewObjectProtoClass(ctx, pahostapis_proto, js_pahostapis_class_id);

  if(m) {
    JS_SetModuleExport(ctx, m, "PaStream", pastream_ctor);
    JS_SetModuleExport(ctx, m, "PaDeviceInfo", padeviceinfo_ctor);
    JS_SetModuleExport(ctx, m, "HostApiInfo", hostapiinfo_ctor);
    JS_SetModuleExport(ctx, m, "devices", padevices_obj);
    JS_SetModuleExport(ctx, m, "hostApis", pahostapis_obj);
    JS_SetModuleExportList(ctx, m, js_portaudio_funcs, countof(js_portaudio_funcs));
  }

  return 0;
}

VISIBLE void
js_init_module_portaudio(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "PaStream");
  JS_AddModuleExport(ctx, m, "PaDeviceInfo");
  JS_AddModuleExport(ctx, m, "HostApiInfo");
  JS_AddModuleExport(ctx, m, "devices");
  JS_AddModuleExport(ctx, m, "hostApis");
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
