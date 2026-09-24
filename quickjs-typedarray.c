#include "quickjs-typedarray.h"
#include <cutils.h>
#include <string.h>

static size_t
js_typedarray_kind_size(JSTypedArrayKind kind) {
  switch(kind) {
    case JS_TYPEDARRAY_INT8:
    case JS_TYPEDARRAY_UINT8:
    case JS_TYPEDARRAY_UINT8_CLAMPED: return 1;
    case JS_TYPEDARRAY_INT16:
    case JS_TYPEDARRAY_UINT16: return 2;
    case JS_TYPEDARRAY_INT32:
    case JS_TYPEDARRAY_UINT32:
    case JS_TYPEDARRAY_FLOAT32: return 4;
    case JS_TYPEDARRAY_FLOAT64: return 8;
    case JS_TYPEDARRAY_NONE: break;
  }
  return 1;
}

const char*
js_typedarray_kind_name(JSTypedArrayKind kind) {
  switch(kind) {
    case JS_TYPEDARRAY_INT8: return "Int8Array";
    case JS_TYPEDARRAY_UINT8: return "Uint8Array";
    case JS_TYPEDARRAY_UINT8_CLAMPED: return "Uint8ClampedArray";
    case JS_TYPEDARRAY_INT16: return "Int16Array";
    case JS_TYPEDARRAY_UINT16: return "Uint16Array";
    case JS_TYPEDARRAY_INT32: return "Int32Array";
    case JS_TYPEDARRAY_UINT32: return "Uint32Array";
    case JS_TYPEDARRAY_FLOAT32: return "Float32Array";
    case JS_TYPEDARRAY_FLOAT64: return "Float64Array";
    case JS_TYPEDARRAY_NONE: break;
  }
  return "ArrayBuffer";
}

/* No public quickjs.h API reports a TypedArray's own element type (only
 * JS_GetTypedArrayBuffer's bytes_per_element, which is ambiguous between
 * e.g. Int16Array/Uint16Array) - duck-type it off .constructor.name
 * instead, same probing spirit as the rest of this file. */
static JSTypedArrayKind
js_typedarray_detect_kind(JSContext* ctx, JSValueConst val) {
  JSValue ctor = JS_GetPropertyStr(ctx, val, "constructor");
  JSValue name_val = JS_GetPropertyStr(ctx, ctor, "name");
  const char* name = JS_ToCString(ctx, name_val);
  JSTypedArrayKind kind = JS_TYPEDARRAY_NONE;

  if(name) {
    if(!strcmp(name, "Int8Array"))
      kind = JS_TYPEDARRAY_INT8;
    else if(!strcmp(name, "Uint8Array"))
      kind = JS_TYPEDARRAY_UINT8;
    else if(!strcmp(name, "Uint8ClampedArray"))
      kind = JS_TYPEDARRAY_UINT8_CLAMPED;
    else if(!strcmp(name, "Int16Array"))
      kind = JS_TYPEDARRAY_INT16;
    else if(!strcmp(name, "Uint16Array"))
      kind = JS_TYPEDARRAY_UINT16;
    else if(!strcmp(name, "Int32Array"))
      kind = JS_TYPEDARRAY_INT32;
    else if(!strcmp(name, "Uint32Array"))
      kind = JS_TYPEDARRAY_UINT32;
    else if(!strcmp(name, "Float32Array"))
      kind = JS_TYPEDARRAY_FLOAT32;
    else if(!strcmp(name, "Float64Array"))
      kind = JS_TYPEDARRAY_FLOAT64;

    JS_FreeCString(ctx, name);
  }

  JS_FreeValue(ctx, name_val);
  JS_FreeValue(ctx, ctor);
  return kind;
}

BOOL
js_bufferview_get(JSContext* ctx, JSValueConst val, JSBufferView* out) {
  size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);

  if(!JS_IsException(buf)) {
    size_t ab_size = 0;
    uint8_t* ab_data = JS_GetArrayBuffer(ctx, &ab_size, buf);
    JS_FreeValue(ctx, buf);
    if(!ab_data)
      return FALSE;

    out->ptr = ab_data + byte_offset;
    out->byte_length = byte_length;
    out->element_size = bytes_per_element ? bytes_per_element : 1;
    out->kind = js_typedarray_detect_kind(ctx, val);
    return TRUE;
  }

  /* not a TypedArray/DataView: discard the probe exception, try a plain ArrayBuffer */
  JS_FreeValue(ctx, JS_GetException(ctx));

  uint8_t* ptr;
  size_t len;
  if(!(ptr = JS_GetArrayBuffer(ctx, &len, val)))
    return FALSE;

  out->ptr = ptr;
  out->byte_length = len;
  out->element_size = 1;
  out->kind = JS_TYPEDARRAY_NONE;
  return TRUE;
}

BOOL
js_bufferview_get_kind(JSContext* ctx, JSValueConst val, JSTypedArrayKind kind, JSBufferView* out) {
  if(!js_bufferview_get(ctx, val, out))
    return FALSE;

  if(out->kind != kind) {
    JS_ThrowTypeError(ctx, "expected a %s", js_typedarray_kind_name(kind));
    return FALSE;
  }

  return TRUE;
}

static JSValue
js_typedarray_wrap_buffer(JSContext* ctx, JSValue buffer, JSTypedArrayKind kind) {
  if(kind == JS_TYPEDARRAY_NONE)
    return buffer;

  JSValue global = JS_GetGlobalObject(ctx);
  JSValue ctor = JS_GetPropertyStr(ctx, global, js_typedarray_kind_name(kind));
  JS_FreeValue(ctx, global);

  JSValueConst args[1] = {buffer};
  JSValue ta = JS_CallConstructor(ctx, ctor, 1, args);
  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, buffer);
  return ta;
}

static void
js_typedarray_free_malloc(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

JSValue
js_typedarray_from_malloc(JSContext* ctx, void* data, size_t count, JSTypedArrayKind kind) {
  JSValue ab = JS_NewArrayBuffer(ctx, data, count * js_typedarray_kind_size(kind), js_typedarray_free_malloc, NULL, FALSE);

  if(JS_IsException(ab))
    return ab;

  return js_typedarray_wrap_buffer(ctx, ab, kind);
}

JSValue
js_typedarray_from_copy(JSContext* ctx, const void* data, size_t count, JSTypedArrayKind kind) {
  JSValue ab = JS_NewArrayBufferCopy(ctx, data, count * js_typedarray_kind_size(kind));

  if(JS_IsException(ab))
    return ab;

  return js_typedarray_wrap_buffer(ctx, ab, kind);
}
