#ifndef CPPUTILS_H
#define CPPUTILS_H

#include <quickjs.h>
#include <cutils.h>

/**
 * \defgroup from_js<Output> shims
 * @{
 */
template <class Output>
inline Output
from_js(JSContext* ctx, JSValueConst val) {
}

template <>
inline int64_t
from_js<int64_t>(JSContext* ctx, JSValueConst val) {
  int64_t i;
  JS_ToInt64(ctx, &i, val);
  return i;
}

template <>
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
template <class Input>
inline JSValue
to_js(JSContext* ctx, Input num) {
  return JS_EXCEPTION;
}

template <>
inline JSValue
to_js<const std::string&>(JSContext* ctx, const std::string& str) {
  return JS_NewString(ctx, str.c_str());
}

template <>
inline JSValue
to_js<int64_t>(JSContext* ctx, int64_t num) {
  return JS_NewInt64(ctx, num);
}

template <>
inline JSValue
to_js<int32_t>(JSContext* ctx, int32_t num) {
  return JS_NewInt32(ctx, num);
}

template <>
inline JSValue
to_js<uint32_t>(JSContext* ctx, uint32_t num) {
  return JS_NewUint32(ctx, num);
}

template <template <class> class Container, class Input>
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


/**
 * \class JSClassID container
 */
struct ClassId {
  ClassId() : cid() {}
  ClassId(JSClassID _id) : cid(_id) {}

  /* clang-format off */

  JSClassID* operator&() { return &cid; };
  JSClassID const* operator&() const { return &cid; };

  operator JSClassID&() { return cid; };
  operator JSClassID const&() const { return cid; };

  template<class T = void> T* opaque(JSValueConst val) const { return JS_GetOpaque(val, cid); }
  template<class T = void> T* opaque(JSContext *ctx, JSValueConst val) const { return JS_GetOpaque2(ctx, val, cid); }

  /* clang-format on */

private:
  JSClassID cid;
};

#endif /* defined(CPPUTILS_H) */
