# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## Skills

Load the `quickjs-native-bindings` skill before reading or editing any `quickjs-*.c`/`.cpp` file or
`cmake/*.cmake`, and `quickjs-scripting` when working on the JS tests and examples.

## What this is

Native QuickJS bindings for audio libraries, one loadable module per library: `stk`, `labsound`
(Web Audio API shaped), `portaudio`, `portmidi`, `sndfile`, `samplerate`, `soundtouch`, `rubberband`,
`aubio`, `sndobj`. Each is `quickjs-<name>.c`/`.cpp`, built as `<name>.so` and, with `-DBUILD_STATIC=ON`,
as `quickjs-<name>.a` (embeddable in qjsm as a builtin; see "Static modules"). `third_party/` holds the
vendored libraries as submodules; the system copy is used unless `BUILD_<LIB>` is ON. `tonnetz/` and
`xi-instruments/` are JS programs built on the modules.

## Build

```sh
cmake -B build/$(cc -dumpmachine) -S .      # USE_<LIB>=OFF skips a binding
cmake --build build/$(cc -dumpmachine)
cmake -B /tmp/b -S . -DBUILD_STATIC=ON -DBUILD_SHARED=OFF   # quickjs-<name>.a + .module.cmake
```

- The shared cmake layer (`cmake/{FindQuickJS,BuildQuickJS,QuickJSModule}.cmake`) is synced from the
  `quickjs-native-bindings` skill (`scripts/cmake-sync.sh`); do not edit those three files here, change
  the canonical copy and sync. `Find<Lib>`/`Build<Lib>` are specific to this project.
- `make_module(<name> [sources])` takes sources, not a language: `make_module(stk quickjs-stk.cpp)`.
- A module is built as `qjs-<name>` (shared) and `qjs-<name>-static`; code that configures a target
  after `make_module` must handle both (`module_*` helpers in `CMakeLists.txt`).

## Static modules

A static archive exports only `js_init_module_<name>`; `hide_static_symbols()` makes every other strong
global local after the build (`-DHIDE_STATIC_SYMBOLS=OFF` disables it). Check:

```sh
nm -g --defined-only build/*/quickjs-*.a | awk '$2 ~ /[TDBRCSG]/ && $3 !~ /^js_init_module_/'   # prints nothing
```

A helper that declares exports is `js_<name>_add_exports`, never `js_init_module_<name>`.

## Run / test

Tests are scripts in `tests/` (`*-test.js`), run with qjsm and `QUICKJS_MODULE_PATH` pointing at the
build dir, under limits (see "Running scripts with qjsm"). Most need a sound card or MIDI device and
are meant to be run by hand; there is no `ctest` suite.

```sh
QUICKJS_MODULE_PATH=build/$(cc -dumpmachine) timeout 60 qjsm tests/aubio-test.js
```

## Docs and tracking

- `doc/native/<name>.md`: API of each binding; `doc/api-interoperability.md`: comparison with
  other bindings of the same libraries (survey first, see the skill, section 0).
- `TODO.md`: open work. `BUGS`: bugs found and left unfixed (format below).
- `ChangeLog`: GNU-style changelog of completed work, one entry per day at the root. Write the
  day's entry (fixes, new tests, docs, and the pending "left unfixed" review) when finishing a
  task, without being asked.

## BUGS file format

Newly discovered bugs are appended to `BUGS` in this directory, one entry per bug, in the order
found, shaped like:

    - <canonical-name> (<file>:<line>): <short plain-text description
      of what's broken and how it was confirmed>

        <JS code that triggers it, or a one-line note if not
        practically reproducible in plain JS>

`canonical-name` is a short kebab-case handle for referring back to the
bug elsewhere (commit messages, other BUGS entries). The source location
is the primary place the bug lives, not every place it's felt.

`BUGS` itself is plain lowercase text: no back-quotes in descriptions,
keep descriptions to a sentence or two, and always include the source
location of the bug.

Always append newly discovered bugs to `BUGS` as soon as you find them,
without asking first or waiting to be told - this applies any time you're
working in this directory, not just when explicitly hunting for bugs.
Check at the end of a task whether anything surfaced during the work
(errors seen while testing, oddities noticed while reading code, things
that "shouldn't happen" but did) got logged; if not, log it before
finishing.

Once a bug is actually fixed, remove its entry from `BUGS` entirely -
don't leave it in tagged `[FIXED]`. `BUGS` tracks what's still open, not a
changelog of past fixes; a fix's own commit message (or, if the fix
uncovered something worth remembering during the chase - a wrong
assumption, a second bug found along the way, an install/build gotcha -
a note in the relevant source comment) is where that history belongs
instead.

## Comments

These rules govern every comment you write or rewrite in this repo, from
here on. Keep comments short and readable, not a running log of debugging
history — and not a packed block of prose either. A comment should be
something the reader's eye takes in as a shape, the way a table or a
diagram is, not something they have to read start to end to parse.

- Struct-member comments: 1-2 lines, right on the member.
- Any other comment explaining behavior: 4 lines max. If the full
  rationale genuinely needs more room, don't write a longer paragraph —
  restructure: a one-line summary, then a short list (one point per
  fact), or the argument table below. Never let a comment become an
  unbroken block of sentences; break it into pieces the eye can scan.
- Never reference `BUGS` / `TODO.md` entries, issue names, or "confirmed
  via repro X" in a comment — that history belongs in the commit message
  not in the source. State the current rule and its reason, not how it was discovered or what broke before it existed.
- Prefer showing over telling: when a C expression, a literal value, or
  a short before/after pair makes the point, put that in the comment
  instead of describing it in words. code says more, faster, than a sentence explaining the same fact.
- If a comment describes a function's parameters, give each one its own
  line: 2-space indent, then type, name, and description as aligned
  columns (pad names so the descriptions all start at the same column):
  ```
  /* one-line summary of what the function does.
   *
   *   const char*  name   what this argument is / controls
   *   size_t       len    what this argument is / controls
   *
   *   returns ssize_t     what the return value means
   /
  ```
- Write sentences a reader can take in on one pass: subject, verb,
  concrete fact, in that order. No hedging, no throat-clearing, no
  "used to X / now Y" history — state the current behavior and, if it's
  not obvious, the one reason it has to be that way.
- A comment starts lowercase, unless its first word is an identifier that
  starts with an uppercase letter (`CFunction: ...`, `JSClassDef.call ...`).
  Applies to every comment you write, rewritten or new. Later sentences in
  a comment are fragments or follow after `;`, not new capitalised ones.
- Comment text is 75 columns at most, measured after the leading ` * `
  (or `/* `); with the prefix that is 78, so a closing ` */` still fits.
- Multi-line code in a comment is fenced: ```` ```js ```` for JS and
  ```` ```c ```` for C, each fence on its own comment line. A one-line
  snippet stays in single backticks.
- A function's comment says what the function does, in plain words, in its
  first line. After two lines a reader who has not seen the code can say
  what goes in and what comes out; if they cannot, rewrite it.
- Show one concrete input and its result instead of describing the shape:
  `{ abs: <function> }` beats "an object with one entry per key".
- One comment per function, never one block shared by several. Name what
  each parameter is for in plain words, with the real value where it is a
  message (`"dlopen: symbol not found"`), not jargon like "prefixes the
  error messages".
- Never a packed block of text. Summary line, example, parameter columns
  and `returns` are separate paragraphs, split by an empty ` *` line.

### A JS-facing function, class or object gets a header block

Whatever C exposes to JS (a function, a class, an object spliced into the
export list) is explained once, in a block at its declaration: in the `.h`
for what other files call, above the definition for a `static`. The block
reads like the JS docs, in JS terms, and is the one place the 4-line prose
cap does not count the example and the table. Order:

1. `Name: what it is` (and the bun/Node API it mirrors, if any).
2. The JS usage as code in a ```js fence, not described in words.
3. Arguments and result as aligned columns, with the accepted forms, the
   default and the range in the description.
4. `throws`: which error type, for what.
5. One line on how it is wired in (`JS_OBJECT_DEF(...)` entry, `defaults`).

Example, after `ffi-read.h`:

````c
/* read: direct memory reads, no DataView or ArrayBuffer (bun:ffi's read).
 *
 * ```js
 * read.u8(ptr, byteOffset);
 * read.i64(ptr); // a bigint
 * ```
 *
 *   number|bigint|buffer  ptr         address, or an ArrayBuffer/view
 *   number                byteOffset  default 0, may be negative
 *
 *   returns  Number, bigint (i64, u64) or pointer (ptr), by member name
 *   throws   TypeError for a NULL pointer
 *
 * spliced into the export list:
 * JS_OBJECT_DEF("read", js_ffiread_funcs, FFI_READ_COUNT, ...).
 */
````

The same shape serves a class: constructor usage, then one line per
method/getter, then `throws`. Internals (ownership, why a pointer is
held) go in the 4-line comments at the code, not in this block.

## Running scripts with qjsm

To run a script (e.g. a throwaway test file while debugging), invoke it
as the plain positional argument: `qjsm script.js [args]` - same as
`repl.js`'s own header comment documents running itself
(`qjsm repl.js [--model ...]`).

Never use `qjsm -m script.js` (or `--module`) for this - `-m`/`--module`
is qjsm's special module loader (package.json/.ts-aware resolution,
per `qjsm --help`: "load an ES6 module"), unrelated to running a script
as the main program. A plain script run without `-m` already supports
`import`/`export` (it's ES module syntax either way) - `-m` is not
"the way to get module support", it's a different, unrelated loading
path, and using it to run a script is a category error, not just a
stylistic difference.

## Patches for vendored libraries

When fixing a bug in a vendored library (LabSound, stk, aubio, libsndfile,
libsamplerate, soundtouch, rubberband, portaudio, portmidi, miniaudio, dsp,
marsyas, sndobj, Soundpipe), never edit the submodule directly — it is
someone else's repository. Instead write a patch file in `patches/` and wire
it into the build.

Patch file format:

- A `.patch` file: a normal `diff --git` patch.
- A comment header on top (C-style `/* ... */` or `# ...` lines) with the bug
  description and a repro. `git apply` skips comment lines, so the header does
  not affect application and `git apply --reverse --check` still validates
  cleanly — verified.
- The patch must apply to a copy of the submodule in the build tree, never to
  the submodule itself.

Two build paths exist, and which one a library uses decides how the patch is
wired:

- `vendored_build_static_subdirectory()` in `cmake/VendoredLibrary.cmake`
  (aubio, and any future library that follows the `Find<Lib>`/`Build<Lib>`
  pattern): pass `PATCH <path>` to the function. It copies the submodule to
  `<build>/third_party/<lib>-patched/`, applies the patch there, stamps the
  submodule commit + patch hash, and re-runs are a no-op.
- `ExternalProject_Add()` (LabSound at `CMakeLists.txt:43`, stk at `:66`):
  use `PATCH_COMMAND` on the ExternalProject. `ExternalProject_Add` does not
  have the same copy+stamp logic, so the patch applies to the real source
  tree — make sure the patch is idempotent and that a clean checkout still
  applies.

Note: the one existing patch, `cmake/patches/aubio-static-lib.patch`, lives in
`cmake/patches/`, not `patches/`. New vendored-library bug fixes go in
`patches/`.

On LabSound specifically: LabSound is the Web Audio implementation in this
project, and its engine bugs (deferred disconnect, scheduler epoch, offline
silence) are real and tracked in `BUGS`. Any engine patch must not break Web
Audio compatibility — the binding's own workarounds (e.g.
`quickjs-labsound.cpp`'s `disconnect_now`) stay in place until the engine fix
is verified against the Web Audio test suite, not just against the qjs-sound
tests.

If a bug in a vendored library can be worked around in the C/C++ binding,
work around it there instead of patching the vendored library. Only patch
the vendored library when the workaround is not possible or not sufficient
— e.g. the bug is in engine code the binding cannot intercept, or the
workaround would change observable behavior consumers depend on.

## Tests

A bug fix is not done until a regression test exists. Regression tests live
in `tests/` (not `tests/unittest/`), separate from the feature/smoke scripts
in `examples/` (e.g. `examples/portaudio-test.js`). Every test in `tests/`
is wired into the cmake build and runs under `ctest` — no hand-run-only
tests in that directory.

- **Vendored-library bugs** (LabSound, stk, aubio, ...) get a **C/C++ test**:
  one executable, linked directly against the library, no QuickJS, no binding
  in between. Proves the fix is in the library, not in a binding workaround.
  File: `tests/vendor/<bug-name>.cpp` (matching the `BUGS` canonical name,
  e.g. `tests/vendor/labsound-disconnect-deferred.cpp`).
- **Binding bugs** (the `quickjs-<lib>.c`/`.cpp` code) get a **JS test**: run
  with `qjsm` and `QUICKJS_MODULE_PATH` pointing at the build dir; one
  process per test (like `examples/errors-test.js`) so a crash fails only
  that test.
  File: `tests/bindings/<lib>-<bug-name>.js` (e.g.
  `tests/bindings/portaudio-pastream-getters.js`).

Naming: the `BUGS` canonical name, e.g.
`tests/vendor/labsound-scheduler-epoch.cpp` or
`tests/bindings/portaudio-pastream-getters.js`.

Wiring (top-level `CMakeLists.txt`):

```cmake
enable_testing()                      # once, near the top
find_program(QJSM qjsm)               # system install; qjs-sound does not build qjsm

add_executable(test-labsound-epoch tests/vendor/labsound-scheduler-epoch.cpp)
target_link_libraries(test-labsound-epoch PRIVATE ${labsound_LIBRARIES})
add_test(NAME labsound-scheduler-epoch COMMAND test-labsound-epoch)

if(QJSM)
  add_test(NAME portaudio-pastream-getters
           COMMAND ${QJSM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bindings/portaudio-pastream-getters.js
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(portaudio-pastream-getters PROPERTIES
    ENVIRONMENT "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR}")
else()
  message(WARNING "qjsm not found; binding tests will be skipped")
endif()
```

Notes:

- Tests needing a real audio device fail in the sandbox; such tests must
  detect no-device and exit 0 with a printed skip note (or be gated with
  a `LABSOUND_TEST_DEVICE` env var) instead of hard-failing.
- Precedent: `test-labsound.cpp` (repo root, built at `CMakeLists.txt:265`)
  was an unwired C++ LabSound smoke test — deleted. New C++ tests go in
  `tests/vendor/` and get wired with `add_test`.
- A test for a binding workaround (a bug in `BUGS` that is worked around in
  the binding, not fixed in the library) still goes in `tests/` — it pins
  the workaround's observable behavior so a library fix can be checked
  against it.

Do not add the `Co-Authored-By:` or `Claude-Session:` trailers (or any other
attribution lines) to commit messages in this project, even when Claude Code's
attribution guidance asks for them. This project's own rule overrides it.
