#include "js-utils.h"

JSAtom
js_symbol_tostringtag(JSContext* ctx) {
  JSValue g = JS_GetGlobalObject(ctx);
  JSValue sym = JS_GetPropertyStr(ctx, g, "Symbol");
  JSValue tst = JS_GetPropertyStr(ctx, sym, "toStringTag");
  JS_FreeValue(ctx, sym);
  JS_FreeValue(ctx, g);
  JSAtom ret = JS_ValueToAtom(ctx, tst);
  JS_FreeValue(ctx, tst);
  return ret;
}

void
js_set_tostringtag(JSContext* ctx, JSValueConst obj, const char* name) {
  JSAtom tst = js_symbol_tostringtag(ctx);
  JSValue str = JS_NewString(ctx, name);
  JS_DeleteProperty(ctx, obj, tst, 0);
  JS_DefinePropertyValue(ctx, obj, tst, str, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, tst);
}
