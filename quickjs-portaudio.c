#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include <portaudio.h>

static JSClassID js_pastream_class_id;
static JSValue pastream_proto, pastream_ctor;

static JSValue
js_pastream_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  PaStream* st = js_mallocz(ctx, sizeof(PaStream));

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

  PROP_INDEX,
};

static JSValue
js_pastream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  PaStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_pastream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}

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
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaStream", JS_PROP_CONFIGURABLE),
};

static JSClassID js_padeviceinfo_class_id;
static JSValue padeviceinfo_proto, padeviceinfo_ctor;

static JSValue
js_padeviceinfo_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  PaDeviceInfo* di = js_mallocz(ctx, sizeof(PaDeviceInfo));

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
    JS_CGETSET_MAGIC_DEF("name", js_padeviceinfo_get, js_padeviceinfo_set, PROP_NAME),
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

  switch(magic) {}

  return ret;
}

static JSValue
js_pastreamparameters_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  PaStreamParameters* sp;
  JSValue ret = JS_UNDEFINED;

  if(!(sp = JS_GetOpaque2(ctx, this_val, js_pastreamparameters_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}

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
    JS_CGETSET_MAGIC_DEF("hostApiSpecificStreamInfo",
                         js_pastreamparameters_get,
                         js_pastreamparameters_set,
                         PROP_HOSTAPISPECIFICSTREAMINFO),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PaStreamParameters", JS_PROP_CONFIGURABLE),
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

  if(m) {
    JS_SetModuleExport(ctx, m, "PaStream", pastream_ctor);
    JS_SetModuleExport(ctx, m, "PaDeviceInfo", padeviceinfo_ctor);
    JS_SetModuleExport(ctx, m, "PaStreamParameters", pastreamparameters_ctor);
  }

  return 0;
}

VISIBLE void
js_init_module_portaudio(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "PaStream");
  JS_AddModuleExport(ctx, m, "PaDeviceInfo");
  JS_AddModuleExport(ctx, m, "PaStreamParameters");
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_portaudio_init))) {
    js_init_module_portaudio(ctx, m);
  }

  return m;
}
