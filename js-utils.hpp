#ifndef QUICKJS_CPP_HPP
#define QUICKJS_CPP_HPP

#include <quickjs.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#include "js-typedarray.h"
#include "js-utils.h"

/* Typed marshalling helpers shared by the C++ bindings: JS TypedArrays and
 * array-likes to and from C++ element types. Built on js-typedarray.h, so
 * the module must link js-typedarray.c. Every function that returns
 * nonzero has thrown a JS exception. */
namespace qjsx {

/* Maps a C++ element type to its TypedArray kind and to the JS-to-C++ number
 * conversion(integers wrap modulo 2^n like a typed array store). */
template <class T> struct element;

template <> struct element<float> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_FLOAT32;
  static int
  from_js(JSContext* ctx, float* out, JSValueConst val) {
    double d;
    if(!JS_ToFloat64(ctx, &d, val)) {
      *out = static_cast<float>(d);
      return 0;
    }
    return -1;
  }
  static JSValue
  to_js(JSContext* ctx, float in) {
    return JS_NewFloat64(ctx, in);
  }
};

template <> struct element<double> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_FLOAT64;
  static int
  from_js(JSContext* ctx, double* out, JSValueConst val) {
    return JS_ToFloat64(ctx, out, val);
  }
  static JSValue
  to_js(JSContext* ctx, double in) {
    return JS_NewFloat64(ctx, in);
  }
};

template <> struct element<int32_t> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_INT32;
  static int
  from_js(JSContext* ctx, int32_t* out, JSValueConst val) {
    return JS_ToInt32(ctx, out, val);
  }
  static JSValue
  to_js(JSContext* ctx, int32_t in) {
    return JS_NewInt32(ctx, in);
  }
};

template <> struct element<uint32_t> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_UINT32;
  static int
  from_js(JSContext* ctx, uint32_t* out, JSValueConst val) {
    return JS_ToUint32(ctx, out, val);
  }
  static JSValue
  to_js(JSContext* ctx, uint32_t in) {
    return JS_NewUint32(ctx, in);
  }
};

/**
 * @brief JS to C++ conversion for 64-bit signed integers.
 *
 * Has no `kind` member on purpose: TypedArrayKind has no 64-bit integer, so
 * `get_array<int64_t>` does not compile.
 */
template <> struct element<int64_t> {
  static int
  from_js(JSContext* ctx, int64_t* out, JSValueConst val) {
    return JS_ToInt64(ctx, out, val);
  }
  static JSValue
  to_js(JSContext* ctx, int64_t in) {
    return JS_NewInt64(ctx, in);
  }
};

template <> struct element<int16_t> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_INT16;
  static int
  from_js(JSContext* ctx, int16_t* out, JSValueConst val) {
    int32_t i;
    if(!JS_ToInt32(ctx, &i, val)) {
      *out = static_cast<int16_t>(i);
      return 0;
    }
    return -1;
  }
  static JSValue
  to_js(JSContext* ctx, int16_t in) {
    return JS_NewInt32(ctx, in);
  }
};

template <> struct element<uint16_t> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_UINT16;
  static int
  from_js(JSContext* ctx, uint16_t* out, JSValueConst val) {
    int32_t i;
    if(!JS_ToInt32(ctx, &i, val)) {
      *out = static_cast<uint16_t>(i);
      return 0;
    }
    return -1;
  }
  static JSValue
  to_js(JSContext* ctx, uint16_t in) {
    return JS_NewUint32(ctx, in);
  }
};

template <> struct element<int8_t> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_INT8;
  static int
  from_js(JSContext* ctx, int8_t* out, JSValueConst val) {
    int32_t i;
    if(!JS_ToInt32(ctx, &i, val)) {
      *out = static_cast<int8_t>(i);
      return 0;
    }
    return -1;
  }
  static JSValue
  to_js(JSContext* ctx, int8_t in) {
    return JS_NewInt32(ctx, in);
  }
};

template <> struct element<uint8_t> {
  static constexpr TypedArrayKind kind = TYPEDARRAY_UINT8;
  static int
  from_js(JSContext* ctx, uint8_t* out, JSValueConst val) {
    int32_t i;
    if(!JS_ToInt32(ctx, &i, val)) {
      *out = static_cast<uint8_t>(i);
      return 0;
    }
    return -1;
  }
  static JSValue
  to_js(JSContext* ctx, uint8_t in) {
    return JS_NewUint32(ctx, in);
  }
};

template <> struct element<bool> {
  static int
  from_js(JSContext* ctx, bool* out, JSValueConst val) {
    if(!JS_IsBool(val)) {
      JS_ThrowTypeError(ctx, "expecting a boolean");
      return -1;
    }
    *out = JS_ToBool(ctx, val);
    return 0;
  }
  static JSValue
  to_js(JSContext* ctx, bool in) {
    return in ? JS_TRUE : JS_FALSE;
  }
};

template <> struct element<const char*> {
  // static constexpr TypedArrayKind kind = TYPEDARRAY_NONE;
  static int
  from_js(JSContext* ctx, const char** out, JSValueConst val) {
    if(!JS_IsString(val)) {
      JS_ThrowTypeError(ctx, "expecting a string");
      return -1;
    }
    const char* str;
    if((str = JS_ToCString(ctx, val))) {
      *out = str;
      return 0;
    }
    return -1;
  }
  static JSValue
  to_js(JSContext* ctx, const char* in) {
    return JS_NewString(ctx, in);
  }
};

/* ref::release */
template <class T> struct ref {
  static void
  release(JSContext* ctx, T& val) {}
};

template <> struct ref<const char*> {
  static void
  release(JSContext* ctx, const char*& val) {
    if(val) {
      JS_FreeCString(ctx, val);
      val = nullptr;
    }
  }
};

/* Zero-copy window onto a TypedArray's backing store. It is only valid until
 * JS code can run again: the array may be detached or resized by then. */
template <class T> struct array_view {
  T* data = nullptr;
  size_t size = 0;

  T*
  begin() const {
    return data;
  }
  T*
  end() const {
    return data + size;
  }
  T&
  operator[](size_t i) const {
    return data[i];
  }
};

/* Views a TypedArray whose element type is exactly T(a Float32Array for
 * float, and so on; other arrays with the same element width are rejected).
 * Throws a TypeError on a kind mismatch and a RangeError if the array holds
 * fewer than min_size elements. */
template <class T>
int
get_array(JSContext* ctx, JSValueConst val, array_view<T>& out, size_t min_size = 0) {
  BufferView view;

  if(js_typedarray_kind(ctx, val, element<T>::kind, &view))
    return -1;

  if((out.size = typedarray_size(view)) < min_size) {
    JS_ThrowRangeError(ctx, "%s must have at least %zu elements(has %zu)", typedarray_name(element<T>::kind), min_size, out.size);
    return -1;
  }

  out.data = reinterpret_cast<T*>(view.ptr);
  return 0;
}

/* Views the bytes of an ArrayBuffer, or of any TypedArray or DataView, with
 * no copy. Throws a TypeError for anything else. */
inline int
get_bytes(JSContext* ctx, JSValueConst val, array_view<uint8_t>& out) {
  BufferView view;

  if(js_typedarray_get(ctx, val, &view))
    return -1;

  out.data = static_cast<uint8_t*>(view.ptr);
  out.size = typedarray_size(view);
  return 0;
}

/* Copies any array-like of numbers(Array, TypedArray, or an object with
 * length and indexed elements) into out, converting each element to T. The
 * length comes from the value. Throws a TypeError if val is not an object. */
template <class T>
int
read_array(JSContext* ctx, JSValueConst val, std::vector<T>& out) {
  if(!JS_IsObject(val)) {
    JS_ThrowTypeError(ctx, "expecting an Array or TypedArray");
    return -1;
  }

  int64_t len = js_array_length(ctx, val);
  if(len == -1)
    return -1;

  std::vector<T> tmp;
  try {
    tmp.resize(len);
  } catch(const std::bad_alloc&) {
    JS_ThrowOutOfMemory(ctx);
    return -1;
  }

  for(int64_t i = 0; i < len; i++) {
    JSValue e = JS_GetPropertyUint32(ctx, val, i);
    int r = element<T>::from_js(ctx, &tmp[i], e);
    JS_FreeValue(ctx, e);
    if(r)
      return -1;
  }

  out.swap(tmp);
  return 0;
}

/* Copies a TypedArray of exactly T, or else any array-like of numbers, into
 * out. A matching TypedArray is copied in one pass; anything else goes
 * through read_array. Throws a TypeError if val is not an object. */
template <class T>
int
copy_array(JSContext* ctx, JSValueConst val, std::vector<T>& out) {
  array_view<T> view;

  if(!get_array(ctx, val, view)) {
    out.assign(view.begin(), view.end());
    return 0;
  }

  JS_FreeValue(ctx, JS_GetException(ctx));
  return read_array(ctx, val, out);
}

/* Copies data into a new TypedArray of the matching kind. */
template <class T>
JSValue
new_array(JSContext* ctx, const T* data, size_t count) {
  return js_typedarray_copy(ctx, data, count, element<T>::kind);
}

/* view_array: a TypedArray that aliases native memory, no copy.
 *
 * `view_array(ctx, ch->mutableData(), ch->length(), w->bus)` // Float32Array
 *
 *   T*         data   first element; must stay valid while owner lives
 *   size_t     count  number of elements
 *   Owner      owner  copied into the array, released when it is collected;
 *                     a std::shared_ptr to whatever owns data
 *
 *   returns  the array, or JS_EXCEPTION
 */
template <class T, class Owner>
JSValue
view_array(JSContext* ctx, T* data, size_t count, Owner owner) {
  Owner* keep = new Owner(std::move(owner));
  JSValue buf = JS_NewArrayBuffer(
      ctx, reinterpret_cast<uint8_t*>(data), count * sizeof(T),
      [](JSRuntime*, void* opaque, void*) { delete static_cast<Owner*>(opaque); }, keep, FALSE
  );

  if(JS_IsException(buf)) {
    delete keep;
    return buf;
  }

  return js_typedarray_buffer(ctx, buf, element<T>::kind);
}

template <class T>
JSValue
new_array(JSContext* ctx, const std::vector<T>& v) {
  return new_array(ctx, v.data(), v.size());
}

template <class T>
JSValue
new_value(JSContext* ctx, const T& val) {
  return element<T>::to_js(ctx, val);
}

template <class T>
int
value(JSContext* ctx, JSValueConst val, T& out) {
  return element<T>::from_js(ctx, &out, val);
}

template <class T>
T
value(JSContext* ctx, JSValueConst obj) {
  T ret;
  if(value(ctx, obj, ret))
    ret = T();
  return ret;
}

template <class T>
int
free_value(JSContext* ctx, JSValue val, T& out) {
  int r = value(ctx, val, out);
  JS_FreeValue(ctx, val);
  return r;
}

template <class T>
T
free_value(JSContext* ctx, JSValue val) {
  T ret = value<T>(ctx, val);
  JS_FreeValue(ctx, val);
  return ret;
}

template <class T>
void
release(JSContext* ctx, T& out) {
  ref<T>::release(ctx, out);
}

template <class T>
int
property(JSContext* ctx, JSValueConst obj, JSAtom key, T& out) {
  int ret = -1;
  if(JS_HasProperty(ctx, obj, key)) {
    JSValue prop = JS_GetProperty(ctx, obj, key);
    ret = value(ctx, prop, out);
    JS_FreeValue(ctx, prop);
  }
  return ret;
}

template <class T>
int
property(JSContext* ctx, JSValueConst obj, const char* name, T& out) {
  JSAtom key = JS_NewAtom(ctx, name);
  int ret = property(ctx, obj, key, out);
  JS_FreeAtom(ctx, key);
  return ret;
}

template <class T, typename KeyType>
T
property_default(JSContext* ctx, JSValueConst obj, KeyType prop, T def = T()) {
  T val;
  if(property<T>(ctx, obj, prop, val))
    val = def;
  return val;
}

template <class T, typename KeyType>
T
property(JSContext* ctx, JSValueConst obj, KeyType prop) {
  return property_default(ctx, obj, prop, T());
}

#include <type_traits>

/* base template delegates to operator() */
template <typename T> struct argument_trait : argument_trait<decltype(&T::operator())> {};

/* specialization for standard (const) lambdas */
template <typename ClassType, typename ReturnType, typename ArgType, typename... Rest>
struct argument_trait<ReturnType (ClassType::*)(ArgType, Rest...) const> {
  using type = std::remove_cv_t<std::remove_reference_t<ArgType>>;
};

/* specialization-for-mutable-(non-const)-lambdas */
template <typename ClassType, typename ReturnType, typename ArgType, typename... Rest>
struct argument_trait<ReturnType (ClassType::*)(ArgType, Rest...)> {
  using type = std::remove_cv_t<std::remove_reference_t<ArgType>>;
};

/* convenient alias template */
template <typename FunctionType> using argument_type = typename argument_trait<std::decay_t<FunctionType>>::type;

template <typename FunctionType>
int
with(JSContext* ctx, JSValueConst val, FunctionType&& fn) {
  /* automatically deduce T from the lambda's parameter type (e.g., const int&) */
  using T = argument_type<FunctionType>;
  T tmp;

  /* calls the underlying conversion overload */
  if(!value(ctx, val, tmp)) {
    fn(tmp);
    release(ctx, tmp);
    return 0;
  }

  return -1;
}

template <typename FunctionType, typename KeyType>
int
with(JSContext* ctx, JSValueConst obj, KeyType key, FunctionType&& fn) {
  /* automatically deduce T from the lambda's parameter type (e.g., const int&) */
  using T = argument_type<FunctionType>;
  T tmp;

  /* calls the underlying getter & conversion overload */
  int r = property(ctx, obj, key, tmp);
  if(r == 0) {
    fn(tmp);
    release(ctx, tmp);
    return 0;
  }
  if(r < 0)
    return -1; // property not found, no exception pending

  return -1; // conversion failed, exception pending
}

/* get_opaque: copies the T stored in an object of a class into out.
 *
 * `if(qjsx::get_opaque(ctx, argv[0], js_audiocontext_class_id, ac)) return JS_EXCEPTION;`
 *
 *   JSValueConst  val       object that should be an instance of the class
 *   JSClassID     class_id  class whose opaque holds a T
 *   T&            out       receives a copy of the stored T
 *
 *   returns  0, or -1 with a TypeError thrown when val is not of the class
 */
template <class T>
int
get_opaque(JSContext* ctx, JSValueConst val, JSClassID class_id, T& out) {
  T* p = static_cast<T*>(JS_GetOpaque2(ctx, val, class_id));

  if(!p)
    return -1;

  out = *p;
  return 0;
}

/* own: attach a placement-constructed T to a new object, as its opaque.
 *
 *   JSValue  obj   new object of the class, consumed on failure
 *   Args...  args  brace-initializers of T (T is an aggregate)
 *
 *   returns  obj, or JS_EXCEPTION with obj freed and no T left
 */
template <class T, class... Args>
JSValue
own(JSContext* ctx, JSValue obj, Args&&... args) {
  T* w;

  if(JS_IsException(obj))
    return obj;

  if(!(w = static_cast<T*>(js_mallocz(ctx, sizeof(T))))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

  new(w) T{ std::forward<Args>(args)... };
  JS_SetOpaque(obj, w);
  return obj;
}

/* wrap: new object owning a T, for a constructor so subclasses work.
 *
 * `wrap(ctx, new_target, id, std::move(ac))` // T is deduced as AudioContextPtr
 *
 *   JSValueConst  new_target  prototype is new_target.prototype, else
 *                             the class prototype
 *   JSClassID     class_id    class whose finalizer destroys the T
 *   T&&           value       moved or copied into the object; T is
 *                             deduced, or given when value converts to it
 *
 *   returns  the object, or JS_EXCEPTION
 */
template <class T>
JSValue
wrap(JSContext* ctx, JSValueConst new_target, JSClassID class_id, T&& value) {
  return own<std::remove_cv_t<std::remove_reference_t<T>>>(ctx, js_object_from_target(ctx, new_target, class_id), std::forward<T>(value));
}

/* wrap_proto: like wrap, with an explicit prototype instead of new_target.
 *
 * `wrap_proto<JsAudioNode>(ctx, proto, id, std::move(node), std::move(ac))`
 *
 *   JSValueConst  proto     prototype of the new object
 *   JSClassID     class_id  class whose finalizer destroys the T
 *   Args...       args      brace-initializers of T (T is an aggregate)
 *
 *   returns  the object, or JS_EXCEPTION
 */
template <class T, class... Args>
JSValue
wrap_proto(JSContext* ctx, JSValueConst proto, JSClassID class_id, Args&&... args) {
  return own<T>(ctx, JS_NewObjectProtoClass(ctx, proto, class_id), std::forward<Args>(args)...);
}

/* wrap_shared: like wrap, for an object owning a std::shared_ptr<T> that
 * is made from the constructor arguments.
 *
 * `wrap_shared<stk::Filter, stk::BiQuad>(ctx, new_target, id)`
 *
 *   JSValueConst  new_target  as for wrap
 *   JSClassID     class_id    class whose finalizer destroys the pointer
 *   Args...       args        forwarded to the constructor of Derived
 *
 *   template T        the pointer type the object holds, default Derived
 *   template Derived  the type that is constructed, default T
 *
 *   returns  the object, or JS_EXCEPTION
 *   throws   whatever the constructor of Derived throws, before an object exists
 */
template <class T, class Derived = T, class... Args>
JSValue
wrap_shared(JSContext* ctx, JSValueConst new_target, JSClassID class_id, Args&&... args) {
  return wrap(ctx, new_target, class_id, std::shared_ptr<T>(std::make_shared<Derived>(std::forward<Args>(args)...)));
}

namespace detail {

/**
 * @brief Converts argv[i] into out; a missing or undefined argument keeps out.
 *
 * @tparam T  number or bool type with an `element<T>` specialisation;
 *            `const char*` is rejected (it needs `release`, use `with()`)
 * @return 0 on success or when nothing was converted, -1 with a JS
 *         exception thrown when the conversion failed
 */
template <class T>
int
arg_at(JSContext* ctx, int argc, JSValueConst* argv, int i, T& out) {
  static_assert(!std::is_same<T, const char*>::value, "strings need release(): use qjsx::with()");

  if(i >= argc || JS_IsUndefined(argv[i]))
    return 0;

  return element<T>::from_js(ctx, &out, argv[i]) ? -1 : 0;
}

/**
 * @brief Calls f with obj cast to T when obj is an instance of T.
 *
 * @return true when the cast succeeded and f ran
 */
template <class T, class Base, class F>
bool
try_cast(Base* obj, F& f) {
  if(T* p = dynamic_cast<T*>(obj)) {
    f(p);
    return true;
  }

  return false;
}

} // namespace detail

/**
 * @brief Converts argv[first], argv[first + 1], ... into the given variables.
 *
 * The variables are preset by the caller with the defaults. An argument that
 * is absent (`i >= argc`) or `undefined` leaves its variable unchanged; `null`
 * converts like any value (to 0). Conversion stops at the first failure.
 *
 * @code
 * double fr = 440, amp = 1;
 * int32_t harms = 1;
 *
 * if(qjsx::args(ctx, argc, argv, fr, amp, harms))
 *   return JS_EXCEPTION;
 * @endcode
 *
 * Objects between the numbers are read with another call that starts later:
 * `qjsx::args_from(ctx, 5, argc, argv, vecsize, sr)`.
 *
 * @param ctx    JS context
 * @param first  index of the argument that fills the first variable
 * @param argc   number of arguments passed
 * @param argv   the arguments
 * @param out    one variable per argument, each a type with `element<T>`
 * @return 0, or -1 with the exception of the failed conversion thrown; after
 *         -1 the variables are unspecified (QuickJS stores NaN on failure)
 * @note Never reads `argv[i]` for `i >= argc`.
 */
template <class... T>
int
args_from(JSContext* ctx, int first, int argc, JSValueConst* argv, T&... out) {
  int i = first;

  return (... || (detail::arg_at(ctx, argc, argv, i++, out) != 0)) ? -1 : 0;
}

/**
 * @brief Converts argv[0], argv[1], ... into the given variables.
 *
 * Shorthand for `args_from(ctx, 0, argc, argv, out...)`; see there.
 */
template <class... T>
int
args(JSContext* ctx, int argc, JSValueConst* argv, T&... out) {
  return args_from(ctx, 0, argc, argv, out...);
}

/**
 * @brief Tells whether an optional object argument was given.
 *
 * @param argc  number of arguments passed
 * @param argv  the arguments
 * @param i     index to test
 * @return true when `i < argc` and the argument is neither undefined nor null
 */
inline bool
present(int argc, JSValueConst* argv, int i) {
  return i < argc && !JS_IsUndefined(argv[i]) && !JS_IsNull(argv[i]);
}

/**
 * @brief Calls f with obj cast to the first type of Ts it is an instance of.
 *
 * Replaces an `if(auto* p = dynamic_cast<A*>(obj)) ... else if ...` ladder.
 * The generic lambda is compiled for every type in Ts, so a member that only
 * some of them have needs `if constexpr` or a second call.
 *
 * @code
 * qjsx::dispatch<Oscil, Buzz, Randh>(obj, [&](auto* p) { p->SetFreq(fr, mod); });
 * @endcode
 *
 * @tparam Ts   candidate types, tested in order; the first match wins
 * @param obj   pointer to an object of a polymorphic class
 * @param f     callable taking a pointer to the matched type
 * @return true when a type matched and f ran
 */
template <class... Ts, class Base, class F>
bool
dispatch(Base* obj, F&& f) {
  return (... || detail::try_cast<Ts>(obj, f));
}

/**
 * @brief dispatch for a `std::shared_ptr`; see the pointer overload.
 */
template <class... Ts, class Base, class F>
bool
dispatch(const std::shared_ptr<Base>& obj, F&& f) {
  return (... || detail::try_cast<Ts>(obj.get(), f));
}

/**
 * @brief Casts obj to T, or throws a TypeError.
 *
 * @code
 * ADSR* p = qjsx::require<ADSR>(ctx, obj, "sustain needs an ADSR");
 *
 * if(!p)
 *   return JS_EXCEPTION;
 * @endcode
 *
 * @param ctx      JS context
 * @param obj      pointer to an object of a polymorphic class
 * @param message  text of the TypeError
 * @return the pointer, or nullptr with the TypeError thrown
 */
template <class T, class Base>
T*
require(JSContext* ctx, Base* obj, const char* message) {
  T* p = dynamic_cast<T*>(obj);

  if(!p)
    JS_ThrowTypeError(ctx, "%s", message);

  return p;
}

/**
 * @brief Finalizer of a class whose opaque is a placement-constructed T.
 *
 * Runs the destructor, then frees the block made with `js_mallocz`. Used
 * directly as the class's finalizer:
 *
 * @code
 * static JSClassDef js_stkfilter_class = {
 *   .class_name = "Filter",
 *   .finalizer = qjsx::finalize<StkFilterPtr, js_stkfilter_class_id>,
 * };
 * @endcode
 *
 * @tparam T   type stored in the opaque
 * @tparam Id  the class id variable; it is read when the finalizer runs
 */
template <class T, JSClassID& Id>
void
finalize(JSRuntime* rt, JSValue val) {
  if(T* p = static_cast<T*>(JS_GetOpaque(val, Id))) {
    p->~T();
    js_free_rt(rt, p);
  }
}

/**
 * @brief Finalizer of a class whose opaque was made with `new`.
 *
 * @tparam T   type stored in the opaque
 * @tparam Id  the class id variable; it is read when the finalizer runs
 */
template <class T, JSClassID& Id>
void
finalize_delete(JSRuntime*, JSValue val) {
  delete static_cast<T*>(JS_GetOpaque(val, Id));
}

/**
 * @brief One JS constructor of a family that shares one C function.
 *
 * A `static const ctor_def table[]` is the single source of the class names:
 * `declare_ctors` declares the exports, `bind_ctors` creates the
 * constructors, `ctor_name` gives the `Symbol.toStringTag`.
 */
struct ctor_def {
  const char* name;                   /**< JS class name, also the toStringTag */
  int length;                         /**< the constructor's `.length` */
  int magic;                          /**< value passed to the C function as its `magic` */
  const char* export_name = nullptr;  /**< module export name when it differs from name */
};

/**
 * @brief Declares the module exports of a constructor table.
 *
 * Call it where the exports are declared (before the init callback runs).
 *
 * @param ctx   JS context
 * @param m     module being created
 * @param defs  the constructor table
 */
template <size_t N>
void
declare_ctors(JSContext* ctx, JSModuleDef* m, const ctor_def (&defs)[N]) {
  for(const ctor_def& d : defs)
    JS_AddModuleExport(ctx, m, d.export_name ? d.export_name : d.name);
}

/**
 * @brief Creates the constructors of a table and binds them to their exports.
 *
 * Every constructor gets `prototype` set to proto, so subclassing with
 * `class X extends Ctor {}` works.
 *
 * @param ctx    JS context
 * @param m      module in its init callback
 * @param fn     the C function shared by the family, taking `magic`
 * @param proto  the family's prototype object
 * @param defs   the constructor table, declared with declare_ctors
 */
template <size_t N>
void
bind_ctors(JSContext* ctx, JSModuleDef* m, JSCFunctionMagic* fn, JSValueConst proto, const ctor_def (&defs)[N]) {
  for(const ctor_def& d : defs) {
    JSValue ctor = JS_NewCFunctionMagic(ctx, fn, d.name, d.length, JS_CFUNC_constructor_magic, d.magic);

    JS_DefinePropertyValueStr(ctx, ctor, "prototype", JS_DupValue(ctx, proto), 0);
    JS_SetModuleExport(ctx, m, d.export_name ? d.export_name : d.name, ctor);
  }
}

/**
 * @brief Looks up the class name of the constructor with this magic value.
 *
 * @param defs   the constructor table
 * @param magic  value the C function received
 * @return the name (use it for `js_set_tostringtag`), or "" when not found
 */
template <size_t N>
const char*
ctor_name(const ctor_def (&defs)[N], int magic) {
  for(const ctor_def& d : defs)
    if(d.magic == magic)
      return d.name;

  return "";
}

/**
 * @brief Several classes whose opaque has the same type.
 *
 * Replaces a chain of `JS_GetOpaque(v, id)` calls, one per class. Each class
 * registers its id in its init function:
 *
 * @code
 * static qjsx::class_group<JsAudioNode> audio_nodes;
 * // in the init of each node class:
 * audio_nodes.add(js_gainnode_class_id);
 * // at a use:
 * JsAudioNode* w = audio_nodes.get(argv[0]);
 * @endcode
 *
 * @tparam T  the opaque type shared by all member classes
 */
template <class T> class class_group {
  std::vector<JSClassID> ids_;

public:
  /**
   * @brief Adds a class to the group.
   *
   * @param id  class id, valid after `JS_NewClassID`
   */
  void
  add(JSClassID id) {
    ids_.push_back(id);
  }

  /**
   * @brief Gets the opaque of v when v is an instance of a member class.
   *
   * @param v  any value; a non-object never matches
   * @return the opaque, or nullptr (no exception thrown)
   */
  T*
  get(JSValueConst v) const {
    JSClassID id = JS_GetClassID(v);

    if(id && std::find(ids_.begin(), ids_.end(), id) != ids_.end())
      return static_cast<T*>(JS_GetOpaque(v, id));

    return nullptr;
  }

  /**
   * @brief Like get(), throwing a TypeError when v is not a member.
   *
   * @param ctx       JS context
   * @param v         any value; a non-object never matches
   * @param expected  completes the message "expected <expected>"
   * @return the opaque, or nullptr with the TypeError thrown
   */
  T*
  get2(JSContext* ctx, JSValueConst v, const char* expected) const {
    T* p = get(v);

    if(!p)
      JS_ThrowTypeError(ctx, "expected %s", expected);

    return p;
  }
};

/**
 * @brief The opaque of an object that must be of the given class.
 *
 * @param ctx  JS context
 * @param v    the object, normally `this_val`
 * @param id   class id the object must have
 * @return the opaque, or nullptr with a TypeError thrown when v is of
 *         another class
 */
template <class T>
T*
self(JSContext* ctx, JSValueConst v, JSClassID id) {
  return static_cast<T*>(JS_GetOpaque2(ctx, v, id));
}

/**
 * @brief The opaque of v when v is of the given class, without an exception.
 *
 * Use it to ask "is this argument a X?"; use self() when it must be.
 *
 * @return the opaque, or nullptr
 */
template <class T>
T*
opaque(JSValueConst v, JSClassID id) {
  return static_cast<T*>(JS_GetOpaque(v, id));
}

/**
 * @brief Casts a `std::shared_ptr` to a derived type, or throws.
 *
 * @code
 * auto osc = qjsx::cast_shared<lab::OscillatorNode>(ctx, w->node, "OscillatorNode");
 *
 * if(!osc)
 *   return JS_EXCEPTION;
 * @endcode
 *
 * @param ctx   JS context
 * @param p     pointer to the base class
 * @param name  class name for the message; "an" is used before a vowel
 *              (A, E, I, O, U), else "a"
 * @return the cast pointer, or an empty one with an InternalError
 *         "not a(n) <name>" thrown
 */
template <class Leaf, class Base>
std::shared_ptr<Leaf>
cast_shared(JSContext* ctx, const std::shared_ptr<Base>& p, const char* name) {
  std::shared_ptr<Leaf> r = std::dynamic_pointer_cast<Leaf>(p);

  if(!r) {
    char c = name[0];

    if(c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U')
      JS_ThrowInternalError(ctx, "not an %s", name);
    else
      JS_ThrowInternalError(ctx, "not a %s", name);
  }

  return r;
}

/**
 * @brief Reads one member of an options object.
 *
 * Unlike `property()`, it tells an absent member from a failed conversion.
 *
 * @code
 * float frequency = 0;
 * int r;
 *
 * if((r = qjsx::option(ctx, argv[1], "frequency", frequency)) < 0)
 *   return JS_EXCEPTION;
 * if(r)
 *   osc->frequency()->setValue(frequency);
 * @endcode
 *
 * A `const char*` result must be given back with `release()` when 1 is
 * returned.
 *
 * @param ctx   JS context
 * @param opts  the options object; a non-object counts as "no options"
 * @param name  member name
 * @param out   receives the converted value
 * @return 0 when opts is not an object or the member is undefined (out is
 *         untouched), 1 when out was set, -1 with the exception thrown
 */
template <class T>
int
option(JSContext* ctx, JSValueConst opts, const char* name, T& out) {
  JSValue v;
  int ret = 0;

  if(!JS_IsObject(opts))
    return 0;

  v = JS_GetPropertyStr(ctx, opts, name);

  if(JS_IsException(v))
    return -1;

  if(!JS_IsUndefined(v))
    ret = element<T>::from_js(ctx, &out, v) ? -1 : 1;

  JS_FreeValue(ctx, v);
  return ret;
}

} // namespace qjsx

#endif /* defined(QUICKJS_CPP_HPP) */
