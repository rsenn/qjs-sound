#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include "defines.h"
#include "quickjs-typedarray.h"
#include <sndfile.h>

static JSClassID js_sndfile_class_id;
static JSValue sndfile_proto, sndfile_ctor;

typedef struct {
  SNDFILE* file;
  SF_INFO info; /* snapshotted at open time; read/write don't change it */
} JSSndFile;

static JSValue
js_sndfile_throw(JSContext* ctx, SNDFILE* file) {
  return JS_ThrowInternalError(ctx, "libsndfile error: %s", sf_strerror(file));
}

static BOOL
js_sndfile_info_fromobj(JSContext* ctx, JSValueConst obj, SF_INFO* out) {
  JSValue v;

  if(!JS_IsObject(obj))
    return FALSE;

  memset(out, 0, sizeof(*out));

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "samplerate")))
    JS_ToInt32(ctx, &out->samplerate, v);
  JS_FreeValue(ctx, v);

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "channels")))
    JS_ToInt32(ctx, &out->channels, v);
  JS_FreeValue(ctx, v);

  if(!JS_IsUndefined(v = JS_GetPropertyStr(ctx, obj, "format")))
    JS_ToInt32(ctx, &out->format, v);
  JS_FreeValue(ctx, v);

  return TRUE;
}

static JSValue
js_sndfile_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JSSndFile* w = NULL;
  const char* path;
  int32_t mode = SFM_READ;
  SF_INFO info;
  SNDFILE* file;

  memset(&info, 0, sizeof(info));

  if(argc < 1 || !(path = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a path string");

  if(argc > 1)
    JS_ToInt32(ctx, &mode, argv[1]);

  if((mode & SFM_WRITE) && argc > 2)
    js_sndfile_info_fromobj(ctx, argv[2], &info);

  file = sf_open(path, mode, &info);
  JS_FreeCString(ctx, path);

  if(!file) {
    JS_ThrowInternalError(ctx, "libsndfile error: %s", sf_strerror(NULL));
    goto fail;
  }

  if(!(w = js_mallocz(ctx, sizeof(JSSndFile)))) {
    sf_close(file);
    goto fail;
  }

  w->file = file;
  w->info = info;

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, sndfile_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_sndfile_class_id);
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
js_sndfile_finalizer(JSRuntime* rt, JSValue val) {
  JSSndFile* w;

  if((w = JS_GetOpaque(val, js_sndfile_class_id))) {
    if(w->file)
      sf_close(w->file);
    js_free_rt(rt, w);
  }
}

enum {
  PROP_FRAMES = 0,
  PROP_SAMPLERATE,
  PROP_CHANNELS,
  PROP_FORMAT,
  PROP_SECTIONS,
  PROP_SEEKABLE,
};

static JSValue
js_sndfile_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSSndFile* w;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_sndfile_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_FRAMES: return JS_NewInt64(ctx, w->info.frames);
    case PROP_SAMPLERATE: return JS_NewInt32(ctx, w->info.samplerate);
    case PROP_CHANNELS: return JS_NewInt32(ctx, w->info.channels);
    case PROP_FORMAT: return JS_NewInt32(ctx, w->info.format);
    case PROP_SECTIONS: return JS_NewInt32(ctx, w->info.sections);
    case PROP_SEEKABLE: return JS_NewBool(ctx, w->info.seekable);
  }

  return JS_UNDEFINED;
}

/* Picks the sf_readf_ / sf_writef_ variant matching view's TypedArray
 * kind - Int16Array -> short, Int32Array -> int, Float32Array -> float,
 * Float64Array -> double, anything else is rejected. */
static sf_count_t
js_sndfile_readf(SNDFILE* file, const JSBufferView* view, sf_count_t frames) {
  switch(view->kind) {
    case JS_TYPEDARRAY_INT16: return sf_readf_short(file, (short*)view->ptr, frames);
    case JS_TYPEDARRAY_INT32: return sf_readf_int(file, (int*)view->ptr, frames);
    case JS_TYPEDARRAY_FLOAT32: return sf_readf_float(file, (float*)view->ptr, frames);
    case JS_TYPEDARRAY_FLOAT64: return sf_readf_double(file, (double*)view->ptr, frames);
    default: return -1;
  }
}

static sf_count_t
js_sndfile_writef(SNDFILE* file, const JSBufferView* view, sf_count_t frames) {
  switch(view->kind) {
    case JS_TYPEDARRAY_INT16: return sf_writef_short(file, (const short*)view->ptr, frames);
    case JS_TYPEDARRAY_INT32: return sf_writef_int(file, (const int*)view->ptr, frames);
    case JS_TYPEDARRAY_FLOAT32: return sf_writef_float(file, (const float*)view->ptr, frames);
    case JS_TYPEDARRAY_FLOAT64: return sf_writef_double(file, (const double*)view->ptr, frames);
    default: return -1;
  }
}

enum {
  METHOD_READ = 0,
  METHOD_WRITE,
  METHOD_SEEK,
  METHOD_CLOSE,
};

static JSValue
js_sndfile_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSSndFile* w;

  if(!(w = JS_GetOpaque2(ctx, this_val, js_sndfile_class_id)))
    return JS_EXCEPTION;

  if(!w->file)
    return magic == METHOD_CLOSE ? JS_UNDEFINED : JS_ThrowTypeError(ctx, "SndFile is closed");

  switch(magic) {
    case METHOD_READ:
    case METHOD_WRITE: {
      JSBufferView view;
      int32_t channels = w->info.channels > 0 ? w->info.channels : 1;
      sf_count_t frames;
      sf_count_t got;

      if(argc < 1 || !js_bufferview_get(ctx, argv[0], &view))
        return JS_EXCEPTION;

      if(view.kind == JS_TYPEDARRAY_NONE)
        return JS_ThrowTypeError(ctx, "argument 1 must be an Int16Array, Int32Array, Float32Array, or Float64Array");

      if(argc > 1)
        JS_ToInt64(ctx, &frames, argv[1]);
      else
        frames = view.byte_length / view.element_size / channels;

      got = magic == METHOD_READ ? js_sndfile_readf(w->file, &view, frames) : js_sndfile_writef(w->file, &view, frames);

      if(got < 0)
        return js_sndfile_throw(ctx, w->file);

      return JS_NewInt64(ctx, got);
    }

    case METHOD_SEEK: {
      int64_t frames = 0;
      int32_t whence = SEEK_SET;
      sf_count_t pos;

      if(argc > 0)
        JS_ToInt64(ctx, &frames, argv[0]);
      if(argc > 1)
        JS_ToInt32(ctx, &whence, argv[1]);

      if((pos = sf_seek(w->file, frames, whence)) < 0)
        return js_sndfile_throw(ctx, w->file);

      return JS_NewInt64(ctx, pos);
    }

    case METHOD_CLOSE: {
      sf_close(w->file);
      w->file = NULL;
      return JS_UNDEFINED;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_sndfile_static_info(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* path;
  SF_INFO info;
  SNDFILE* file;
  JSValue ret;

  memset(&info, 0, sizeof(info));

  if(argc < 1 || !(path = JS_ToCString(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a path string");

  file = sf_open(path, SFM_READ, &info);
  JS_FreeCString(ctx, path);

  if(!file)
    return JS_ThrowInternalError(ctx, "libsndfile error: %s", sf_strerror(NULL));

  ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "frames", JS_NewInt64(ctx, info.frames));
  JS_SetPropertyStr(ctx, ret, "samplerate", JS_NewInt32(ctx, info.samplerate));
  JS_SetPropertyStr(ctx, ret, "channels", JS_NewInt32(ctx, info.channels));
  JS_SetPropertyStr(ctx, ret, "format", JS_NewInt32(ctx, info.format));
  JS_SetPropertyStr(ctx, ret, "sections", JS_NewInt32(ctx, info.sections));
  JS_SetPropertyStr(ctx, ret, "seekable", JS_NewBool(ctx, info.seekable));

  sf_close(file);
  return ret;
}

static const JSCFunctionListEntry js_sndfile_funcs[] = {
    JS_CGETSET_MAGIC_DEF("frames", js_sndfile_get, 0, PROP_FRAMES),
    JS_CGETSET_MAGIC_DEF("samplerate", js_sndfile_get, 0, PROP_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("channels", js_sndfile_get, 0, PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("format", js_sndfile_get, 0, PROP_FORMAT),
    JS_CGETSET_MAGIC_DEF("sections", js_sndfile_get, 0, PROP_SECTIONS),
    JS_CGETSET_MAGIC_DEF("seekable", js_sndfile_get, 0, PROP_SEEKABLE),
    JS_CFUNC_MAGIC_DEF("read", 1, js_sndfile_method, METHOD_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_sndfile_method, METHOD_WRITE),
    JS_CFUNC_MAGIC_DEF("seek", 2, js_sndfile_method, METHOD_SEEK),
    JS_CFUNC_MAGIC_DEF("close", 0, js_sndfile_method, METHOD_CLOSE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SndFile", JS_PROP_CONFIGURABLE),
};

#define SF_CONST(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_CONFIGURABLE)

static const JSCFunctionListEntry js_sndfile_module_funcs[] = {
    SF_CONST(SF_FORMAT_WAV),
    SF_CONST(SF_FORMAT_AIFF),
    SF_CONST(SF_FORMAT_AU),
    SF_CONST(SF_FORMAT_RAW),
    SF_CONST(SF_FORMAT_PAF),
    SF_CONST(SF_FORMAT_SVX),
    SF_CONST(SF_FORMAT_NIST),
    SF_CONST(SF_FORMAT_VOC),
    SF_CONST(SF_FORMAT_IRCAM),
    SF_CONST(SF_FORMAT_W64),
    SF_CONST(SF_FORMAT_MAT4),
    SF_CONST(SF_FORMAT_MAT5),
    SF_CONST(SF_FORMAT_PVF),
    SF_CONST(SF_FORMAT_XI),
    SF_CONST(SF_FORMAT_HTK),
    SF_CONST(SF_FORMAT_SDS),
    SF_CONST(SF_FORMAT_AVR),
    SF_CONST(SF_FORMAT_WAVEX),
    SF_CONST(SF_FORMAT_SD2),
    SF_CONST(SF_FORMAT_FLAC),
    SF_CONST(SF_FORMAT_CAF),
    SF_CONST(SF_FORMAT_WVE),
    SF_CONST(SF_FORMAT_OGG),
    SF_CONST(SF_FORMAT_MPC2K),
    SF_CONST(SF_FORMAT_RF64),
    SF_CONST(SF_FORMAT_MPEG),

    SF_CONST(SF_FORMAT_PCM_S8),
    SF_CONST(SF_FORMAT_PCM_16),
    SF_CONST(SF_FORMAT_PCM_24),
    SF_CONST(SF_FORMAT_PCM_32),
    SF_CONST(SF_FORMAT_PCM_U8),
    SF_CONST(SF_FORMAT_FLOAT),
    SF_CONST(SF_FORMAT_DOUBLE),
    SF_CONST(SF_FORMAT_ULAW),
    SF_CONST(SF_FORMAT_ALAW),
    SF_CONST(SF_FORMAT_IMA_ADPCM),
    SF_CONST(SF_FORMAT_MS_ADPCM),
    SF_CONST(SF_FORMAT_GSM610),
    SF_CONST(SF_FORMAT_VOX_ADPCM),
    SF_CONST(SF_FORMAT_NMS_ADPCM_16),
    SF_CONST(SF_FORMAT_NMS_ADPCM_24),
    SF_CONST(SF_FORMAT_NMS_ADPCM_32),
    SF_CONST(SF_FORMAT_G721_32),
    SF_CONST(SF_FORMAT_G723_24),
    SF_CONST(SF_FORMAT_G723_40),
    SF_CONST(SF_FORMAT_DWVW_12),
    SF_CONST(SF_FORMAT_DWVW_16),
    SF_CONST(SF_FORMAT_DWVW_24),
    SF_CONST(SF_FORMAT_DWVW_N),
    SF_CONST(SF_FORMAT_DPCM_8),
    SF_CONST(SF_FORMAT_DPCM_16),
    SF_CONST(SF_FORMAT_VORBIS),
    SF_CONST(SF_FORMAT_OPUS),
    SF_CONST(SF_FORMAT_ALAC_16),
    SF_CONST(SF_FORMAT_ALAC_20),
    SF_CONST(SF_FORMAT_ALAC_24),
    SF_CONST(SF_FORMAT_ALAC_32),
    SF_CONST(SF_FORMAT_MPEG_LAYER_I),
    SF_CONST(SF_FORMAT_MPEG_LAYER_II),
    SF_CONST(SF_FORMAT_MPEG_LAYER_III),

    SF_CONST(SF_FORMAT_TYPEMASK),
    SF_CONST(SF_FORMAT_SUBMASK),
    SF_CONST(SF_FORMAT_ENDMASK),

    SF_CONST(SF_ENDIAN_FILE),
    SF_CONST(SF_ENDIAN_LITTLE),
    SF_CONST(SF_ENDIAN_BIG),
    SF_CONST(SF_ENDIAN_CPU),

    SF_CONST(SFM_READ),
    SF_CONST(SFM_WRITE),
    SF_CONST(SFM_RDWR),

    JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SEEK_END", SEEK_END, JS_PROP_CONFIGURABLE),

    SF_CONST(SF_ERR_NO_ERROR),
    SF_CONST(SF_ERR_UNRECOGNISED_FORMAT),
    SF_CONST(SF_ERR_SYSTEM),
    SF_CONST(SF_ERR_MALFORMED_FILE),
    SF_CONST(SF_ERR_UNSUPPORTED_ENCODING),
};

static JSClassDef js_sndfile_class = {
    .class_name = "SndFile",
    .finalizer = js_sndfile_finalizer,
};

int
js_sndfile_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_sndfile_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sndfile_class_id, &js_sndfile_class);

  sndfile_ctor = JS_NewCFunction2(ctx, js_sndfile_constructor, "SndFile", 2, JS_CFUNC_constructor, 0);
  sndfile_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, sndfile_proto, js_sndfile_funcs, countof(js_sndfile_funcs));
  JS_SetClassProto(ctx, js_sndfile_class_id, sndfile_proto);

  JS_SetPropertyStr(ctx, sndfile_ctor, "info", JS_NewCFunction(ctx, js_sndfile_static_info, "info", 1));

  if(m) {
    JS_SetModuleExport(ctx, m, "SndFile", sndfile_ctor);
    JS_SetModuleExportList(ctx, m, js_sndfile_module_funcs, countof(js_sndfile_module_funcs));
  }

  return 0;
}

VISIBLE void
js_init_module_sndfile(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "SndFile");
  JS_AddModuleExportList(ctx, m, js_sndfile_module_funcs, countof(js_sndfile_module_funcs));
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_sndfile_init)))
    js_init_module_sndfile(ctx, m);

  return m;
}
