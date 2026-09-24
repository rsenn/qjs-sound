#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include <math.h>
#include "defines.h"
#include "quickjs-typedarray.h"
#include <samplerate.h>

/* Extra frames of headroom on top of ceil(input_frames * ratio) when
 * sizing an internally-allocated output buffer (no `output` argument
 * passed to .process()/.simple()) - libsamplerate does not report in
 * advance exactly how many frames a given input+ratio will produce, see
 * doc/samplerate.md's "output-sizing heuristic" open question. */
#define SRC_OUTPUT_HEADROOM_FRAMES 256

static JSClassID js_srcstate_class_id;
static JSValue srcstate_proto, srcstate_ctor;

typedef struct {
  SRC_STATE* state;
  int32_t channels;
} JSSrcState;

static JSValue
js_samplerate_throw(JSContext* ctx, int err) {
  return JS_ThrowInternalError(ctx, "libsamplerate error: %s", src_strerror(err));
}

enum {
  METHOD_PROCESS = 0,
  METHOD_SETRATIO,
  METHOD_RESET,
  METHOD_CLOSE,
};

static JSValue
js_srcstate_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JSSrcState* w = NULL;
  int32_t converterType = 0, channels = 1;
  int error = 0;
  SRC_STATE* state;

  if(argc > 0)
    JS_ToInt32(ctx, &converterType, argv[0]);
  if(argc > 1)
    JS_ToInt32(ctx, &channels, argv[1]);

  if(!(state = src_new(converterType, channels, &error))) {
    js_samplerate_throw(ctx, error);
    goto fail;
  }

  if(!(w = js_mallocz(ctx, sizeof(JSSrcState)))) {
    src_delete(state);
    goto fail;
  }

  w->state = state;
  w->channels = channels;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, srcstate_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_srcstate_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  return obj;

fail:
  if(w)
    js_free(ctx, w);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_srcstate_finalizer(JSRuntime* rt, JSValue val) {
  JSSrcState* w;

  if((w = JS_GetOpaque(val, js_srcstate_class_id))) {
    if(w->state)
      src_delete(w->state);
    js_free_rt(rt, w);
  }
}

enum {
  PROP_CHANNELS = 0,
};

static JSValue
js_srcstate_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSSrcState* w;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_srcstate_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_CHANNELS: return JS_NewInt32(ctx, w->state ? src_get_channels(w->state) : w->channels);
  }

  return JS_UNDEFINED;
}

static JSValue
js_srcstate_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSSrcState* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_srcstate_class_id)))
    return JS_EXCEPTION;

  if(!w->state)
    return magic == METHOD_CLOSE ? JS_UNDEFINED : JS_ThrowTypeError(ctx, "SampleRateConverter is closed");

  switch(magic) {
    case METHOD_PROCESS: {
      JSBufferView in, out;
      BOOL has_output;
      double ratio = 1.0;
      BOOL endOfInput = FALSE;
      long input_frames;
      SRC_DATA data;
      int err;

      if(argc < 1 || !js_bufferview_get_kind(ctx, argv[0], JS_TYPEDARRAY_FLOAT32, &in))
        return JS_EXCEPTION;

      if(argc > 1)
        JS_ToFloat64(ctx, &ratio, argv[1]);
      if(argc > 2)
        endOfInput = JS_ToBool(ctx, argv[2]);

      has_output = argc > 3 && !JS_IsUndefined(argv[3]);
      if(has_output && !js_bufferview_get_kind(ctx, argv[3], JS_TYPEDARRAY_FLOAT32, &out))
        return JS_EXCEPTION;

      input_frames = (long)(in.byte_length / sizeof(float) / w->channels);

      memset(&data, 0, sizeof(data));
      data.data_in = (const float*)in.ptr;
      data.input_frames = input_frames;
      data.src_ratio = ratio;
      data.end_of_input = endOfInput;

      if(has_output) {
        data.data_out = (float*)out.ptr;
        data.output_frames = (long)(out.byte_length / sizeof(float) / w->channels);

        if((err = src_process(w->state, &data)))
          return js_samplerate_throw(ctx, err);

        ret = JS_NewInt64(ctx, data.output_frames_gen);
      } else {
        long out_capacity = (long)ceil(input_frames * ratio) + SRC_OUTPUT_HEADROOM_FRAMES;
        float* out_buf;

        if(!(out_buf = js_malloc(ctx, (size_t)out_capacity * w->channels * sizeof(float))))
          return JS_EXCEPTION;

        data.data_out = out_buf;
        data.output_frames = out_capacity;

        if((err = src_process(w->state, &data))) {
          js_free(ctx, out_buf);
          return js_samplerate_throw(ctx, err);
        }

        ret = js_typedarray_from_malloc(ctx, out_buf, (size_t)data.output_frames_gen * w->channels, JS_TYPEDARRAY_FLOAT32);
      }

      break;
    }

    case METHOD_SETRATIO: {
      double ratio = 1.0;
      int err;

      if(argc > 0)
        JS_ToFloat64(ctx, &ratio, argv[0]);

      if((err = src_set_ratio(w->state, ratio)))
        return js_samplerate_throw(ctx, err);

      break;
    }

    case METHOD_RESET: {
      int err;

      if((err = src_reset(w->state)))
        return js_samplerate_throw(ctx, err);

      break;
    }

    case METHOD_CLOSE: {
      src_delete(w->state);
      w->state = NULL;
      break;
    }
  }

  return ret;
}

static JSValue
js_srcstate_simple(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSBufferView in;
  double ratio = 0;
  int32_t converterType = 0, channels = 1;
  long input_frames, out_capacity;
  float* out_buf;
  SRC_DATA data;
  int err;

  if(argc < 4)
    return JS_ThrowTypeError(ctx, "expected 4 arguments: input, ratio, converterType, channels");

  if(!js_bufferview_get_kind(ctx, argv[0], JS_TYPEDARRAY_FLOAT32, &in))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &ratio, argv[1]);
  JS_ToInt32(ctx, &converterType, argv[2]);
  JS_ToInt32(ctx, &channels, argv[3]);

  input_frames = (long)(in.byte_length / sizeof(float) / channels);
  out_capacity = (long)ceil(input_frames * ratio) + SRC_OUTPUT_HEADROOM_FRAMES;

  if(!(out_buf = js_malloc(ctx, (size_t)out_capacity * channels * sizeof(float))))
    return JS_EXCEPTION;

  memset(&data, 0, sizeof(data));
  data.data_in = (const float*)in.ptr;
  data.input_frames = input_frames;
  data.data_out = out_buf;
  data.output_frames = out_capacity;
  data.src_ratio = ratio;
  data.end_of_input = TRUE;

  if((err = src_simple(&data, converterType, channels))) {
    js_free(ctx, out_buf);
    return js_samplerate_throw(ctx, err);
  }

  return js_typedarray_from_malloc(ctx, out_buf, (size_t)data.output_frames_gen * channels, JS_TYPEDARRAY_FLOAT32);
}

static JSValue
js_srcstate_getname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t converterType = 0;
  const char* name;

  if(argc > 0)
    JS_ToInt32(ctx, &converterType, argv[0]);

  return (name = src_get_name(converterType)) ? JS_NewString(ctx, name) : JS_NULL;
}

static JSValue
js_srcstate_getdescription(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t converterType = 0;
  const char* str;

  if(argc > 0)
    JS_ToInt32(ctx, &converterType, argv[0]);

  return (str = src_get_description(converterType)) ? JS_NewString(ctx, str) : JS_NULL;
}

static JSValue
js_srcstate_getversion(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_NewString(ctx, src_get_version());
}

static JSValue
js_srcstate_isvalidratio(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  double ratio = 0;

  if(argc > 0)
    JS_ToFloat64(ctx, &ratio, argv[0]);

  return JS_NewBool(ctx, src_is_valid_ratio(ratio));
}

static const JSCFunctionListEntry js_srcstate_funcs[] = {
    JS_CGETSET_MAGIC_DEF("channels", js_srcstate_get, 0, PROP_CHANNELS),
    JS_CFUNC_MAGIC_DEF("process", 1, js_srcstate_method, METHOD_PROCESS),
    JS_CFUNC_MAGIC_DEF("setRatio", 1, js_srcstate_method, METHOD_SETRATIO),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_srcstate_method, METHOD_RESET),
    JS_CFUNC_MAGIC_DEF("close", 0, js_srcstate_method, METHOD_CLOSE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SampleRateConverter", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_samplerate_funcs[] = {
    JS_PROP_INT32_DEF("SRC_SINC_BEST_QUALITY", SRC_SINC_BEST_QUALITY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SRC_SINC_MEDIUM_QUALITY", SRC_SINC_MEDIUM_QUALITY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SRC_SINC_FASTEST", SRC_SINC_FASTEST, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SRC_ZERO_ORDER_HOLD", SRC_ZERO_ORDER_HOLD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SRC_LINEAR", SRC_LINEAR, JS_PROP_CONFIGURABLE),
};

static JSClassDef js_srcstate_class = {
    .class_name = "SampleRateConverter",
    .finalizer = js_srcstate_finalizer,
};

int
js_samplerate_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_srcstate_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_srcstate_class_id, &js_srcstate_class);

  srcstate_ctor = JS_NewCFunction2(ctx, js_srcstate_constructor, "SampleRateConverter", 2, JS_CFUNC_constructor, 0);
  srcstate_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, srcstate_proto, js_srcstate_funcs, countof(js_srcstate_funcs));
  JS_SetClassProto(ctx, js_srcstate_class_id, srcstate_proto);

  JS_SetPropertyStr(ctx, srcstate_ctor, "simple", JS_NewCFunction(ctx, js_srcstate_simple, "simple", 4));
  JS_SetPropertyStr(ctx, srcstate_ctor, "getName", JS_NewCFunction(ctx, js_srcstate_getname, "getName", 1));
  JS_SetPropertyStr(ctx, srcstate_ctor, "getDescription", JS_NewCFunction(ctx, js_srcstate_getdescription, "getDescription", 1));
  JS_SetPropertyStr(ctx, srcstate_ctor, "getVersion", JS_NewCFunction(ctx, js_srcstate_getversion, "getVersion", 0));
  JS_SetPropertyStr(ctx, srcstate_ctor, "isValidRatio", JS_NewCFunction(ctx, js_srcstate_isvalidratio, "isValidRatio", 1));

  if(m) {
    JS_SetModuleExport(ctx, m, "SampleRateConverter", srcstate_ctor);
    JS_SetModuleExportList(ctx, m, js_samplerate_funcs, countof(js_samplerate_funcs));
  }

  return 0;
}

VISIBLE void
js_init_module_samplerate(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "SampleRateConverter");
  JS_AddModuleExportList(ctx, m, js_samplerate_funcs, countof(js_samplerate_funcs));
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_samplerate_init)))
    js_init_module_samplerate(ctx, m);

  return m;
}
