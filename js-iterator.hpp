#ifndef JS_ITERATOR_HPP
#define JS_ITERATOR_HPP

#include "js-utils.hpp"
#include <iterator>
#include <memory>

namespace qjsx {

class iterator {
public:
  /* iterator traits for C++20 / C++17 compatibility */
  using iterator_concept = std::forward_iterator_tag;
  using iterator_category = std::forward_iterator_tag;
  using value_type = JSValue;
  using difference_type = std::ptrdiff_t;
  using pointer = const JSValue*;
  using reference = JSValue;

  /* default constructor represents the end iterator / sentinel state */
  iterator() = default;

  /* construct from a QuickJS iterable or iterator object */
  iterator(JSContext* ctx, JSValueConst obj) : state_(std::make_shared<State>(ctx, obj)) {}

  /* dereference operators */
  reference
  operator*() const {
    return JS_DupValue(state_->ctx, state_->current_value);
  }

  /* pre-increment */
  iterator&
  operator++() {
    state_->advance();
    return *this;
  }

  /* post-increment */
  iterator
  operator++(int) {
    iterator tmp = *this;
    ++(*this);
    return tmp;
  }

  /* equality comparison */
  friend bool
  operator==(const iterator& a, const iterator& b) {
    bool a_end = !a.state_ || a.state_->done;
    bool b_end = !b.state_ || b.state_->done;

    /* if both are at the end (or default constructed), they are equal */
    if(a_end && b_end)
      return true;

    /* otherwise, compare their underlying states */
    return a.state_ == b.state_;
  }

  friend bool
  operator!=(const iterator& a, const iterator& b) {
    return !(a == b);
  }

private:
  struct State {
    JSContext* ctx;
    BOOL done;
    JSValue current_value;
    Iterator iter;

    State(JSContext* c, JSValueConst val) : ctx(c), done(FALSE), current_value(JS_UNDEFINED) {
      if(!js_iterator_init(ctx, &iter, val)) {
        done = TRUE;
      } else {
        advance();
      }
    }

    ~State() {
      JS_FreeValue(ctx, current_value);
      js_iterator_free(ctx, &iter);
    }

    void
    advance() {
      JS_FreeValue(ctx, current_value);
      current_value = js_iterator_next(ctx, &iter, &done);
      if(JS_IsException(current_value))
        done = TRUE;
    }
  };

  std::shared_ptr<State> state_;
};

class back_insert_iterator {
public:
  /* output iterator required traits */
  using iterator_category = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  /* constructor taking the context and the target JS array */
  back_insert_iterator(JSContext* ctx, JSValueConst array) : ctx(ctx), obj(js_object_pointer(array)) {}

  /* assignment operator handles the insertion when writing to the iterator (e.g., *it = val) */
  back_insert_iterator&
  operator=(JSValue value) {
    /* query the current array length using js_array_length from js-utils.h */
    int64_t len;

    if((len = js_array_length(ctx, js_object(obj))) < 0)
      len = 0; /* fallback if it's not strictly array-like or empty */

    /* insert the value at the next index using QuickJS C API
       JS_SetPropertyInt64 appends/sets the element at index
       len and consumes the reference. */
    JS_SetPropertyUint32(ctx, js_object(obj), len, value);
    return *this;
  }

  /* output iterators must return *this on dereference and increment operations */
  back_insert_iterator&
  operator*() {
    return *this;
  }
  back_insert_iterator&
  operator++() {
    return *this;
  }
  back_insert_iterator
  operator++(int) {
    return *this;
  }

private:
  JSContext* ctx;
  void* obj;
};
} /* namespace qjsx */

#endif /* JS_ITERATOR_HPP */