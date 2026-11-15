# Call-site inventory for the js-utils.hpp refactoring

Companion of `js-utils-helpers-plan.md` (what to do, in which order) and
`js-utils-helpers-recipes.md` (how to transform each pattern). This file says
**where**. Generated 2026-11-15 from commit `d530d27` (working tree: only `BUGS`
modified). Line numbers are those of that commit and shift while you edit; use the
"find again" command at the top of each section, not the numbers, to see what is
left. Function names do not change.

Read the sections in the order of the plan phases. Each table row is one thing to
open in the editor.

## 1. H1 `args`: unchecked conversions and argc guards

Find again (statement-position conversions, the ones whose result is ignored):

```sh
grep -nE '^\s+JS_To(Float64|Int32|Uint32|Int64)\(' quickjs-{sndobj,stk,labsound}.cpp
```

Done when that command prints nothing for the file. `argc` columns are the lines
with an `argc` comparison; they disappear with the conversions they guard, except
guards that select a code path (`argc == 0` constructors, `argc < N` errors), which stay.

### 1.1 quickjs-sndobj.cpp

| Function | Lines | Ignored conversions (line) | argc tests (line) |
|---|---|---|---|
| `js_sndobj_common_method` | 98-140 | 113 | 112 |
| `js_sndobj_common_set` | 154-170 | 158, 165 | - |
| `js_sndobjtable_method` | 201-256 | 214, 227, 230, 242 | 213, 226, 229, 241 |
| `js_sndobjtable_constructor` | 291-346 | 307, 310, 313, 316 | 300, 309, 312, 315 |
| `js_sndobjgenerator_method` | 380-469 | 394, 414, 448, 459 | 395, 415 |
| `js_sndobjgenerator_constructor` | 509-670 | 526, 529, 543, 546, 557, 560, 563, 577, 580, 590, 593, 607, 610 | 520, 525, 528, 533, 536, 542, 545, 556, 559, 562, 567, 570, 576, 579, 589, 592, 597, 600, 606, 609, 638, 640, 642, 646, 648, 652, 654 |
| `js_sndobjeffect_method` | 778-1010 | 793, 825, 826, 827, 828, 839, 849, 865, 881, 893, 903, 914, 924, 934, 950, 966 | 850, 866, 935, 951, 967 |
| `js_sndobjeffect_constructor` | 1068-1343 | 1079, 1082, 1085, 1088, 1091, 1094, 1105, 1108, 1118, 1121, 1145, 1148, 1159, 1162, 1177, 1180, 1193, 1196, 1199, 1202, 1205, 1233, 1236, 1247, 1262, 1265, 1276, 1279 | 1078, 1081, 1084, 1087, 1090, 1093, 1098, 1104, 1107, 1117, 1120, 1123, 1135, 1138, 1144, 1147, 1158, 1161, 1164, 1176, 1179, 1192, 1195, 1198, 1201, 1204, 1207, 1217, 1220, 1223, 1226, 1232, 1235, 1246, 1249, 1261, 1264, 1275, 1278, 1306, 1311, 1313, 1320, 1322, 1324, 1326 |

Total ignored conversions: 72.

### 1.2 quickjs-stk.cpp

| Function | Lines | Ignored conversions (line) | argc tests (line) |
|---|---|---|---|
| `js_stk_set` | 258-275 | 268 | - |
| `js_stkframes_constructor` | 325-349 | 332, 335, 339 | 331, 334, 338, 341 |
| `js_stkframes_method` | 361-471 | 372, 374, 378, 389, 400, 405, 415, 420 | 373, 376, 390, 430, 455 |
| `js_stkframes_set` | 541-558 | 551 | - |
| `js_stkgenerator_constructor` | 607-711 | 612, 649 | 611, 625, 629, 633, 641, 650, 663, 677 |
| `js_stkfilter_constructor` | 852-999 | 862, 864, 921, 934 | 861, 863, 885, 899, 900, 913, 920, 933 |
| `js_stkfilter_method` | 1006-1043 | 1029 | 1019 |
| `js_stkeffect_constructor` | 1077-1145 | 1081, 1104 | 1080, 1086, 1090, 1098, 1103, 1105, 1109, 1117 |
| `js_stkeffect_method` | 1154-1247 | 1196, 1198, 1236 | 1169, 1197 |
| `js_stkfm_constructor` | 1282-1340 | 1286 | 1285 |
| `js_stkfm_method` | 1354-1412 | 1384, 1386, 1394, 1402, 1404 | 1366, 1367, 1376, 1385, 1393, 1403 |
| `js_stkinstrmnt_constructor` | 1461-1565 | 1465, 1507, 1508 | 1464, 1482, 1486, 1490, 1512, 1515, 1532, 1540, 1544 |
| `js_stkinstrmnt_method` | 1575-1633 | 1605, 1607, 1615, 1623, 1625 | 1587, 1588, 1597, 1606, 1614, 1624 |
| `js_stkfunction_method` | 1702-1759 | 1729, 1742 | 1715 |
| `js_twintdrum_constructor` | 1791-1811 | 1794 | 1793 |
| `js_twintdrum_method` | 1825-1866 | 1838, 1840 | 1837, 1839, 1845, 1846, 1847, 1849, 1854 |
| `js_tr909bassdrum_method` | 1981-2093 | 1995, 1996, 1997, 2003, 2005, 2011, 2017, 2019, 2025, 2035, 2041, 2047, 2049, 2055, 2057, 2063, 2070, 2080 | 1998, 2004, 2012, 2018, 2026, 2048, 2056, 2069, 2077, 2079 |
| `js_tr909percussion_method` | 2143-2236 | 2157, 2158, 2159, 2160, 2166, 2167, 2168, 2170, 2176, 2177, 2183, 2185, 2191, 2198, 2200, 2206, 2213, 2223 | 2169, 2184, 2186, 2192, 2199, 2212, 2220, 2222 |
| `js_stkwvin_constructor` | 2260-2323 | 2271, 2273, 2275, 2277, 2279, 2288, 2290 | 2270, 2272, 2274, 2276, 2278, 2287, 2289 |
| `js_inetwvin_method` | 2456-2495 | 2473, 2475, 2477, 2479 | 2472, 2474, 2476, 2478 |
| `js_stkwvout_constructor` | 2512-2577 | 2523, 2525, 2527, 2529, 2531, 2540 | 2522, 2524, 2526, 2528, 2530, 2539 |
| `js_stkwvout_method` | 2586-2618 | 2603 | 2597 |
| `js_inetwvout_method` | 2708-2758 | 2725, 2727, 2733, 2735 | 2726, 2728, 2732, 2734 |
| `js_midifilein_method` | 2817-2875 | 2828, 2839, 2851 | 2827, 2838, 2850 |
| `js_rtmidiin_constructor` | 2929-2976 | 2935, 2944 | 2934, 2936, 2943 |
| `js_rtmidiin_method` | 2991-3096 | 3003, 3049 | 3002, 3004, 3020, 3048, 3061, 3063, 3065 |
| `js_rtmidiout_constructor` | 3123-3167 | 3128 | 3127, 3129 |
| `js_rtmidiout_method` | 3181-3267 | 3193, 3239 | 3192, 3194, 3210, 3238, 3251 |
| `js_rtaudio_constructor` | 3302-3332 | 3305 | 3304 |
| `js_rtaudio_method` | 3357-3479 | 3391, 3457 | 3471 |

Total ignored conversions: 115.

### 1.3 quickjs-labsound.cpp

| Function | Lines | Ignored conversions (line) | argc tests (line) |
|---|---|---|---|
| `anchor_node_in_context` | 327-359 | 347 | - |
| `js_audiocontext_get` | 515-554 | 548 | - |
| `js_audiocontext_connect` | 557-588 | 571, 573, 582 | 561, 570, 572, 581 |
| `js_audionode_connect` | 972-1002 | 983, 985, 996 | 976, 982, 984, 995 |
| `js_oscillator_constructor` | 1184-1237 | 1211, 1219 | 1185, 1195 |
| `js_audiobuffer_constructor` | 1754-1784 | 1763, 1768, 1773 | 1755 |
| `js_audiobuffer_method` | 1793-1856 | 1804, 1816, 1825, 1838, 1847 | 1801, 1813, 1824, 1835, 1846 |
| `js_absource_constructor` | 1905-1967 | 1930, 1938 | 1906, 1917 |
| `js_absource_start` | 1970-1992 | 1980, 1982 | 1979, 1981 |
| `js_delay_constructor` | 2236-2277 | 2250, 2255 | 2237, 2247 |
| `js_stereopanner_constructor` | 2517-2547 | 2530 | 2518, 2526 |
| `js_compressor_constructor` | 2867-2907 | 2889 | 2868, 2876 |
| `js_constantsource_constructor` | 2960-2990 | 2973 | 2961, 2969 |

Total ignored conversions: 27.

## 2. H2 `dispatch` and `require`: dynamic_cast sites

Find again: `grep -n "dynamic_cast<" quickjs-{sndobj,stk}.cpp`. Done when every
remaining hit is one of the plain checked casts listed under 2.3.

### 2.1 quickjs-sndobj.cpp

Multi-type ladders (3): use `dispatch<...>`.

| Line | Function | Type list, in ladder order (first match wins) | Call per branch |
|---|---|---|---|
| 401 | `js_sndobjgenerator_method` on `obj` | Oscil, Buzz, Randh | `p->SetFreq((float)fr, mod);` |
| 421 | `js_sndobjgenerator_method` on `obj` | Oscil, Buzz, Rand | `p->SetAmp((float)amp, mod);` |
| 883 | `js_sndobjeffect_method` on `obj` | Comb, Gain | `p->SetGain((float)v);` |

Single-type checks (20): use `require<T>` (decision D1) or keep the cast.

| Line | Function | Type | Statement |
|---|---|---|---|
| 232 | `js_sndobjtable_method` | HarmTable | `p->SetHarm(harm, type);` |
| 244 | `js_sndobjtable_method` | HarmTable | `p->SetPhase((float)phase);` |
| 438 | `js_sndobjgenerator_method` | Oscil | `p->SetTable(table);` |
| 450 | `js_sndobjgenerator_method` | Oscil | `p->SetPhase((float)phase);` |
| 461 | `js_sndobjgenerator_method` | Buzz | `p->SetHarm(harm);` |
| 795 | `js_sndobjeffect_method` | ADSR | `p->SetMaxAmp((float)v);` |
| 802 | `js_sndobjeffect_method` | ADSR | `p->Sustain();` |
| 809 | `js_sndobjeffect_method` | ADSR | `p->Release();` |
| 816 | `js_sndobjeffect_method` | ADSR | `p->Restart();` |
| 830 | `js_sndobjeffect_method` | ADSR | `p->SetADSR((float)a, (float)d, (float)s, (float)r);` |
| 841 | `js_sndobjeffect_method` | ADSR | `p->SetDur((float)v);` |
| 856 | `js_sndobjeffect_method` | Reson | `p->SetFreq((float)v, mod);` |
| 872 | `js_sndobjeffect_method` | Reson | `p->SetBW((float)v, mod);` |
| 895 | `js_sndobjeffect_method` | Gain | `p->SetGainM((float)v);` |
| 906 | `js_sndobjeffect_method` | Gain | `r = p->dBToAmp((float)v);` |
| 916 | `js_sndobjeffect_method` | VDelay | `p->SetMaxDelayTime((float)v);` |
| 926 | `js_sndobjeffect_method` | VDelay | `p->SetDelayTime((float)v);` |
| 941 | `js_sndobjeffect_method` | VDelay | `p->SetFdbgain((float)v, mod);` |
| 957 | `js_sndobjeffect_method` | VDelay | `p->SetFwdgain((float)v, mod);` |
| 973 | `js_sndobjeffect_method` | VDelay | `p->SetDirgain((float)v, mod);` |

### 2.2 quickjs-stk.cpp

Multi-type ladders (3): use `dispatch<...>`.

| Line | Function | Type list, in ladder order (first match wins) | Call per branch |
|---|---|---|---|
| 748 | `js_stkgenerator_method` on `g` | stk::Granulate, stk::ADSR, stk::Asymp, stk::BlitSaw, stk::BlitSquare, stk::Blit, stk::Envelope, stk::Modulate, stk::Noise, stk::SineWave, stk::SingWave | `sample = p->tick();`; `sample = p->tick(channel);` |
| 1174 | `js_stkeffect_method` on `eff` | stk::FreeVerb, stk::JCRev, stk::PRCRev, stk::NRev, stk::Chorus, stk::Echo, stk::PitShift, stk::LentPitShift | `p->tick(*a->get(), channel);` |
| 1203 | `js_stkeffect_method` on `eff` | stk::FreeVerb, stk::JCRev, stk::PRCRev, stk::NRev, stk::Chorus, stk::Echo, stk::PitShift, stk::LentPitShift | `p->tick(in1);`; `p->tick(in1, in2);` |

Single-type checks (2): use `require<T>` (decision D1) or keep the cast.

| Line | Function | Type | Statement |
|---|---|---|---|
| 1721 | `js_stkfunction_method` | stk::Cubic | `cubic->tick(*a->get(), channel);` |
| 1744 | `js_stkfunction_method` | stk::Cubic | `switch(magic) {` |

### 2.3 Plain checked casts in stk (keep, or convert with `require<T>`)

| Line | Function | Statement |
|---|---|---|
| 1832 | `js_twintdrum_method` | `TwinTDrum* d = dynamic_cast<TwinTDrum*>(p->get());` |
| 1988 | `js_tr909bassdrum_method` | `Tr909BassDrum* d = dynamic_cast<Tr909BassDrum*>(p->get());` |
| 2150 | `js_tr909percussion_method` | `Tr909Percussion* d = dynamic_cast<Tr909Percussion*>(p->get());` |
| 2432 | `js_rtwvin_method` | `stk::RtWvIn* r = dynamic_cast<stk::RtWvIn*>(w->get());` |
| 2463 | `js_inetwvin_method` | `stk::InetWvIn* n = dynamic_cast<stk::InetWvIn*>(w->get());` |
| 2684 | `js_rtwvout_method` | `stk::RtWvOut* r = dynamic_cast<stk::RtWvOut*>(w->get());` |
| 2714 | `js_inetwvout_method` | `stk::InetWvOut* n = dynamic_cast<stk::InetWvOut*>(w->get());` |

## 3. H3 `finalize`: finalizers

Find again: `grep -n "_finalizer(JSRuntime" quickjs-{sndobj,stk,labsound}.cpp`. Done when
every finalizer in the "pure" and "delete" classes is gone and its `JSClassDef` names
`qjsx::finalize<...>` or `qjsx::finalize_delete<...>` directly.

### 3.1 quickjs-sndobj.cpp

| Line | Function | Opaque type | Class id | Class |
|---|---|---|---|---|
| 259 | `js_sndobjtable_finalizer` | SndObjTablePtr | js_sndobjtable_class_id | pure (destroy + free): finalize<T, id> |
| 472 | `js_sndobjgenerator_finalizer` | SndObjPtr | js_sndobjgenerator_class_id | pure (destroy + free): finalize<T, id> |
| 1013 | `js_sndobjeffect_finalizer` | SndObjPtr | js_sndobjeffect_class_id | pure (destroy + free): finalize<T, id> |

### 3.2 quickjs-stk.cpp

| Line | Function | Opaque type | Class id | Class |
|---|---|---|---|---|
| 278 | `js_stk_finalizer` | StkPtr | js_stk_class_id | pure (destroy + free): finalize<T, id> |
| 561 | `js_stkframes_finalizer` | StkFramesPtr | js_stkframes_class_id | pure (destroy + free): finalize<T, id> |
| 815 | `js_stkgenerator_finalizer` | StkGeneratorPtr | js_stkgenerator_class_id | pure (destroy + free): finalize<T, id> |
| 1046 | `js_stkfilter_finalizer` | StkFilterPtr | js_stkfilter_class_id | pure (destroy + free): finalize<T, id> |
| 1250 | `js_stkeffect_finalizer` | StkEffectPtr | js_stkeffect_class_id | pure (destroy + free): finalize<T, id> |
| 1415 | `js_stkfm_finalizer` | StkFMPtr | js_stkfm_class_id | pure (destroy + free): finalize<T, id> |
| 1636 | `js_stkinstrmnt_finalizer` | StkInstrmntPtr | js_stkinstrmnt_class_id | pure (destroy + free): finalize<T, id> |
| 1762 | `js_stkfunction_finalizer` | StkFunctionPtr | js_stkfunction_class_id | pure (destroy + free): finalize<T, id> |
| 2396 | `js_stkwvin_finalizer` | StkWvInPtr | js_stkwvin_class_id | pure (destroy + free): finalize<T, id> |
| 2648 | `js_stkwvout_finalizer` | StkWvOutPtr | js_stkwvout_class_id | pure (destroy + free): finalize<T, id> |
| 2901 | `js_midifilein_finalizer` | stk::MidiFileIn | js_midifilein_class_id | allocated with new, `delete`: finalize_delete<T, id> |
| 3099 | `js_rtmidiin_finalizer` | RtMidiIn | js_rtmidiin_class_id | allocated with new, `delete`: finalize_delete<T, id> |
| 3270 | `js_rtmidiout_finalizer` | RtMidiOut | js_rtmidiout_class_id | allocated with new, `delete`: finalize_delete<T, id> |
| 3482 | `js_rtaudio_finalizer` | RtAudio | js_rtaudio_class_id | allocated with new, `delete`: finalize_delete<T, id> |

### 3.3 quickjs-labsound.cpp

| Line | Function | Opaque type | Class id | Class |
|---|---|---|---|---|
| 906 | `js_audiocontext_finalizer` | AudioContextPtr | js_audiocontext_class_id | SPECIAL: keep the body, see notes |
| 1056 | `js_audionode_finalize_with` | JsAudioNode | cid | pure (destroy + free): finalize<T, id> |
| 1134 | `js_audiodestinationnode_finalizer` | - | js_audiodestinationnode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 1280 | `js_oscillatornode_finalizer` | - | js_oscillatornode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 1333 | `js_gainnode_finalizer` | - | js_gainnode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 1463 | `js_biquadfilternode_finalizer` | - | js_biquadfilternode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 1592 | `js_audioparam_finalizer` | JsAudioParam | js_audioparam_class_id | pure (destroy + free): finalize<T, id> |
| 1647 | `js_audiolistener_finalizer` | AudioListenerPtr | js_audiolistener_class_id | pure (destroy + free): finalize<T, id> |
| 1712 | `js_audiodevice_finalizer` | AudioDevicePtr | js_audiodevice_class_id | pure (destroy + free): finalize<T, id> |
| 1877 | `js_audiobuffer_finalizer` | JsAudioBuffer | js_audiobuffer_class_id | pure (destroy + free): finalize<T, id> |
| 2050 | `js_audiobuffersourcenode_finalizer` | - | js_audiobuffersourcenode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2155 | `js_noisenode_finalizer` | - | js_noisenode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2210 | `js_audiosetting_finalizer` | JsAudioSetting | js_audiosetting_class_id | pure (destroy + free): finalize<T, id> |
| 2291 | `js_delaynode_finalizer` | - | js_delaynode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2373 | `js_iirfilternode_finalizer` | - | js_iirfilternode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2499 | `js_waveshapernode_finalizer` | - | js_waveshapernode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2561 | `js_stereopannernode_finalizer` | - | js_stereopannernode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2668 | `js_convolvernode_finalizer` | - | js_convolvernode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 2836 | `js_analysernode_finalizer` | JsAudioNode | js_analysernode_class_id | pure (destroy + free): finalize<T, id> |
| 2938 | `js_dynamicscompressornode_finalizer` | - | js_dynamicscompressornode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |
| 3004 | `js_constantsourcenode_finalizer` | - | js_constantsourcenode_class_id | forwarder to js_audionode_finalize_with: use finalize<JsAudioNode, id> |

Note on `js_audiocontext_finalizer` (labsound): it also erases the context from the
`offline_state` map before destroying. Keep the function; replace only its last two
lines by a call to `qjsx::finalize<AudioContextPtr, js_audiocontext_class_id>` if you want,
after the map cleanup (the map lookup needs the pointer first, so read it before).

## 4. H4 constructor tables (stk, sndobj)

Find again: `grep -n "JS_CFUNC_constructor_magic" quickjs-{sndobj,stk}.cpp`. Done when it
prints nothing and `JS_AddModuleExport` appears only for the non-constructor exports.

The tables below are the current registrations, in bind order (the order of the
`JS_NewCFunction2` lines), ready to paste. Check each against the enum
before pasting: where the bind order differs from the enum order the table here follows
the bind order and says so, because `ctor_name(table, magic)` looks the name up by
magic value, not by index (so any order is correct).

### 4.1 quickjs-sndobj.cpp

`js_sndobjtable_constructor`: 1 constructor(s), bound at lines 1370-1370; prototype property defined: no

```cpp
static const qjsx::ctor_def sndobjtable_ctors[] = {
  { "HarmTable", 4, INSTANCE_HARM_TABLE },
};
```

`js_sndobjgenerator_constructor`: 3 constructor(s), bound at lines 1373-1377; prototype property defined: no

```cpp
static const qjsx::ctor_def sndobjgenerator_ctors[] = {
  { "Oscili", 7, INSTANCE_OSCILI },
  { "Buzz", 7, INSTANCE_BUZZ },
  { "Randi", 6, INSTANCE_RANDI },
};
```

`js_sndobjeffect_constructor`: 7 constructor(s), bound at lines 1380-1392; prototype property defined: no

```cpp
static const qjsx::ctor_def sndobjeffect_ctors[] = {
  { "ADSR", 9, INSTANCE_ADSR },
  { "Reson", 7, INSTANCE_RESON },
  { "Comb", 5, INSTANCE_COMB },
  { "Allpass", 5, INSTANCE_ALLPASS },
  { "VDelay", 12, INSTANCE_VDELAY },
  { "Gain", 4, INSTANCE_GAIN },
  { "Mixer", 2, INSTANCE_MIXER },
};
```

### 4.2 quickjs-stk.cpp

`js_stkfilter_constructor`: 13 constructor(s), bound at lines 3553-3589; prototype property defined: yes (stkfilter_proto)

```cpp
static const qjsx::ctor_def stkfilter_ctors[] = {
  { "BiQuad", 0, INSTANCE_BIQUAD },
  { "DelayA", 0, INSTANCE_DELAY_A },
  { "DelayL", 0, INSTANCE_DELAY_L },
  { "Delay", 0, INSTANCE_DELAY },
  { "Fir", 1, INSTANCE_FIR },
  { "FormSwep", 0, INSTANCE_FORM_SWEP },
  { "Iir", 0, INSTANCE_IIR },
  { "OnePole", 0, INSTANCE_ONE_POLE },
  { "OneZero", 0, INSTANCE_ONE_ZERO },
  { "PoleZero", 0, INSTANCE_POLE_ZERO },
  { "TapDelay", 0, INSTANCE_TAP_DELAY },
  { "TwoPole", 0, INSTANCE_TWO_POLE },
  { "TwoZero", 0, INSTANCE_TWO_ZERO },
};
```

`js_stkgenerator_constructor`: 11 constructor(s), bound at lines 3605-3625; prototype property defined: no

```cpp
static const qjsx::ctor_def stkgenerator_ctors[] = {
  { "ADSR", 0, INSTANCE_ADSR },
  { "Asymp", 0, INSTANCE_ASYMP },
  { "BlitSaw", 0, INSTANCE_BLIT_SAW },
  { "BlitSquare", 0, INSTANCE_BLIT_SQUARE },
  { "Blit", 0, INSTANCE_BLIT },
  { "Envelope", 0, INSTANCE_ENVELOPE },
  { "Granulate", 0, INSTANCE_GRANULATE },
  { "Modulate", 0, INSTANCE_MODULATE },
  { "Noise", 0, INSTANCE_NOISE },
  { "SineWave", 0, INSTANCE_SINE_WAVE },
  { "SingWave", 0, INSTANCE_SING_WAVE, "StkSingWave" },
};
```

`js_stkeffect_constructor`: 8 constructor(s), bound at lines 3641-3655; prototype property defined: no

```cpp
static const qjsx::ctor_def stkeffect_ctors[] = {
  { "Chorus", 0, INSTANCE_CHORUS },
  { "Echo", 0, INSTANCE_ECHO },
  { "FreeVerb", 0, INSTANCE_FREEVERB },
  { "JCRev", 0, INSTANCE_JCREV },
  { "LentPitShift", 0, INSTANCE_LENTPITSHIFT },
  { "NRev", 0, INSTANCE_NREV },
  { "PitShift", 0, INSTANCE_PITSHIFT },
  { "PRCRev", 0, INSTANCE_PRCREV },
};
```

`js_stkfm_constructor`: 7 constructor(s), bound at lines 3672-3684; prototype property defined: no

```cpp
static const qjsx::ctor_def stkfm_ctors[] = {
  { "BeeThree", 0, INSTANCE_BEETHREE },
  { "FMVoices", 0, INSTANCE_FMVOICES },
  { "HevyMetl", 0, INSTANCE_HEVYMETL },
  { "PercFlut", 0, INSTANCE_PERCFLUT },
  { "Rhodey", 0, INSTANCE_RHODEY },
  { "TubeBell", 0, INSTANCE_TUBEBELL },
  { "Wurley", 0, INSTANCE_WURLEY },
};
```

`js_stkinstrmnt_constructor`: 20 constructor(s), bound at lines 3699-3737; prototype property defined: no

```cpp
static const qjsx::ctor_def stkinstrmnt_ctors[] = {
  { "BandedWG", 0, INSTANCE_BANDEDWG },
  { "BlowBotl", 0, INSTANCE_BLOWBOTL },
  { "BlowHole", 0, INSTANCE_BLOWHOLE },
  { "Bowed", 0, INSTANCE_BOWED },
  { "Brass", 0, INSTANCE_BRASS },
  { "Clarinet", 0, INSTANCE_CLARINET },
  { "Drummer", 0, INSTANCE_DRUMMER },
  { "Flute", 0, INSTANCE_FLUTE },
  { "Mandolin", 0, INSTANCE_MANDOLIN },
  { "Mesh2D", 0, INSTANCE_MESH2D },
  { "Plucked", 0, INSTANCE_PLUCKED },
  { "Recorder", 0, INSTANCE_RECORDER },
  { "Resonate", 0, INSTANCE_RESONATE },
  { "Saxofony", 0, INSTANCE_SAXOFONY },
  { "Shakers", 0, INSTANCE_SHAKERS },
  { "Simple", 0, INSTANCE_SIMPLE },
  { "Sitar", 0, INSTANCE_SITAR },
  { "StifKarp", 0, INSTANCE_STIFKARP },
  { "VoicForm", 0, INSTANCE_VOICFORM },
  { "Whistle", 0, INSTANCE_WHISTLE },
};
```

`js_stkfunction_constructor`: 1 constructor(s), bound at lines 3787-3787; prototype property defined: no

```cpp
static const qjsx::ctor_def stkfunction_ctors[] = {
  { "Cubic", 0, INSTANCE_CUBIC },
};
```

`js_stkwvin_constructor`: 2 constructor(s), bound at lines 3810-3814; prototype property defined: no

```cpp
static const qjsx::ctor_def stkwvin_ctors[] = {
  { "RtWvIn", 0, INSTANCE_WVIN_RTWVIN },
  { "InetWvIn", 0, INSTANCE_WVIN_INETWVIN },
};
```

`js_stkwvout_constructor`: 2 constructor(s), bound at lines 3836-3840; prototype property defined: no

```cpp
static const qjsx::ctor_def stkwvout_ctors[] = {
  { "RtWvOut", 0, INSTANCE_WVOUT_RTWVOUT },
  { "InetWvOut", 0, INSTANCE_WVOUT_INETWVOUT },
};
```

Not table-driven (one-off registrations, leave them): in `quickjs-stk.cpp` the exports
Effect, FM, Filter, Function, Generator, MidiFileIn, RtAudio, RtMidiIn, RtMidiOut, Stk, StkFrames, Tr909BassDrum, Tr909Percussion, TwinTDrum.

Known disagreement the table removes (BUGS `stk-tostringtag-array-out-of-enum-order`,
`stk-singwave-exported-as-stksingwave`): the name arrays at `quickjs-stk.cpp:698`
(generators) and `:1133` (effects) list `Blit, BlitSaw, BlitSquare` and `PRCRev, PitShift`
in a different order than the enums, and SingWave is exported as `StkSingWave`.

## 5. H5 `class_group`, H6 `self` / `opaque` / `cast_shared` (labsound, sndobj)

### 5.1 Node classes of `any_audio_node` (labsound, `quickjs-labsound.cpp:114`)

Add each id to the group in the class init function of that class (the place that
calls `JS_NewClassID(&id)`); the order is the order of the old probe.

- `js_audiodestinationnode_class_id`
- `js_oscillatornode_class_id`
- `js_gainnode_class_id`
- `js_biquadfilternode_class_id`
- `js_iirfilternode_class_id`
- `js_audiobuffersourcenode_class_id`
- `js_noisenode_class_id`
- `js_delaynode_class_id`
- `js_waveshapernode_class_id`
- `js_stereopannernode_class_id`
- `js_convolvernode_class_id`
- `js_analysernode_class_id`
- `js_dynamicscompressornode_class_id`
- `js_constantsourcenode_class_id`

Not nodes (never in the group): `js_audiocontext_class_id`, `js_audiobuffer_class_id`,
`js_audioparam_class_id`, `js_audiosetting_class_id`, `js_audiolistener_class_id`,
`js_audiodevice_class_id`.

### 5.2 sndobj probes

- `js_sndobj_arg` (line 57): group of `js_sndobjgenerator_class_id` and `js_sndobjeffect_class_id`, element `SndObjPtr`, message `expected an sndobj generator or effect instance`
- `js_sndobjtable_arg` (line 71): single class `js_sndobjtable_class_id`, element `SndObjTablePtr`; use `self`-style check, no group

Both return the raw pointer inside the `unique_ptr` (`->get()`), so the group is of
`SndObjPtr` and the caller does `->get()`; see recipe R5.

### 5.3 `JS_GetOpaque` / `JS_GetOpaque2` by type and class id

Strict lookups (`Opaque2`, throw) become `qjsx::self<T>`; soft lookups (`Opaque`,
null when it is another class) become `qjsx::opaque<T>`.

**quickjs-sndobj.cpp** (11 typed lookups)

| Sites | Becomes | Type | Class id |
|---|---|---|---|
| 3 | `self` | `SndObjPtr` | `js_sndobjeffect_class_id` |
| 3 | `self` | `SndObjPtr` | `js_sndobjgenerator_class_id` |
| 2 | `self` | `SndObjTablePtr` | `js_sndobjtable_class_id` |
| 1 | `opaque` | `SndObjPtr` | `js_sndobjeffect_class_id` |
| 1 | `opaque` | `SndObjPtr` | `js_sndobjgenerator_class_id` |
| 1 | `opaque` | `SndObjTablePtr` | `js_sndobjtable_class_id` |

**quickjs-stk.cpp** (53 typed lookups)

| Sites | Becomes | Type | Class id |
|---|---|---|---|
| 11 | `opaque` | `StkFramesPtr` | `js_stkframes_class_id` |
| 4 | `self` | `StkInstrmntPtr` | `js_stkinstrmnt_class_id` |
| 4 | `self` | `StkWvInPtr` | `js_stkwvin_class_id` |
| 4 | `self` | `StkWvOutPtr` | `js_stkwvout_class_id` |
| 3 | `self` | `StkFramesPtr` | `js_stkframes_class_id` |
| 3 | `self` | `StkGeneratorPtr` | `js_stkgenerator_class_id` |
| 2 | `self` | `StkPtr` | `js_stk_class_id` |
| 2 | `self` | `stk::MidiFileIn` | `js_midifilein_class_id` |
| 1 | `opaque` | `RtAudio` | `js_rtaudio_class_id` |
| 1 | `self` | `RtAudio` | `js_rtaudio_class_id` |
| 1 | `opaque` | `RtMidiIn` | `js_rtmidiin_class_id` |
| 1 | `self` | `RtMidiIn` | `js_rtmidiin_class_id` |
| 1 | `opaque` | `RtMidiOut` | `js_rtmidiout_class_id` |
| 1 | `self` | `RtMidiOut` | `js_rtmidiout_class_id` |
| 1 | `opaque` | `StkEffectPtr` | `js_stkeffect_class_id` |
| 1 | `self` | `StkEffectPtr` | `js_stkeffect_class_id` |
| 1 | `opaque` | `StkFMPtr` | `js_stkfm_class_id` |
| 1 | `self` | `StkFMPtr` | `js_stkfm_class_id` |
| 1 | `opaque` | `StkFilterPtr` | `js_stkfilter_class_id` |
| 1 | `self` | `StkFilterPtr` | `js_stkfilter_class_id` |
| 1 | `opaque` | `StkFunctionPtr` | `js_stkfunction_class_id` |
| 1 | `self` | `StkFunctionPtr` | `js_stkfunction_class_id` |
| 1 | `opaque` | `StkGeneratorPtr` | `js_stkgenerator_class_id` |
| 1 | `opaque` | `StkInstrmntPtr` | `js_stkinstrmnt_class_id` |
| 1 | `opaque` | `StkPtr` | `js_stk_class_id` |
| 1 | `opaque` | `StkWvInPtr` | `js_stkwvin_class_id` |
| 1 | `opaque` | `StkWvOutPtr` | `js_stkwvout_class_id` |
| 1 | `opaque` | `stk::MidiFileIn` | `js_midifilein_class_id` |

**quickjs-labsound.cpp** (69 typed lookups)

| Sites | Becomes | Type | Class id |
|---|---|---|---|
| 18 | `self` | `AudioContextPtr` | `js_audiocontext_class_id` |
| 6 | `self` | `JsAudioBuffer` | `js_audiobuffer_class_id` |
| 4 | `self` | `JsAudioNode` | `js_convolvernode_class_id` |
| 4 | `self` | `JsAudioParam` | `js_audioparam_class_id` |
| 3 | `self` | `JsAudioNode` | `js_analysernode_class_id` |
| 3 | `self` | `JsAudioNode` | `js_audiobuffersourcenode_class_id` |
| 3 | `self` | `JsAudioNode` | `js_waveshapernode_class_id` |
| 3 | `self` | `JsAudioSetting` | `js_audiosetting_class_id` |
| 2 | `opaque` | `AudioContextPtr` | `js_audiocontext_class_id` |
| 2 | `self` | `JsAudioNode` | `js_audiodestinationnode_class_id` |
| 2 | `self` | `JsAudioNode` | `js_biquadfilternode_class_id` |
| 2 | `self` | `JsAudioNode` | `js_noisenode_class_id` |
| 2 | `self` | `JsAudioNode` | `js_oscillatornode_class_id` |
| 2 | `opaque` | `JsAudioParam` | `js_audioparam_class_id` |
| 1 | `opaque` | `AudioDevicePtr` | `js_audiodevice_class_id` |
| 1 | `self` | `AudioDevicePtr` | `js_audiodevice_class_id` |
| 1 | `opaque` | `AudioListenerPtr` | `js_audiolistener_class_id` |
| 1 | `opaque` | `JsAudioBuffer` | `js_audiobuffer_class_id` |
| 1 | `opaque` | `JsAudioNode` | `cid` |
| 1 | `opaque` | `JsAudioNode` | `js_analysernode_class_id` |
| 1 | `self` | `JsAudioNode` | `js_constantsourcenode_class_id` |
| 1 | `self` | `JsAudioNode` | `js_delaynode_class_id` |
| 1 | `self` | `JsAudioNode` | `js_dynamicscompressornode_class_id` |
| 1 | `self` | `JsAudioNode` | `js_gainnode_class_id` |
| 1 | `self` | `JsAudioNode` | `js_iirfilternode_class_id` |
| 1 | `self` | `JsAudioNode` | `js_stereopannernode_class_id` |
| 1 | `opaque` | `JsAudioSetting` | `js_audiosetting_class_id` |

Find again: `grep -nE "static_cast<[A-Za-z:<>]+\*>\(\s*JS_GetOpaque2?\(" quickjs-*.cpp`.

### 5.4 labsound `dynamic_pointer_cast` sites (`cast_shared`)

The message in the last column is what the site throws today; `cast_shared` builds the
same text from the name argument with "an" before a vowel and "a" otherwise, so
pass the class name (`"OscillatorNode"`) and the message stays identical.

| Line | Function | Leaf type | Name argument | Current message |
|---|---|---|---|---|
| 1080 | `js_scheduledsource_start` | `lab::AudioScheduledSourceNode` | `"scheduled source node"` | InternalError: not a scheduled source node |
| 1099 | `js_scheduledsource_stop` | `lab::AudioScheduledSourceNode` | `"scheduled source node"` | InternalError: not a scheduled source node |
| 1250 | `js_oscillator_get` | `lab::OscillatorNode` | `"OscillatorNode"` | InternalError: not an OscillatorNode |
| 1266 | `js_oscillator_set` | `lab::OscillatorNode` | `"OscillatorNode"` | InternalError: not an OscillatorNode |
| 1326 | `js_gain_get_gain` | `lab::GainNode` | `"GainNode"` | InternalError: not a GainNode |
| 1431 | `js_biquadfilter_get` | `lab::BiquadFilterNode` | `"BiquadFilterNode"` | InternalError: not a BiquadFilterNode |
| 1449 | `js_biquadfilter_set` | `lab::BiquadFilterNode` | `"BiquadFilterNode"` | InternalError: not a BiquadFilterNode |
| 1703 | `js_audiodevice_set` | `lab::AudioDestinationNode` | `""` | InternalError: value must be AudioDestinationNode (**different wording, keep the throw**) |
| 1974 | `js_absource_start` | `lab::SampledAudioNode` | `"SampledAudioNode"` | InternalError: not a SampledAudioNode |
| 2004 | `js_absource_get` | `lab::SampledAudioNode` | `"SampledAudioNode"` | InternalError: not a SampledAudioNode |
| 2035 | `js_absource_set_buffer` | `lab::SampledAudioNode` | `"SampledAudioNode"` | InternalError: not a SampledAudioNode |
| 2132 | `js_noise_get_type` | `lab::NoiseNode` | `"NoiseNode"` | InternalError: not a NoiseNode |
| 2143 | `js_noise_set_type` | `lab::NoiseNode` | `"NoiseNode"` | InternalError: not a NoiseNode |
| 2284 | `js_delay_get_delaytime` | `lab::DelayNode` | `"DelayNode"` | InternalError: not a DelayNode |
| 2351 | `js_iirfilter_get_frequency_response` | `lab::IIRFilterNode` | `"IIRFilterNode"` | InternalError: not an IIRFilterNode |
| 2462 | `js_waveshaper_get_oversample` | `lab::WaveShaperNode` | `"WaveShaperNode"` | InternalError: not a WaveShaperNode |
| 2473 | `js_waveshaper_set_oversample` | `lab::WaveShaperNode` | `"WaveShaperNode"` | InternalError: not a WaveShaperNode |
| 2489 | `js_waveshaper_set_curve` | `lab::WaveShaperNode` | `"WaveShaperNode"` | InternalError: not a WaveShaperNode |
| 2554 | `js_stereopanner_get_pan` | `lab::StereoPannerNode` | `"StereoPannerNode"` | InternalError: not a StereoPannerNode |
| 2620 | `js_convolver_get_buffer` | `lab::ConvolverNode` | `"ConvolverNode"` | InternalError: not a ConvolverNode |
| 2634 | `js_convolver_set_buffer` | `lab::ConvolverNode` | `"ConvolverNode"` | InternalError: not a ConvolverNode |
| 2649 | `js_convolver_get_normalize` | `lab::ConvolverNode` | `"ConvolverNode"` | InternalError: not a ConvolverNode |
| 2660 | `js_convolver_set_normalize` | `lab::ConvolverNode` | `"ConvolverNode"` | InternalError: not a ConvolverNode |
| 2736 | `js_analyser_get` | `lab::AnalyserNode` | `"AnalyserNode"` | InternalError: not an AnalyserNode |
| 2754 | `js_analyser_set` | `lab::AnalyserNode` | `"AnalyserNode"` | InternalError: not an AnalyserNode |
| 2807 | `js_analyser_method` | `lab::AnalyserNode` | `"AnalyserNode"` | InternalError: not an AnalyserNode |
| 2923 | `js_compressor_get` | `lab::DynamicsCompressorNode` | `"DynamicsCompressorNode"` | InternalError: not a DynamicsCompressorNode |
| 2997 | `js_constantsource_get_offset` | `lab::ConstantSourceNode` | `"ConstantSourceNode"` | InternalError: not a ConstantSourceNode |

## 6. H7 `new_target.prototype` ladders (use `qjsx::wrap`)

Find again: `grep -n 'new_target, "prototype"' quickjs-*.cpp`. Done when it prints nothing.
The fallback prototype variable in the last column becomes unnecessary only if its
class calls `JS_SetClassProto(ctx, id, proto)` (check: `grep -n JS_SetClassProto`),
because `js_object_from_target` falls back to the class prototype. The target
shape is `js_gain_constructor` (`quickjs-labsound.cpp:1299`).

### 6.1 quickjs-sndobj.cpp

| Line | Function | Falls back to | Builds object with |
|---|---|---|---|
| 325 | `js_sndobjtable_constructor` | `sndobjtable_proto` | JS_NewObjectProtoClass with `js_sndobjtable_class_id` |
| 618 | `js_sndobjgenerator_constructor` | `sndobjgenerator_proto` | ? |
| 1287 | `js_sndobjeffect_constructor` | `sndobjeffect_proto` | JS_NewObjectProtoClass with `js_sndobjeffect_class_id` |

### 6.2 quickjs-stk.cpp

| Line | Function | Falls back to | Builds object with |
|---|---|---|---|
| 2301 | `js_stkwvin_constructor` | `?` | JS_NewObjectProtoClass with `js_stkwvin_class_id` |
| 2555 | `js_stkwvout_constructor` | `?` | JS_NewObjectProtoClass with `js_stkwvout_class_id` |
| 2784 | `js_midifilein_constructor` | `midifilein_proto` | JS_NewObjectProtoClass with `js_midifilein_class_id` |
| 2953 | `js_rtmidiin_constructor` | `rtmidiin_proto` | JS_NewObjectProtoClass with `js_rtmidiin_class_id` |
| 3144 | `js_rtmidiout_constructor` | `rtmidiout_proto` | JS_NewObjectProtoClass with `js_rtmidiout_class_id` |
| 3309 | `js_rtaudio_constructor` | `rtaudio_proto` | JS_NewObjectProtoClass with `js_rtaudio_class_id` |

### 6.3 quickjs-labsound.cpp

| Line | Function | Falls back to | Builds object with |
|---|---|---|---|
| 481 | `js_offlineaudiocontext_constructor` | `offlineaudiocontext_proto` | JS_NewObjectProtoClass with `js_audiocontext_class_id` |
| 1225 | `js_oscillator_constructor` | `oscillatornode_proto` | make_audio_node_js with `js_oscillatornode_class_id` |
| 1405 | `js_biquadfilter_constructor` | `biquadfilternode_proto` | make_audio_node_js with `js_biquadfilternode_class_id` |
| 1627 | `js_audiolistener_constructor` | `audiolistener_proto` | JS_NewObjectProtoClass with `js_audiolistener_class_id` |
| 1673 | `js_audiodevice_constructor` | `audiodevice_proto` | JS_NewObjectProtoClass with `js_audiodevice_class_id` |
| 1944 | `js_absource_constructor` | `audiobuffersourcenode_proto` | make_audio_node_js with `js_audiobuffersourcenode_class_id` |
| 2114 | `js_noise_constructor` | `noisenode_proto` | make_audio_node_js with `js_noisenode_class_id` |
| 2266 | `js_delay_constructor` | `delaynode_proto` | make_audio_node_js with `js_delaynode_class_id` |
| 2333 | `js_iirfilter_constructor` | `iirfilternode_proto` | make_audio_node_js with `js_iirfilternode_class_id` |
| 2444 | `js_waveshaper_constructor` | `waveshapernode_proto` | make_audio_node_js with `js_waveshapernode_class_id` |
| 2536 | `js_stereopanner_constructor` | `stereopannernode_proto` | make_audio_node_js with `js_stereopannernode_class_id` |
| 2602 | `js_convolver_constructor` | `convolvernode_proto` | make_audio_node_js with `js_convolvernode_class_id` |
| 2710 | `js_analyser_constructor` | `analysernode_proto` | make_audio_node_js with `js_analysernode_class_id` |
| 2896 | `js_compressor_constructor` | `dynamicscompressornode_proto` | make_audio_node_js with `js_dynamicscompressornode_class_id` |
| 2979 | `js_constantsource_constructor` | `constantsourcenode_proto` | make_audio_node_js with `js_constantsourcenode_class_id` |

## 7. H8 `option` (labsound options dictionaries)

Find again: `grep -nE "JS_GetPropertyStr\(ctx, argv\[[0-9]\]" quickjs-labsound.cpp` and
`grep -n "qjsx::property(" quickjs-*.cpp` (the already migrated style, which also ignores
a failed conversion).

| Line | Constructor | Option | Value kind today |
|---|---|---|---|
| 1198 | `js_oscillator_constructor` | `type` | string |
| 1208 | `js_oscillator_constructor` | `frequency` | number (IsNumber guard) |
| 1216 | `js_oscillator_constructor` | `detune` | number (IsNumber guard) |
| 1761 | `js_audiobuffer_constructor` | `numberOfChannels` | number (IsNumber guard) |
| 1766 | `js_audiobuffer_constructor` | `length` | number (IsNumber guard) |
| 1771 | `js_audiobuffer_constructor` | `sampleRate` | number (IsNumber guard) |
| 1920 | `js_absource_constructor` | `buffer` | bool |
| 1922 | `js_absource_constructor` | `loop` | number (IsNumber guard) |
| 1927 | `js_absource_constructor` | `playbackRate` | number (IsNumber guard) |
| 1935 | `js_absource_constructor` | `detune` | number (IsNumber guard) |
| 2103 | `js_noise_constructor` | `type` | string |
| 2248 | `js_delay_constructor` | `maxDelayTime` | number (IsNumber guard) |
| 2253 | `js_delay_constructor` | `delayTime` | number (IsNumber guard) |
| 2425 | `js_waveshaper_constructor` | `curve` | other (keep as is) |
| 2433 | `js_waveshaper_constructor` | `oversample` | string |
| 2527 | `js_stereopanner_constructor` | `pan` | number (IsNumber guard) |
| 2588 | `js_convolver_constructor` | `buffer` | object/opaque (keep as is) |
| 2596 | `js_convolver_constructor` | `normalize` | bool |
| 2970 | `js_constantsource_constructor` | `offset` | number (IsNumber guard) |

`qjsx::property(` call sites today: quickjs-labsound.cpp 5.

