#ifndef CPPUTILS_H
#define CPPUTILS_H

#include <quickjs.h>
#include <cutils.h>
#include <vector>
#include <string>

#include "LabSound/LabSound.h"

/**
 * \defgroup from_js<Output> shims
 * @{
 */
template<class Output>
inline Output
from_js(JSContext* ctx, JSValueConst val) {
  throw std::exception();
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

template<class Output>
inline Output
from_js(JSValueConst val) {
  throw std::exception();
}

template<template<class> class Container, class Input>
inline Container<Input> 
from_js(JSContext* ctx, JSValueConst val) {
  JSValue lprop = JS_GetPropertyStr(ctx, val, "length");
  uint32_t i, len = from_js<uint32_t>(lprop);
  JS_FreeValue(ctx, lprop);
  Container<Input> ret(len);

  for(i = 0; i < len; ++i) {
    JSValue elem = JS_GetPropertyUint32(ctx, val, i);

    ret[i] = from_js<Input>(ctx, elem);

    JS_FreeValue(ctx, elem);
  }

  return ret;
}

template<>
inline JSObject*
from_js<JSObject*>(JSValueConst val) {
  return JS_VALUE_GET_TAG(val) == JS_TAG_OBJECT ? JS_VALUE_GET_OBJ(val) : nullptr;
}

template<>
inline lab::Channel
from_js<lab::Channel>(JSContext* ctx, JSValueConst value) {
  int32_t ret = -1;
  const char* s;
  static const char* const names[] = {"Left", "Right", "Center", "LFE", "SurroundLeft", "SurroundRight", "BackLeft", "BackRight"};

  if((s = JS_ToCString(ctx, value))) {
    if(!strcasecmp(s, "first")) {
      ret = 0;
    } else if(!strcasecmp(s, "mono")) {
      ret = 2;
    } else
      for(size_t i = 0; i < countof(names); ++i)
        if(!strcasecmp(s, names[i])) {
          ret = i;
          break;
        }
    JS_FreeCString(ctx, s);
  }
  if(ret == -1)
    JS_ToInt32(ctx, &ret, value);
  return lab::Channel(ret + int(lab::Channel::Left));
}

template<>
inline lab::ChannelInterpretation
from_js<lab::ChannelInterpretation>(JSContext* ctx, JSValueConst value) {
  int32_t ret = -1;
  const char* s;
  static const char* const names[] = {"Speakers", "Discrete"};

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
  return lab::ChannelInterpretation(ret + int(lab::ChannelInterpretation::Speakers));
}

template<>
inline lab::ChannelCountMode
from_js<lab::ChannelCountMode>(JSContext* ctx, JSValueConst value) {
  int32_t ret = -1;
  const char* s;
  static const char* const names[] = {"Max", "ClampedMax", "Explicit", "End"};

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
  return lab::ChannelCountMode(ret + int(lab::ChannelCountMode::Max));
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

template<class Input>
inline JSValue
to_js(Input num) {
  return JS_EXCEPTION;
}

template<>
inline JSValue
to_js<JSObject*>(JSObject* obj) {
  return JS_MKPTR(JS_TAG_OBJECT, obj);
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
