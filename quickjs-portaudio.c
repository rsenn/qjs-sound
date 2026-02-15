#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include <portaudio.h>

static JSClassID js_pastream_class_id, js_audiodestinationnode_class_id, js_audiolistener_class_id,
    js_audiodevice_class_id;
static JSValue pastream_proto, pastream_ctor, audiodestinationnode_proto, audiodestinationnode_ctor,
    audiolistener_proto, audiolistener_ctor, audiodevice_proto, audiodevice_ctor;

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

int
js_portaudio_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_pastream_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pastream_class_id, &js_pastream_class);

  pastream_ctor = JS_NewCFunction2(ctx, js_pastream_constructor, "PaStream", 1, JS_CFUNC_constructor, 0);
  pastream_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, pastream_proto, js_pastream_funcs, countof(js_pastream_funcs));

  JS_SetClassProto(ctx, js_pastream_class_id, pastream_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "PaStream", pastream_ctor);
  }

  return 0;
}

VISIBLE void
js_init_module_portaudio(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "PaStream");
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_portaudio_init))) {
    js_init_module_portaudio(ctx, m);
  }

  return m;
}
