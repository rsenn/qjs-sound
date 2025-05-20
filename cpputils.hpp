#ifndef CPPUTILS_HPP
#define CPPUTILS_HPP

#include <quickjs.h>
#include <cutils.h>
#include <vector>
#include <string>
#include <ranges>
#include <iterator>
#include <type_traits>

#include "LabSound/LabSound.h"

// template<class T> using PointerRange = std::pair<T*, T*>;
template<class T> using PointerRange = std::ranges::subrange<T*, T*>;
typedef JSObject* JSObjectPtr;

template<class T>
static inline T*
js_alloc(JSContext* ctx, size_t n = 1) {
  return static_cast<T*>(js_malloc(ctx, n * sizeof(T)));
}

template<class T>
static inline bool
js_alloc(JSContext* ctx, T*& ref, size_t n = 1) {
  ref = js_alloc<T>(ctx, n);
  return ref != nullptr;
}

/*template<class T>
static inline void
js_delete(JSContext* ctx, T**& ref) {
  for(size_t i=0; ref[i]; ++i)
   js_delete<T*>(ctx, ref[i]);

    js_free(ctx,  ref);
  ref=nullptr;
}*/

template<class T>
static inline void
js_delete(JSContext* ctx, T*& ref) {
  if(ref)
    js_free(ctx, ref);
  ref = nullptr;
}

template<class T>
inline void
js_delete(JSContext* ctx, T**& ref) {
  for(size_t i = 0; ref[i]; ++i) {
    js_free(ctx, ref[i]);
    ref[i] = nullptr;
  }

  js_free(ctx, ref);
  ref = nullptr;
}

template<>
inline void
js_delete<const char>(JSContext* ctx, const char*& ref) {
  char*& ptr = *const_cast<char**>(&ref);

  js_delete(ctx, ptr);
}

struct ArrayBufferData {
  size_t len;
  uint8_t* ptr;
};

static inline int64_t
js_array_length(JSContext* ctx, JSValueConst arr) {
  int64_t len = -1;
  JSValue lprop = JS_GetPropertyStr(ctx, arr, "length");

  if(!JS_IsException(lprop))
    JS_ToInt64(ctx, &len, lprop);

  JS_FreeValue(ctx, lprop);
  return len;
}

template<class R>
inline R*
js_array_get(JSContext* ctx, JSValueConst arr, R fn(JSContext*, JSValueConst), bool free = false) {
  uint32_t i, len = js_array_length(ctx, arr);
  R* ret;

  if((ret = js_alloc<R>(ctx, len + 1))) {
    for(i = 0; i < len; ++i) {
      JSValue v = JS_GetPropertyUint32(ctx, arr, i);
      new(&ret[i]) R(fn(ctx, v));
      JS_FreeValue(ctx, v);
    }
    new(&ret[i]) R{nullptr, nullptr};
  }

  if(free)
    JS_FreeValue(ctx, arr);

  return ret;
}

template<class R, class Iterator>
inline JSValue
js_array_build(JSContext* ctx, Iterator it, Iterator end, JSValue fn(JSContext*, R*)) {
  JSValue ret = JS_NewArray(ctx);
  uint32_t i = 0;

  while(it != end) {
    JSValue v = fn(ctx, &(*it));
    JS_SetPropertyUint32(ctx, ret, i++, v);

    ++it;
  }

  return ret;
}

/**
 * \defgroup from_js<Output> shims
 * @{
 */
template<class T>
T
from_js(JSContext* ctx, JSValueConst val) {
  /*T v;
  from_js(ctx, val, v);
  return v;*/
  static_assert(false, "from_js template");
}

template<>
inline int32_t
from_js<int32_t>(JSContext* ctx, JSValueConst val) {
  int32_t i;
  JS_ToInt32(ctx, &i, val);
  return i;
}

template<>
inline uint32_t
from_js<uint32_t>(JSContext* ctx, JSValueConst val) {
  uint32_t u;
  JS_ToUint32(ctx, &u, val);
  return u;
}

/*template<>
inline unsigned int
from_js<unsigned int>(JSContext* ctx, JSValueConst val) {
  return from_js<uint32_t>(ctx, val);
}*/

template<>
inline int64_t
from_js<int64_t>(JSContext* ctx, JSValueConst val) {
  int64_t i;
  JS_ToInt64(ctx, &i, val);
  return i;
}

/*template<>
inline uint64_t
from_js<uint64_t>(JSContext* ctx, JSValueConst val) {
  uint64_t i;
  JS_ToIndex(ctx, &i, val);
  return i;
}*/

template<>
inline double
from_js<double>(JSContext* ctx, JSValueConst val) {
  double d;
  JS_ToFloat64(ctx, &d, val);
  return d;
}

template<>
inline float
from_js<float>(JSContext* ctx, JSValueConst val) {
  return from_js<double>(ctx, val);
}

template<>
inline std::string
from_js<std::string>(JSContext* ctx, JSValueConst val) {
  const char* s;
  std::string ret;

  if((s = JS_ToCString(ctx, val))) {
    ret = s;
    JS_FreeCString(ctx, s);
  }

  return ret;
}

template<>
inline char*
from_js<char*>(JSContext* ctx, JSValueConst val) {
  const char* s;
  char* ret;

  if((s = JS_ToCString(ctx, val))) {
    ret = js_strdup(ctx, s);
    JS_FreeCString(ctx, s);
  }
  return ret;
}

template<>
inline char**
from_js<char**>(JSContext* ctx, JSValueConst val) {
  int64_t len = js_array_length(ctx, val);
  char** ret;

  if((ret = js_alloc<char*>(ctx, (len + 1)))) {

    for(int64_t i = 0; i < len; ++i)
      ret[i] = from_js<char*>(ctx, val);

    ret[len] = nullptr;
  }

  return ret;
}

template<>
inline const char*
from_js<const char*>(JSContext* ctx, JSValueConst val) {
  return JS_ToCString(ctx, val);
}

template<>
inline const char* const*
from_js<const char* const*>(JSContext* ctx, JSValueConst val) {
  uint32_t len = js_array_length(ctx, val);
  const char** ret = static_cast<const char**>(js_malloc(ctx, sizeof(const char*) * (len + 1)));

  for(uint32_t i = 0; i < len; ++i)
    ret[i] = from_js<char*>(ctx, JS_GetPropertyUint32(ctx, val, i));

  ret[len] = nullptr;
  return ret;
}

template<class T>
inline T
from_js(JSContext* ctx, JSAtom atom) {
  JSValue value = JS_AtomToValue(ctx, atom);
  T ret = from_js<T>(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

template<class T>
T
from_js(JSValueConst val) {
  static_assert(false, "from_js<>(val)");
}

template<template<class> class Container, class Input>
inline Container<Input>
from_js(JSContext* ctx, JSValueConst val) {
  uint32_t len = js_array_length(ctx, val);
  Container<Input> ret;
  // auto ins = std::back_inserter(ret);

  for(uint32_t i = 0; i < len; ++i) {
    JSValue elem = JS_GetPropertyUint32(ctx, val, i);
    Input value;
    from_js_free(ctx, elem, value);
    ret.push_back(value);
  }

  return ret;
}

template<>
inline unsigned long
from_js<unsigned long>(JSContext* ctx, JSValueConst val) {
  return from_js<uint32_t>(ctx, val);
}

template<>
inline JSObject*
from_js<JSObject*>(JSValueConst val) {
  return JS_VALUE_GET_TAG(val) == JS_TAG_OBJECT ? JS_VALUE_GET_OBJ(val) : nullptr;
}

template<>
inline JSObject*
from_js<JSObject*>(JSContext* ctx, JSValueConst val) {
  JS_DupValue(ctx, val);
  return from_js<JSObject*>(val);
}

template<class T, class Output = bool>
inline Output
from_js(JSContext* ctx, JSValueConst val, T& ref) {
  static_assert(false, "from_js<Output, T>(ctx, val, ref)");
}

template<template<class> class Container, class Input>
inline uint32_t
from_js(JSContext* ctx, JSValueConst val, Container<Input>& ret) {
  uint32_t i, len = js_array_length(ctx, val);

  for(i = 0; i < len; ++i) {
    JSValue elem = JS_GetPropertyUint32(ctx, val, i);
    Input value = from_js_free<Input>(ctx, elem);
    ret.push_back(value);
  }

  return i;
}

template<>
inline bool
from_js<double>(JSContext* ctx, JSValueConst val, double& ref) {
  return !JS_ToFloat64(ctx, &ref, val);
}

template<>
inline bool
from_js<int32_t>(JSContext* ctx, JSValueConst val, int32_t& ref) {
  return !JS_ToInt32(ctx, &ref, val);
}

template<>
inline bool
from_js<uint32_t>(JSContext* ctx, JSValueConst val, uint32_t& ref) {
  return !JS_ToUint32(ctx, &ref, val);
}

template<>
inline bool
from_js<int64_t>(JSContext* ctx, JSValueConst val, int64_t& ref) {
  return !JS_ToInt64(ctx, &ref, val);
}

template<>
inline bool
from_js<uint64_t>(JSContext* ctx, JSValueConst val, uint64_t& ref) {
  return !JS_ToIndex(ctx, &ref, val);
}

template<>
inline void
from_js<BOOL, void>(JSContext* ctx, JSValueConst val, BOOL& ref) {
  ref = JS_ToBool(ctx, val);
}

template<>
inline bool
from_js<const char*, bool>(JSContext* ctx, JSValueConst val, const char*& ref) {
  ref = JS_ToCString(ctx, val);
  return ref != nullptr;
}

template<>
inline bool
from_js<char*, bool>(JSContext* ctx, JSValueConst val, char*& ref) {
  const char* s;

  if((s = JS_ToCString(ctx, val))) {
    ref = js_strdup(ctx, s);
    JS_FreeCString(ctx, s);
    return true;
  }

  return false;
}

template<>
inline int64_t
from_js<const char* const*, int64_t>(JSContext* ctx, JSValueConst val, const char* const*& ref) {
  int64_t i, len = js_array_length(ctx, val);
  const char** arr = js_alloc<const char*>(ctx, len + 1);

  for(i = 0; i < len; ++i) {
    JSValue item = JS_GetPropertyUint32(ctx, val, i);
    from_js<const char*, bool>(ctx, item, *(const char**)&arr[i]);
    JS_FreeValue(ctx, item);
  }

  arr[i] = nullptr;
  return i;
}

template<>
inline int64_t
from_js<char**, int64_t>(JSContext* ctx, JSValueConst val, char**& ref) {
  int64_t i, len = js_array_length(ctx, val);
  ref = nullptr;

  if((ref = js_alloc<char*>(ctx, len + 1))) {
    for(i = 0; i < len; ++i) {
      JSValue item = JS_GetPropertyUint32(ctx, val, i);
      from_js(ctx, item, ref[i]);
      JS_FreeValue(ctx, item);
    }

    ref[i] = nullptr;
  }
  return i;
}

template<>
inline bool
from_js<ArrayBufferData, bool>(JSContext* ctx, JSValueConst val, ArrayBufferData& data) {
  return !!(data.ptr = JS_GetArrayBuffer(ctx, &data.len, val));
}

template<>
inline PointerRange<uint8_t>
from_js<PointerRange<uint8_t>>(JSContext* ctx, JSValueConst val) {
  ArrayBufferData data;
  from_js<ArrayBufferData>(ctx, val, data);
  return PointerRange<uint8_t>(data.ptr, data.ptr ? data.ptr + data.len : nullptr);
}

template<class T>
inline T
from_js_property(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value = JS_GetPropertyStr(ctx, obj, prop);
  T v = from_js<T>(ctx, value);
  JS_FreeValue(ctx, value);
  return v;
}

template<class T>
inline T
from_js_property(JSContext* ctx, JSValueConst obj, const char* prop, T defaultValue) {
  JSAtom atom = JS_NewAtom(ctx, prop);
  if(!JS_HasProperty(ctx, obj, atom)) {
    JS_FreeAtom(ctx, atom);
    return defaultValue;
  }
  JSValue value = JS_GetProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  T ret;
  from_js<T>(ctx, value, ret);
  JS_FreeValue(ctx, value);
  return ret;
}

/**
 * @}
 */

template<class T, class R = bool>
inline R
from_js_free(JSContext* ctx, JSValue val, T& v) {
  from_js<T, R>(ctx, val, v);
  JS_FreeValue(ctx, val);
  return v;
}

template<class T>
inline T
from_js_free(JSContext* ctx, JSValue val) {
  T v;
  from_js_free<T>(ctx, val, v);
  return v;
}

/**
 * \defgroup to_js<Input> shims
 * @{
 */
template<class T>
inline JSValue
to_js(JSContext* ctx, const T& num) {
  static_assert(false, "to_js<T>(ctx, T arg)");
}

template<>
inline JSValue
to_js<const char*>(JSContext* ctx, const char* const& str) {
  return JS_NewString(ctx, str);
}

template<>
inline JSValue
to_js<std::string>(JSContext* ctx, const std::string& str) {
  return to_js<const char*>(ctx, str.c_str());
}

template<>
inline JSValue
to_js<int64_t>(JSContext* ctx, const int64_t& num) {
  return JS_NewInt64(ctx, num);
}

template<>
inline JSValue
to_js<int32_t>(JSContext* ctx, const int32_t& num) {
  return JS_NewInt32(ctx, num);
}

template<>
inline JSValue
to_js<uint32_t>(JSContext* ctx, const uint32_t& num) {
  return JS_NewUint32(ctx, num);
}

template<>
inline JSValue
to_js<double>(JSContext* ctx, const double& d) {
  return JS_NewFloat64(ctx, d);
}

template<>
inline JSValue
to_js<float>(JSContext* ctx, const float& f) {
  return JS_NewFloat64(ctx, f);
}

template<>
inline JSValue
to_js<const char* const*>(JSContext* ctx, const char* const* const& values) {
  JSValue ret = JS_NewArray(ctx);

  for(uint32_t i = 0; values[i]; ++i)
    JS_SetPropertyUint32(ctx, ret, i, to_js<const char*>(ctx, values[i]));

  return ret;
}

template<class Range, /*class V =*/typename std::indirectly_readable_traits<std::remove_cvref_t<std::ranges::iterator_t<Range>>>::value_type>
inline JSValue
to_js(JSContext* ctx, const Range& container) {
  typedef std::iter_value_t<std::ranges::iterator_t<Range>> value_type;
  uint32_t i = 0;
  JSValue ret = JS_NewArray(ctx);

  for(auto val : container)
    JS_SetPropertyUint32(ctx, ret, i++, to_js<value_type>(ctx, val));

  return ret;
}

template<class Iterator>
inline JSValue
to_js(JSContext* ctx, const Iterator& start, const Iterator& end) {
  uint32_t i = 0;
  JSValue ret = JS_NewArray(ctx);

  for(Iterator it = start; it != end; ++it)
    JS_SetPropertyUint32(ctx, ret, i++, to_js(ctx, *it));

  return ret;
}

template<template<class> class Container, class Input>
inline JSValue
to_js(JSContext* ctx, const Container<Input>& container, const typename Container<Input>::value_type* tn = 0) {
  uint32_t i = 0;
  JSValue ret = JS_NewArray(ctx);

  for(const Input& val : container)
    JS_SetPropertyUint32(ctx, ret, i++, to_js<Input>(ctx, val));

  return ret;
}

template<class T>
inline JSValue
to_js(T arg) {
  static_assert(false, "to_js<T>(T arg)");
}

template<>
inline JSValueConst
to_js<JSObjectPtr>(JSObjectPtr obj) {
  return obj ? JS_MKPTR(JS_TAG_OBJECT, obj) : JS_NULL;
}

template<>
inline JSValueConst
to_js<JSObjectPtr>(JSContext* ctx, const JSObjectPtr& obj) {
  return JS_DupValue(ctx, to_js<JSObject*>(obj));
}

template<class T>
inline void
to_js_property(JSContext* ctx, JSValueConst obj, const char* prop, T val) {
  JSValue value = to_js<T>(ctx, val);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

template<template<class> class Container, class Input>
inline void
to_js_property(JSContext* ctx, JSValueConst obj, const char* prop, const Container<Input>& val) {
  auto s = val.begin(), e = val.end();
  JSValue value = to_js(ctx, s, e);
  JS_SetPropertyStr(ctx, obj, prop, value);
}
/**
 * @}
 */

BOOL
js_has_property(JSContext* ctx, JSValueConst obj, const char* name) {
  JSAtom atom = JS_NewAtom(ctx, name);
  BOOL ret = JS_HasProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

/*! \class ClassWrapper
 *  \brief JSClassID container
 */
class ClassWrapper {
private:
  JSClassID cid{0};
  ClassWrapper *next{nullptr}, *parent{nullptr};
  std::vector<ClassWrapper*> descendants;

  static ClassWrapper *wrappers, **wrapper_ptr;

public:
  mutable JSValue ctor{JS_UNDEFINED}, proto{JS_UNDEFINED};
  JSClassDef* class_def{nullptr};

  bool
  initialized() const {
    return cid != 0;
  }

  const char*
  name() const {
    return class_def ? class_def->class_name : nullptr;
  }

  ClassWrapper() {
    *wrapper_ptr = this;
    wrapper_ptr = &next;
  }
  ~ClassWrapper() {}

  ClassWrapper&
  init() {
    if(!initialized()) {
      JS_NewClassID(&cid);
    }

    return *this;
  }

  ClassWrapper&
  init(JSContext* ctx, JSClassDef* cdef) {
    if(!initialized()) {
      init();

      if((class_def = cdef))
        JS_NewClass(JS_GetRuntime(ctx), cid, class_def);
    }

    return *this;
  }

  ClassWrapper&
  inherit(ClassWrapper& p) {
    for(ClassWrapper* ptr = parent = &p; ptr; ptr = ptr->parent)
      ptr->descendants.push_back(this);

    return *this;
  }

  void
  setProtoConstructor(JSContext* ctx) const {
    if(JS_IsObject(proto) || JS_IsNull(proto)) {
      JS_SetClassProto(ctx, cid, proto);

      if(JS_IsObject(ctor) && !JS_IsNull(proto))
        JS_SetConstructor(ctx, ctor, proto);
    }
  }

  void
  setModuleExport(JSContext* ctx, JSModuleDef* m) const {
    JS_SetModuleExport(ctx, m, name(), ctor);
  }

  static ClassWrapper*
  get(JSClassID cid) {
    for(ClassWrapper* ptr = ClassWrapper::wrappers; ptr; ptr = ptr->next)
      if(ptr->cid == cid)
        return ptr;

    return nullptr;
  }

  /* clang-format off */

  operator JSClassID&() { return cid; };
  operator JSClassID const&() const { return cid; };

  /* clang-format on */

  void
  constructor(JSContext* ctx, JSCFunction& func, int length, int magic) {
    ctor = JS_NewCFunction2(ctx, func, name(), length, JS_CFUNC_constructor, magic);
  }

  /*template<class RetType>
  auto
  recurse(const std::function<RetType(const ClassWrapper&)>& fn) const -> RetType {
    RetType r;
    if((r = fn(*this)))
      return r;
    for(ClassWrapper* cidp : descendants)
      if((r = cidp->recurse(fn)))
        return r;
    return r;
  }*/

  template<class T = void>
  T*
  opaque(JSValueConst val) const {
    T* ptr;

    if((ptr = static_cast<T*>(JS_GetOpaque(val, cid))))
      return ptr;

    for(ClassWrapper* cidp : descendants)
      if((ptr = static_cast<T*>(JS_GetOpaque(val, *cidp))))
        return ptr;

    return nullptr;
  }

  template<class T = void>
  T*
  opaque(JSContext* ctx, JSValueConst val) const {
    T* ptr;
    std::string ids;

    if((ptr = static_cast<T*>(JS_GetOpaque2(ctx, val, cid))))
      return ptr;

    if(!JS_IsObject(JS_GetException(ctx)))
      return nullptr;

    if(class_def && class_def->class_name)
      ids.append(class_def->class_name);
    else
      ids.append(std::to_string(cid));

    for(ClassWrapper* cidp : descendants) {
      if((ptr = static_cast<T*>(JS_GetOpaque2(ctx, val, *cidp))))
        return ptr;

      if(!JS_IsObject(JS_GetException(ctx)))
        return nullptr;

      ids.append(", ");

      if(cidp->name())
        ids.append(cidp->name());
      else
        ids.append(std::to_string(cidp->cid));
    }

    JS_ThrowTypeError(ctx, "Object is not of class id %s", ids.c_str());
    return nullptr;
  }

  template<class T = void>
  bool
  opaque(JSContext* ctx, JSValueConst val, T*& ref) {
    ref = opaque<T>(ctx, val);
    return ref != nullptr;
  }

  template<class T = void>
  bool
  opaque(JSValueConst val, T*& ref) {
    ref = opaque<T>(val);
    return ref != nullptr;
  }
};

template<class T> struct ClassObjectMap {
  typedef std::shared_ptr<T> base_type;
  typedef std::weak_ptr<T> weak_type;
  typedef std::map<weak_type, JSObjectPtr> map_type;

  static void
  set(const base_type& ptr, JSObjectPtr obj) {
    weak_type weak(ptr);
    object_map[weak] = obj;
  }

  static JSObjectPtr
  get(const base_type& ptr) {
    weak_type weak(ptr);
    auto const it = object_map.find(weak);

    if(it != object_map.end())
      return it->second;

    return nullptr;
  }

  static JSValueConst
  getValue(const base_type& ptr) {
    JSObject* obj;

    if((obj = get(ptr)))
      return to_js(obj);

    return JS_NULL;
  }

  static void
  remove(JSObjectPtr obj) {
    std::erase_if(object_map, [obj](const auto& item) -> bool {
      auto const& [key, value] = item;
      return value == obj;
    });
  }

  static void
  remove(JSContext* ctx) {
    std::erase_if(object_map, [ctx](const auto& item) -> bool {
      auto const& [key, value] = item;

      if(key.expired()) {
        JS_FreeValue(ctx, to_js<JSObject*>(value));
        return true;
      }

      return false;
    });
  }

private:
  static std::map<weak_type, JSObjectPtr> object_map;
};

template<class T, class U> struct ClassPtr : public std::shared_ptr<T> {
  typedef std::shared_ptr<T> base_type;
  typedef U value_type;

  ClassPtr() : base_type(), value() {}
  ClassPtr(const base_type& b, const value_type& v) : base_type(b), value(v) {}

  T*
  get() const {
    return base_type::get();
  }

  value_type value;
};

template<class T> struct ClassPtr<T, void> : public std::shared_ptr<T> {
  typedef std::shared_ptr<T> base_type;

  ClassPtr(const base_type& b) : base_type(b) {}

  T*
  get() const {
    return base_type::get();
  }
};

template<class T, class R = T> class NonOwningRef {
public:
  NonOwningRef() = delete;
  NonOwningRef(const NonOwningRef& other) = delete;

  operator bool() const { return m_ref.has_value(); }

protected:
  NonOwningRef(T value) : m_ref(value) {}

  /* clang-format off */
  void clear() { m_ref.reset(); }

  R constValue() const { return m_ref.value_or(R()); }
  /* clang-format on */

  std::optional<T> m_ref{std::nullopt};
};

template<> class NonOwningRef<JSObject*, JSValue> {
public:
  NonOwningRef() = delete;
  NonOwningRef(const NonOwningRef& other) = delete;

  operator bool() const { return m_obj != nullptr; }

  JSValueConst
  constValue() const {
    return m_obj ? JS_MKPTR(JS_TAG_OBJECT, m_obj) : JS_UNDEFINED;
  }

protected:
  NonOwningRef(JSValueConst value) : m_obj(JS_IsObject(value) ? JS_VALUE_GET_OBJ(value) : nullptr) {}

  /* clang-format off */
  void clear() { m_obj = nullptr; }
  /* clang-format on */

  JSValue
  replace(JSValue val) {
    JSValue ret = m_obj ? constValue() : JS_UNDEFINED;

    clear();

    if(!JS_IsObject(val))
      throw std::runtime_error("NonOwningRef<JSObject*, JSValue>::replace is not an object");

    m_obj = JS_VALUE_GET_OBJ(val);
    return ret;
  }

  JSObject* m_obj{nullptr};
};

template<class T, class R = T> class OwningRef : public NonOwningRef<T, R> {
public:
  typedef NonOwningRef<T, R> base_type;

  OwningRef() = delete;
  OwningRef(const OwningRef& other) : m_ctx(other.m_ctx), NonOwningRef<T, R>(*other.m_val) { reference(); }
  OwningRef(OwningRef&& other) : m_ctx(std::move(other.m_ctx)), NonOwningRef<T, R>(std::move(*other.m_val)) {}
  OwningRef(JSContext* ctx, R val, bool dupValue = true) : m_ctx(ctx), NonOwningRef<T, R>(val) { reference(dupValue); }
  ~OwningRef() { release(); }

  JSContext*
  context() const {
    return m_ctx;
  }

  R
  value() const {
    if(!base_type::operator bool())
      return JS_ThrowTypeError(m_ctx, "OwningRef member m_ref has no value");

    return JS_DupValue(m_ctx, base_type::constValue());
  }

  OwningRef&
  operator=(const R& val) {
    setRef(val);
    return *this;
  }

  OwningRef&
  operator=(const OwningRef& other) {
    if(other) {
      if(m_ctx == nullptr)
        m_ctx = JS_DupContext(other.context());

      setRef(other.constValue());
    }
    return *this;
  }

protected:
  static OwningRef<T, R>
  property(JSContext* ctx, JSValueConst obj, const char* name) {
    return OwningRef<T, R>(ctx, JS_GetPropertyStr(ctx, obj, name), false);
  }

  void
  setRef(JSValueConst val) {
    JSValue old = base_type::replace(JS_DupValue(m_ctx, val));
    JS_FreeValue(m_ctx, old);
  }

  void
  reference(bool dupValue = true) const {
    JS_DupContext(m_ctx);

    if(dupValue)
      if(base_type::operator bool())
        JS_DupValue(m_ctx, base_type::constValue());
  }

  void
  release() {
    if(m_ctx == nullptr)
      throw std::runtime_error("OwningRef no m_ctx");

    if(base_type::operator bool()) {
      JS_FreeValue(m_ctx, base_type::constValue());
      base_type::clear();
    }
    JS_FreeContext(m_ctx);
  }

  JSContext* m_ctx;
};

// typedef OwningRef<JSObject*, JSValue> ObjectRef;

class ObjectRef : public OwningRef<JSObject*, JSValue> {
public:
  typedef OwningRef<JSObject*, JSValue> base_type;

  ObjectRef() = delete;
  ObjectRef(JSContext* ctx, JSValueConst obj, const char* prop) : base_type(ctx, JS_GetPropertyStr(ctx, obj, prop), false) {}
  ObjectRef(JSContext* ctx, JSValueConst val, bool dupValue = true) : base_type(ctx, val, dupValue) {}
};

template<class R = ObjectRef> class ArrayBufferView : protected R, public std::ranges::view_interface<ArrayBufferView<R>> {
public:
  typedef PointerRange<uint8_t> pointer_range;

  ArrayBufferView() = delete;
  ArrayBufferView(JSContext* ctx, JSValueConst buf) : R(ctx, buf) {}

  uint8_t*
  begin() const {
    return from_js<uint8_t*>(R::m_ctx, R::constValue());
  }
  uint8_t*
  end() const {
    pointer_range range(from_js(R::m_ctx, R::constValue()));
    return range.end();
  }
};

struct TypedArray {
  TypedArray(JSContext* ctx, JSValueConst obj) : buffer(ctx, JS_GetTypedArrayBuffer(ctx, obj, &byte_offset, &byte_length, &bytes_per_element)) {}

  size_t byte_offset, byte_length, bytes_per_element;
  ArrayBufferView<ObjectRef> buffer;
};

template<class T> class TypedArrayView : public std::ranges::view_interface<TypedArrayView<T>>, protected TypedArray {

  TypedArrayView() = delete;
  TypedArrayView(JSContext* ctx, JSValueConst buf) : TypedArray(ctx, buf) {}

  T*
  begin() const {
    return reinterpret_cast<T*>(buffer.begin() + byte_offset);
  }

  T*
  end() const {
    return reinterpret_cast<T*>(buffer.begin() + byte_offset + byte_length);
  }
};

template<class T>
std::weak_ptr<T>
to_weak(const std::shared_ptr<T>& ptr) {
  return ptr;
}

template<class T>
std::weak_ptr<T>
to_weak(T* ptr) {
  std::shared_ptr<T> shared(ptr);
  return shared;
}

template<class T>
std::shared_ptr<T>
to_shared(T* ptr) {
  std::shared_ptr<T> shared(ptr);
  return shared;
}

template<class T>
std::shared_ptr<T>
to_shared(const std::weak_ptr<T>& ptr) {
  return ptr;
}

template<class T>
T*
to_pointer(const std::weak_ptr<T>& ptr) {
  std::shared_ptr<T> shared(ptr);
  return shared.get();
}

template<class T>
T*
to_pointer(const std::shared_ptr<T>& ptr) {
  return ptr.get();
}

template<class T>
JSObject*
get_object(const std::shared_ptr<T>& ptr) {
  return ClassObjectMap<T>::get(ptr);
}

template<class T>
JSObject*
get_object(const std::weak_ptr<T>& ptr) {
  std::shared_ptr<T> sh(ptr);
  return ClassObjectMap<T>::get(sh);
}

template<class T>
JSObject*
get_object(T* ptr) {
  std::shared_ptr<T> sh(ptr);
  return get_object(sh);
}

template<class T>
JSValueConst
get_value(const T& ptr) {
  return to_js(get_object(ptr));
}

template<class T>
ptrdiff_t
size(T const* ptr) {
  T const* start = ptr;
  if(start)
    while(*ptr)
      ++ptr;
  return ptr - start;
}

template<>
ptrdiff_t
size<lab::AudioParamDescriptor>(lab::AudioParamDescriptor const* ptr) {
  lab::AudioParamDescriptor const* start = ptr;
  if(start)
    while(ptr->name)
      ++ptr;
  return ptr - start;
}

template<class T>
static auto
range_from(const T* const* ptr) {
  return std::ranges::subrange<decltype(ptr)>{ptr, ptr + size(ptr)};
}

template<class T>
static auto
range_from(const std::vector<T>& vec) {
  return std::ranges::subrange<typename std::vector<T>::iterator>{vec.begin(), vec.end()};
}

template<class T> struct enumeration_type { static_assert(false, "struct enumeration_type<T>"); };

template<class Range>
static inline int32_t
find_enumeration(const Range& range, const char* str) {
  auto it = std::ranges::find_if(range, [str](const char* enval) -> bool { return !strcasecmp(str, enval); });

  if(it != range.end())
    return std::distance(range.begin(), it);

  return -1;
}

template<class Enum>
static inline Enum
find_enumeration(const char* str) {
  return Enum(find_enumeration(range_from(enumeration_type<Enum>::enums), str));
}

template<class Enum>
static inline Enum
find_enumeration(JSContext* ctx, JSValueConst value) {
  const char* str;
  int32_t r = -1;

  if((str = JS_ToCString(ctx, value))) {
    r = int32_t(find_enumeration<Enum>(str));
    JS_FreeCString(ctx, str);
  }

  if(r == -1)
    JS_ToInt32(ctx, &r, value);

  return Enum(r);
}

template<class Enum>
static inline Enum
find_enumeration_free(JSContext* ctx, JSValue value) {
  Enum r = find_enumeration<Enum>(ctx, value);
  JS_FreeValue(ctx, value);
  return r;
}

template<class T, typename = std::enable_if_t<std::is_enum<T>::value>>
inline T
from_js(JSContext* ctx, JSValueConst value) {
  int32_t ret = -1;
  static const char* const* names = enumeration_type<T>::enums;
  const char* s;

  if((s = JS_ToCString(ctx, value))) {
    for(size_t i = 0; i < countof(names); ++i)
      if(!strcasecmp(s, names[i])) {
        ret = i;
        break;
      }
    JS_FreeCString(ctx, s);
  }
  if(ret == -1)
    JS_ToInt32(ctx, &ret, value);

  return T(ret);
}

static inline int
js_copy(JSContext* ctx, JSValueConst dest, JSValueConst src) {
  JSPropertyEnum* ptab;
  uint32_t i, len;

  if(JS_GetOwnPropertyNames(ctx, &ptab, &len, src, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;

  for(i = 0; i < len; i++) {
    JSValue prop = JS_GetProperty(ctx, src, ptab[i].atom);
    JS_SetProperty(ctx, dest, ptab[i].atom, prop);
  }

  js_free(ctx, ptab);

  return i;
}

static JSValue
js_float32array_constructor(JSContext* ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue f32arr = JS_GetPropertyStr(ctx, global, "Float32Array");
  JS_FreeValue(ctx, global);
  return f32arr;
}

static JSValue
js_float32array_new(JSContext* ctx, uint8_t* buf, size_t len, JSFreeArrayBufferDataFunc* free_func = nullptr, void* opaque = nullptr) {
  JSValue f32arr = js_float32array_constructor(ctx);
  JSValue args[] = {
      free_func ? JS_NewArrayBuffer(ctx, buf, len, free_func, opaque, FALSE) : JS_NewArrayBufferCopy(ctx, buf, len),
      JS_NewUint32(ctx, 0),
      JS_NewUint32(ctx, len / sizeof(float)),
  };

  JSValue ret = JS_CallConstructor(ctx, f32arr, countof(args), args);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
  JS_FreeValue(ctx, f32arr);

  return ret;
}

#endif // defined(CPPUTILS_HPP)
