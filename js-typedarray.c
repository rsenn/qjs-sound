#include "js-typedarray.h"
#include "js-utils.h"
#include <cutils.h>
#include <string.h>

const char*
typedarray_name(TypedArrayKind kind) {
  switch(kind) {
    case TYPEDARRAY_INT8: return "Int8Array";
    case TYPEDARRAY_UINT8: return "Uint8Array";
    case TYPEDARRAY_UINT8_CLAMPED: return "Uint8ClampedArray";
    case TYPEDARRAY_INT16: return "Int16Array";
    case TYPEDARRAY_UINT16: return "Uint16Array";
    case TYPEDARRAY_INT32: return "Int32Array";
    case TYPEDARRAY_UINT32: return "Uint32Array";
    case TYPEDARRAY_FLOAT32: return "Float32Array";
    case TYPEDARRAY_FLOAT64: return "Float64Array";
    case TYPEDARRAY_NONE: break;
  }
  return "ArrayBuffer";
}

/* No public quickjs.h API reports a TypedArray's own element type(only
 * JS_GetTypedArrayBuffer's bytes_per_element, which is ambiguous between
 * e.g. Int16Array/Uint16Array) - duck-type it off .constructor.name
 * instead, same probing spirit as the rest of this file. */
static TypedArrayKind
typedarray_kind(JSContext* ctx, JSValueConst val) {
  JSValue ctor = JS_GetPropertyStr(ctx, val, "constructor");
  JSValue name_val = JS_GetPropertyStr(ctx, ctor, "name");
  const char* name = JS_ToCString(ctx, name_val);
  TypedArrayKind kind = TYPEDARRAY_NONE;

  if(name) {
    if(!strcmp(name, "Int8Array"))
      kind = TYPEDARRAY_INT8;
    else if(!strcmp(name, "Uint8Array"))
      kind = TYPEDARRAY_UINT8;
    else if(!strcmp(name, "Uint8ClampedArray"))
      kind = TYPEDARRAY_UINT8_CLAMPED;
    else if(!strcmp(name, "Int16Array"))
      kind = TYPEDARRAY_INT16;
    else if(!strcmp(name, "Uint16Array"))
      kind = TYPEDARRAY_UINT16;
    else if(!strcmp(name, "Int32Array"))
      kind = TYPEDARRAY_INT32;
    else if(!strcmp(name, "Uint32Array"))
      kind = TYPEDARRAY_UINT32;
    else if(!strcmp(name, "Float32Array"))
      kind = TYPEDARRAY_FLOAT32;
    else if(!strcmp(name, "Float64Array"))
      kind = TYPEDARRAY_FLOAT64;

    JS_FreeCString(ctx, name);
  }

  JS_FreeValue(ctx, name_val);
  JS_FreeValue(ctx, ctor);
  return kind;
}

int
js_typedarray_get(JSContext* ctx, JSValueConst obj, BufferView* out) {
  size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, obj, &byte_offset, &byte_length, &bytes_per_element);

  if(!JS_IsException(buf)) {
    size_t size = 0;
    uint8_t* data = JS_GetArrayBuffer(ctx, &size, buf);
    JS_FreeValue(ctx, buf);
    if(!data)
      return -1;

    *out = (BufferView){
      data + byte_offset,
      byte_length,
      bytes_per_element ? bytes_per_element : 1,
      typedarray_kind(ctx, obj),
    };
    return 0;
  }

  /* not a TypedArray/DataView: discard the probe exception, try a plain ArrayBuffer */
  JS_FreeValue(ctx, JS_GetException(ctx));

  uint8_t* ptr;
  size_t len;
  if(!(ptr = JS_GetArrayBuffer(ctx, &len, obj)))
    return -1;

  *out = (BufferView){ ptr, len, 1, TYPEDARRAY_NONE };
  return 0;
}

int
js_typedarray_kind(JSContext* ctx, JSValueConst val, TypedArrayKind kind, BufferView* out) {
  if(js_typedarray_get(ctx, val, out))
    return -1;

  if(out->kind != kind) {
    JS_ThrowTypeError(ctx, "expected a %s", typedarray_name(kind));
    return -1;
  }

  return 0;
}

JSValue
js_typedarray_buffer(JSContext* ctx, JSValue buffer, TypedArrayKind kind) {
  if(kind == TYPEDARRAY_NONE)
    return buffer;

  JSValue ctor = js_global_get_str(ctx, typedarray_name(kind));
  JSValue ret = JS_CallConstructor(ctx, ctor, 1, &buffer);
  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, buffer);
  return ret;
}

static void
js_typedarray_free_malloc(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

JSValue
js_typedarray_malloc(JSContext* ctx, void* data, size_t count, TypedArrayKind kind) {
  JSValue ab = JS_NewArrayBuffer(ctx, data, count * bytes_per_element(kind), js_typedarray_free_malloc, NULL, FALSE);

  if(JS_IsException(ab))
    return ab;

  return js_typedarray_buffer(ctx, ab, kind);
}

JSValue
js_typedarray_copy(JSContext* ctx, const void* data, size_t count, TypedArrayKind kind) {
  JSValue ab = JS_NewArrayBufferCopy(ctx, data, count * bytes_per_element(kind));

  if(JS_IsException(ab))
    return ab;

  return js_typedarray_buffer(ctx, ab, kind);
}
