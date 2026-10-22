#ifndef JS_UTILS_H
#define JS_UTILS_H

#include <quickjs.h>
#include <cutils.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  JSValue length = JS_GetPropertyStr(ctx, array, "length");

  if(JS_IsNumber(length))
    JS_ToInt64(ctx, &len, length);

  JS_FreeValue(ctx, length);
  return len;
}

static inline BOOL
js_is_arraylike(JSContext* ctx, JSValueConst array) {
  JSValue length = JS_GetPropertyStr(ctx, array, "length");
  BOOL ret = JS_IsNumber(length);
  JS_FreeValue(ctx, length);
  return ret;
}

JSValue js_global_get_str(JSContext*, const char*);
JSAtom js_symbol_atom(JSContext*, const char*);
JSAtom js_symbol_tostringtag(JSContext*);
JSAtom js_symbol_iterator(JSContext*);
void js_set_tostringtag(JSContext*, JSValueConst, const char* name);

static inline void*
js_object_pointer(JSValueConst obj) {
  return JS_IsObject(obj) ? JS_VALUE_GET_PTR(obj) : 0;
}

static inline void*
js_object_pointer2(JSContext* ctx, JSValueConst obj) {
  return js_object_pointer(JS_DupValue(ctx, obj));
}

static inline JSValue
js_object(void* ptr) {
  return ptr ? JS_MKPTR(JS_TAG_OBJECT, ptr) : JS_NULL;
}

/* create a new object of class id for a constructor.
 * its prototype is new_target.prototype so that subclasses work,
 * else the registered class prototype.
 *
 * returns object or JS_EXCEPTION - the caller frees what it owns. */
JSValue js_object_from_target(JSContext* ctx, JSValueConst new_target, JSClassID id);

typedef struct Iterator {
  void* obj;
  JSAtom next, done, value;
} Iterator;

BOOL js_is_iterable(JSContext* ctx, JSValueConst object);
BOOL js_is_iterator(JSContext* ctx, JSValueConst object);

JSValue js_iterable_iterator(JSContext* ctx, JSValueConst object);

BOOL js_iterator_init(JSContext* ctx, Iterator*, JSValueConst);
JSValue js_iterator_next(JSContext* ctx, Iterator*, BOOL* done);

static inline void
js_iterator_free(JSContext* ctx, Iterator* iter) {
  JS_FreeValue(ctx, js_object(iter->obj));
  JS_FreeAtom(ctx, iter->next);
  JS_FreeAtom(ctx, iter->done);
  JS_FreeAtom(ctx, iter->value);
}

#ifdef __cplusplus
}
#endif

#endif /* defined(JS_UTILS_H) */
