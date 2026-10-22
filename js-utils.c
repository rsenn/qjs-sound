#include "js-utils.h"

JSValue
js_global_get_str(JSContext* ctx, const char* prop) {
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue ret = JS_GetPropertyStr(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSAtom
js_symbol_atom(JSContext* ctx, const char* name) {
  JSValue ctor = js_global_get_str(ctx, "Symbol");
  JSValue sym = JS_GetPropertyStr(ctx, ctor, name);
  JS_FreeValue(ctx, ctor);
  JSAtom ret = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return ret;
}

JSAtom
js_symbol_tostringtag(JSContext* ctx) {
  return js_symbol_atom(ctx, "toStringTag");
}

JSAtom
js_symbol_iterator(JSContext* ctx) {
  return js_symbol_atom(ctx, "iterator");
}

void
js_set_tostringtag(JSContext* ctx, JSValueConst obj, const char* name) {
  JSAtom key = js_symbol_tostringtag(ctx);
  JSValue str = JS_NewString(ctx, name);
  JS_DeleteProperty(ctx, obj, key, 0);
  JS_DefinePropertyValue(ctx, obj, key, str, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, key);
}

JSValue
js_object_from_target(JSContext* ctx, JSValueConst new_target, JSClassID id) {
  JSValue obj, proto = JS_GetPropertyStr(ctx, new_target, "prototype");

  if(JS_IsException(proto))
    return proto;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_GetClassProto(ctx, id);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, id);
  JS_FreeValue(ctx, proto);
  return obj;
}

BOOL
js_is_iterable(JSContext* ctx, JSValueConst object) {
  JSAtom atom = js_symbol_iterator(ctx);
  JSValue prop = JS_GetProperty(ctx, object, atom);
  JS_FreeAtom(ctx, atom);
  BOOL ret = JS_IsFunction(ctx, prop);
  JS_FreeValue(ctx, prop);
  return ret;
}

BOOL
js_is_iterator(JSContext* ctx, JSValueConst object) {
  JSValue prop = JS_GetPropertyStr(ctx, object, "next");
  BOOL ret = JS_IsFunction(ctx, prop);
  JS_FreeValue(ctx, prop);
  return ret;
}

JSValue
js_iterable_iterator(JSContext* ctx, JSValueConst object) {
  JSAtom atom = js_symbol_iterator(ctx);
  JSValue ret = JS_Invoke(ctx, object, atom, 0, 0);
  JS_FreeAtom(ctx, atom);
  return ret;
}

BOOL
js_iterator_init(JSContext* ctx, Iterator* iter, JSValueConst val) {
  JSValue arg = JS_DupValue(ctx, val);

  if(!js_is_iterator(ctx, arg))
    if(js_is_iterable(ctx, arg)) {
      JSValue tmp = js_iterable_iterator(ctx, arg);
      JS_FreeValue(ctx, arg);
      arg = tmp;
    }

  if(!js_is_iterator(ctx, arg) || !(iter->obj = js_object_pointer(arg))) {
    JS_FreeValue(ctx, arg);
    return FALSE;
  }

  iter->next = JS_NewAtom(ctx, "next");
  iter->done = JS_NewAtom(ctx, "done");
  iter->value = JS_NewAtom(ctx, "value");
  return TRUE;
}

JSValue
js_iterator_next(JSContext* ctx, Iterator* iter, BOOL* done_p) {
  JSValue ret = JS_EXCEPTION, item = JS_Invoke(ctx, js_object(iter->obj), iter->next, 0, 0);
  if(!JS_IsException(item)) {
    ret = JS_GetProperty(ctx, item, iter->done);
    if(!JS_IsException(ret)) {
      *done_p = JS_ToBool(ctx, ret);
      JS_FreeValue(ctx, ret);
    }
    ret = JS_GetProperty(ctx, item, iter->value);
  }
  JS_FreeValue(ctx, item);
  return ret;
}