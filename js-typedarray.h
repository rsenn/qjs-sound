#ifndef JS_TYPEDARRAY_H
#define JS_TYPEDARRAY_H

#include <quickjs.h>
#include <cutils.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared TypedArray/ArrayBuffer/DataView marshalling helpers, used by
 * quickjs-sndfile.c, quickjs-samplerate.c, quickjs-soundtouch.cpp, and
 * quickjs-rubberband.c(see doc/sndfile.md's "Shared TypedArray/buffer
 * interop" section). Plain C, no templates - usable unchanged from a .c
 * or .cpp translation unit(the extern "C" block below is required for
 * the .cpp consumers: without it, a .cpp translation unit mangles these
 * declarations differently than how js-typedarray.c itself, always
 * compiled as C, actually exports them, and the module fails to load
 * with an "undefined symbol" naming the mangled name). */

typedef enum {
  TYPEDARRAY_NONE = 0, /* plain ArrayBuffer/DataView - no element type */
  TYPEDARRAY_INT8,
  TYPEDARRAY_UINT8,
  TYPEDARRAY_UINT8_CLAMPED,
  TYPEDARRAY_INT16,
  TYPEDARRAY_UINT16,
  TYPEDARRAY_INT32,
  TYPEDARRAY_UINT32,
  TYPEDARRAY_FLOAT32,
  TYPEDARRAY_FLOAT64,
} TypedArrayKind;

typedef struct {
  void* ptr; /* already offset into the backing ArrayBuffer */
  size_t len;
  TypedArrayKind kind;
} BufferView;

static inline int
shift_count(TypedArrayKind kind) {
  switch(kind) {
    case TYPEDARRAY_INT8:
    case TYPEDARRAY_UINT8:
    case TYPEDARRAY_UINT8_CLAMPED: return 0;
    case TYPEDARRAY_INT16:
    case TYPEDARRAY_UINT16: return 1;
    case TYPEDARRAY_INT32:
    case TYPEDARRAY_UINT32:
    case TYPEDARRAY_FLOAT32: return 2;
    case TYPEDARRAY_FLOAT64: return 3;
    case TYPEDARRAY_NONE: break;
  }
  return 1;
}

static inline int
bytes_per_element(TypedArrayKind kind) {
  return 1 << shift_count(kind);
}

static inline size_t
typedarray_size(BufferView view) {
  return view.len >> shift_count(view.kind);
}

const char* typedarray_name(TypedArrayKind kind);

/* resolves argument obj (TypedArray, DataView, or plain ArrayBuffer) to
 * a view with no copy.
 * returns FALSE and throws a TypeError if obj is none of those. */
int js_typedarray_get(JSContext* ctx, JSValueConst val, BufferView* out);

/* js_typedarray_get restricted to one element kind - throws naming
 * kind (e.g. "Float32Array") on any mismatch, including TYPEDARRAY_NONE
 * (a plain ArrayBuffer never satisfies a kind check). */
int js_typedarray_kind(JSContext* ctx, JSValueConst obj, TypedArrayKind kind, BufferView* out);

/* creates a TypedArray from an underlying ArrayBuffer */
JSValue js_typedarray_buffer(JSContext*, JSValueConst, TypedArrayKind kind);

/* hands a native js_malloc'd buffer to JS as a new TypedArray of kind,
 * no copy - freed via js_free_rt when the ArrayBuffer dies. count is in
 * ELEMENTS. For TYPEDARRAY_NONE, returns a plain ArrayBuffer. */
JSValue js_typedarray_malloc(JSContext* ctx, void* data, size_t count, TypedArrayKind kind);

/* Copies count elements from data into a freshly allocated TypedArray
 * of kind. For TYPEDARRAY_NONE, returns a plain ArrayBuffer. */
JSValue js_typedarray_copy(JSContext* ctx, const void* data, size_t count, TypedArrayKind kind);

static inline JSValue
js_float32array_copy(JSContext* ctx, const float* data, size_t count) {
  return js_typedarray_copy(ctx, data, count, TYPEDARRAY_FLOAT32);
}

#ifdef __cplusplus
}
#endif

#endif /* defined(JS_TYPEDARRAY_H) */
