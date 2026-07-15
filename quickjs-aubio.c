#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include "defines.h"
#include <aubio.h>

/* ---------- shared helpers ---------- */

/* aubio's smpl_t is float unless HAVE_AUBIO_DOUBLE is set, which this build
 * does not set, so Float32Array is the matching JS-side type. */

static int
js_aubio_get_fvec_ptr(JSContext* ctx, JSValueConst val, float** pdata, uint32_t* plen) {
  size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);

  if(JS_IsException(buf))
    return -1;

  if(bytes_per_element != sizeof(float)) {
    JS_FreeValue(ctx, buf);
    JS_ThrowTypeError(ctx, "expected a Float32Array");
    return -1;
  }

  size_t ab_size = 0;
  uint8_t* ab_data = JS_GetArrayBuffer(ctx, &ab_size, buf);
  JS_FreeValue(ctx, buf);

  if(!ab_data)
    return -1;

  *pdata = (float*)(ab_data + byte_offset);
  *plen = (uint32_t)(byte_length / sizeof(float));
  return 0;
}

/* quickjs.h has no JS_NewFloat32Array-style helper; wrap a copied
 * ArrayBuffer with the global constructor instead. */
static JSValue
js_aubio_make_float32array(JSContext* ctx, const float* data, size_t count) {
  JSValue ab = JS_NewArrayBufferCopy(ctx, (const uint8_t*)data, count * sizeof(float));

  if(JS_IsException(ab))
    return ab;

  JSValue global = JS_GetGlobalObject(ctx);
  JSValue ctor = JS_GetPropertyStr(ctx, global, "Float32Array");
  JSValue args[1] = {ab};
  JSValue ta = JS_CallConstructor(ctx, ctor, 1, args);

  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, global);
  JS_FreeValue(ctx, ab);
  return ta;
}

static char_t*
js_aubio_get_method(JSContext* ctx, int argc, JSValueConst argv[], char_t* buf, size_t buflen) {
  if(argc > 0 && !JS_IsUndefined(argv[0])) {
    const char* str = JS_ToCString(ctx, argv[0]);

    if(str) {
      strncpy(buf, str, buflen - 1);
      buf[buflen - 1] = '\0';
      JS_FreeCString(ctx, str);
      return buf;
    }
  }

  strncpy(buf, "default", buflen - 1);
  buf[buflen - 1] = '\0';
  return buf;
}

/* ---------- AubioNotes ---------- */

typedef struct {
  aubio_notes_t* obj;
  uint_t hop_size;
} JSAubioNotes;

static JSClassID js_aubionotes_class_id;
static JSValue aubionotes_proto, aubionotes_ctor;

static JSValue
js_aubionotes_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  char_t method[64];
  uint32_t buf_size = 1024, hop_size = 256, samplerate = 44100;
  JSAubioNotes* w = 0;

  js_aubio_get_method(ctx, argc, argv, method, sizeof(method));

  if(argc > 1)
    JS_ToUint32(ctx, &buf_size, argv[1]);
  if(argc > 2)
    JS_ToUint32(ctx, &hop_size, argv[2]);
  if(argc > 3)
    JS_ToUint32(ctx, &samplerate, argv[3]);

  aubio_notes_t* notes = new_aubio_notes(method, buf_size, hop_size, samplerate);

  if(!notes) {
    JS_ThrowInternalError(ctx, "aubio: failed to create notes detection object");
    goto fail;
  }

  if(!(w = js_mallocz(ctx, sizeof(JSAubioNotes)))) {
    del_aubio_notes(notes);
    goto fail;
  }

  w->obj = notes;
  w->hop_size = hop_size;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = aubionotes_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_aubionotes_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  return obj;

fail:
  if(w) {
    del_aubio_notes(w->obj);
    js_free(ctx, w);
  }
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_NOTES_SILENCE = 0,
  PROP_NOTES_MINIOI_MS,
  PROP_NOTES_RELEASE_DROP,
};

static JSValue
js_aubionotes_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSAubioNotes* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubionotes_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_NOTES_SILENCE: ret = JS_NewFloat64(ctx, aubio_notes_get_silence(w->obj)); break;
    case PROP_NOTES_MINIOI_MS: ret = JS_NewFloat64(ctx, aubio_notes_get_minioi_ms(w->obj)); break;
    case PROP_NOTES_RELEASE_DROP: ret = JS_NewFloat64(ctx, aubio_notes_get_release_drop(w->obj)); break;
  }

  return ret;
}

static JSValue
js_aubionotes_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JSAubioNotes* w;
  double d;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubionotes_class_id)))
    return JS_EXCEPTION;

  if(JS_ToFloat64(ctx, &d, value))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_NOTES_SILENCE: aubio_notes_set_silence(w->obj, (smpl_t)d); break;
    case PROP_NOTES_MINIOI_MS: aubio_notes_set_minioi_ms(w->obj, (smpl_t)d); break;
    case PROP_NOTES_RELEASE_DROP: aubio_notes_set_release_drop(w->obj, (smpl_t)d); break;
  }

  return JS_UNDEFINED;
}

enum {
  METHOD_NOTES_PROCESS = 0,
};

static JSValue
js_aubionotes_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSAubioNotes* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubionotes_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_NOTES_PROCESS: {
      float* data;
      uint32_t len;

      if(argc < 1 || js_aubio_get_fvec_ptr(ctx, argv[0], &data, &len))
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");

      if(len != w->hop_size)
        return JS_ThrowRangeError(ctx, "input length (%u) must equal hop_size (%u)", len, w->hop_size);

      fvec_t input = {len, data};
      float out_data[3] = {0, 0, 0};
      fvec_t output = {3, out_data};

      aubio_notes_do(w->obj, &input, &output);

      ret = js_aubio_make_float32array(ctx, out_data, 3);
      break;
    }
  }

  return ret;
}

static void
js_aubionotes_finalizer(JSRuntime* rt, JSValue val) {
  JSAubioNotes* w;

  if((w = JS_GetOpaque(val, js_aubionotes_class_id))) {
    del_aubio_notes(w->obj);
    js_free_rt(rt, w);
  }
}

static JSClassDef js_aubionotes_class = {
    .class_name = "AubioNotes",
    .finalizer = js_aubionotes_finalizer,
};

static const JSCFunctionListEntry js_aubionotes_funcs[] = {
    JS_CGETSET_MAGIC_DEF("silence", js_aubionotes_get, js_aubionotes_set, PROP_NOTES_SILENCE),
    JS_CGETSET_MAGIC_DEF("minioiMs", js_aubionotes_get, js_aubionotes_set, PROP_NOTES_MINIOI_MS),
    JS_CGETSET_MAGIC_DEF("releaseDrop", js_aubionotes_get, js_aubionotes_set, PROP_NOTES_RELEASE_DROP),
    JS_CFUNC_MAGIC_DEF("process", 1, js_aubionotes_method, METHOD_NOTES_PROCESS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AubioNotes", JS_PROP_CONFIGURABLE),
};

/* ---------- AubioOnset ---------- */

typedef struct {
  aubio_onset_t* obj;
  uint_t hop_size;
} JSAubioOnset;

static JSClassID js_aubioonset_class_id;
static JSValue aubioonset_proto, aubioonset_ctor;

static JSValue
js_aubioonset_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  char_t method[64];
  uint32_t buf_size = 1024, hop_size = 256, samplerate = 44100;
  JSAubioOnset* w = 0;

  js_aubio_get_method(ctx, argc, argv, method, sizeof(method));

  if(argc > 1)
    JS_ToUint32(ctx, &buf_size, argv[1]);
  if(argc > 2)
    JS_ToUint32(ctx, &hop_size, argv[2]);
  if(argc > 3)
    JS_ToUint32(ctx, &samplerate, argv[3]);

  aubio_onset_t* onset = new_aubio_onset(method, buf_size, hop_size, samplerate);

  if(!onset) {
    JS_ThrowInternalError(ctx, "aubio: failed to create onset detection object");
    goto fail;
  }

  if(!(w = js_mallocz(ctx, sizeof(JSAubioOnset)))) {
    del_aubio_onset(onset);
    goto fail;
  }

  w->obj = onset;
  w->hop_size = hop_size;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = aubioonset_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_aubioonset_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  return obj;

fail:
  if(w) {
    del_aubio_onset(w->obj);
    js_free(ctx, w);
  }
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_ONSET_LAST = 0,
  PROP_ONSET_LAST_S,
  PROP_ONSET_LAST_MS,
  PROP_ONSET_AWHITENING,
  PROP_ONSET_COMPRESSION,
  PROP_ONSET_SILENCE,
  PROP_ONSET_DESCRIPTOR,
  PROP_ONSET_THRESHOLDED_DESCRIPTOR,
  PROP_ONSET_THRESHOLD,
  PROP_ONSET_MINIOI,
  PROP_ONSET_MINIOI_S,
  PROP_ONSET_MINIOI_MS,
  PROP_ONSET_DELAY,
  PROP_ONSET_DELAY_S,
  PROP_ONSET_DELAY_MS,
};

static JSValue
js_aubioonset_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSAubioOnset* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubioonset_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ONSET_LAST: ret = JS_NewUint32(ctx, aubio_onset_get_last(w->obj)); break;
    case PROP_ONSET_LAST_S: ret = JS_NewFloat64(ctx, aubio_onset_get_last_s(w->obj)); break;
    case PROP_ONSET_LAST_MS: ret = JS_NewFloat64(ctx, aubio_onset_get_last_ms(w->obj)); break;
    case PROP_ONSET_AWHITENING: ret = JS_NewFloat64(ctx, aubio_onset_get_awhitening(w->obj)); break;
    case PROP_ONSET_COMPRESSION: ret = JS_NewFloat64(ctx, aubio_onset_get_compression(w->obj)); break;
    case PROP_ONSET_SILENCE: ret = JS_NewFloat64(ctx, aubio_onset_get_silence(w->obj)); break;
    case PROP_ONSET_DESCRIPTOR: ret = JS_NewFloat64(ctx, aubio_onset_get_descriptor(w->obj)); break;
    case PROP_ONSET_THRESHOLDED_DESCRIPTOR: ret = JS_NewFloat64(ctx, aubio_onset_get_thresholded_descriptor(w->obj)); break;
    case PROP_ONSET_THRESHOLD: ret = JS_NewFloat64(ctx, aubio_onset_get_threshold(w->obj)); break;
    case PROP_ONSET_MINIOI: ret = JS_NewUint32(ctx, aubio_onset_get_minioi(w->obj)); break;
    case PROP_ONSET_MINIOI_S: ret = JS_NewFloat64(ctx, aubio_onset_get_minioi_s(w->obj)); break;
    case PROP_ONSET_MINIOI_MS: ret = JS_NewFloat64(ctx, aubio_onset_get_minioi_ms(w->obj)); break;
    case PROP_ONSET_DELAY: ret = JS_NewUint32(ctx, aubio_onset_get_delay(w->obj)); break;
    case PROP_ONSET_DELAY_S: ret = JS_NewFloat64(ctx, aubio_onset_get_delay_s(w->obj)); break;
    case PROP_ONSET_DELAY_MS: ret = JS_NewFloat64(ctx, aubio_onset_get_delay_ms(w->obj)); break;
  }

  return ret;
}

static JSValue
js_aubioonset_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JSAubioOnset* w;
  double d;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubioonset_class_id)))
    return JS_EXCEPTION;

  if(JS_ToFloat64(ctx, &d, value))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ONSET_AWHITENING: aubio_onset_set_awhitening(w->obj, (uint_t)d); break;
    case PROP_ONSET_COMPRESSION: aubio_onset_set_compression(w->obj, (smpl_t)d); break;
    case PROP_ONSET_SILENCE: aubio_onset_set_silence(w->obj, (smpl_t)d); break;
    case PROP_ONSET_THRESHOLD: aubio_onset_set_threshold(w->obj, (smpl_t)d); break;
    case PROP_ONSET_MINIOI: aubio_onset_set_minioi(w->obj, (uint_t)d); break;
    case PROP_ONSET_MINIOI_S: aubio_onset_set_minioi_s(w->obj, (smpl_t)d); break;
    case PROP_ONSET_MINIOI_MS: aubio_onset_set_minioi_ms(w->obj, (smpl_t)d); break;
    case PROP_ONSET_DELAY: aubio_onset_set_delay(w->obj, (uint_t)d); break;
    case PROP_ONSET_DELAY_S: aubio_onset_set_delay_s(w->obj, (smpl_t)d); break;
    case PROP_ONSET_DELAY_MS: aubio_onset_set_delay_ms(w->obj, (smpl_t)d); break;
  }

  return JS_UNDEFINED;
}

enum {
  METHOD_ONSET_PROCESS = 0,
  METHOD_ONSET_SET_DEFAULT_PARAMETERS,
  METHOD_ONSET_RESET,
};

static JSValue
js_aubioonset_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSAubioOnset* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubioonset_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_ONSET_PROCESS: {
      float* data;
      uint32_t len;

      if(argc < 1 || js_aubio_get_fvec_ptr(ctx, argv[0], &data, &len))
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");

      if(len != w->hop_size)
        return JS_ThrowRangeError(ctx, "input length (%u) must equal hop_size (%u)", len, w->hop_size);

      fvec_t input = {len, data};
      float out_data[1] = {0};
      fvec_t output = {1, out_data};

      aubio_onset_do(w->obj, &input, &output);

      ret = js_aubio_make_float32array(ctx, out_data, 1);
      break;
    }

    case METHOD_ONSET_SET_DEFAULT_PARAMETERS: {
      const char* mode = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;

      ret = JS_NewUint32(ctx, aubio_onset_set_default_parameters(w->obj, mode ? mode : "default"));

      if(mode)
        JS_FreeCString(ctx, mode);
      break;
    }

    case METHOD_ONSET_RESET: {
      aubio_onset_reset(w->obj);
      break;
    }
  }

  return ret;
}

static void
js_aubioonset_finalizer(JSRuntime* rt, JSValue val) {
  JSAubioOnset* w;

  if((w = JS_GetOpaque(val, js_aubioonset_class_id))) {
    del_aubio_onset(w->obj);
    js_free_rt(rt, w);
  }
}

static JSClassDef js_aubioonset_class = {
    .class_name = "AubioOnset",
    .finalizer = js_aubioonset_finalizer,
};

static const JSCFunctionListEntry js_aubioonset_funcs[] = {
    JS_CGETSET_MAGIC_DEF("last", js_aubioonset_get, 0, PROP_ONSET_LAST),
    JS_CGETSET_MAGIC_DEF("lastS", js_aubioonset_get, 0, PROP_ONSET_LAST_S),
    JS_CGETSET_MAGIC_DEF("lastMs", js_aubioonset_get, 0, PROP_ONSET_LAST_MS),
    JS_CGETSET_MAGIC_DEF("awhitening", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_AWHITENING),
    JS_CGETSET_MAGIC_DEF("compression", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_COMPRESSION),
    JS_CGETSET_MAGIC_DEF("silence", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_SILENCE),
    JS_CGETSET_MAGIC_DEF("descriptor", js_aubioonset_get, 0, PROP_ONSET_DESCRIPTOR),
    JS_CGETSET_MAGIC_DEF("thresholdedDescriptor", js_aubioonset_get, 0, PROP_ONSET_THRESHOLDED_DESCRIPTOR),
    JS_CGETSET_MAGIC_DEF("threshold", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_THRESHOLD),
    JS_CGETSET_MAGIC_DEF("minioi", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_MINIOI),
    JS_CGETSET_MAGIC_DEF("minioiS", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_MINIOI_S),
    JS_CGETSET_MAGIC_DEF("minioiMs", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_MINIOI_MS),
    JS_CGETSET_MAGIC_DEF("delay", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_DELAY),
    JS_CGETSET_MAGIC_DEF("delayS", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_DELAY_S),
    JS_CGETSET_MAGIC_DEF("delayMs", js_aubioonset_get, js_aubioonset_set, PROP_ONSET_DELAY_MS),
    JS_CFUNC_MAGIC_DEF("process", 1, js_aubioonset_method, METHOD_ONSET_PROCESS),
    JS_CFUNC_MAGIC_DEF("setDefaultParameters", 1, js_aubioonset_method, METHOD_ONSET_SET_DEFAULT_PARAMETERS),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_aubioonset_method, METHOD_ONSET_RESET),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AubioOnset", JS_PROP_CONFIGURABLE),
};

/* ---------- AubioPitch ---------- */

typedef struct {
  aubio_pitch_t* obj;
  uint_t hop_size;
} JSAubioPitch;

static JSClassID js_aubiopitch_class_id;
static JSValue aubiopitch_proto, aubiopitch_ctor;

static JSValue
js_aubiopitch_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  char_t method[64];
  uint32_t buf_size = 1024, hop_size = 256, samplerate = 44100;
  JSAubioPitch* w = 0;

  js_aubio_get_method(ctx, argc, argv, method, sizeof(method));

  if(argc > 1)
    JS_ToUint32(ctx, &buf_size, argv[1]);
  if(argc > 2)
    JS_ToUint32(ctx, &hop_size, argv[2]);
  if(argc > 3)
    JS_ToUint32(ctx, &samplerate, argv[3]);

  aubio_pitch_t* pitch = new_aubio_pitch(method, buf_size, hop_size, samplerate);

  if(!pitch) {
    JS_ThrowInternalError(ctx, "aubio: failed to create pitch detection object");
    goto fail;
  }

  if(!(w = js_mallocz(ctx, sizeof(JSAubioPitch)))) {
    del_aubio_pitch(pitch);
    goto fail;
  }

  w->obj = pitch;
  w->hop_size = hop_size;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = aubiopitch_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_aubiopitch_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  return obj;

fail:
  if(w) {
    del_aubio_pitch(w->obj);
    js_free(ctx, w);
  }
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_PITCH_TOLERANCE = 0,
  PROP_PITCH_SILENCE,
  PROP_PITCH_CONFIDENCE,
};

static JSValue
js_aubiopitch_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSAubioPitch* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubiopitch_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PITCH_TOLERANCE: ret = JS_NewFloat64(ctx, aubio_pitch_get_tolerance(w->obj)); break;
    case PROP_PITCH_SILENCE: ret = JS_NewFloat64(ctx, aubio_pitch_get_silence(w->obj)); break;
    case PROP_PITCH_CONFIDENCE: ret = JS_NewFloat64(ctx, aubio_pitch_get_confidence(w->obj)); break;
  }

  return ret;
}

static JSValue
js_aubiopitch_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JSAubioPitch* w;
  double d;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubiopitch_class_id)))
    return JS_EXCEPTION;

  if(JS_ToFloat64(ctx, &d, value))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PITCH_TOLERANCE: aubio_pitch_set_tolerance(w->obj, (smpl_t)d); break;
    case PROP_PITCH_SILENCE: aubio_pitch_set_silence(w->obj, (smpl_t)d); break;
  }

  return JS_UNDEFINED;
}

enum {
  METHOD_PITCH_PROCESS = 0,
  METHOD_PITCH_SET_UNIT,
};

static JSValue
js_aubiopitch_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSAubioPitch* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_aubiopitch_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_PITCH_PROCESS: {
      float* data;
      uint32_t len;

      if(argc < 1 || js_aubio_get_fvec_ptr(ctx, argv[0], &data, &len))
        return JS_ThrowTypeError(ctx, "argument 1 must be a Float32Array");

      if(len != w->hop_size)
        return JS_ThrowRangeError(ctx, "input length (%u) must equal hop_size (%u)", len, w->hop_size);

      fvec_t input = {len, data};
      float out_data[1] = {0};
      fvec_t output = {1, out_data};

      aubio_pitch_do(w->obj, &input, &output);

      ret = js_aubio_make_float32array(ctx, out_data, 1);
      break;
    }

    case METHOD_PITCH_SET_UNIT: {
      const char* mode = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;

      if(!mode)
        return JS_ThrowTypeError(ctx, "argument 1 must be a string");

      ret = JS_NewUint32(ctx, aubio_pitch_set_unit(w->obj, mode));
      JS_FreeCString(ctx, mode);
      break;
    }
  }

  return ret;
}

static void
js_aubiopitch_finalizer(JSRuntime* rt, JSValue val) {
  JSAubioPitch* w;

  if((w = JS_GetOpaque(val, js_aubiopitch_class_id))) {
    del_aubio_pitch(w->obj);
    js_free_rt(rt, w);
  }
}

static JSClassDef js_aubiopitch_class = {
    .class_name = "AubioPitch",
    .finalizer = js_aubiopitch_finalizer,
};

static const JSCFunctionListEntry js_aubiopitch_funcs[] = {
    JS_CGETSET_MAGIC_DEF("tolerance", js_aubiopitch_get, js_aubiopitch_set, PROP_PITCH_TOLERANCE),
    JS_CGETSET_MAGIC_DEF("silence", js_aubiopitch_get, js_aubiopitch_set, PROP_PITCH_SILENCE),
    JS_CGETSET_MAGIC_DEF("confidence", js_aubiopitch_get, 0, PROP_PITCH_CONFIDENCE),
    JS_CFUNC_MAGIC_DEF("process", 1, js_aubiopitch_method, METHOD_PITCH_PROCESS),
    JS_CFUNC_MAGIC_DEF("setUnit", 1, js_aubiopitch_method, METHOD_PITCH_SET_UNIT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AubioPitch", JS_PROP_CONFIGURABLE),
};

/* ---------- module init ---------- */

int
js_aubio_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_aubionotes_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_aubionotes_class_id, &js_aubionotes_class);

  aubionotes_ctor = JS_NewCFunction2(ctx, js_aubionotes_constructor, "AubioNotes", 1, JS_CFUNC_constructor, 0);
  aubionotes_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, aubionotes_proto, js_aubionotes_funcs, countof(js_aubionotes_funcs));
  JS_SetClassProto(ctx, js_aubionotes_class_id, aubionotes_proto);
  JS_SetConstructor(ctx, aubionotes_ctor, aubionotes_proto);

  JS_NewClassID(&js_aubioonset_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_aubioonset_class_id, &js_aubioonset_class);

  aubioonset_ctor = JS_NewCFunction2(ctx, js_aubioonset_constructor, "AubioOnset", 1, JS_CFUNC_constructor, 0);
  aubioonset_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, aubioonset_proto, js_aubioonset_funcs, countof(js_aubioonset_funcs));
  JS_SetClassProto(ctx, js_aubioonset_class_id, aubioonset_proto);
  JS_SetConstructor(ctx, aubioonset_ctor, aubioonset_proto);

  JS_NewClassID(&js_aubiopitch_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_aubiopitch_class_id, &js_aubiopitch_class);

  aubiopitch_ctor = JS_NewCFunction2(ctx, js_aubiopitch_constructor, "AubioPitch", 1, JS_CFUNC_constructor, 0);
  aubiopitch_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, aubiopitch_proto, js_aubiopitch_funcs, countof(js_aubiopitch_funcs));
  JS_SetClassProto(ctx, js_aubiopitch_class_id, aubiopitch_proto);
  JS_SetConstructor(ctx, aubiopitch_ctor, aubiopitch_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "AubioNotes", aubionotes_ctor);
    JS_SetModuleExport(ctx, m, "AubioOnset", aubioonset_ctor);
    JS_SetModuleExport(ctx, m, "AubioPitch", aubiopitch_ctor);
  }

  return 0;
}

VISIBLE void
js_init_module_aubio(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "AubioNotes");
  JS_AddModuleExport(ctx, m, "AubioOnset");
  JS_AddModuleExport(ctx, m, "AubioPitch");
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_aubio_init))) {
    js_init_module_aubio(ctx, m);
  }

  return m;
}
