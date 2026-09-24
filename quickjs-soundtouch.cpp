#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include <cmath>
#include "defines.h"
#include "quickjs-cpp.hpp"
#include <soundtouch/SoundTouch.h>

using namespace soundtouch;

static JSClassID js_soundtouch_class_id;
static JSValue soundtouch_proto, soundtouch_ctor;

/* tempo/pitch/rate have no native getter in SoundTouch's own C++ API
 * (only setters) - cached here at the last successful set, per
 * doc/soundtouch.md's "Properties" section. */
typedef struct {
  SoundTouch* st;
  double cachedTempo;
  double cachedPitch;
  double cachedRate;
  double cachedSampleRate;
} JSSoundTouch;

static JSValue
js_soundtouch_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JSSoundTouch* w = NULL;
  double sampleRate = 44100;
  int32_t channels = 2;

  if(argc > 0)
    JS_ToFloat64(ctx, &sampleRate, argv[0]);
  if(argc > 1)
    JS_ToInt32(ctx, &channels, argv[1]);

  if(!(w = (JSSoundTouch*)js_mallocz(ctx, sizeof(JSSoundTouch))))
    return JS_EXCEPTION;

  w->st = new SoundTouch();
  w->st->setSampleRate((uint)sampleRate);
  w->st->setChannels((uint)channels);
  w->cachedTempo = 1.0;
  w->cachedPitch = 1.0;
  w->cachedRate = 1.0;
  w->cachedSampleRate = sampleRate;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, soundtouch_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_soundtouch_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  return obj;

fail:
  if(w) {
    delete w->st;
    js_free(ctx, w);
  }
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_soundtouch_finalizer(JSRuntime* rt, JSValue val) {
  JSSoundTouch* w;

  if((w = (JSSoundTouch*)JS_GetOpaque(val, js_soundtouch_class_id))) {
    delete w->st;
    js_free_rt(rt, w);
  }
}

enum {
  PROP_TEMPO = 0,
  PROP_PITCH,
  PROP_PITCH_SEMITONES,
  PROP_PITCH_OCTAVES,
  PROP_RATE,
  PROP_CHANNELS,
  PROP_SAMPLERATE,
};

static JSValue
js_soundtouch_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSSoundTouch* w;

  if(!(w = (JSSoundTouch*)JS_GetOpaque2(ctx, this_val, js_soundtouch_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_TEMPO: return JS_NewFloat64(ctx, w->cachedTempo);
    case PROP_PITCH: return JS_NewFloat64(ctx, w->cachedPitch);
    case PROP_PITCH_SEMITONES: return JS_NewFloat64(ctx, 12.0 * log2(w->cachedPitch));
    case PROP_PITCH_OCTAVES: return JS_NewFloat64(ctx, log2(w->cachedPitch));
    case PROP_RATE: return JS_NewFloat64(ctx, w->cachedRate);
    case PROP_CHANNELS: return JS_NewUint32(ctx, w->st->numChannels());
    case PROP_SAMPLERATE: return JS_NewFloat64(ctx, w->cachedSampleRate);
  }

  return JS_UNDEFINED;
}

static JSValue
js_soundtouch_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JSSoundTouch* w;
  double d;

  if(!(w = (JSSoundTouch*)JS_GetOpaque2(ctx, this_val, js_soundtouch_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_TEMPO:
      if(!JS_ToFloat64(ctx, &d, value)) {
        w->st->setTempo(d);
        w->cachedTempo = d;
      }
      break;
    case PROP_PITCH:
      if(!JS_ToFloat64(ctx, &d, value)) {
        w->st->setPitch(d);
        w->cachedPitch = d;
      }
      break;
    case PROP_PITCH_SEMITONES:
      if(!JS_ToFloat64(ctx, &d, value)) {
        w->st->setPitchSemiTones(d);
        w->cachedPitch = pow(2.0, d / 12.0);
      }
      break;
    case PROP_PITCH_OCTAVES:
      if(!JS_ToFloat64(ctx, &d, value)) {
        w->st->setPitchOctaves(d);
        w->cachedPitch = pow(2.0, d);
      }
      break;
    case PROP_RATE:
      if(!JS_ToFloat64(ctx, &d, value)) {
        w->st->setRate(d);
        w->cachedRate = d;
      }
      break;
  }

  return JS_UNDEFINED;
}

enum {
  METHOD_PUTSAMPLES = 0,
  METHOD_RECEIVESAMPLES,
  METHOD_NUMSAMPLES,
  METHOD_NUMUNPROCESSEDSAMPLES,
  METHOD_FLUSH,
  METHOD_CLEAR,
  METHOD_SETSETTING,
  METHOD_GETSETTING,
  METHOD_CLOSE,
};

static JSValue
js_soundtouch_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSSoundTouch* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = (JSSoundTouch*)JS_GetOpaque2(ctx, this_val, js_soundtouch_class_id)))
    return JS_EXCEPTION;

  if(!w->st)
    return magic == METHOD_CLOSE ? JS_UNDEFINED : JS_ThrowTypeError(ctx, "SoundTouch is closed");

  switch(magic) {
    case METHOD_PUTSAMPLES: {
      qjsx::array_view<float> view;
      uint channels = w->st->numChannels();

      if(argc < 1 || !qjsx::get_array(ctx, argv[0], view))
        return JS_EXCEPTION;

      uint numSamples = (uint)(view.size / (channels ? channels : 1));
      w->st->putSamples(view.data, numSamples);
      break;
    }

    case METHOD_RECEIVESAMPLES: {
      qjsx::array_view<float> view;
      uint channels = w->st->numChannels();

      if(argc < 1 || !qjsx::get_array(ctx, argv[0], view))
        return JS_EXCEPTION;

      uint maxSamples = (uint)(view.size / (channels ? channels : 1));
      uint got = w->st->receiveSamples(view.data, maxSamples);
      ret = JS_NewUint32(ctx, got);
      break;
    }

    case METHOD_NUMSAMPLES: ret = JS_NewUint32(ctx, w->st->numSamples()); break;
    case METHOD_NUMUNPROCESSEDSAMPLES: ret = JS_NewUint32(ctx, w->st->numUnprocessedSamples()); break;
    case METHOD_FLUSH: w->st->flush(); break;
    case METHOD_CLEAR: w->st->clear(); break;

    case METHOD_SETSETTING: {
      int32_t id = 0, value = 0;

      if(argc > 0)
        JS_ToInt32(ctx, &id, argv[0]);
      if(argc > 1)
        JS_ToInt32(ctx, &value, argv[1]);

      ret = JS_NewBool(ctx, w->st->setSetting(id, value));
      break;
    }

    case METHOD_GETSETTING: {
      int32_t id = 0;

      if(argc > 0)
        JS_ToInt32(ctx, &id, argv[0]);

      ret = JS_NewInt32(ctx, w->st->getSetting(id));
      break;
    }

    case METHOD_CLOSE: {
      delete w->st;
      w->st = NULL;
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_soundtouch_funcs[] = {
    JS_CGETSET_MAGIC_DEF("tempo", js_soundtouch_get, js_soundtouch_set, PROP_TEMPO),
    JS_CGETSET_MAGIC_DEF("pitch", js_soundtouch_get, js_soundtouch_set, PROP_PITCH),
    JS_CGETSET_MAGIC_DEF("pitchSemitones", js_soundtouch_get, js_soundtouch_set, PROP_PITCH_SEMITONES),
    JS_CGETSET_MAGIC_DEF("pitchOctaves", js_soundtouch_get, js_soundtouch_set, PROP_PITCH_OCTAVES),
    JS_CGETSET_MAGIC_DEF("rate", js_soundtouch_get, js_soundtouch_set, PROP_RATE),
    JS_CGETSET_MAGIC_DEF("channels", js_soundtouch_get, 0, PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("sampleRate", js_soundtouch_get, 0, PROP_SAMPLERATE),
    JS_CFUNC_MAGIC_DEF("putSamples", 1, js_soundtouch_method, METHOD_PUTSAMPLES),
    JS_CFUNC_MAGIC_DEF("receiveSamples", 1, js_soundtouch_method, METHOD_RECEIVESAMPLES),
    JS_CFUNC_MAGIC_DEF("numSamples", 0, js_soundtouch_method, METHOD_NUMSAMPLES),
    JS_CFUNC_MAGIC_DEF("numUnprocessedSamples", 0, js_soundtouch_method, METHOD_NUMUNPROCESSEDSAMPLES),
    JS_CFUNC_MAGIC_DEF("flush", 0, js_soundtouch_method, METHOD_FLUSH),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_soundtouch_method, METHOD_CLEAR),
    JS_CFUNC_MAGIC_DEF("setSetting", 2, js_soundtouch_method, METHOD_SETSETTING),
    JS_CFUNC_MAGIC_DEF("getSetting", 1, js_soundtouch_method, METHOD_GETSETTING),
    JS_CFUNC_MAGIC_DEF("close", 0, js_soundtouch_method, METHOD_CLOSE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SoundTouch", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_soundtouch_module_funcs[] = {
    JS_PROP_INT32_DEF("SETTING_USE_AA_FILTER", SETTING_USE_AA_FILTER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_AA_FILTER_LENGTH", SETTING_AA_FILTER_LENGTH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_USE_QUICKSEEK", SETTING_USE_QUICKSEEK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_SEQUENCE_MS", SETTING_SEQUENCE_MS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_SEEKWINDOW_MS", SETTING_SEEKWINDOW_MS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_OVERLAP_MS", SETTING_OVERLAP_MS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_NOMINAL_INPUT_SEQUENCE", SETTING_NOMINAL_INPUT_SEQUENCE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_NOMINAL_OUTPUT_SEQUENCE", SETTING_NOMINAL_OUTPUT_SEQUENCE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SETTING_INITIAL_LATENCY", SETTING_INITIAL_LATENCY, JS_PROP_CONFIGURABLE),
};

static JSClassDef js_soundtouch_class = {
    .class_name = "SoundTouch",
    .finalizer = js_soundtouch_finalizer,
};

int
js_soundtouch_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_soundtouch_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_soundtouch_class_id, &js_soundtouch_class);

  soundtouch_ctor = JS_NewCFunction2(ctx, js_soundtouch_constructor, "SoundTouch", 2, JS_CFUNC_constructor, 0);
  soundtouch_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, soundtouch_proto, js_soundtouch_funcs, countof(js_soundtouch_funcs));
  JS_SetClassProto(ctx, js_soundtouch_class_id, soundtouch_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "SoundTouch", soundtouch_ctor);
    JS_SetModuleExportList(ctx, m, js_soundtouch_module_funcs, countof(js_soundtouch_module_funcs));
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_soundtouch(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "SoundTouch");
  JS_AddModuleExportList(ctx, m, js_soundtouch_module_funcs, countof(js_soundtouch_module_funcs));
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_soundtouch_init)))
    js_init_module_soundtouch(ctx, m);

  return m;
}
