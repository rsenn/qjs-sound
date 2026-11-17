#ifndef JS_UTILS_C_H
#define JS_UTILS_C_H

#include <quickjs.h>
#include <cutils.h>
#include <stdio.h>
#include <stdarg.h>

/* Helpers for parsing JS property objects and function arguments in C bindings.
 * These reduce the repetitive JS_GetPropertyStr/JS_FreeValue/JS_To* boilerplate.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Helpers for throwing errors consistently */
static inline JSValue
js_throw_error(JSContext* ctx, const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return JS_ThrowInternalError(ctx, "%s", buf);
}

/* Helpers for reading properties from a JS object */
static inline int
js_prop_int32(JSContext* ctx, JSValueConst obj, const char* name, int32_t* out) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  int ret = 0;
  if (!JS_IsUndefined(v)) {
    ret = JS_ToInt32(ctx, out, v);
  }
  JS_FreeValue(ctx, v);
  return ret;
}

static inline int
js_prop_uint32(JSContext* ctx, JSValueConst obj, const char* name, uint32_t* out) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  int ret = 0;
  if (!JS_IsUndefined(v)) {
    ret = JS_ToUint32(ctx, out, v);
  }
  JS_FreeValue(ctx, v);
  return ret;
}

static inline int
js_prop_float64(JSContext* ctx, JSValueConst obj, const char* name, double* out) {
  JSValue v = JS_GetPropertyStr(ctx, obj, name);
  int ret = 0;
  if (!JS_IsUndefined(v)) {
    ret = JS_ToFloat64(ctx, out, v);
  }
  JS_FreeValue(ctx, v);
  return ret;
}

/* Helpers for converting a single JS value (e.g. a setter argument) */
static inline int
js_value_int32(JSContext* ctx, JSValueConst val, int32_t* out) {
  return JS_ToInt32(ctx, out, val);
}

static inline int
js_value_uint32(JSContext* ctx, JSValueConst val, uint32_t* out) {
  return JS_ToUint32(ctx, out, val);
}

static inline int
js_value_float64(JSContext* ctx, JSValueConst val, double* out) {
  return JS_ToFloat64(ctx, out, val);
}

/* Helpers for reading function arguments */
static inline int
js_arg_int32(JSContext* ctx, int argc, JSValueConst argv[], int i, int32_t* out) {
  if (i < argc && !JS_IsUndefined(argv[i])) {
    return JS_ToInt32(ctx, out, argv[i]);
  }
  return 0;
}

static inline int
js_arg_uint32(JSContext* ctx, int argc, JSValueConst argv[], int i, uint32_t* out) {
  if (i < argc && !JS_IsUndefined(argv[i])) {
    return JS_ToUint32(ctx, out, argv[i]);
  }
  return 0;
}

static inline int
js_arg_float64(JSContext* ctx, int argc, JSValueConst argv[], int i, double* out) {
  if (i < argc && !JS_IsUndefined(argv[i])) {
    return JS_ToFloat64(ctx, out, argv[i]);
  }
  return 0;
}

static inline int
js_arg_int64(JSContext* ctx, int argc, JSValueConst argv[], int i, int64_t* out) {
  if (i < argc && !JS_IsUndefined(argv[i])) {
    return JS_ToInt64(ctx, out, argv[i]);
  }
  return 0;
}

static inline int
js_value_int64(JSContext* ctx, JSValueConst val, int64_t* out) {
  return JS_ToInt64(ctx, out, val);
}

#ifdef __cplusplus
}
#endif

#endif
