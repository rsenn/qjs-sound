# Recipes: transforming call sites for the js-utils.hpp helpers

Part 2 of 3 of the refactoring documentation:

| File | Says |
|---|---|
| `js-utils-helpers-plan.md` | **what to do in which order**, gates, decisions, tracking |
| `js-utils-helpers-recipes.md` (this file) | **how to transform each pattern**: helper code, before and after, traps |
| `js-utils-helpers-sites.md` | **where**: every call site, with line numbers and `grep` commands |

Everything in this file was compiled and run on 2026-11-15: the helper code
in section 1 passes the self-test of appendix B under `g++ -std=c++17`
(with AddressSanitizer and UBSan) and parses with `clang++ -std=c++17`; every
"before" excerpt is copied from commit `d530d27`; every claim about current
behaviour was reproduced with the scripts named next to it.

## 0. The loop for one function

Do this for each row of `js-utils-helpers-sites.md`, in plan order. One
function (or one `case` of a big `switch`) at a time, never a whole file.

1. Open the function. Read it once completely.
2. Apply the recipes in this order, because later ones read the output of
   earlier ones: R6 (opaque lookups) → R1 (arguments) → R2 (type ladders) →
   R7 (object creation) → R8 (options) → R3/R4/R5 (class level, only at class
   registration code).
3. Build: `cmake --build build/$(cc -dumpmachine) 2>&1 | grep -E 'error|warning'`. Fix before going on.
4. Add or update the error-test case for what you touched (section 9), run the driver loop.
5. Run the headless tests (plan section 2, gate G2).
6. If a `BUGS` entry is now fixed, delete the entry (project rule), in the same commit.
7. Tick the row in the tracking table of the plan; commit (no `Co-Authored-By` trailer in this project).

A step that does not build is reverted with `git checkout -- <file>` and redone
smaller; do not stack unbuilt edits.

## 1. The helper code

**Status: applied to `js-utils.hpp` on 2026-11-16** (575 -> 1029 lines, all new code documented in
Doxygen format: `@brief`, `@param`, `@tparam`, `@return`, `@code`). The listings below are a copy of the
header for reading next to the recipes; the header is the source of truth. Where they are in the file:
the `<algorithm>` and `<type_traits>` includes at the top, R0 before `element<int16_t>`, everything else
before the closing `} // namespace qjsx`. Checked on 2026-11-16: the self-test of appendix B passes
against the real header under `g++ -std=c++17` with AddressSanitizer and UBSan, `clang++ -std=c++17`
reports no error, `doxygen` reports no warning for the new blocks, and `qjs-sndobj`, `qjs-stk` and
`qjs-labsound` rebuild without a warning.

To read the generated reference: `doxygen -g /tmp/Doxyfile` then set `INPUT = js-utils.hpp`,
`EXTRACT_ALL = YES`, `ENABLE_PREPROCESSING = NO`, `GENERATE_HTML = YES`.

Language level: C++17 (fold expressions, class-id references as template arguments). The compiler
default here is C++20; the Termux clang default is C++17, so a build there is the real check.

### R0 `element<int64_t>`

```cpp
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
```

Why: `HarmTable`'s length is read with `JS_ToInt64` (`quickjs-sndobj.cpp:307`),
the only 64-bit conversion in the three bindings. It has no `kind` member on
purpose: `TypedArrayKind` has no 64-bit integer, so `get_array<int64_t>` must
not compile.

### R1 to R8 helpers

```cpp
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
```

Facts about these helpers that a caller must know, each tested by the self-test:

- after a failed conversion the output variables are **unspecified**
  (QuickJS stores NaN into a `double` on failure); return at once
- `args`: an argument that is absent or `undefined` keeps the preset; `null` converts (to 0)
- `dispatch`: the generic lambda is instantiated for **every** listed type,
  so a member that exists on only some of them is a compile error (R2)
- `class_group::get` accepts non-objects and returns `nullptr` (no exception)
- `cast_shared` builds today's messages: "not an OscillatorNode", "not a GainNode"

## R1 `args` and `present`: argument reading

**Recognise it**

```sh
grep -nE '^\s+JS_To(Float64|Int32|Uint32|Int64)\(' quickjs-sndobj.cpp   # statement position: result ignored
grep -nE 'if\(argc > [0-9]+\)' quickjs-sndobj.cpp                       # the guard that precedes it
```

**Becomes**: one `qjsx::args` (or `args_from` with the index of the first argument)
call that converts every number in one go, and `return JS_EXCEPTION` when it fails.

**Before → after, a constructor** (`js_sndobjgenerator_constructor`, `case INSTANCE_OSCILI`, lines 517-549)

```cpp
case INSTANCE_OSCILI: {
  Table* table = nullptr;

  if(argc > 0 && !(table = js_sndobjtable_arg(ctx, argv[0])))
    goto argfail;

  double fr = 440, amp = 1;

  if(argc > 1)
    JS_ToFloat64(ctx, &fr, argv[1]);

  if(argc > 2)
    JS_ToFloat64(ctx, &amp, argv[2]);

  SndObj *freqMod = nullptr, *ampMod = nullptr;

  if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]) && !(freqMod = js_sndobj_arg(ctx, argv[3])))
    goto argfail;

  if(argc > 4 && !JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4]) && !(ampMod = js_sndobj_arg(ctx, argv[4])))
    goto argfail;

  int32_t vecsize = DEF_VECSIZE;
  double sr = DEF_SR;

  if(argc > 5)
    JS_ToInt32(ctx, &vecsize, argv[5]);

  if(argc > 6)
    JS_ToFloat64(ctx, &sr, argv[6]);

  g->reset(new Oscili(table, (float)fr, (float)amp, freqMod, ampMod, vecsize, (float)sr));
  break;
}
```

```cpp
case INSTANCE_OSCILI: {
  Table* table = nullptr;
  float fr = 440, amp = 1, sr = DEF_SR;
  int32_t vecsize = DEF_VECSIZE;
  SndObj *freqMod = nullptr, *ampMod = nullptr;

  /* argv[0] is required: js_sndobjtable_arg throws a TypeError for undefined */
  if(!(table = js_sndobjtable_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED)))
    goto argfail;

  if(qjsx::args_from(ctx, 1, argc, argv, fr, amp))
    goto argfail;

  if(qjsx::present(argc, argv, 3) && !(freqMod = js_sndobj_arg(ctx, argv[3])))
    goto argfail;

  if(qjsx::present(argc, argv, 4) && !(ampMod = js_sndobj_arg(ctx, argv[4])))
    goto argfail;

  if(qjsx::args_from(ctx, 5, argc, argv, vecsize, sr))
    goto argfail;

  g->reset(new Oscili(table, fr, amp, freqMod, ampMod, vecsize, sr));
  break;
}
```

What changed and why it is safe:

- `fr`, `amp`, `sr` are `float`, the type the `Oscili` constructor takes, so the four `(float)` casts go away
- object arguments (the modulators at 3 and 4) sit between numbers: two `args_from` calls with their start index
- the first line also fixes BUGS `sndobj-oscili-null-table-segfault`: `new Oscili()` used to pass a NULL table (decision D3)
- `argfail` frees `g` (the existing label); a failed `args_from` must go there, not `return`

**Before → after, a method** (`js_sndobjeffect_method`, `EMETHOD_SET_ADSR`, lines 822-838)

```cpp
case EMETHOD_SET_ADSR: {
  double a = 0, d = 0, s = 0, r = 0;

  JS_ToFloat64(ctx, &a, argv[0]);
  JS_ToFloat64(ctx, &d, argv[1]);
  JS_ToFloat64(ctx, &s, argv[2]);
  JS_ToFloat64(ctx, &r, argv[3]);

  if(auto* p = dynamic_cast<ADSR*>(obj))
    p->SetADSR((float)a, (float)d, (float)s, (float)r);

  return JS_UNDEFINED;
}
```

```cpp
case EMETHOD_SET_ADSR: {
  float a = 0, d = 0, s = 0, r = 0;

  if(qjsx::args(ctx, argc, argv, a, d, s, r))
    return JS_EXCEPTION;

  if(auto* p = dynamic_cast<ADSR*>(obj))
    p->SetADSR(a, d, s, r);

  return JS_UNDEFINED;
}
```

**Before → after, a default that depends on the class** (`js_stkeffect_constructor`, lines 1077-1145)

```cpp
StkEffectPtr e;
double arg = 0;
if(argc > 0)
  JS_ToFloat64(ctx, &arg, argv[0]);

try {
  switch(magic) {
    case INSTANCE_CHORUS: {
      e = std::make_shared<stk::Chorus>(argc > 0 ? arg : 6000);
      break;
    }
    case INSTANCE_LENTPITSHIFT: {
      int32_t tmax = stk::RT_BUFFER_SIZE;
      if(argc > 1)
        JS_ToInt32(ctx, &tmax, argv[1]);
      e = std::make_shared<stk::LentPitShift>(argc > 0 ? arg : 1.0, tmax);
      break;
    }
```

```cpp
StkEffectPtr e;

try {
  switch(magic) {
    case INSTANCE_CHORUS: {
      double delay = 6000;

      if(qjsx::args(ctx, argc, argv, delay))
        return JS_EXCEPTION;

      e = std::make_shared<stk::Chorus>(delay);
      break;
    }
    case INSTANCE_LENTPITSHIFT: {
      double shift = 1.0;
      int32_t tmax = stk::RT_BUFFER_SIZE;

      if(qjsx::args(ctx, argc, argv, shift, tmax))
        return JS_EXCEPTION;

      e = std::make_shared<stk::LentPitShift>(shift, tmax);
      break;
    }
```

The shared `double arg` read at the top is removed; each case owns its
preset. This is the fix for stk `new Echo(Symbol())` and `new Chorus(Symbol())`:
they used to construct with `0` after the failed conversion and crash inside STK.

**Traps**

1. **Presets are the defaults.** The old guard `argc > N ? value : DEFAULT`
   becomes "preset = DEFAULT". An explicitly passed `undefined` used to give NaN
   (`JS_ToFloat64(undefined)`); now it keeps the preset (decision D3).
2. **Use the native parameter type** for the variable (`float` for SndObj, `double`
   for STK's `StkFloat`, `int16_t` for `short`, `uint32_t` for `unsigned int`, `int64_t` for `long`),
   then delete the casts. `float` through `element<float>` rounds the same way as the cast did.
3. **`argc` that selects a code path stays.** `if(argc == 0) { default constructor } else { ... }`
   (`HarmTable`, line 300) keeps its test; call `args` inside the branch.
4. **Required arguments** that were read unguarded (`argv[0]` of `setADSR`) are fine: QuickJS pads
   `argv` up to the `length` of `JS_CFUNC_MAGIC_DEF("setADSR", 4, ...)`, so `args` sees them as `undefined`
   and keeps the preset. If the declared length is smaller than the highest `argv[i]` the code touches,
   that is an out-of-bounds read; make the length match (check each `JS_CFUNC_MAGIC_DEF` row of the function you edit).
5. **Strings are not supported** (`static_assert`): keep `JS_ToCString` / `JS_FreeCString`, or use `qjsx::with`.
6. **Resources acquired before the call** (the `g` block of the sndobj constructors, a `std::vector`)
   must be released on the failure path. In a `try` block, `return JS_EXCEPTION` is fine.
7. **`JS_ToBool` sites** (12 in the three files) never fail; leave them.

**Check**: `grep -nE '^\s+JS_To(Float64|Int32|Uint32|Int64)\(' <file>` prints fewer lines after each
function; the matching `errors-test` cases (section 9) go from FAIL to PASS.

## R2 `dispatch` and `require`: type ladders

**Recognise it**: `grep -n 'dynamic_cast<' quickjs-sndobj.cpp quickjs-stk.cpp`; the two kinds are
listed in sites section 2 (multi-type, single-type).

**Multi-type ladder → `dispatch`** (`js_sndobjgenerator_method`, `GMETHOD_SET_FREQ`, lines 392-410)

```cpp
case GMETHOD_SET_FREQ: {
  double fr = 0;
  JS_ToFloat64(ctx, &fr, argv[0]);
  bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
  SndObj* mod = nullptr;

  if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
    return JS_EXCEPTION;

  if(auto* p = dynamic_cast<Oscil*>(obj))
    p->SetFreq((float)fr, mod);
  else if(auto* p = dynamic_cast<Buzz*>(obj))
    p->SetFreq((float)fr, mod);
  else if(auto* p = dynamic_cast<Randh*>(obj))
    p->SetFreq((float)fr, mod);

  js_sndobj_retain(ctx, this_val, "__freqMod", hasMod ? argv[1] : JS_UNDEFINED);
  return JS_UNDEFINED;
}
```

```cpp
case GMETHOD_SET_FREQ: {
  float fr = 0;
  SndObj* mod = nullptr;

  if(qjsx::args(ctx, argc, argv, fr))
    return JS_EXCEPTION;

  bool hasMod = qjsx::present(argc, argv, 1);

  if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
    return JS_EXCEPTION;

  qjsx::dispatch<Oscil, Buzz, Randh>(obj, [&](auto* p) { p->SetFreq(fr, mod); });

  js_sndobj_retain(ctx, this_val, "__freqMod", hasMod ? argv[1] : JS_UNDEFINED);
  return JS_UNDEFINED;
}
```

**A ladder whose branches differ** (`js_stkeffect_method`, scalar `tick`, from line 1203): `FreeVerb` takes two inputs,
the other seven one. Two calls, joined with `||` so the second only runs when the first did not match:

```cpp
qjsx::dispatch<stk::FreeVerb>(eff, [&](auto* p) { p->tick(in1, in2); }) ||
    qjsx::dispatch<stk::JCRev, stk::PRCRev, stk::NRev, stk::Chorus, stk::Echo, stk::PitShift, stk::LentPitShift>(
        eff, [&](auto* p) { p->tick(in1); });
```

The frame overload (from line 1174) is the same call for all eight types:
`qjsx::dispatch<stk::FreeVerb, stk::JCRev, stk::PRCRev, stk::NRev, stk::Chorus, stk::Echo, stk::PitShift, stk::LentPitShift>(eff, [&](auto* p) { p->tick(*a->get(), channel); });`

The generator scalar `tick` (lines 748-770): `Granulate::tick(channel)`, the ten others `tick()`:

```cpp
double sample = 0;

qjsx::dispatch<stk::Granulate>(g, [&](auto* p) { sample = p->tick(channel); }) ||
    qjsx::dispatch<stk::ADSR, stk::Asymp, stk::BlitSaw, stk::BlitSquare, stk::Blit, stk::Envelope, stk::Modulate,
                   stk::Noise, stk::SineWave, stk::SingWave>(g, [&](auto* p) { sample = p->tick(); });
```

**Single-type check → keep, or `require`** (decision D1)

Twenty of the sndobj checks and the two stk `Cubic` ones test a single leaf type. Two choices:

- D1 = keep the silent no-op: **leave these lines alone**. `dispatch` with one type is not shorter than the cast.
- D1 = throw: replace the check with `require`, which returns the pointer or throws:

```cpp
case EMETHOD_SUSTAIN: {
  if(auto* p = dynamic_cast<ADSR*>(obj))
    p->Sustain();

  return JS_UNDEFINED;
}
```

```cpp
case EMETHOD_SUSTAIN: {
  ADSR* p = qjsx::require<ADSR>(ctx, obj, "sustain needs an ADSR");

  if(!p)
    return JS_EXCEPTION;

  p->Sustain();
  return JS_UNDEFINED;
}
```

Message form: `<method> needs a(n) <Class>`. The methods are registered on the shared effect prototype,
so `new Reson(...).sustain()` currently does nothing and reports success.

**Traps**

1. **Compile errors name the type that lacks the member.** The generic lambda is compiled for every `T` in the list;
   split the list (as for `FreeVerb` above) or use `if constexpr (std::is_same<std::remove_pointer_t<decltype(p)>, stk::FreeVerb>::value)`.
2. **List order = old ladder order** (first match wins). It matters where one listed class derives from another
   (`Oscili` derives from `Oscil`; the order `Oscil, Buzz, Randh` is kept).
3. **Capture by reference** (`[&]`); the lambda writes the result into a local (`sample`).
4. The result of `dispatch` is a `bool`; ignoring it keeps today's silent no-op, `if(!dispatch...) return JS_ThrowTypeError(...)` is D1 = throw.
5. `dynamic_cast` needs RTTI; the build already has it (the current code uses it).

**Check**: `grep -c 'dynamic_cast<' <file>` drops; `errors-test` unchanged (these sites do not convert numbers).
A behaviour check for the D1 = throw sites: calling `new Reson(...).sustain()` must throw a `TypeError`.

## R3 `finalize` and `finalize_delete`

**Recognise it**: sites section 3. Two shapes only:

```cpp
static void
js_stkfilter_finalizer(JSRuntime* rt, JSValue val) {
  StkFilterPtr* f;

  if((f = static_cast<StkFilterPtr*>(JS_GetOpaque(val, js_stkfilter_class_id)))) {
    f->~StkFilterPtr();
    js_free_rt(rt, f);
  }
}

static JSClassDef js_stkfilter_class = {
  .class_name = "Filter",
  .finalizer = js_stkfilter_finalizer,
};
```

becomes (delete the function, change one line):

```cpp
static JSClassDef js_stkfilter_class = {
  .class_name = "Filter",
  .finalizer = qjsx::finalize<StkFilterPtr, js_stkfilter_class_id>,
};
```

The four stk classes made with `new` (`MidiFileIn`, `RtMidiIn`, `RtMidiOut`, `RtAudio`; verified: `quickjs-stk.cpp:2777, 2948, 3139, 3307`)
use `qjsx::finalize_delete<RtAudio, js_rtaudio_class_id>`.

labsound: the 13 forwarders `js_xxx_finalizer → js_audionode_finalize_with(rt, val, id)` become
`qjsx::finalize<JsAudioNode, js_xxx_class_id>`; then delete `js_audionode_finalize_with`.

**Traps**

1. The class id must be a variable with static storage (`static JSClassID js_x_class_id;`); it is, in all three files.
   It must be **declared before** the `JSClassDef` that names it (it is, at the top of each file).
2. Delete the old function, or the compiler warns `unused function`.
3. `js_audiocontext_finalizer` (labsound, line 906) also erases the context from `offline_state`;
   keep that function as it is.
4. Read the old body before replacing it: the plan section 2 checklist says "pure" for every row that is safe.
   A body with any extra statement stays hand-written.

**Check**: build; `G4` (leak check, section 10) still shows equal object counts; `grep -c '_finalizer(JSRuntime' <file>` drops to the
number of special cases.

## R4 `ctor_def` tables (stk, sndobj)

**Recognise it**: sites section 4 has the table for every family, ready to paste. Three places per family are
edited, in this order, building after each:

(a) **Define the table** right after the family's `INSTANCE_*` enum (it must precede the constructor function).
Paste from sites section 4. Example, the filters (`quickjs-stk.cpp`, enum at 835-849, constructor at 852):

```cpp
static const qjsx::ctor_def stkfilter_ctors[] = {
  { "BiQuad", 0, INSTANCE_BIQUAD },
  { "DelayA", 0, INSTANCE_DELAY_A },
  /* ... the other 11 rows from sites section 4.2 ... */
};
```

(b) **The constructor**: replace the `toStringTag` array (it starts at line 982 with `"BiQuad"`) with a lookup.

```cpp
  js_set_tostringtag(
      ctx, obj,
      ((const char*[]){
          "BiQuad",
          "Delay",
          /* ... */
      })[magic]
  );
```

```cpp
  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkfilter_ctors, magic));
```

This is the edit that fixes BUGS `stk-tostringtag-array-out-of-enum-order` for generators (`quickjs-stk.cpp:698`)
and effects (`:1133`): the name now comes from the same row that creates the class, so it cannot drift.

(c) **Declare** in `js_stk_add_exports` (line 3902): delete the 13 `JS_AddModuleExport(ctx, m, "BiQuad")` ... `"TwoZero"`
lines (3903-3915) and put one line in their place:

```cpp
  qjsx::declare_ctors(ctx, m, stkfilter_ctors);
```

(d) **Bind** in `js_stk_init` inside the `if(m) { ... }` block (lines 3553-3589): delete the 13
`ctor = JS_NewCFunction2(...)` / `JS_DefinePropertyValueStr(...)` / `JS_SetModuleExport(...)` triples and put:

```cpp
    qjsx::bind_ctors(ctx, m, js_stkfilter_constructor, stkfilter_proto, stkfilter_ctors);
```

**Traps**

1. **Declare and bind use the same table**, so the names always agree. Never leave a hand-written
   `JS_AddModuleExport` for a name that `declare_ctors` also declares: QuickJS aborts on a duplicate export.
2. **The `if(m)` guard stays** around `bind_ctors`. The only caller of `js_stk_init` is `JS_NewCModule` (line 3993),
   so `m` is never null today and the guards could go, but removing them is a separate change.
3. **`prototype` property (decision D2)**: `bind_ctors` defines `ctor.prototype` on every constructor.
   The filters already have it; generators, effects, FM, instruments, functions and I/O do not,
   which is why `class X extends stk.ADSR {}` throws "parent prototype must be an object or null" today
   (reproduced; the `errors-test` cases `stk/class extends ADSR` etc.). After this step they pass.
4. **`SingWave` (decision D5)**: the table row is `{ "SingWave", 0, INSTANCE_SING_WAVE }`. To keep the old name
   as an alias add a second row `{ "SingWave", 0, INSTANCE_SING_WAVE, "StkSingWave" }`; both constructors then share `magic` and `ctor_name` returns the first row's name.
5. The cast `(JSCFunction*)` disappears: `bind_ctors` uses `JS_NewCFunctionMagic`, whose function type is the
   constructor's own signature `(JSContext*, JSValueConst, int, JSValueConst*, int)`.
6. The exports that are **not** constructors of a family (`Stk`, `StkFrames`, `Effect`, `FM`, `Filter`, `Function`,
   `Generator`, `MidiFileIn`, `RtAudio`, `RtMidiIn`, `RtMidiOut`, `Tr909BassDrum`, `Tr909Percussion`, `TwinTDrum`)
   keep their hand-written declare and bind lines.
7. sndobj has the same three places (declare list in `js_sndobj_add_exports`, line 1400; bind at lines 1370-1392) and the
   same three tables; its `toStringTag` arrays already agree with the enums.

**Check, per family** (save before, compare after; both runs must print the same):

```sh
cat > /tmp/exports.js <<'EOF'
const m = await import(scriptArgs[1]);
const t = o => Object.prototype.toString.call(o).slice(8, -1);
for(const k of Object.keys(m).sort()) console.log(k, typeof m[k]);
EOF
QUICKJS_MODULE_PATH=build/$(cc -dumpmachine) qjsm /tmp/exports.js stk > /tmp/stk.before   # before the edit
QUICKJS_MODULE_PATH=build/$(cc -dumpmachine) qjsm /tmp/exports.js stk > /tmp/stk.after    # after
diff /tmp/stk.before /tmp/stk.after
```

The only allowed difference is the one you chose for `SingWave` (D5). The `errors-test` cases
`stk/toStringTag of ...` and `stk/class extends ...` flip from FAIL to PASS family by family.

## R5 `class_group`: the "any of these classes" probe

**labsound**: `any_audio_node` (`quickjs-labsound.cpp:113-145`) tests 14 class ids one after the other.

Step 1, keep the 10 call sites unchanged by turning the function into a one-liner:

```cpp
static qjsx::class_group<JsAudioNode> audio_nodes;

static JsAudioNode*
any_audio_node(JSValueConst v) {
  return audio_nodes.get(v);
}
```

Step 2, in the init of each node class, right after `JS_NewClassID(&js_xxx_class_id);`, add
`audio_nodes.add(js_xxx_class_id);`. The 14 ids and their order are in sites section 5.1.

Step 3, check that none is missing:

```sh
grep -c 'audio_nodes.add(' quickjs-labsound.cpp     # 14
```

A missing `add` makes `connect()` reject that node type with "destination must be an AudioNode or AudioParam";
the labsound smoke test (plan, phase 4) connects every node type for exactly this reason. This also removes
step 4 of the six-step checklist at the top of `TODO.md`: adding a node class no longer touches the probe's body.

**sndobj**: `js_sndobj_arg` (`quickjs-sndobj.cpp:57-68`) accepts a generator or an effect.

```cpp
static qjsx::class_group<SndObjPtr> sndobj_inputs;

static SndObj*
js_sndobj_arg(JSContext* ctx, JSValueConst v) {
  SndObjPtr* p = sndobj_inputs.get2(ctx, v, "an sndobj generator or effect instance");

  return p ? p->get() : nullptr;
}
```

and `sndobj_inputs.add(js_sndobjgenerator_class_id); sndobj_inputs.add(js_sndobjeffect_class_id);` in the init function
next to the two `JS_NewClassID` calls. The thrown text is unchanged ("expected an sndobj generator or effect instance").
`js_sndobjtable_arg` (one class) becomes `qjsx::self`-free: keep it, or `qjsx::opaque<SndObjTablePtr>(v, js_sndobjtable_class_id)` plus the throw.

**Traps**: `JS_GetClassID` of a non-object is 0 and never matches; the group is a file-scope static, constructed when
the module loads (also for the static build). The probe is O(n) in the number of classes, like the code it replaces.

## R6 `self`, `opaque`, `cast_shared`: opaque lookups

**Recognise it**: `grep -nE 'static_cast<[A-Za-z:<>]+\*>\(\s*JS_GetOpaque2?\(' quickjs-*.cpp`; sites section 5.3 lists every type and class id.

| Old | New |
|---|---|
| `static_cast<T*>(JS_GetOpaque2(ctx, v, id))` (throws on mismatch) | `qjsx::self<T>(ctx, v, id)` |
| `static_cast<T*>(JS_GetOpaque(v, id))` (null on mismatch, no exception) | `qjsx::opaque<T>(v, id)` |
| `std::dynamic_pointer_cast<L>(w->node)` + `JS_ThrowInternalError(ctx, "not a(n) X")` | `qjsx::cast_shared<L>(ctx, w->node, "X")` |

**Before → after, a labsound property getter** (`js_oscillator_get`, from line 1246)

```cpp
JsAudioNode* w = static_cast<JsAudioNode*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id));
if(!w)
  return JS_EXCEPTION;
auto osc = std::dynamic_pointer_cast<lab::OscillatorNode>(w->node);
if(!osc)
  return JS_ThrowInternalError(ctx, "not an OscillatorNode");
```

```cpp
JsAudioNode* w = qjsx::self<JsAudioNode>(ctx, this_val, js_oscillatornode_class_id);
if(!w)
  return JS_EXCEPTION;
auto osc = qjsx::cast_shared<lab::OscillatorNode>(ctx, w->node, "OscillatorNode");
if(!osc)
  return JS_EXCEPTION;
```

Where the function needs only the leaf (not `w`), add this local helper to `quickjs-labsound.cpp` (not to the header: it knows `JsAudioNode`):

```cpp
template <class Leaf>
static std::shared_ptr<Leaf>
node_as(JSContext* ctx, JSValueConst this_val, JSClassID id, const char* name) {
  JsAudioNode* w = qjsx::self<JsAudioNode>(ctx, this_val, id);

  return w ? qjsx::cast_shared<Leaf>(ctx, w->node, name) : nullptr;
}

/* use */
auto osc = node_as<lab::OscillatorNode>(ctx, this_val, js_oscillatornode_class_id, "OscillatorNode");
if(!osc)
  return JS_EXCEPTION;
```

**Soft probe, stk** (`js_stkeffect_method`, `METHOD_EFFECT_TICK`): `argv[0]` may or may not be a frame buffer:

```cpp
StkFramesPtr* a;

if((a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
```

```cpp
if(StkFramesPtr* a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id)) {
```

**Traps**

1. The **name argument is the class name**; the helper adds "a" or "an" (vowel A/E/I/O/U). All 28 messages in sites
   section 5.4 are reproduced exactly except the ones the table marks "different wording" (keep their own `JS_ThrowInternalError`).
2. `self` and `opaque` are the same two functions QuickJS gives you, with the cast folded in; use the **registered**
   class id of the object's own class, not the base's.
3. Do not touch the `js_audionode_finalize_with` / finalizer lookups: they are R3.

**Check**: build; labsound getters and setters unchanged (`webaudio-test`, once P0 is done).

## R7 `wrap`: the constructor tail

**Recognise it**: `grep -n 'new_target, "prototype"' quickjs-*.cpp` (24 hits; sites section 6).

**Target shape** is `js_gain_constructor` (`quickjs-labsound.cpp:1299`), which already does it:

```cpp
JSValue obj = qjsx::wrap(ctx, new_target, js_gainnode_class_id, JsAudioNode{ std::static_pointer_cast<lab::AudioNode>(g), ac });
if(JS_IsException(obj))
  return obj;
anchor_node_in_context(ctx, argv[0], obj);
return obj;
```

**Before → after** (`js_oscillator_constructor`, lines 1225-1236)

```cpp
JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
if(JS_IsException(proto))
  return JS_EXCEPTION;
if(!JS_IsObject(proto)) {
  JS_FreeValue(ctx, proto);
  proto = JS_DupValue(ctx, oscillatornode_proto);
}

JSValue obj = make_audio_node_js(ctx, proto, js_oscillatornode_class_id, std::static_pointer_cast<lab::AudioNode>(osc), ac);
JS_FreeValue(ctx, proto);
anchor_node_in_context(ctx, argv[0], obj);
return obj;
```

```cpp
JSValue obj = qjsx::wrap(ctx, new_target, js_oscillatornode_class_id, JsAudioNode{ std::static_pointer_cast<lab::AudioNode>(osc), ac });
if(JS_IsException(obj))
  return obj;
anchor_node_in_context(ctx, argv[0], obj);
return obj;
```

Two behaviours differ for the better: a **failed** `make_audio_node_js` used to be passed to
`anchor_node_in_context` unchecked; and `proto` is no longer leaked when `JS_GetPropertyStr` returns an exception.

**sndobj / stk constructors that allocate the opaque first** (`js_sndobjgenerator_constructor`, `js_sndobjtable_constructor`,
`js_sndobjeffect_constructor`): build the `std::unique_ptr` into a local, then wrap it. The `js_mallocz` + placement-new block,
the `fail:` and the `argfail:` labels disappear (this fixes BUGS `sndobj-constructors-leak-on-prototype-failure`):

```cpp
SndObjPtr g;                      /* instead of SndObjPtr* g = js_mallocz(...); new(g) SndObjPtr(); */

switch(magic) {
  case INSTANCE_OSCILI: {
    /* ... conversions as in R1; on failure: return JS_EXCEPTION ... */
    g.reset(new Oscili(table, fr, amp, freqMod, ampMod, vecsize, sr));
    break;
  }
}

JSValue obj = qjsx::wrap(ctx, new_target, js_sndobjgenerator_class_id, std::move(g));

if(JS_IsException(obj))
  return obj;

js_set_tostringtag(ctx, obj, qjsx::ctor_name(sndobjgenerator_ctors, magic));
/* the js_sndobj_retain(...) calls stay here */
return obj;
```

**Traps**

1. **The fallback prototype comes from `JS_SetClassProto(ctx, id, proto)`.** If the class never calls it, `wrap`
   falls back to `Object.prototype` and the methods vanish. Before converting a class run
   `grep -n 'JS_SetClassProto(ctx, js_oscillatornode_class_id' quickjs-labsound.cpp`; every class in sites section 6 has one today, but check.
2. `wrap` stores `T{ value }` by brace-initialisation; `JsAudioNode` is an aggregate (`node`, `ctx`), a `unique_ptr` is moved in.
3. Keep `anchor_node_in_context` where it is; it must run after the object exists and only on success.
4. The module-level `*_proto` variables stay: they are still used by the class init code (`JS_SetClassProto`) and by getters that make wrappers (`make_audio_param_js`).
5. **Three constructors are not nodes and own their `fail:` label**: `js_offlineaudiocontext_constructor` (line 481),
   `js_audiolistener_constructor` (1627), `js_audiodevice_constructor` (1673). They allocate with `js_mallocz` and placement-new
   first. Convert them last, in the same way as the sndobj ones (build the `shared_ptr` into a local, then `qjsx::wrap`).
   `OfflineAudioContext` shares `js_audiocontext_class_id` with `AudioContext`, whose class prototype is `audiocontext_proto`, while its
   own fallback is `offlineaudiocontext_proto`. The fallback is used only when `new_target.prototype` is not an object, which cannot happen
   for a constructor made with `JS_SetConstructor` (line 3047), so `wrap` is equivalent in practice; if you want it exact, compute the
   prototype with the old ladder and call `qjsx::wrap_proto`.
6. `make_audio_node_js` stays for the remaining non-constructor callers (e.g. the destination node getter, line 526). Delete it only when `grep -n make_audio_node_js` is empty.

**Check**: build; `new class extends OscillatorNode {}` still works (subclass test); G4 on a loop creating nodes.

## R8 `option`: options dictionaries

**Recognise it**: sites section 7. The old shapes:

```cpp
v = JS_GetPropertyStr(ctx, argv[1], "frequency");
if(JS_IsNumber(v)) {
  double f;
  JS_ToFloat64(ctx, &f, v);
  osc->frequency()->setValue((float)f);
}
JS_FreeValue(ctx, v);
```

and the already migrated `if(argc > 1 && JS_IsObject(argv[1]) && !qjsx::property(ctx, argv[1], "gain", gain))` of `js_gain_constructor`.
`property` returns nonzero both for "absent" and for "conversion failed", and leaves the exception pending in the second case,
which is the stale-exception bug again (expected with `new GainNode(ctx, { gain: Symbol() })`; it cannot be run until P0 is done).

**Becomes**

```cpp
float frequency = 0;
int r;

if((r = qjsx::option(ctx, argv[1], "frequency", frequency)) < 0)
  return JS_EXCEPTION;
if(r)
  osc->frequency()->setValue(frequency);
```

(`argv[1]` need not be tested with `JS_IsObject` first: `option` returns 0 for a non-object, which is what "no options" means.
If `argc <= 1` guard the call with `argc > 1`, because `argv[1]` is out of range otherwise.)

A string option allocates, release it:

```cpp
const char* type = nullptr;
int r;

if((r = qjsx::option(ctx, argv[1], "type", type)) < 0)
  return JS_EXCEPTION;
if(r) {
  osc->setType(parse_oscillator_type(type));
  qjsx::release(ctx, type);
}
```

**Traps**

1. **Decision D4**: `option<double>` converts a string `"440"` to 440 and a `true` to 1, like the WebAudio dictionary
   conversion; the old `JS_IsNumber` guard ignored them. A `Symbol` now throws.
2. Object-valued options (`buffer`, `curve`, `context`) are not covered by `option`; keep their code, but make sure
   an exception from `JS_GetPropertyStr` is returned (`JS_IsException(v)`).
3. `option` returns 0 for `undefined`: `{ frequency: undefined }` keeps the default, as the WebAudio spec says.
4. After a failure (-1) free anything created earlier in the constructor (`osc` is a `shared_ptr`, nothing to free).

## 9. Tests: `tests/errors-test.js`

One process per case, so a crash is reported instead of ending the run. Not yet in the tree: create the file with
the content below (verified against the build of 2026-11-15; the module-load blocks make it run with only
the modules that load).

```js
// qjsm tests/errors-test.js --list      prints the case names
// qjsm tests/errors-test.js <case>      runs one case, exit 0 = pass
// one process per case, so a crash is reported instead of ending the run.

function assert(cond, msg) {
  if(!cond)
    throw new Error('assertion failed: ' + msg);
}

// the call must throw a TypeError; a call that returns normally has left
// the exception pending (it would surface after the script ended). Build
// the object first: a constructor that throws must not count as a pass.
function throwsTypeError(fn, what) {
  let err;

  try {
    fn();
  } catch(e) {
    err = e;
  }

  assert(err instanceof TypeError, what + ' must throw a TypeError (got ' + (err ? err.name : 'no exception') + ')');
}

const bad = Symbol('bad');
const cases = {};

const sndobj = await import('sndobj').catch(() => null);
if(sndobj) {
  const { HarmTable, Oscili, Buzz, ADSR, Reson, Gain, VDelay } = sndobj;
  const table = () => new HarmTable();
  const input = () => new Oscili(table(), 440, 1);
  const call = (make, method, ...args) => () => {
    const obj = make();
    throwsTypeError(() => obj[method](...args), method);
  };

  cases['sndobj/Oscili.setFreq(Symbol)'] = call(input, 'setFreq', bad);
  cases['sndobj/Oscili.setAmp(Symbol)'] = call(input, 'setAmp', bad);
  cases['sndobj/Oscili.setPhase(Symbol)'] = call(input, 'setPhase', bad);
  cases['sndobj/Oscili.output(Symbol)'] = call(input, 'output', bad);
  cases['sndobj/Buzz.setHarm(Symbol)'] = call(() => new Buzz(), 'setHarm', bad);
  cases['sndobj/ADSR.setADSR(Symbol)'] = call(() => new ADSR(), 'setADSR', bad, 1, 1, 1);
  cases['sndobj/ADSR.setMaxAmp(Symbol)'] = call(() => new ADSR(), 'setMaxAmp', bad);
  cases['sndobj/Reson.setFreq(Symbol)'] = call(() => new Reson(800, 150, input()), 'setFreq', bad);
  cases['sndobj/Gain.setGain(Symbol)'] = call(() => new Gain(-6, input()), 'setGain', bad);
  cases['sndobj/VDelay.setDelayTime(Symbol)'] = call(() => new VDelay(0.1, 0.05, 0.5, 0.5, 0.5, input()), 'setDelayTime', bad);
  cases['sndobj/new Oscili(table, Symbol)'] = () => throwsTypeError(() => new Oscili(table(), bad), 'Oscili constructor');
  cases['sndobj/new Buzz(Symbol)'] = () => throwsTypeError(() => new Buzz(bad), 'Buzz constructor');
  cases['sndobj/new HarmTable(Symbol)'] = () => throwsTypeError(() => new HarmTable(bad), 'HarmTable constructor');
  cases['sndobj/new Oscili() without a table'] = () => throwsTypeError(() => new Oscili(), 'Oscili constructor');
}

const stk = await import('stk').catch(() => null);
if(stk) {
  const { Echo, Chorus, NRev, JCRev, PitShift, StkFrames, SineWave, BiQuad } = stk;

  cases['stk/new Echo(Symbol)'] = () => throwsTypeError(() => new Echo(bad), 'Echo constructor');
  cases['stk/new Chorus(Symbol)'] = () => throwsTypeError(() => new Chorus(bad), 'Chorus constructor');
  cases['stk/new NRev(Symbol)'] = () => throwsTypeError(() => new NRev(bad), 'NRev constructor');
  cases['stk/new JCRev(Symbol)'] = () => throwsTypeError(() => new JCRev(bad), 'JCRev constructor');
  cases['stk/new StkFrames(Symbol)'] = () => throwsTypeError(() => new StkFrames(bad), 'StkFrames constructor');
  const stkcall = (make, method, ...args) => () => {
    const obj = make();
    throwsTypeError(() => obj[method](...args), method);
  };

  cases['stk/NRev.tick(Symbol)'] = stkcall(() => new NRev(1), 'tick', bad);
  cases['stk/PitShift.setEffectMix(Symbol)'] = stkcall(() => new PitShift(), 'setEffectMix', bad);
  cases['stk/BiQuad.tick(Symbol)'] = stkcall(() => new BiQuad(), 'tick', bad);
  cases['stk/SineWave.tick(Symbol)'] = stkcall(() => new SineWave(), 'tick', bad);
}

// decision D2: every constructor has a prototype property, so subclassing works
const subclass = (C, args = []) => () => {
  class Sub extends C {}
  assert(new Sub(...args) instanceof C, 'a subclass instance is an instance of the base');
};

if(sndobj) {
  cases['sndobj/class extends Buzz'] = subclass(sndobj.Buzz);
  cases['sndobj/class extends Oscili'] = subclass(sndobj.Oscili, [new sndobj.HarmTable()]);
}

if(stk) {
  cases['stk/class extends ADSR'] = subclass(stk.ADSR);
  cases['stk/class extends NRev'] = subclass(stk.NRev, [1]);
  cases['stk/class extends BiQuad'] = subclass(stk.BiQuad);
}

// the toStringTag of a class is its name
if(stk) {
  for(const name of ['BlitSaw', 'BlitSquare', 'Blit', 'PitShift', 'PRCRev', 'Delay', 'DelayL', 'ADSR', 'SingWave'])
    cases['stk/toStringTag of ' + name] = () => {
      const C = stk[name];
      assert(C, 'stk.' + name + ' exists');
      const tag = Object.prototype.toString.call(new C(...(name == 'Delay' || name == 'DelayL' ? [1, 8] : []))).slice(8, -1);
      assert(tag == name, 'toStringTag of ' + name + ' is ' + tag);
    };
}

const arg = scriptArgs[1];

if(arg == '--list') {
  console.log(Object.keys(cases).join('\n'));
} else {
  assert(cases[arg], 'unknown case ' + arg);
  cases[arg]();
  console.log('PASS ' + arg);
}
```

Driver loop (put it in a shell function or a `tests/run-errors.sh`):

```sh
cd ~/Sources/quickjs/qjs-sound
export QUICKJS_MODULE_PATH=build/$(cc -dumpmachine)
T=tests/errors-test.js
qjsm $T --list | while IFS= read -r c; do
  timeout 20 qjsm $T "$c" >/tmp/case.out 2>&1; rc=$?
  if [ $rc = 0 ]; then r=PASS; elif [ $rc -ge 124 ]; then r=CRASH; else r=FAIL; fi
  printf '%-44s %s\n' "$c" "$r"
done 2>/dev/null | tee /tmp/errors.now | awk '{print $NF}' | sort | uniq -c
```

Case names contain spaces, hence `read` instead of `for`.

**Baseline** before any change (commit `d530d27`, `build/x86_64-linux-gnu`, 2026-11-15):
37 cases: 5 PASS, 29 FAIL, 3 CRASH.

```
sndobj/Oscili.setFreq(Symbol)            FAIL
sndobj/Oscili.setAmp(Symbol)             FAIL
sndobj/Oscili.setPhase(Symbol)           FAIL
sndobj/Oscili.output(Symbol)             FAIL
sndobj/Buzz.setHarm(Symbol)              FAIL
sndobj/ADSR.setADSR(Symbol)              FAIL
sndobj/ADSR.setMaxAmp(Symbol)            FAIL
sndobj/Reson.setFreq(Symbol)             FAIL
sndobj/Gain.setGain(Symbol)              FAIL
sndobj/VDelay.setDelayTime(Symbol)       FAIL
sndobj/new Oscili(table, Symbol)         FAIL
sndobj/new Buzz(Symbol)                  FAIL
sndobj/new HarmTable(Symbol)             FAIL
sndobj/new Oscili() without a table      CRASH
stk/new Echo(Symbol)                     CRASH
stk/new Chorus(Symbol)                   CRASH
stk/new NRev(Symbol)                     FAIL
stk/new JCRev(Symbol)                    FAIL
stk/new StkFrames(Symbol)                FAIL
stk/NRev.tick(Symbol)                    FAIL
stk/PitShift.setEffectMix(Symbol)        FAIL
stk/BiQuad.tick(Symbol)                  FAIL
stk/SineWave.tick(Symbol)                PASS
sndobj/class extends Buzz                FAIL
sndobj/class extends Oscili              FAIL
stk/class extends ADSR                   FAIL
stk/class extends NRev                   FAIL
stk/class extends BiQuad                 PASS
stk/toStringTag of BlitSaw               FAIL
stk/toStringTag of BlitSquare            FAIL
stk/toStringTag of Blit                  FAIL
stk/toStringTag of PitShift              FAIL
stk/toStringTag of PRCRev                FAIL
stk/toStringTag of Delay                 PASS
stk/toStringTag of DelayL                PASS
stk/toStringTag of ADSR                  PASS
stk/toStringTag of SingWave              FAIL
```

Target after all phases: every line PASS. Each recipe above names the cases it should flip.

**Adding a case** when you migrate a function: add one line to `cases` (a `call(make, 'method', badArgs...)` or
`throwsTypeError(() => new X(bad), 'X constructor')`), run the driver, expect FAIL before your edit and PASS after.
Build the object **outside** `throwsTypeError`: a constructor that throws would otherwise make the case pass for the wrong
reason (this happened to `Reson`, `Gain`, `VDelay` in the first draft: they throw "requires an input SndObj").

## 10. Leak check (gate G4)

`qjsm --dump` prints `JS_ComputeMemoryUsage` at exit. Run the same script with few and many rounds; the object
counts must be equal (only digits of the printed totals may change). `-d` alone starts the interactive shell
in this build; use `--dump` with stdin closed.

```sh
cat > /tmp/leak.js <<'EOF'
import { HarmTable, Oscili, ADSR, Reson } from 'sndobj';
import { NRev, BiQuad, SineWave } from 'stk';
const rounds = +process.env.ROUNDS || 10;
for(let i = 0; i < rounds; i++) {
  const t = new HarmTable();
  const o = new Oscili(t, 440, 1);
  o.setFreq(220); o.setAmp(0.5); o.output(0);
  const e = new ADSR(); e.setADSR(0.1, 0.1, 0.5, 0.1);
  const r = new Reson(800, 150, o);
  const n = new NRev(1); n.tick(0.5);
  new BiQuad().tick(0.1); new SineWave().tick();
  try { o.setFreq(Symbol()); } catch(x) {}
}
EOF
export QUICKJS_MODULE_PATH=build/$(cc -dumpmachine)
ROUNDS=10  qjsm --dump /tmp/leak.js </dev/null > /tmp/d10.txt  2>&1
ROUNDS=250 qjsm --dump /tmp/leak.js </dev/null > /tmp/d250.txt 2>&1
diff <(sed -n '/QuickJS memory usage/,$p' /tmp/d10.txt) <(sed -n '/QuickJS memory usage/,$p' /tmp/d250.txt)
```

Baseline: 8 diff lines, all of them the two total-bytes lines that grow by one byte (`1674566` vs `1674567`). Any difference in an
object-class line is a leak. Extend the loop with the code you migrated (constructors, methods, the failing-argument calls).
(The `try { o.setFreq(Symbol()) } catch` line prints one uncaught TypeError at the top of the dump before the fix, none after.)

## Appendix A: what not to convert

- `with(...)` / `property(...)` call sites that already check their result (`quickjs-stk.cpp` has 21 `qjsx::` uses)
- `JS_ToBool`, `JS_ToCString` + `JS_FreeCString` pairs that are correct (15 in stk, 9 in labsound); only the numeric conversions are in scope
- `guarded<F>` (stk): it translates C++ exceptions, unrelated to the helpers; leave it
- `js_stk_channel` and the other file-local helpers that already check and throw
- the C layer (`js-utils.h`, `js-typedarray.[ch]`)

## Appendix B: self-test of the helpers (gate G0)

Compile and run after pasting section 1 into `js-utils.hpp`; it needs only the headers and `libquickjs`
(no module code). Save as `/tmp/qjsx-selftest.cpp`:

```cpp
#include "js-utils.hpp"
#include <cstdio>
#include <cstring>
#include <string>

struct Base { virtual ~Base() {} };
struct A : Base { int v = 1; };
struct B : Base { int v = 2; };
struct C : Base { int v = 3; };

static JSClassID cls1, cls2;
static int fails = 0;
#define CHECK(...) do { if(!(__VA_ARGS__)) { std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); fails++; } } while(0)

static std::string
pending(JSContext* ctx) {
  JSValue e = JS_GetException(ctx);
  JSValue m = JS_GetPropertyStr(ctx, e, "message");
  const char* s = JS_ToCString(ctx, m);
  std::string r = s ? s : "";
  JS_FreeCString(ctx, s); JS_FreeValue(ctx, m); JS_FreeValue(ctx, e);
  return r;
}

int
main() {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  /* args */
  {
    JSValue argv[3] = { JS_NewFloat64(ctx, 2.5), JS_UNDEFINED, JS_NewInt32(ctx, 7) };
    double a = 1, b = 5; int32_t c = 0;
    CHECK(qjsx::args(ctx, 3, argv, a, b, c) == 0);
    CHECK(a == 2.5 && b == 5 && c == 7);
    double a2 = 9, b2 = 9; int32_t c2 = 9;
    CHECK(qjsx::args(ctx, 1, argv, a2, b2, c2) == 0);
    CHECK(a2 == 2.5 && b2 == 9 && c2 == 9);
    float f = 1; int16_t s = 0; int64_t l = 0; bool bo = false;
    JSValue argv2[4] = { JS_NewFloat64(ctx, 0.5), JS_NewInt32(ctx, 70000), JS_NewInt64(ctx, 1LL << 40), JS_TRUE };
    CHECK(qjsx::args(ctx, 4, argv2, f, s, l, bo) == 0);
    CHECK(f == 0.5f && s == (int16_t)70000 && l == (1LL << 40) && bo);
    double d1 = 1, d2 = 2;
    CHECK(qjsx::args_from(ctx, 1, 3, argv, d1, d2) == 0 && d1 == 1 && d2 == 7);
    JSValue sym = JS_Eval(ctx, "Symbol('x')", 11, "t", JS_EVAL_TYPE_GLOBAL);
    JSValue argv3[3] = { JS_NewFloat64(ctx, 3), sym, JS_NewFloat64(ctx, 4) };
    double p = 0, q = 0, r = 0;
    CHECK(qjsx::args(ctx, 3, argv3, p, q, r) == -1);
    CHECK(p == 3 && r == 0);
    CHECK(JS_HasException(ctx));
    CHECK(pending(ctx) == "cannot convert symbol to number");
    CHECK(!JS_HasException(ctx));
    CHECK(qjsx::present(2, argv, 0) && !qjsx::present(2, argv, 1) && !qjsx::present(1, argv, 3));
    JSValue nul[1] = { JS_NULL };
    CHECK(!qjsx::present(1, nul, 0));
    JS_FreeValue(ctx, sym);
  }

  /* dispatch / require */
  {
    A a; B b; C c; Base* pa = &a; Base* pc = &c;
    int hit = 0;
    CHECK(qjsx::dispatch<B, A>(pa, [&](auto* p) { hit = p->v; }) && hit == 1);
    hit = 0;
    CHECK(qjsx::dispatch<A, B>(&b, [&](auto* p) { hit = sizeof(*p); }) && hit == sizeof(B));
    CHECK(!qjsx::dispatch<A, B>(pc, [&](auto*) { hit = 99; }));
    std::shared_ptr<Base> sp = std::make_shared<B>();
    CHECK(qjsx::dispatch<B>(sp, [&](auto* p) { hit = p->v; }) && hit == 2);
    CHECK(qjsx::require<A>(ctx, pa, "needs an A") == &a);
    CHECK(!JS_HasException(ctx));
    CHECK(qjsx::require<A>(ctx, pc, "needs an A") == nullptr);
    CHECK(pending(ctx) == "needs an A");
  }

  /* class_group, self, opaque */
  {
    JS_NewClassID(&cls1); JS_NewClassID(&cls2);
    JSClassDef d1 = { "One", nullptr, nullptr, nullptr, nullptr };
    JSClassDef d2 = { "Two", nullptr, nullptr, nullptr, nullptr };
    JS_NewClass(rt, cls1, &d1); JS_NewClass(rt, cls2, &d2);
    int v1 = 11, v2 = 22;
    JSValue o1 = JS_NewObjectClass(ctx, cls1); JS_SetOpaque(o1, &v1);
    JSValue o2 = JS_NewObjectClass(ctx, cls2); JS_SetOpaque(o2, &v2);
    JSValue plain = JS_NewObject(ctx);
    qjsx::class_group<int> g;
    g.add(cls1);
    CHECK(g.get(o1) == &v1 && g.get(o2) == nullptr && g.get(plain) == nullptr && g.get(JS_NewInt32(ctx, 1)) == nullptr);
    g.add(cls2);
    CHECK(g.get(o2) == &v2);
    CHECK(g.get2(ctx, plain, "a node") == nullptr);
    CHECK(pending(ctx) == "expected a node");
    CHECK(qjsx::self<int>(ctx, o1, cls1) == &v1);
    CHECK(qjsx::self<int>(ctx, o1, cls2) == nullptr && JS_HasException(ctx));
    JS_FreeValue(ctx, JS_GetException(ctx));
    CHECK(qjsx::opaque<int>(o1, cls2) == nullptr && !JS_HasException(ctx));
    JS_FreeValue(ctx, o1); JS_FreeValue(ctx, o2); JS_FreeValue(ctx, plain);
  }

  /* cast_shared */
  {
    std::shared_ptr<Base> sp = std::make_shared<A>();
    CHECK(qjsx::cast_shared<A>(ctx, sp, "A"));
    CHECK(!qjsx::cast_shared<B>(ctx, sp, "OscillatorNode") && pending(ctx) == "not an OscillatorNode");
    CHECK(!qjsx::cast_shared<B>(ctx, sp, "GainNode") && pending(ctx) == "not a GainNode");
    CHECK(!qjsx::cast_shared<B>(ctx, sp, "IIRFilterNode") && pending(ctx) == "not an IIRFilterNode");
    CHECK(!qjsx::cast_shared<B>(ctx, sp, "scheduled source node") && pending(ctx) == "not a scheduled source node");
  }

  /* option */
  {
    JSValue o = JS_Eval(ctx, "({x: 1.5, y: undefined, z: Symbol('s'), s: 'hi'})", 49, "t", JS_EVAL_TYPE_GLOBAL);
    double x = 0, y = 7, z = 8, w = 9;
    CHECK(qjsx::option(ctx, o, "x", x) == 1 && x == 1.5);
    CHECK(qjsx::option(ctx, o, "y", y) == 0 && y == 7);
    CHECK(qjsx::option(ctx, o, "missing", w) == 0 && w == 9);
    CHECK(qjsx::option(ctx, o, "z", z) == -1);
    CHECK(JS_HasException(ctx)); JS_FreeValue(ctx, JS_GetException(ctx));
    const char* s = nullptr;
    CHECK(qjsx::option(ctx, o, "s", s) == 1 && s && !std::strcmp(s, "hi"));
    qjsx::release(ctx, s);
    CHECK(qjsx::option(ctx, JS_UNDEFINED, "x", x) == 0);
    JS_FreeValue(ctx, o);
  }

  /* ctor_def, finalize (compile + run) */
  {
    static const qjsx::ctor_def defs[] = { { "One", 0, 5 }, { "Two", 1, 9, "Zwei" } };
    CHECK(!std::strcmp(qjsx::ctor_name(defs, 9), "Two") && !std::strcmp(qjsx::ctor_name(defs, 77), ""));
    CHECK(defs[0].export_name == nullptr && !std::strcmp(defs[1].export_name, "Zwei"));
    auto f1 = &qjsx::finalize<int, cls1>; auto f2 = &qjsx::finalize_delete<int, cls2>;
    (void)f1; (void)f2;
  }

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  std::printf(fails ? "%d FAILED\n" : "all helper checks passed\n", fails);
  return fails != 0;
}
```

```sh
cd ~/Sources/quickjs/qjs-sound
c++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I. -I/usr/local/include/quickjs \
    /tmp/qjsx-selftest.cpp -L/usr/local/lib/x86_64-linux-gnu -lquickjs -Wl,-rpath,/usr/local/lib/x86_64-linux-gnu -o /tmp/qjsx-selftest \
  && /tmp/qjsx-selftest       # prints: all helper checks passed
```
