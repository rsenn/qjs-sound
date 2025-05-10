#ifndef CPPUTILS_H
#define CPPUTILS_H

#include <quickjs.h>
#include <cutils.h>
#include <vector>
#include <string>

/**
 * \defgroup from_js<Output> shims
 * @{
 */
template<class Output>
inline Output
from_js(JSContext* ctx, JSValueConst val) {}

template<>
inline int64_t
from_js<int64_t>(JSContext* ctx, JSValueConst val) {
  int64_t i;
  JS_ToInt64(ctx, &i, val);
  return i;
}

template<>
inline double
from_js<double>(JSContext* ctx, JSValueConst val) {
  double d;
  JS_ToFloat64(ctx, &d, val);
  return d;
}

template<>
inline std::string
from_js<std::string>(JSContext* ctx, JSValueConst val) {
  const char* s = JS_ToCString(ctx, val);
  std::string ret(s);
  JS_FreeCString(ctx, s);
  return ret;
}
/**
 * @}
 */

/**
 * \defgroup to_js<Input> shims
 * @{
 */
template<class Input>
inline JSValue
to_js(JSContext* ctx, Input num) {
  return JS_EXCEPTION;
}

template<>
inline JSValue
to_js<const std::string&>(JSContext* ctx, const std::string& str) {
  return JS_NewString(ctx, str.c_str());
}

template<>
inline JSValue
to_js<int64_t>(JSContext* ctx, int64_t num) {
  return JS_NewInt64(ctx, num);
}

template<>
inline JSValue
to_js<int32_t>(JSContext* ctx, int32_t num) {
  return JS_NewInt32(ctx, num);
}

template<>
inline JSValue
to_js<uint32_t>(JSContext* ctx, uint32_t num) {
  return JS_NewUint32(ctx, num);
}

template<template<class> class Container, class Input>
inline JSValue
to_js(JSContext* ctx, const Container<Input>& container) {
  uint32_t i = 0;
  JSValue ret = JS_NewArray(ctx);

  for(const Input& val : container)
    JS_SetPropertyUint32(ctx, ret, i++, to_js(ctx, val));

  return ret;
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

/*! \class ClassId
 *  \brief JSClassID container
 */
struct ClassId {
  ClassId&
  init() {
    JS_NewClassID(&cid);
    return *this;
  }

  ClassId&
  inherit(ClassId& p) {
    for(ClassId* ptr = parent = &p; ptr; ptr = ptr->parent)
      ptr->descendants.push_back(this);

    return *this;
  }

  /* clang-format off */

  operator JSClassID&() { return cid; };
  operator JSClassID const&() const { return cid; };

  /* clang-format on */

  template<class Fn>
  void
  recurse(const Fn& fn) const {
    fn(cid);

    for(ClassId* cidp : descendants)
      cidp->recurse(fn);
  }

  template<class T = void>
  T*
  opaque(JSValueConst val) const {
    T* ptr;

    if((ptr = static_cast<T*>(JS_GetOpaque(val, cid))))
      return ptr;

    for(ClassId* cidp : descendants)
      if((ptr = cidp->opaque<T>(val)))
        return ptr;

    return nullptr;
  }

  template<class T = void>
  T*
  opaque(JSContext* ctx, JSValueConst val) const {
    T* ptr = opaque<T>(val);

    if(ptr == nullptr) {
      std::string ids = "";

      recurse([&ids](JSClassID id) {
        ids.append(",");
        ids.append(std::to_string(id));
      });

      JS_ThrowTypeError(ctx, "Object is not of class id %s", ids.c_str() + 1);
    }

    return ptr;
  }

private:
  JSClassID cid;
  ClassId* parent;
  std::vector<ClassId*> descendants;
};

template<class T, class U = std::shared_ptr<lab::AudioContext>> struct ClassPtr : std::shared_ptr<T> {
  typedef std::shared_ptr<T> base_type;
  typedef U value_type;

  ClassPtr(const base_type& b, const value_type& v) : base_type(b), value(v) {}

  std::shared_ptr<T>
  get() const {
    return base_type::get();
  }

  value_type value;
};

/*template<class T> struct ClassPtr : std::shared_ptr<T> {
  typedef std::shared_ptr<T> base_type;
  typedef std::shared_ptr<lab::AudioContext> context_type;

  ClassPtr(const base_type& an, const context_type& ac) : base_type(an), context(ac) {}

  context_type context;
};
*/

template<class T>
static inline T*
js_malloc(JSContext* ctx) {
  return static_cast<T*>(js_malloc(ctx, sizeof(T)));
}

#endif /* defined(CPPUTILS_H) */
