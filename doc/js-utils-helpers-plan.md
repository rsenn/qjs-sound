# Plan: C++ binding helpers in js-utils.hpp

Part 1 of 3 of the refactoring documentation. Written 2026-11-15 from a survey of
`quickjs-{labsound,stk,sndobj}.cpp` at commit `d530d27`, revised 2026-11-16 after a
call-site by call-site inventory. **Phase 1 (the helpers) is done and in `js-utils.hpp`, documented in
Doxygen format**; phases 2 to 5 are the manual refactoring.

| File | Says |
|---|---|
| `js-utils-helpers-plan.md` (this file) | **what to do in which order**: gates, decisions, steps, tracking |
| `js-utils-helpers-recipes.md` | **how**: the tested helper code, before and after for every pattern, traps, the test file |
| `js-utils-helpers-sites.md` | **where**: every call site with line numbers and a `grep` that finds what is left |

You work by hand. The loop for one function is in recipes section 0; this file tells you
which function comes next and how to know a step is finished.

## 1. Goal and scope

Move the code that the three bindings (8725 lines) repeat into `js-utils.hpp`
(namespace `qjsx`), and close the bug class the repetition hides: an ignored failed
`JS_ToFloat64` leaves a TypeError pending while the call returns normally.

The new helpers are commented in Doxygen format (`@brief`, `@param`, `@return`, `@code`) as requested; the
lowercase / 75-column comment rules of `CLAUDE.md` apply to every other comment you write in the bindings.

Not in scope: any change of a JS API except the five decisions of section 5; the C layer
(`js-utils.h`, `js-typedarray.[ch]`); the InputArray family and a type-keyed class registry
(`helpers.md` of the `quickjs-native-bindings` skill, sections 1 to 3b) - they are the next
step after this one.

### 1.1 The helpers, with corrected scope

The first draft of this plan overstated two of them; these are the verified numbers.

| # | Helper | Sites | Notes |
|---|---|---|---|
| R0 | `element<int64_t>` | 1 | one `JS_ToInt64` in sndobj `HarmTable` |
| R1 | `args`, `args_from`, `present` | 214 ignored conversions (sndobj 72, stk 115, labsound 27), 291 `argc` tests | fixes the stale-exception bug; the main gain |
| R2 | `dispatch<A, B, ...>` | 6 multi-type ladders (sndobj 3, stk 3) | not 68: the 68 casts form 28 ladders, 22 of which test one type |
| R2b | `require<T>` | 20 single-type checks in sndobj, 2 in stk `Cubic` | only with decision D1 = throw; otherwise leave the casts |
| R3 | `finalize`, `finalize_delete` | pure: stk 10, sndobj 3, labsound 6, plus 13 labsound forwarders; 4 stk `delete` | 1 special (`js_audiocontext_finalizer`) stays |
| R4 | `ctor_def` + `declare_ctors`/`bind_ctors`/`ctor_name` | stk 64 constructors in 8 families, sndobj 11 in 3 | **not labsound** (its 19 classes are registered one by one) |
| R5 | `class_group<T>` | labsound `any_audio_node` (14 ids), sndobj `js_sndobj_arg` (2 ids) | |
| R6 | `self`, `opaque`, `cast_shared` | 133 typed opaque lookups, 28 labsound `dynamic_pointer_cast` | message text is preserved |
| R7 | use `qjsx::wrap` | 24 `new_target.prototype` ladders (labsound 15, stk 6, sndobj 3) | 3 labsound ones are not nodes |
| R8 | `option` | about 24 option reads in 12 labsound constructors, 5 `qjsx::property` calls | |

### 1.2 Bugs the work fixes (`BUGS` entries; delete each when its step is done)

| Entry | Fixed by |
|---|---|
| `sndobj-oscili-null-table-segfault` | step 2.5 (done) |
| `unchecked-js-to-number-leaves-exception-pending` | steps 2.5 to 2.7, 3.5, 4.5 (delete when all three files are done) |
| `sndobj-constructors-leak-on-prototype-failure` | steps 2.4 to 2.6 (R7) (done) |
| `stk-singwave-exported-as-stksingwave` | step 3.3 (D5) |
| `stk-tostringtag-array-out-of-enum-order` | steps 3.3 (generators) and 3.3 (effects) |
| `labsound-so-undefined-iirfilternode-symbols` | step 0.3 |

## 2. Gates: what "green" means

Run after every step; a step is finished only when its gates pass. All commands run from
`~/Sources/quickjs/qjs-sound`; `$B` is `build/$(cc -dumpmachine)`.

| Gate | Command | Passes when |
|---|---|---|
| G0 helper self-test | recipes appendix B | prints `all helper checks passed` (after phase 1) |
| G1 build | `cmake --build $B 2>&1 \| grep -E 'error\|warning: '` | prints nothing new |
| G2 headless tests | `for t in sndobj-test stkframes-test shakers-test tr909bassdrum-test twintdrum-test; do QUICKJS_MODULE_PATH=$B timeout 90 qjsm tests/$t.js >/dev/null 2>&1; echo "$t $?"; done` | all `0`, as at baseline |
| G3 error test | driver loop of recipes section 9 | the PASS count rises as the plan says, no case that passed goes red |
| G4 leaks | recipes section 10 | the two dumps differ only in the two byte totals |
| G5 static symbols | `cmake -B /tmp/b -S . -DBUILD_STATIC=ON -DBUILD_SHARED=OFF && cmake --build /tmp/b && nm -g --defined-only /tmp/b/quickjs-*.a \| awk '$2 ~ /[TDBRCSG]/ && $3 !~ /^js_init_module_/'` | prints nothing |
| G6 exports | the `exports.js` diff of recipes R4, for stk and sndobj | identical except the allowed `SingWave` change |
| G7 status | the status command of section 4 | the counts moved as the step says |

Labsound gates (G2 with `webaudio-test`, `fx-test`, `synth-test`, `track-test`, G3 labsound
cases) need step 0.3.

## 3. Baseline, taken 2026-11-15 at `d530d27` (working tree: `BUGS` modified only)

G2: `sndobj-test`, `stkframes-test`, `shakers-test`, `tr909bassdrum-test`, `twintdrum-test` exit 0.
`fx-test`, `synth-test`, `webaudio-test`, `track-test` fail to start (labsound.so does not load, P0).

G3 (`tests/errors-test.js`, recipes section 9): 37 cases, 5 PASS, 29 FAIL, 3 CRASH. The crashes:
`sndobj/new Oscili() without a table`, `stk/new Echo(Symbol)`, `stk/new Chorus(Symbol)`.

G4 baseline: the 10-round and 250-round dumps differ in 8 lines, all of them the two total-byte lines.

## 4. The status command (G7)

Paste into a file `~/bin/qjsx-status` or run inline. It prints every counter this plan uses.

```sh
cd ~/Sources/quickjs/qjs-sound || exit 1
printf '%-9s %6s %6s %6s %6s %6s %6s %6s %6s\n' file conv dcast fin ctor proto opaque dpcast lines
for f in sndobj stk labsound; do
  F=quickjs-$f.cpp
  printf '%-9s %6s %6s %6s %6s %6s %6s %6s %6s\n' "$f" \
    "$(grep -cE '^\s+JS_To(Float64|Int32|Uint32|Int64)\(' $F)" \
    "$(grep -c 'dynamic_cast<' $F)" \
    "$(grep -c '_finalizer(JSRuntime' $F)" \
    "$(grep -c 'JS_CFUNC_constructor_magic' $F)" \
    "$(grep -c 'new_target, "prototype"' $F)" \
    "$(grep -cE 'static_cast<[A-Za-z:<>]+\*>\(\s*JS_GetOpaque2?\(' $F)" \
    "$(grep -c 'dynamic_pointer_cast' $F)" \
    "$(wc -l < $F)"
done
printf '%-9s %55s %6s\n' js-utils.hpp '' "$(wc -l < js-utils.hpp)"
```

| Column | Counts | Start | Target (D1 = keep, all decisions at the recommendation) |
|---|---|---|---|
| conv | ignored `JS_To*` conversions | 72 / 115 / 27 | 0 / 0 / 0 |
| dcast | `dynamic_cast<` | 31 / 36 / 0 | 23 / 9 / 0 (D1 = throw: lower; `require` replaces the single casts) |
| fin | `_finalizer(JSRuntime` | 3 / 14 / 20 | 0 / 0 / 1 (`js_audiocontext_finalizer`) |
| ctor | `JS_CFUNC_constructor_magic` | 11 / 64 / 0 | 0 / 0 / 0 |
| proto | `new_target, "prototype"` | 3 / 6 / 15 | 0 / 0 / 0 (1 if you keep `OfflineAudioContext` as is) |
| opaque | typed `JS_GetOpaque` casts | 11 / 53 / 69 | 0 / 0 / 0 |
| dpcast | `dynamic_pointer_cast` | 0 / 0 / 28 | 0 / 0 / 1 (`js_audiodevice_set`, different message) |

Targets for `lines` are filled in section 8 as you finish each phase (record, do not guess).

## 5. Decisions (answer before the step that needs them)

The steps below assume the recommendation. "If you decline" says what to do instead.

| # | Question | Recommendation | If you decline | Needed at |
|---|---|---|---|---|
| D1 | `dispatch` finds no type, and the 20+2 single-type checks of sndobj and stk `Cubic` do nothing when the object is the wrong class (`new Reson(...).sustain()` returns success): throw a `TypeError`? | throw, with `require` | keep the casts as they are; ignore the result of `dispatch` | steps 2.7, 3.5 |
| D2 | Generator, effect, FM, instrument, function and I/O constructors have no `prototype` property, so `class X extends stk.ADSR {}` throws "parent prototype must be an object or null" (reproduced; filters work). Define it on all? | yes | add a `bool define_prototype` parameter to `bind_ctors` and pass `false` for every family except filters | steps 1.4, 3.3 |
| D3 | A missing numeric argument gave NaN when passed as `undefined` (`JS_ToFloat64(undefined)`); `args` keeps the preset instead. A missing `Table` for `Oscili` throws a `TypeError`. | accept both | do not convert the required-argument reads (they keep their ignored `JS_ToFloat64`); the bug class stays for them | phases 2 to 4 |
| D4 | labsound options dictionary values of the wrong type (`{ frequency: "440" }`) were ignored by an `IsNumber` guard; WebAudio converts and throws on a Symbol. | follow the spec (convert, throw) | keep the guard and use `option` only for `bool` and `string` options | step 4.5 |
| D5 | `stk.SingWave` is exported as `StkSingWave` (the docs say `SingWave`). | rename; keep `StkSingWave` as an alias | keep the old name only; the `toStringTag` becomes `SingWave` anyway | step 3.3 |

## 6. Phases and steps

Format of a step: **sites** (section of `js-utils-helpers-sites.md`), **recipe**
(section of `js-utils-helpers-recipes.md`), **done when**, **flips** (G3 cases that go
from FAIL/CRASH to PASS), **commit** (suggested subject; this project adds no
`Co-Authored-By` trailer). Build (G1) after every edit inside a step, not at its end.

### Phase 0: preparation (no source change)

**0.1 Baseline files.** Create `/tmp/qjsx-baseline/` (it is not in the repo) and save:
the output of each G2 test, `qjsm /tmp/exports.js stk` and `... sndobj` (recipes R4 shows the script),
the status command output, `git rev-parse HEAD`. Done when the five files exist.

**0.2 Decide D1 to D5.** Write the answers at the top of section 8.

**0.3 Make `labsound.so` load.** Symptom: qjsm reports "could not load module ... as shared library";
`ldd -r build/x86_64-linux-gnu/labsound.so` lists undefined `lab::IIRFilterNode` symbols (typeinfo,
constructor, `getFrequencyResponse`), although `third_party/LabSound` is on the branch
`webaudio-spec-extensions` (commit `f4d0a1f5`). Cause not established; investigate in this order and stop at the first hit:

1. `nm -C third_party/LabSound/bin/libLabSound.a | grep -c IIRFilterNode` - is the class in the linked library at all? (`find build third_party -name 'libLabSound*.a'` shows every copy; the one linked is the one the link line names)
2. `ls third_party/LabSound/src/core/IIRFilterNode.cpp` (it exists in the checked-out tree) and `git -C third_party/LabSound log --oneline -3 -- src/core/IIRFilterNode.cpp` - when did it arrive, and is `libLabSound.a` older than that commit?
3. the LabSound `CMakeLists.txt` source list (`grep -n IIR third_party/LabSound/CMakeLists.txt`; it may glob `src/core/*.cpp` instead of naming files) - is it compiled?
4. the external-project step in this project's `CMakeLists.txt` (lines 43-46, binary dir `$B/third_party/LabSound`): is the build directory stale? Rebuild from clean: `rm -rf $B/third_party/LabSound && cmake --build $B --target LabSound`.

Done when `qjsm -e "import('labsound').then(m => console.log(Object.keys(m).length))"` (with `QUICKJS_MODULE_PATH=$B`)
prints a number, `webaudio-test` runs, and the `BUGS` entry `labsound-so-undefined-iirfilternode-symbols` is deleted with
the cause written into the commit message. Phases 1 to 3 do not wait for this; phase 4 does.

**0.4 The error test.** Create `tests/errors-test.js` from recipes section 9. Run the driver loop; the result must match the baseline of section 3 (5 PASS, 29 FAIL, 3 CRASH).
Commit: `tests: errors-test.js, bad-argument cases for sndobj and stk`. If labsound loads (0.3), add the labsound cases from step 4.6 later.

### Phase 1: the helpers (additive, nothing else changes) - DONE 2026-11-16

All sub-steps below are applied to `js-utils.hpp` (1029 lines, was 575). G0 passes against the real header, G1 and the
G2 spot checks pass, `doxygen` reports no warning for the new blocks. Nothing is committed yet: the working tree has
`js-utils.hpp` and `BUGS` modified plus the three `doc/js-utils-helpers-*.md` files. Commit them before starting phase 2.

One commit per sub-step; each only adds code to `js-utils.hpp`, so G1 and G2 cannot change.
The code is in recipes section 1; paste exactly as written (it passes G0).

| Step | Adds | Where in the file |
|---|---|---|
| 1.1 | `#include <algorithm>`, `#include <type_traits>` | top, next to the other includes |
| 1.2 | R0 `element<int64_t>` | before `element<int16_t>` |
| 1.3 | R1 `detail::arg_at`, `args_from`, `args`, `present` | before `} // namespace qjsx` |
| 1.4 | R2 `detail::try_cast`, `dispatch` (2 overloads), `require`; R3 `finalize`, `finalize_delete`; R4 `ctor_def`, `declare_ctors`, `bind_ctors`, `ctor_name` (D2: if declined, add the `define_prototype` parameter now) | same place, same order |
| 1.5 | R5 `class_group`; R6 `self`, `opaque`, `cast_shared`; R8 `option` | same place |
| 1.6 | G0: compile and run the self-test of recipes appendix B | `/tmp`, not in the tree |

Done when G0 prints `all helper checks passed` and G1/G2 are unchanged. Commit: `js-utils.hpp: argument, dispatch, finalizer, constructor-table, class-group and option helpers`.

### Phase 2: `quickjs-sndobj.cpp` (1428 lines; sndobj is the smallest, so it proves the recipes)

The order follows the file from the top; do not jump. Sites for all of phase 2: sites sections 1.1, 2.1, 3.1, 4.1, 5.2, 5.3, 6.1.

**2.1 Finalizers (R3).** 3 rows, sites 3.1. Done when `grep -c '_finalizer(JSRuntime' quickjs-sndobj.cpp` is 0. Commit: `sndobj: use qjsx::finalize`.

**2.2 Input probe (R5).** `js_sndobj_arg` (line 57) with `class_group<SndObjPtr>`; `js_sndobjtable_arg` (line 71) stays. Done when `js_sndobj_arg` is the 4-line function of recipes R5 and the two `add` calls are in the init function.
G2 `sndobj-test` passes (it builds chains of generators and effects, so a missing `add` fails it).

**2.3 Constructor tables (R4).** Three families, one commit each: table (`sndobjtable_ctors`, 1 row), generators (3 rows), effects (7 rows). Sites 4.1 has the tables.
Each family: define the table after its enum, replace the `toStringTag` array by `ctor_name`, replace the declare lines in `js_sndobj_add_exports` (line 1400) and the bind lines (1370-1392) by `declare_ctors`/`bind_ctors`.
Done when `grep -c JS_CFUNC_constructor_magic quickjs-sndobj.cpp` is 0 and G6 (exports diff) is identical. **Flips** (D2): `sndobj/class extends Buzz`, `sndobj/class extends Oscili`.

**2.4 `js_sndobjtable_constructor` (lines 291-346; R7 + R1).** The `argc == 0` branch stays; inside the other branch use `args`
(`len` is `int64_t`, R0). Replace the `js_mallocz` block and the `fail:` label by a local `SndObjTablePtr` and `qjsx::wrap`. Sites 1.1 row `js_sndobjtable_constructor`, 6.1.
**Flips**: `sndobj/new HarmTable(Symbol)`.

**2.5 `js_sndobjgenerator_constructor` (509-670; R7 + R1).** Three cases (Oscili, Buzz, Randi), one build each. Use the Oscili example of recipes R1
as the template for the others (mind the different argument positions: Buzz has `harms` at 2 and the mods at 3 and 4; Randi has the mods at 2 and 3).
Replace the tail (prototype ladder, `fail:`, `argfail:`) by `qjsx::wrap`. The `js_sndobj_retain` calls stay after the wrap.
**Flips**: `sndobj/new Oscili(table, Symbol)`, `sndobj/new Buzz(Symbol)`, `sndobj/new Oscili() without a table` (CRASH -> PASS).
Delete BUGS `sndobj-oscili-null-table-segfault`.

**2.6 `js_sndobjeffect_constructor` (1068-1343; R7 + R1).** Seven cases, one build each, in file order: ADSR, Reson, Comb, Allpass, VDelay, Gain, Mixer.
The conversion lines are listed in sites 1.1 (about 40). `VDelay` takes five numbers and the input at `argv[5]`: one `args` call for the numbers, then
`js_sndobj_arg(ctx, argv[5])`. Done when the function has no `js_mallocz`, no `goto`, no prototype ladder.
Delete BUGS `sndobj-constructors-leak-on-prototype-failure` after 2.4, 2.5 and 2.6.

**2.7 Methods (R1, R2; R6 for the prologues).** Four functions, one commit each; inside each go `case` by `case`.

| Function | Lines | Conversions | Ladders | Flips |
|---|---|---|---|---|
| `js_sndobj_common_method`, `js_sndobj_common_set` | 98-170 | 3 | - | `sndobj/Oscili.output(Symbol)` |
| `js_sndobjtable_method` | 201-256 | 4 | 2 single (`HarmTable`) | - |
| `js_sndobjgenerator_method` | 380-469 | 4 | 2 multi + 3 single | `Oscili.setFreq/setAmp/setPhase(Symbol)`, `Buzz.setHarm(Symbol)` |
| `js_sndobjeffect_method` | 778-1010 | 16 | 1 multi (`Comb, Gain`) + 15 single | `ADSR.setADSR/setMaxAmp`, `Reson.setFreq`, `Gain.setGain`, `VDelay.setDelayTime` |

With D1 = keep leave the single-type casts. With D1 = throw convert them with `require` (recipes R2) and add one assertion per converted method to `errors-test.js`
(`new Reson(...).sustain()` must throw `TypeError`).
After this step: `conv` and `proto` and `fin` and `ctor` are 0 for sndobj; delete BUGS `unchecked-js-to-number-leaves-exception-pending` only after phases 3 and 4 as well (it names all three files).

**2.8 Phase gate.** G1, G2, G3 (the sndobj cases all PASS), G4 with the sndobj lines of the leak script, G5, G6. Record `wc -l quickjs-sndobj.cpp` in section 8.

### Phase 3: `quickjs-stk.cpp` (4000 lines)

Sites: 1.2, 2.2, 2.3, 3.2, 4.2, 5.3, 6.2. File order again; but the class-level steps (3.1 to 3.3) come first because they shrink the file and make the later reading easier.

**3.1 Finalizers (R3).** 14 rows (sites 3.2): 10 `finalize`, 4 `finalize_delete` (`MidiFileIn`, `RtMidiIn`, `RtMidiOut`, `RtAudio`). Done when the status column `fin` is 0.

**3.2 Opaque lookups (R6).** 53 sites (sites 5.3), class by class: `StkFramesPtr` (14: 11 `opaque`, 3 `self`), instruments (5), `StkWvIn`/`StkWvOut` (10), generator (4), then the single ones.
Done when the status column `opaque` is 0. G1/G2 only; nothing changes behaviour.

**3.3 Constructor tables (R4), one commit per family, in this order** (sites 4.2 has every table):

| Family | Rows | Special |
|---|---|---|
| filters | 13 | already has the `prototype` property; bind order differs from enum order, the table handles it |
| generators | 11 | D5: the `SingWave` row; fixes the `toStringTag` array at line 698 (BUGS `stk-tostringtag-array-out-of-enum-order`, generators) |
| effects | 8 | fixes the array at line 1133 (BUGS, effects) |
| FM | 7 | |
| instruments | 20 | no tag array (`js_stkinstrmnt_constructor` sets none); the table still serves declare and bind |
| function | 1 | `Cubic` |
| wv-in | 2 | |
| wv-out | 2 | |

After each family: G6 on stk, the matching G3 cases. **Flips** (D2): `stk/class extends ADSR`, `stk/class extends NRev`; (tags) `stk/toStringTag of BlitSaw`, `BlitSquare`, `Blit`, `PitShift`, `PRCRev`, `SingWave`.
Done when `ctor` is 0. Delete BUGS `stk-singwave-exported-as-stksingwave` and `stk-tostringtag-array-out-of-enum-order`.

**3.4 Constructors' tails (R7).** The six prototype ladders (sites 6.2). For each: build the opaque pointer first, then `qjsx::wrap`. Done when `proto` is 0.

**3.5 Argument reading (R1) and ladders (R2).** Function by function from sites 1.2 (25 functions, in file order). Group them into commits by class:

| Commit | Functions (sites 1.2) |
|---|---|
| frames | `js_stk_set`, `js_stkframes_constructor`, `js_stkframes_method`, `js_stkframes_set` |
| generator | `js_stkgenerator_constructor` (2 sites), `js_stkgenerator_method`, with ladder R2 at line 748 |
| filter | `js_stkfilter_constructor`, `js_stkfilter_method` |
| effect | `js_stkeffect_constructor` (the `Chorus`/`Echo` crash is here), `js_stkeffect_method` with the two ladders at 1174 and 1203 |
| fm | `js_stkfm_constructor`, `js_stkfm_method` |
| instruments | `js_stkinstrmnt_constructor`, `js_stkinstrmnt_method` |
| function | `js_stkfunction_method` with the `Cubic` checks |
| drums | `js_twintdrum_*`, `js_tr909bassdrum_*`, `js_tr909percussion_*` (the three plain `dynamic_cast`s of sites 2.3 stay or become `require`, D1) |
| I/O | `js_rtwvin_*`, `js_inetwvin_*`, `js_rtwvout_*`, `js_inetwvout_*`, MIDI and RtAudio methods |

**Flips**: `stk/new Echo(Symbol)` and `stk/new Chorus(Symbol)` (CRASH -> PASS) in the effect commit, `stk/new NRev(Symbol)`, `stk/new JCRev(Symbol)`, `stk/new StkFrames(Symbol)`, `stk/NRev.tick(Symbol)`, `stk/PitShift.setEffectMix(Symbol)`, `stk/BiQuad.tick(Symbol)`.
If `Chorus(0)` or `Echo(0)` still fails inside STK after the conversion is checked, validate the value before the call and throw a `RangeError` (STK prints "parameter cannot be zero" and continues).
Done when `conv` is 0 and `dcast` is at its target. `guarded<F>` stays untouched.

**3.6 Phase gate.** G1 to G6, G4 with the stk lines of the leak script, plus the manual device tests you normally run (portaudio, midi). Record `wc -l`.

### Phase 4: `quickjs-labsound.cpp` (3297 lines; needs step 0.3)

Sites: 1.3, 3.3, 5.1, 5.3, 5.4, 6.3, 7.

**4.1 Finalizers (R3).** 13 forwarders and 6 pure rows (sites 3.3); then delete `js_audionode_finalize_with`. Keep `js_audiocontext_finalizer`. Done when `fin` is 1.

**4.2 Node group (R5).** `audio_nodes` and 14 `add` calls (sites 5.1); `any_audio_node` becomes the one-liner. Gate: `webaudio-test` (connects nodes).
Write the **labsound smoke test** now (`tests/labsound-graph-test.js`): create one of every node type, `connect` it to the destination, `disconnect`; all 14 must succeed. This is the guard for R5 forever.

**4.3 Lookups and casts (R6).** 69 `self`/`opaque` sites, 28 `cast_shared` sites (sites 5.3, 5.4), node type by node type in the order of the table in sites 5.4 (scheduled source, oscillator, gain, biquad, ...). Add the local `node_as` helper (recipes R6) in the first commit. The two "different wording" sites keep their own throw.
Done when `opaque` is 0 and `dpcast` is 1.

**4.4 Constructor tails (R7).** The 15 prototype ladders (sites 6.3): 12 node constructors first (template: `js_gain_constructor`, line 1299), then `OfflineAudioContext`, `AudioListener`, `AudioDevice` (recipes R7, trap 5). Done when `proto` is 0 (or 1 for Offline, by choice).

**4.5 Options and numbers (R8, R1).** The 24 relevant option reads of sites section 7 and the 27 numeric conversions of sites 1.3, constructor by constructor
(the two lists overlap in the constructors). The five `qjsx::property` calls migrate to `option` (they also leave a pending exception on a failed conversion). D4 applies here. Done when `conv` is 0.

**4.6 Phase gate and labsound error cases.** Add to `errors-test.js`: `labsound/new OscillatorNode(ctx, { frequency: Symbol() })`,
`labsound/new GainNode(ctx, { gain: Symbol() })`, `labsound/osc.frequency.value = Symbol()`, and `labsound/class extends OscillatorNode` (subclass). Run G1 to G6 and the four labsound tests.

### Phase 5: closing

1. Delete the remaining `BUGS` entries named in section 1.2 (`unchecked-js-to-number-leaves-exception-pending` last).
2. `TODO.md`: the checklist "adding a node means touching six spots" is down to five (the probe is automatic); edit step 4 and 5 accordingly.
3. Docs: if D2 or D5 changed the JS API, say so in `doc/native/` (subclassing now works for all constructors; `SingWave`). `doc/api-interoperability.md` already says `SingWave`.
4. `helpers.md` of the `quickjs-native-bindings` skill: add a findings-log line with what the helpers did not cover and which call sites could not use them; `ChangeLog` entry (the skill rule).
5. Fill the size table of section 8.

## 7. Risks and how the gates cover them

| Risk | Covered by |
|---|---|
| a conversion keeps its old meaning wrongly (preset vs NaN) | D3; G3 cases; read the "before" of each case once |
| the `dispatch` lambda does not compile for one listed type | the compiler, immediately (recipes R2, trap 1) |
| a class forgot `audio_nodes.add` | the labsound smoke test of step 4.2 |
| a constructor loses its fallback prototype | recipes R7 trap 1 (`grep JS_SetClassProto`); subclass test |
| exports change by accident | G6 on every R4 family |
| a half-converted file stops building | one function per build; revert with `git checkout -- <file>` |
| header-only templates add exported symbols to the static build | G5 (expected: nothing) |
| the toolchain on Termux defaults to C++17 | the helpers use C++17 only; build there once after phase 1 |

## 8. Tracking

Decisions: D1 throw (require) · D2 define prototype on every constructor · D3 keep the preset
for a missing/undefined argument · D4 follow the WebAudio dictionary conversion (convert, throw on
Symbol) · D5 rename `StkSingWave` to `SingWave` (alias kept)

| Step | State | Date |
|---|---|---|
| 0.1 baseline files | | |
| 0.2 decisions | D1 throw, D2 define prototype, D3 keep preset, D4 convert/throw, D5 rename | 2026-11-16 |
| 0.3 labsound.so loads | | |
| 0.4 errors-test.js | done | 2026-11-16 |
| 1.1 to 1.6 helpers + G0 | done | 2026-11-16 |
| 2.1 sndobj finalizers | done | 2026-11-16 |
| 2.2 sndobj input probe | done | 2026-11-16 |
| 2.3 sndobj ctor tables (table, generator, effect) | done | 2026-11-16 |
| 2.4 sndobj table ctor | done | 2026-11-16 |
| 2.5 sndobj generator ctor | done | 2026-11-16 |
| 2.6 sndobj effect ctor | done | 2026-11-16 |
| 2.7 sndobj methods (common, table, generator, effect) | done | 2026-11-16 |
| 2.8 sndobj gate | done | 2026-11-16 |
| 3.1 stk finalizers | | |
| 3.2 stk opaque lookups | | |
| 3.3 stk ctor tables (8 families) | | |
| 3.4 stk ctor tails | | |
| 3.5 stk args and ladders (9 commits) | | |
| 3.6 stk gate | | |
| 4.1 labsound finalizers | | |
| 4.2 node group + smoke test | | |
| 4.3 lookups and casts | | |
| 4.4 ctor tails | | |
| 4.5 options and numbers | | |
| 4.6 labsound gate | | |
| 5 closing | | |

| File | Lines at `d530d27` | After phase 2 | After phase 3 | After phase 4 |
|---|---|---|---|---|
| `quickjs-sndobj.cpp` | 1428 | 1283 | | |
| `quickjs-stk.cpp` | 4000 | | | |
| `quickjs-labsound.cpp` | 3297 | | | |
| `js-utils.hpp` | 575 | 1029 (phase 1) | | |
