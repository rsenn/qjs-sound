#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include "defines.h"
#include "quickjs-typedarray.h"
#include <rubberband/rubberband-c.h>

/* Cap on channels for the stack-allocated array of channel pointers used
 * by study/process/retrieve - real streams stay far under this. */
#define RB_MAX_CHANNELS 64

static JSClassID js_rubberband_class_id;
static JSValue rubberband_proto, rubberband_ctor;

typedef struct {
  RubberBandState state;
  uint32_t channels;
} JSRubberBand;

/* Resolves a JS array of Float32Arrays (one per channel, per Rubber
 * Band's own planar - never interleaved - calling convention, see
 * doc/rubberband.md) into `ptrs[0..channels)`. `*out_frames` is set to
 * the shortest length (in samples) across all channel buffers, matching
 * how `samples`/capacity is derived elsewhere in this project (e.g.
 * SndFile.read()'s default-frames convention). */
static BOOL
js_rubberband_planar_get(JSContext* ctx, JSValueConst arr, uint32_t channels, float** ptrs, uint32_t* out_frames) {
  uint32_t i, min_frames = UINT32_MAX;

  if(!JS_IsArray(ctx, arr))
    return FALSE;

  for(i = 0; i < channels; i++) {
    JSValue v = JS_GetPropertyUint32(ctx, arr, i);
    JSBufferView view;
    BOOL ok = js_bufferview_get_kind(ctx, v, JS_TYPEDARRAY_FLOAT32, &view);
    JS_FreeValue(ctx, v);

    if(!ok)
      return FALSE;

    ptrs[i] = (float*)view.ptr;

    uint32_t frames = (uint32_t)(view.byte_length / sizeof(float));
    if(frames < min_frames)
      min_frames = frames;
  }

  if(out_frames)
    *out_frames = min_frames;

  return TRUE;
}

static JSValue
js_rubberband_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JSRubberBand* w = NULL;
  uint32_t sampleRate = 44100, channels = 2;
  int32_t options = RubberBandOptionProcessOffline;
  double initialTimeRatio = 1.0, initialPitchScale = 1.0;
  RubberBandState state;

  if(argc > 0)
    JS_ToUint32(ctx, &sampleRate, argv[0]);
  if(argc > 1)
    JS_ToUint32(ctx, &channels, argv[1]);
  if(argc > 2)
    JS_ToInt32(ctx, &options, argv[2]);
  if(argc > 3)
    JS_ToFloat64(ctx, &initialTimeRatio, argv[3]);
  if(argc > 4)
    JS_ToFloat64(ctx, &initialPitchScale, argv[4]);

  if(!(state = rubberband_new(sampleRate, channels, options, initialTimeRatio, initialPitchScale)))
    return JS_ThrowInternalError(ctx, "rubberband: failed to create stretcher");

  if(!(w = js_mallocz(ctx, sizeof(JSRubberBand)))) {
    rubberband_delete(state);
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
    proto = JS_DupValue(ctx, rubberband_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_rubberband_class_id);
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
js_rubberband_finalizer(JSRuntime* rt, JSValue val) {
  JSRubberBand* w;

  if((w = JS_GetOpaque(val, js_rubberband_class_id))) {
    if(w->state)
      rubberband_delete(w->state);
    js_free_rt(rt, w);
  }
}

enum {
  PROP_CHANNELS = 0,
  PROP_TIME_RATIO,
  PROP_PITCH_SCALE,
  PROP_FORMANT_SCALE,
  PROP_ENGINE_VERSION,
  PROP_PREFERRED_START_PAD,
  PROP_START_DELAY,
  PROP_LATENCY,
  PROP_SAMPLES_REQUIRED,
  PROP_PROCESS_SIZE_LIMIT,
};

static JSValue
js_rubberband_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSRubberBand* w;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_rubberband_class_id)))
    return JS_EXCEPTION;

  if(!w->state)
    return JS_ThrowTypeError(ctx, "RubberBandStretcher is closed");

  switch(magic) {
    case PROP_CHANNELS: return JS_NewUint32(ctx, rubberband_get_channel_count(w->state));
    case PROP_TIME_RATIO: return JS_NewFloat64(ctx, rubberband_get_time_ratio(w->state));
    case PROP_PITCH_SCALE: return JS_NewFloat64(ctx, rubberband_get_pitch_scale(w->state));
    case PROP_FORMANT_SCALE: return JS_NewFloat64(ctx, rubberband_get_formant_scale(w->state));
    case PROP_ENGINE_VERSION: return JS_NewInt32(ctx, rubberband_get_engine_version(w->state));
    case PROP_PREFERRED_START_PAD: return JS_NewUint32(ctx, rubberband_get_preferred_start_pad(w->state));
    case PROP_START_DELAY: return JS_NewUint32(ctx, rubberband_get_start_delay(w->state));
    case PROP_LATENCY: return JS_NewUint32(ctx, rubberband_get_latency(w->state));
    case PROP_SAMPLES_REQUIRED: return JS_NewUint32(ctx, rubberband_get_samples_required(w->state));
    case PROP_PROCESS_SIZE_LIMIT: return JS_NewUint32(ctx, rubberband_get_process_size_limit(w->state));
  }

  return JS_UNDEFINED;
}

static JSValue
js_rubberband_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JSRubberBand* w;
  double d;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_rubberband_class_id)))
    return JS_EXCEPTION;

  if(!w->state)
    return JS_ThrowTypeError(ctx, "RubberBandStretcher is closed");

  switch(magic) {
    case PROP_TIME_RATIO:
      if(!JS_ToFloat64(ctx, &d, value))
        rubberband_set_time_ratio(w->state, d);
      break;
    case PROP_PITCH_SCALE:
      if(!JS_ToFloat64(ctx, &d, value))
        rubberband_set_pitch_scale(w->state, d);
      break;
    case PROP_FORMANT_SCALE:
      if(!JS_ToFloat64(ctx, &d, value))
        rubberband_set_formant_scale(w->state, d);
      break;
  }

  return JS_UNDEFINED;
}

enum {
  METHOD_STUDY = 0,
  METHOD_PROCESS,
  METHOD_AVAILABLE,
  METHOD_RETRIEVE,
  METHOD_RESET,
  METHOD_CALCULATE_STRETCH,
  METHOD_SET_EXPECTED_INPUT_DURATION,
  METHOD_SET_MAX_PROCESS_SIZE,
  METHOD_SET_TRANSIENTS_OPTION,
  METHOD_SET_DETECTOR_OPTION,
  METHOD_SET_PHASE_OPTION,
  METHOD_SET_FORMANT_OPTION,
  METHOD_SET_PITCH_OPTION,
  METHOD_CLOSE,
};

static JSValue
js_rubberband_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSRubberBand* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_rubberband_class_id)))
    return JS_EXCEPTION;

  if(!w->state)
    return magic == METHOD_CLOSE ? JS_UNDEFINED : JS_ThrowTypeError(ctx, "RubberBandStretcher is closed");

  switch(magic) {
    case METHOD_STUDY:
    case METHOD_PROCESS: {
      float* ptrs[RB_MAX_CHANNELS];
      uint32_t frames;
      BOOL isFinal = FALSE;

      if(w->channels > RB_MAX_CHANNELS)
        return JS_ThrowRangeError(ctx, "too many channels (max %d)", RB_MAX_CHANNELS);

      if(argc < 1 || !js_rubberband_planar_get(ctx, argv[0], w->channels, ptrs, &frames))
        return JS_ThrowTypeError(ctx, "argument 1 must be an array of %u Float32Arrays", w->channels);

      if(argc > 1)
        isFinal = JS_ToBool(ctx, argv[1]);

      if(magic == METHOD_STUDY)
        rubberband_study(w->state, (const float* const*)ptrs, frames, isFinal);
      else
        rubberband_process(w->state, (const float* const*)ptrs, frames, isFinal);
      break;
    }

    case METHOD_AVAILABLE: ret = JS_NewInt32(ctx, rubberband_available(w->state)); break;

    case METHOD_RETRIEVE: {
      float* ptrs[RB_MAX_CHANNELS];
      uint32_t frames;
      unsigned int got;

      if(w->channels > RB_MAX_CHANNELS)
        return JS_ThrowRangeError(ctx, "too many channels (max %d)", RB_MAX_CHANNELS);

      if(argc < 1 || !js_rubberband_planar_get(ctx, argv[0], w->channels, ptrs, &frames))
        return JS_ThrowTypeError(ctx, "argument 1 must be an array of %u Float32Arrays", w->channels);

      got = rubberband_retrieve(w->state, (float* const*)ptrs, frames);
      ret = JS_NewUint32(ctx, got);
      break;
    }

    case METHOD_RESET: rubberband_reset(w->state); break;
    case METHOD_CALCULATE_STRETCH: rubberband_calculate_stretch(w->state); break;

    case METHOD_SET_EXPECTED_INPUT_DURATION: {
      uint32_t samples = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &samples, argv[0]);
      rubberband_set_expected_input_duration(w->state, samples);
      break;
    }

    case METHOD_SET_MAX_PROCESS_SIZE: {
      uint32_t samples = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &samples, argv[0]);
      rubberband_set_max_process_size(w->state, samples);
      break;
    }

    case METHOD_SET_TRANSIENTS_OPTION:
    case METHOD_SET_DETECTOR_OPTION:
    case METHOD_SET_PHASE_OPTION:
    case METHOD_SET_FORMANT_OPTION:
    case METHOD_SET_PITCH_OPTION: {
      int32_t options = 0;

      if(argc > 0)
        JS_ToInt32(ctx, &options, argv[0]);

      switch(magic) {
        case METHOD_SET_TRANSIENTS_OPTION: rubberband_set_transients_option(w->state, options); break;
        case METHOD_SET_DETECTOR_OPTION: rubberband_set_detector_option(w->state, options); break;
        case METHOD_SET_PHASE_OPTION: rubberband_set_phase_option(w->state, options); break;
        case METHOD_SET_FORMANT_OPTION: rubberband_set_formant_option(w->state, options); break;
        case METHOD_SET_PITCH_OPTION: rubberband_set_pitch_option(w->state, options); break;
      }
      break;
    }

    case METHOD_CLOSE: {
      rubberband_delete(w->state);
      w->state = NULL;
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_rubberband_funcs[] = {
    JS_CGETSET_MAGIC_DEF("channels", js_rubberband_get, 0, PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("timeRatio", js_rubberband_get, js_rubberband_set, PROP_TIME_RATIO),
    JS_CGETSET_MAGIC_DEF("pitchScale", js_rubberband_get, js_rubberband_set, PROP_PITCH_SCALE),
    JS_CGETSET_MAGIC_DEF("formantScale", js_rubberband_get, js_rubberband_set, PROP_FORMANT_SCALE),
    JS_CGETSET_MAGIC_DEF("engineVersion", js_rubberband_get, 0, PROP_ENGINE_VERSION),
    JS_CGETSET_MAGIC_DEF("preferredStartPad", js_rubberband_get, 0, PROP_PREFERRED_START_PAD),
    JS_CGETSET_MAGIC_DEF("startDelay", js_rubberband_get, 0, PROP_START_DELAY),
    JS_CGETSET_MAGIC_DEF("latency", js_rubberband_get, 0, PROP_LATENCY),
    JS_CGETSET_MAGIC_DEF("samplesRequired", js_rubberband_get, 0, PROP_SAMPLES_REQUIRED),
    JS_CGETSET_MAGIC_DEF("processSizeLimit", js_rubberband_get, 0, PROP_PROCESS_SIZE_LIMIT),
    JS_CFUNC_MAGIC_DEF("study", 1, js_rubberband_method, METHOD_STUDY),
    JS_CFUNC_MAGIC_DEF("process", 1, js_rubberband_method, METHOD_PROCESS),
    JS_CFUNC_MAGIC_DEF("available", 0, js_rubberband_method, METHOD_AVAILABLE),
    JS_CFUNC_MAGIC_DEF("retrieve", 1, js_rubberband_method, METHOD_RETRIEVE),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_rubberband_method, METHOD_RESET),
    JS_CFUNC_MAGIC_DEF("calculateStretch", 0, js_rubberband_method, METHOD_CALCULATE_STRETCH),
    JS_CFUNC_MAGIC_DEF("setExpectedInputDuration", 1, js_rubberband_method, METHOD_SET_EXPECTED_INPUT_DURATION),
    JS_CFUNC_MAGIC_DEF("setMaxProcessSize", 1, js_rubberband_method, METHOD_SET_MAX_PROCESS_SIZE),
    JS_CFUNC_MAGIC_DEF("setTransientsOption", 1, js_rubberband_method, METHOD_SET_TRANSIENTS_OPTION),
    JS_CFUNC_MAGIC_DEF("setDetectorOption", 1, js_rubberband_method, METHOD_SET_DETECTOR_OPTION),
    JS_CFUNC_MAGIC_DEF("setPhaseOption", 1, js_rubberband_method, METHOD_SET_PHASE_OPTION),
    JS_CFUNC_MAGIC_DEF("setFormantOption", 1, js_rubberband_method, METHOD_SET_FORMANT_OPTION),
    JS_CFUNC_MAGIC_DEF("setPitchOption", 1, js_rubberband_method, METHOD_SET_PITCH_OPTION),
    JS_CFUNC_MAGIC_DEF("close", 0, js_rubberband_method, METHOD_CLOSE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RubberBandStretcher", JS_PROP_CONFIGURABLE),
};

#define RB_CONST(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_CONFIGURABLE)

static const JSCFunctionListEntry js_rubberband_module_funcs[] = {
    RB_CONST(RubberBandOptionProcessOffline),
    RB_CONST(RubberBandOptionProcessRealTime),

    RB_CONST(RubberBandOptionTransientsCrisp),
    RB_CONST(RubberBandOptionTransientsMixed),
    RB_CONST(RubberBandOptionTransientsSmooth),

    RB_CONST(RubberBandOptionDetectorCompound),
    RB_CONST(RubberBandOptionDetectorPercussive),
    RB_CONST(RubberBandOptionDetectorSoft),

    RB_CONST(RubberBandOptionPhaseLaminar),
    RB_CONST(RubberBandOptionPhaseIndependent),

    RB_CONST(RubberBandOptionThreadingAuto),
    RB_CONST(RubberBandOptionThreadingNever),
    RB_CONST(RubberBandOptionThreadingAlways),

    RB_CONST(RubberBandOptionWindowStandard),
    RB_CONST(RubberBandOptionWindowShort),
    RB_CONST(RubberBandOptionWindowLong),

    RB_CONST(RubberBandOptionSmoothingOff),
    RB_CONST(RubberBandOptionSmoothingOn),

    RB_CONST(RubberBandOptionFormantShifted),
    RB_CONST(RubberBandOptionFormantPreserved),

    RB_CONST(RubberBandOptionPitchHighSpeed),
    RB_CONST(RubberBandOptionPitchHighQuality),
    RB_CONST(RubberBandOptionPitchHighConsistency),

    RB_CONST(RubberBandOptionChannelsApart),
    RB_CONST(RubberBandOptionChannelsTogether),

    RB_CONST(RubberBandOptionEngineFaster),
    RB_CONST(RubberBandOptionEngineFiner),
};

static JSClassDef js_rubberband_class = {
    .class_name = "RubberBandStretcher",
    .finalizer = js_rubberband_finalizer,
};

int
js_rubberband_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_rubberband_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_rubberband_class_id, &js_rubberband_class);

  rubberband_ctor = JS_NewCFunction2(ctx, js_rubberband_constructor, "RubberBandStretcher", 2, JS_CFUNC_constructor, 0);
  rubberband_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, rubberband_proto, js_rubberband_funcs, countof(js_rubberband_funcs));
  JS_SetClassProto(ctx, js_rubberband_class_id, rubberband_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "RubberBandStretcher", rubberband_ctor);
    JS_SetModuleExportList(ctx, m, js_rubberband_module_funcs, countof(js_rubberband_module_funcs));
  }

  return 0;
}

VISIBLE void
js_init_module_rubberband(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "RubberBandStretcher");
  JS_AddModuleExportList(ctx, m, js_rubberband_module_funcs, countof(js_rubberband_module_funcs));
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_rubberband_init)))
    js_init_module_rubberband(ctx, m);

  return m;
}
