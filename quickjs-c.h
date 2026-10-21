#ifndef QUICKJS_C_H
#define QUICKJS_C_H

#include <quickjs.h>
#include <cutils.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  JSValue length = JS_GetPropertyStr(ctx, array, "length");

  if(JS_IsNumber(length))
    JS_ToInt64(ctx, &len, length);

  JS_FreeValue(ctx, length);
  return len;
}

static inline BOOL
js_is_arraylike(JSContext* ctx, JSValueConst array) {
  JSValue length = JS_GetPropertyStr(ctx, array, "length");
  BOOL ret = JS_IsNumber(length);
  JS_FreeValue(ctx, length);
  return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* defined(QUICKJS_C_H) */
