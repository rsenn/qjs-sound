#ifndef QUICKJS_TYPEDARRAY_H
#define QUICKJS_TYPEDARRAY_H

#include <quickjs.h>
#include <cutils.h>

/* Shared TypedArray/ArrayBuffer/DataView marshalling helpers, used by
 * quickjs-sndfile.c, quickjs-samplerate.c, quickjs-soundtouch.cpp, and
 * quickjs-rubberband.c (see doc/sndfile.md's "Shared TypedArray/buffer
 * interop" section). Plain C, no templates - usable unchanged from a .c
 * or .cpp translation unit (the extern "C" block below is required for
 * the .cpp consumers: without it, a .cpp translation unit mangles these
 * declarations differently than how quickjs-typedarray.c itself, always
 * compiled as C, actually exports them, and the module fails to load
 * with an "undefined symbol" naming the mangled name). */

typedef enum {
  JS_TYPEDARRAY_NONE = 0, /* plain ArrayBuffer/DataView - no element type */
  JS_TYPEDARRAY_INT8,
  JS_TYPEDARRAY_UINT8,
  JS_TYPEDARRAY_UINT8_CLAMPED,
  JS_TYPEDARRAY_INT16,
  JS_TYPEDARRAY_UINT16,
  JS_TYPEDARRAY_INT32,
  JS_TYPEDARRAY_UINT32,
  JS_TYPEDARRAY_FLOAT32,
  JS_TYPEDARRAY_FLOAT64,
} JSTypedArrayKind;

typedef struct {
  uint8_t* ptr; /* already offset into the backing ArrayBuffer */
  size_t byte_length;
  size_t element_size; /* 1/2/4/8; 1 for JS_TYPEDARRAY_NONE */
  JSTypedArrayKind kind;
} JSBufferView;

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves argument `val` (TypedArray, DataView, or plain ArrayBuffer) to
 * a view with no copy. Returns FALSE and throws a TypeError if `val` is
 * none of those. */
BOOL js_bufferview_get(JSContext* ctx, JSValueConst val, JSBufferView* out);

/* js_bufferview_get restricted to one element kind - throws naming
 * `kind` (e.g. "Float32Array") on any mismatch, including
 * JS_TYPEDARRAY_NONE (a plain ArrayBuffer never satisfies a kind check). */
BOOL js_bufferview_get_kind(JSContext* ctx, JSValueConst val, JSTypedArrayKind kind, JSBufferView* out);

const char* js_typedarray_kind_name(JSTypedArrayKind kind);

/* Hands a native js_malloc'd buffer to JS as a new TypedArray of `kind`,
 * no copy - freed via js_free_rt when the ArrayBuffer dies. `count` is in
 * ELEMENTS. For JS_TYPEDARRAY_NONE, returns a plain ArrayBuffer. */
JSValue js_typedarray_from_malloc(JSContext* ctx, void* data, size_t count, JSTypedArrayKind kind);

/* Copies `count` elements from `data` into a freshly allocated TypedArray
 * of `kind`. For JS_TYPEDARRAY_NONE, returns a plain ArrayBuffer. */
JSValue js_typedarray_from_copy(JSContext* ctx, const void* data, size_t count, JSTypedArrayKind kind);

#ifdef __cplusplus
}
#endif

#endif /* defined(QUICKJS_TYPEDARRAY_H) */
