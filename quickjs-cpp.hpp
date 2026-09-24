#ifndef QUICKJS_CPP_HPP
#define QUICKJS_CPP_HPP

#include <quickjs.h>
#include <cstdint>
#include <new>
#include <vector>
#include "quickjs-typedarray.h"

/* Typed marshalling helpers shared by the C++ bindings: JS TypedArrays and
 * array-likes to and from C++ element types. Built on quickjs-typedarray.h, so
 * the module must link quickjs-typedarray.c. Every function that returns false
 * has thrown a JS exception. */
namespace qjsx {

/* Maps a C++ element type to its TypedArray kind and to the JS-to-C++ number
 * conversion (integers wrap modulo 2^n like a typed array store). */
template<class T> struct element;

template<> struct element<float> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_FLOAT32;
  static int from_js(JSContext* ctx, float* out, JSValueConst v) {
    double d;
    int r = JS_ToFloat64(ctx, &d, v);
    *out = static_cast<float>(d);
    return r;
  }
};

template<> struct element<double> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_FLOAT64;
  static int from_js(JSContext* ctx, double* out, JSValueConst v) { return JS_ToFloat64(ctx, out, v); }
};

template<> struct element<int32_t> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_INT32;
  static int from_js(JSContext* ctx, int32_t* out, JSValueConst v) { return JS_ToInt32(ctx, out, v); }
};

template<> struct element<uint32_t> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_UINT32;
  static int from_js(JSContext* ctx, uint32_t* out, JSValueConst v) { return JS_ToUint32(ctx, out, v); }
};

template<> struct element<int16_t> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_INT16;
  static int from_js(JSContext* ctx, int16_t* out, JSValueConst v) {
    int32_t i;
    int r = JS_ToInt32(ctx, &i, v);
    *out = static_cast<int16_t>(i);
    return r;
  }
};

template<> struct element<uint16_t> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_UINT16;
  static int from_js(JSContext* ctx, uint16_t* out, JSValueConst v) {
    int32_t i;
    int r = JS_ToInt32(ctx, &i, v);
    *out = static_cast<uint16_t>(i);
    return r;
  }
};

template<> struct element<int8_t> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_INT8;
  static int from_js(JSContext* ctx, int8_t* out, JSValueConst v) {
    int32_t i;
    int r = JS_ToInt32(ctx, &i, v);
    *out = static_cast<int8_t>(i);
    return r;
  }
};

template<> struct element<uint8_t> {
  static constexpr JSTypedArrayKind kind = JS_TYPEDARRAY_UINT8;
  static int from_js(JSContext* ctx, uint8_t* out, JSValueConst v) {
    int32_t i;
    int r = JS_ToInt32(ctx, &i, v);
    *out = static_cast<uint8_t>(i);
    return r;
  }
};

/* Zero-copy window onto a TypedArray's backing store. It is only valid until
 * JS code can run again: the array may be detached or resized by then. */
template<class T> struct array_view {
  T* data = nullptr;
  size_t size = 0;

  T* begin() const { return data; }
  T* end() const { return data + size; }
  T& operator[](size_t i) const { return data[i]; }
};

/* Views a TypedArray whose element type is exactly T (a Float32Array for
 * float, and so on; other arrays with the same element width are rejected).
 * Throws a TypeError on a kind mismatch and a RangeError if the array holds
 * fewer than min_size elements. */
template<class T>
bool
get_array(JSContext* ctx, JSValueConst val, array_view<T>& out, size_t min_size = 0) {
  JSBufferView view;

  if(!js_bufferview_get_kind(ctx, val, element<T>::kind, &view))
    return false;

  if(view.byte_length / view.element_size < min_size) {
    JS_ThrowRangeError(ctx, "%s must have at least %zu elements (has %zu)", js_typedarray_kind_name(element<T>::kind), min_size, view.byte_length / view.element_size);
    return false;
  }

  out.data = reinterpret_cast<T*>(view.ptr);
  out.size = view.byte_length / view.element_size;
  return true;
}

/* Copies any array-like of numbers (Array, TypedArray, or an object with
 * length and indexed elements) into out, converting each element to T. The
 * length comes from the value. Throws a TypeError if val is not an object. */
template<class T>
bool
read_array(JSContext* ctx, JSValueConst val, std::vector<T>& out) {
  if(!JS_IsObject(val)) {
    JS_ThrowTypeError(ctx, "expecting an Array or TypedArray");
    return false;
  }

  JSValue lenv = JS_GetPropertyStr(ctx, val, "length");
  uint32_t len = 0;
  int r = JS_ToUint32(ctx, &len, lenv);
  JS_FreeValue(ctx, lenv);
  if(r)
    return false;

  std::vector<T> tmp;
  try {
    tmp.resize(len);
  } catch(const std::bad_alloc&) {
    JS_ThrowOutOfMemory(ctx);
    return false;
  }

  for(uint32_t i = 0; i < len; i++) {
    JSValue e = JS_GetPropertyUint32(ctx, val, i);
    r = element<T>::from_js(ctx, &tmp[i], e);
    JS_FreeValue(ctx, e);
    if(r)
      return false;
  }

  out.swap(tmp);
  return true;
}

/* Copies data into a new TypedArray of the matching kind. */
template<class T>
JSValue
new_array(JSContext* ctx, const T* data, size_t count) {
  return js_typedarray_from_copy(ctx, data, count, element<T>::kind);
}

template<class T>
JSValue
new_array(JSContext* ctx, const std::vector<T>& v) {
  return new_array(ctx, v.data(), v.size());
}

} // namespace qjsx

#endif /* defined(QUICKJS_CPP_HPP) */
