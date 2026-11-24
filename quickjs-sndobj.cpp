#include <quickjs.h>
#include <cutils.h>

#include <vector>
#include <memory>
#include <cstring>

#include "defines.h"
#include "js-utils.hpp"

#include "SndObj.h"
#include "Table.h"
#include "HarmTable.h"
#include "HammingTable.h"

/* generators */
#include "Oscil.h"
#include "Oscili.h"
#include "Buzz.h"
#include "Rand.h"
#include "Randh.h"
#include "Randi.h"

/* effects */
#include "ADSR.h"
#include "Filter.h"
#include "Reson.h"
#include "DelayLine.h"
#include "Comb.h"
#include "Allpass.h"
#include "VDelay.h"
#include "Gain.h"
#include "Mix.h"

/* spectral(phase vocoder) */
#include "FFT.h"
#include "IFFT.h"
#include "PVA.h"
#include "PVS.h"
#include "PVMorph.h"
#include "PVMix.h"
#include "PVBlur.h"
#include "PVTransp.h"
#include "PVFilter.h"
#include "PVMask.h"

static JSClassID js_sndobjtable_class_id, js_sndobjgenerator_class_id, js_sndobjeffect_class_id;
static JSValue sndobjtable_proto, sndobjgenerator_proto, sndobjeffect_proto;

typedef std::unique_ptr<Table> SndObjTablePtr;
typedef std::unique_ptr<SndObj> SndObjPtr;

static qjsx::class_group<SndObjPtr> sndobj_inputs;

/* SndObj stores raw, non-owning pointers to its inputs/modulators(see
 * SndObj.h). Retaining a strong(hidden, non-enumerable) JS reference on the
 * owner object for every connected dependency keeps QuickJS's GC from
 * collecting a modulator/input that a native SndObj still points to. */
static void
js_sndobj_retain(JSContext* ctx, JSValueConst self, const char* slot, JSValueConst dep) {
  JSAtom atom = JS_NewAtom(ctx, slot);

  if(JS_IsUndefined(dep) || JS_IsNull(dep))
    JS_DeleteProperty(ctx, self, atom, 0);
  else
    JS_DefinePropertyValue(ctx, self, atom, JS_DupValue(ctx, dep), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

  JS_FreeAtom(ctx, atom);
}

static SndObj*
js_sndobj_arg(JSContext* ctx, JSValueConst v) {
  SndObjPtr* p = sndobj_inputs.get2(ctx, v, "an sndobj generator or effect instance");

  return p ? p->get() : nullptr;
}

static Table*
js_sndobjtable_arg(JSContext* ctx, JSValueConst v) {
  SndObjTablePtr* p = qjsx::opaque<SndObjTablePtr>(v, js_sndobjtable_class_id);

  if(!p) {
    JS_ThrowTypeError(ctx, "expected a Table instance");
    return nullptr;
  }

  return p->get();
}

enum {
  METHOD_PROCESS = 0,
  METHOD_OUTPUT,
  METHOD_BLOCK,
  METHOD_ENABLE,
  METHOD_DISABLE,
  METHOD_LEAF_BASE = 100,
};

enum {
  PROP_SR = 0,
  PROP_VECSIZE,
  PROP_ERROR,
  PROP_LEAF_BASE = 100,
};

static JSValue
js_sndobj_common_method(JSContext* ctx, SndObj* obj, int argc, JSValueConst argv[], int magic) {
  switch(magic) {
    case METHOD_PROCESS: {
      short ok = obj->DoProcess();

      if(obj->GetError())
        return JS_ThrowInternalError(ctx, "SndObj::DoProcess failed(error %d)", obj->GetError());

      return JS_NewBool(ctx, ok != 0);
    }

    case METHOD_OUTPUT: {
      int32_t pos = 0;

      if(qjsx::args(ctx, argc, argv, pos))
        return JS_EXCEPTION;

      return JS_NewFloat64(ctx, obj->Output(pos));
    }

    case METHOD_BLOCK: {
      int n = obj->GetVectorSize();
      std::vector<float> buf(n);

      for(int i = 0; i < n; i++)
        buf[i] = obj->Output(i);

      return qjsx::new_array<float>(ctx, buf);
    }

    case METHOD_ENABLE: {
      obj->Enable();
      return JS_UNDEFINED;
    }

    case METHOD_DISABLE: {
      obj->Disable();
      return JS_UNDEFINED;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_sndobj_common_get(JSContext* ctx, SndObj* obj, int magic) {
  switch(magic) {
    case PROP_SR: return JS_NewFloat64(ctx, obj->GetSr());
    case PROP_VECSIZE: return JS_NewInt32(ctx, obj->GetVectorSize());
    case PROP_ERROR: return JS_NewInt32(ctx, obj->GetError());
  }

  return JS_UNDEFINED;
}

static void
js_sndobj_common_set(JSContext* ctx, SndObj* obj, JSValueConst value, int magic) {
  switch(magic) {
    case PROP_SR: {
      float sr = 0;

      if(qjsx::element<float>::from_js(ctx, &sr, value))
        return;

      obj->SetSr(sr);
      break;
    }

    case PROP_VECSIZE: {
      int32_t vs = 0;

      if(qjsx::element<int32_t>::from_js(ctx, &vs, value))
        return;

      obj->SetVectorSize(vs);
      break;
    }
  }
}

/* ==================== Family A: Table(HarmTable) ==================== */

enum {
  TMETHOD_LOOKUP = 0,
  TMETHOD_TO_ARRAY,
  TMETHOD_SET_HARM,
  TMETHOD_SET_PHASE,
  TMETHOD_MAKE_TABLE,
  TMETHOD_SET_PARAM,
};

enum {
  TPROP_LEN = 0,
};

static JSValue
js_sndobjtable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SndObjTablePtr* t;

  if(!(t = static_cast<SndObjTablePtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjtable_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case TPROP_LEN: return JS_NewInt64(ctx, (*t)->GetLen());
  }

  return JS_UNDEFINED;
}

static JSValue
js_sndobjtable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  SndObjTablePtr* t;

  if(!(t = static_cast<SndObjTablePtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjtable_class_id))))
    return JS_EXCEPTION;

  Table* table = t->get();

  switch(magic) {
    case TMETHOD_LOOKUP: {
      int32_t pos = 0;

      if(qjsx::args(ctx, argc, argv, pos))
        return JS_EXCEPTION;

      return JS_NewFloat64(ctx, table->Lookup(pos));
    }

    case TMETHOD_TO_ARRAY: {
      return qjsx::new_array<float>(ctx, table->GetTable(), (size_t)table->GetLen());
    }

    case TMETHOD_SET_HARM: {
      int32_t harm = 0, type = SINE;

      if(qjsx::args(ctx, argc, argv, harm, type))
        return JS_EXCEPTION;

      HarmTable* p = qjsx::require<HarmTable>(ctx, table, "setHarm needs a HarmTable");
      if(!p)
        return JS_EXCEPTION;

      p->SetHarm(harm, type);

      return JS_UNDEFINED;
    }

    case TMETHOD_SET_PHASE: {
      float phase = 0;

      if(qjsx::args(ctx, argc, argv, phase))
        return JS_EXCEPTION;

      HarmTable* p = qjsx::require<HarmTable>(ctx, table, "setPhase needs a HarmTable");
      if(!p)
        return JS_EXCEPTION;

      p->SetPhase(phase);

      return JS_UNDEFINED;
    }

    case TMETHOD_MAKE_TABLE: {
      return JS_NewBool(ctx, table->MakeTable() != 0);
    }

    case TMETHOD_SET_PARAM: {
      int64_t len = 1024;
      float alpha = .54f;

      if(qjsx::args(ctx, argc, argv, len, alpha))
        return JS_EXCEPTION;

      HammingTable* p = qjsx::require<HammingTable>(ctx, table, "setParam needs a HammingTable");
      if(!p)
        return JS_EXCEPTION;

      p->SetParam((long)len, alpha);

      return JS_UNDEFINED;
    }
  }

  return JS_UNDEFINED;
}

static JSClassDef js_sndobjtable_class = {
  .class_name = "Table",
  .finalizer = qjsx::finalize<SndObjTablePtr, js_sndobjtable_class_id>,
};

static const JSCFunctionListEntry js_sndobjtable_funcs[] = {
  JS_CGETSET_MAGIC_DEF("len", js_sndobjtable_get, 0, TPROP_LEN),
  JS_CFUNC_MAGIC_DEF("lookup", 1, js_sndobjtable_method, TMETHOD_LOOKUP),
  JS_CFUNC_MAGIC_DEF("toArray", 0, js_sndobjtable_method, TMETHOD_TO_ARRAY),
  JS_CFUNC_MAGIC_DEF("setHarm", 2, js_sndobjtable_method, TMETHOD_SET_HARM),
  JS_CFUNC_MAGIC_DEF("setPhase", 1, js_sndobjtable_method, TMETHOD_SET_PHASE),
  JS_CFUNC_MAGIC_DEF("makeTable", 0, js_sndobjtable_method, TMETHOD_MAKE_TABLE),
  JS_CFUNC_MAGIC_DEF("setParam", 2, js_sndobjtable_method, TMETHOD_SET_PARAM),
  JS_PROP_INT32_DEF("SINE", SINE, JS_PROP_CONFIGURABLE),
  JS_PROP_INT32_DEF("SAW", SAW, JS_PROP_CONFIGURABLE),
  JS_PROP_INT32_DEF("SQUARE", SQUARE, JS_PROP_CONFIGURABLE),
  JS_PROP_INT32_DEF("BUZZ", BUZZ, JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_HARM_TABLE = 0,
  INSTANCE_HAMMING_TABLE,
};

static const qjsx::ctor_def sndobjtable_ctors[] = {
  { "HarmTable", 4, INSTANCE_HARM_TABLE },
  { "HammingTable", 2, INSTANCE_HAMMING_TABLE },
};

static JSValue
js_sndobjtable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  SndObjTablePtr t;

  switch(magic) {
    case INSTANCE_HARM_TABLE: {
      if(argc == 0) {
        t.reset(new HarmTable());
      } else {
        int64_t len = 4096;
        int32_t harm = 1, type = SINE;
        float phase = 0;

        if(qjsx::args(ctx, argc, argv, len, harm, type, phase))
          return JS_EXCEPTION;

        t.reset(new HarmTable((long)len, harm, type, phase));
      }

      break;
    }

    case INSTANCE_HAMMING_TABLE: {
      int64_t len = 1024;
      float alpha = .54f;

      if(qjsx::args(ctx, argc, argv, len, alpha))
        return JS_EXCEPTION;

      t.reset(new HammingTable((long)len, alpha));
      break;
    }
  }

  JSValue obj = qjsx::wrap(ctx, new_target, js_sndobjtable_class_id, std::move(t));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(sndobjtable_ctors, magic));
  return obj;
}

/* ================ Family B: generators(Oscili/Buzz/Randi) ================ */

enum {
  GMETHOD_SET_FREQ = METHOD_LEAF_BASE,
  GMETHOD_SET_AMP,
  GMETHOD_SET_TABLE,
  GMETHOD_SET_PHASE,
  GMETHOD_SET_HARM,
  GMETHOD_PUSH_IN,
};

static JSValue
js_sndobjgenerator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SndObjPtr* g;

  if(!(g = static_cast<SndObjPtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjgenerator_class_id))))
    return JS_EXCEPTION;

  return js_sndobj_common_get(ctx, g->get(), magic);
}

static JSValue
js_sndobjgenerator_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  SndObjPtr* g;

  if(!(g = static_cast<SndObjPtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjgenerator_class_id))))
    return JS_EXCEPTION;

  js_sndobj_common_set(ctx, g->get(), value, magic);
  return JS_UNDEFINED;
}

static JSValue
js_sndobjgenerator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  SndObjPtr* g;

  if(!(g = static_cast<SndObjPtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjgenerator_class_id))))
    return JS_EXCEPTION;

  if(magic < METHOD_LEAF_BASE)
    return js_sndobj_common_method(ctx, g->get(), argc, argv, magic);

  SndObj* obj = g->get();

  switch(magic) {
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

    case GMETHOD_SET_AMP: {
      float amp = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, amp))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      qjsx::dispatch<Oscil, Buzz, Rand>(obj, [&](auto* p) { p->SetAmp(amp, mod); });

      js_sndobj_retain(ctx, this_val, "__ampMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case GMETHOD_SET_TABLE: {
      Table* table = js_sndobjtable_arg(ctx, argv[0]);

      if(!table)
        return JS_EXCEPTION;

      Oscil* p = qjsx::require<Oscil>(ctx, obj, "setTable needs an Oscil");
      if(!p)
        return JS_EXCEPTION;

      p->SetTable(table);

      js_sndobj_retain(ctx, this_val, "__table", argv[0]);
      return JS_UNDEFINED;
    }

    case GMETHOD_SET_PHASE: {
      float phase = 0;

      if(qjsx::args(ctx, argc, argv, phase))
        return JS_EXCEPTION;

      Oscil* p = qjsx::require<Oscil>(ctx, obj, "setPhase needs an Oscil");
      if(!p)
        return JS_EXCEPTION;

      p->SetPhase(phase);

      return JS_UNDEFINED;
    }

    case GMETHOD_PUSH_IN: {
      std::vector<float> buf;

      if(qjsx::copy_array<float>(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, buf))
        return JS_EXCEPTION;

      return JS_NewInt32(ctx, obj->PushIn(buf.data(), (int)buf.size()));
    }

    case GMETHOD_SET_HARM: {
      int32_t harm = 1;

      if(qjsx::args(ctx, argc, argv, harm))
        return JS_EXCEPTION;

      Buzz* p = qjsx::require<Buzz>(ctx, obj, "setHarm needs a Buzz");
      if(!p)
        return JS_EXCEPTION;

      p->SetHarm(harm);

      return JS_UNDEFINED;
    }
  }

  return JS_UNDEFINED;
}

static JSClassDef js_sndobjgenerator_class = {
  .class_name = "SndObjGenerator",
  .finalizer = qjsx::finalize<SndObjPtr, js_sndobjgenerator_class_id>,
};

static const JSCFunctionListEntry js_sndobjgenerator_funcs[] = {
  JS_CFUNC_MAGIC_DEF("process", 0, js_sndobjgenerator_method, METHOD_PROCESS),
  JS_CFUNC_MAGIC_DEF("output", 1, js_sndobjgenerator_method, METHOD_OUTPUT),
  JS_CFUNC_MAGIC_DEF("block", 0, js_sndobjgenerator_method, METHOD_BLOCK),
  JS_CFUNC_MAGIC_DEF("enable", 0, js_sndobjgenerator_method, METHOD_ENABLE),
  JS_CFUNC_MAGIC_DEF("disable", 0, js_sndobjgenerator_method, METHOD_DISABLE),
  JS_CGETSET_MAGIC_DEF("sr", js_sndobjgenerator_get, js_sndobjgenerator_set, PROP_SR),
  JS_CGETSET_MAGIC_DEF("vecsize", js_sndobjgenerator_get, js_sndobjgenerator_set, PROP_VECSIZE),
  JS_CGETSET_MAGIC_DEF("error", js_sndobjgenerator_get, 0, PROP_ERROR),
  JS_CFUNC_MAGIC_DEF("setFreq", 1, js_sndobjgenerator_method, GMETHOD_SET_FREQ),
  JS_CFUNC_MAGIC_DEF("setAmp", 1, js_sndobjgenerator_method, GMETHOD_SET_AMP),
  JS_CFUNC_MAGIC_DEF("setTable", 1, js_sndobjgenerator_method, GMETHOD_SET_TABLE),
  JS_CFUNC_MAGIC_DEF("setPhase", 1, js_sndobjgenerator_method, GMETHOD_SET_PHASE),
  JS_CFUNC_MAGIC_DEF("setHarm", 1, js_sndobjgenerator_method, GMETHOD_SET_HARM),
  JS_CFUNC_MAGIC_DEF("pushIn", 1, js_sndobjgenerator_method, GMETHOD_PUSH_IN),
};

enum {
  INSTANCE_OSCILI = 0,
  INSTANCE_BUZZ,
  INSTANCE_RANDI,
  INSTANCE_SNDOBJ,
};

static const qjsx::ctor_def sndobjgenerator_ctors[] = {
  { "Oscili", 7, INSTANCE_OSCILI },
  { "Buzz", 7, INSTANCE_BUZZ },
  { "Randi", 6, INSTANCE_RANDI },
  { "SndObj", 3, INSTANCE_SNDOBJ },
};

static JSValue
js_sndobjgenerator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  SndObjPtr g;

  switch(magic) {
    case INSTANCE_OSCILI: {
      Table* table = nullptr;
      float fr = 440, amp = 1, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj *freqMod = nullptr, *ampMod = nullptr;

      if(!(table = js_sndobjtable_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED)))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 1, argc, argv, fr, amp))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(freqMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 4) && !(ampMod = js_sndobj_arg(ctx, argv[4])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 5, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      g.reset(new Oscili(table, fr, amp, freqMod, ampMod, vecsize, sr));
      break;
    }

    case INSTANCE_BUZZ: {
      float fr = 440, amp = 1, sr = DEF_SR;
      int32_t harms = 1, vecsize = DEF_VECSIZE;
      SndObj *freqMod = nullptr, *ampMod = nullptr;

      if(qjsx::args(ctx, argc, argv, fr, amp, harms))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(freqMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 4) && !(ampMod = js_sndobj_arg(ctx, argv[4])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 5, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      g.reset(new Buzz(fr, amp, (short)harms, freqMod, ampMod, vecsize, sr));
      break;
    }

    case INSTANCE_RANDI: {
      float fr = 1, amp = 1, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj *freqMod = nullptr, *ampMod = nullptr;

      if(qjsx::args(ctx, argc, argv, fr, amp))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 2) && !(freqMod = js_sndobj_arg(ctx, argv[2])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(ampMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 4, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      g.reset(new Randi(fr, amp, freqMod, ampMod, vecsize, sr));
      break;
    }

    case INSTANCE_SNDOBJ: {
      float sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj* input = nullptr;

      if(qjsx::present(argc, argv, 0) && !(input = js_sndobj_arg(ctx, argv[0])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 1, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      g.reset(new SndObj(input, vecsize, sr));
      break;
    }
  }

  JSValue obj = qjsx::wrap(ctx, new_target, js_sndobjgenerator_class_id, std::move(g));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(sndobjgenerator_ctors, magic));

  switch(magic) {
    case INSTANCE_OSCILI:
      if(argc > 0)
        js_sndobj_retain(ctx, obj, "__table", argv[0]);
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__freqMod", argv[3]);
      if(qjsx::present(argc, argv, 4))
        js_sndobj_retain(ctx, obj, "__ampMod", argv[4]);
      break;
    case INSTANCE_BUZZ:
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__freqMod", argv[3]);
      if(qjsx::present(argc, argv, 4))
        js_sndobj_retain(ctx, obj, "__ampMod", argv[4]);
      break;
    case INSTANCE_RANDI:
      if(qjsx::present(argc, argv, 2))
        js_sndobj_retain(ctx, obj, "__freqMod", argv[2]);
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__ampMod", argv[3]);
      break;
    case INSTANCE_SNDOBJ:
      if(qjsx::present(argc, argv, 0))
        js_sndobj_retain(ctx, obj, "__input", argv[0]);
      break;
  }

  return obj;
}

/* ========== Family C: effects(ADSR/Reson/Comb/Allpass/VDelay/Gain/Mixer) ========== */

enum {
  EMETHOD_SET_MAX_AMP = METHOD_LEAF_BASE,
  EMETHOD_SUSTAIN,
  EMETHOD_RELEASE,
  EMETHOD_RESTART,
  EMETHOD_SET_ADSR,
  EMETHOD_SET_DUR,
  EMETHOD_SET_FREQ,
  EMETHOD_SET_BW,
  EMETHOD_SET_GAIN,
  EMETHOD_SET_GAIN_M,
  EMETHOD_DB_TO_AMP,
  EMETHOD_SET_MAX_DELAY_TIME,
  EMETHOD_SET_DELAY_TIME,
  EMETHOD_SET_FDBGAIN,
  EMETHOD_SET_FWDGAIN,
  EMETHOD_SET_DIRGAIN,
  EMETHOD_ADD_OBJ,
  EMETHOD_DELETE_OBJ,
  EMETHOD_SET_FFT_SIZE,
  EMETHOD_SET_HOP_SIZE,
  EMETHOD_SET_WINDOW,
  EMETHOD_SET_SCALE,
  EMETHOD_SET_FREQ_MORPH,
  EMETHOD_SET_AMP_MORPH,
  EMETHOD_SET_BLUR_TIME,
  EMETHOD_SET_PITCH,
  EMETHOD_SET_MODE,
  EMETHOD_SET_AMOUNT,
  EMETHOD_SET_MASK_GAIN,
  EMETHOD_SET_SPEC_INPUT,
};

enum {
  EPROP_OBJ_NO = PROP_LEAF_BASE,
  EPROP_FFT_SIZE,
  EPROP_HOP_SIZE,
};

static JSValue
js_sndobjeffect_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SndObjPtr* e;

  if(!(e = static_cast<SndObjPtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjeffect_class_id))))
    return JS_EXCEPTION;

  if(magic == EPROP_OBJ_NO) {
    Mixer* m = dynamic_cast<Mixer*>(e->get());
    return JS_NewInt32(ctx, m ? m->GetObjNo() : 0);
  }

  if(magic == EPROP_FFT_SIZE || magic == EPROP_HOP_SIZE) {
    int v = 0;

    if(!qjsx::dispatch<PVA, PVS>(e->get(), [&](auto* p) { v = magic == EPROP_FFT_SIZE ? p->GetFFTSize() : p->GetHopSize(); }))
      return JS_ThrowTypeError(ctx, "fftSize/hopSize need a PVA or PVS");

    return JS_NewInt32(ctx, v);
  }

  return js_sndobj_common_get(ctx, e->get(), magic);
}

static JSValue
js_sndobjeffect_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  SndObjPtr* e;

  if(!(e = static_cast<SndObjPtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjeffect_class_id))))
    return JS_EXCEPTION;

  js_sndobj_common_set(ctx, e->get(), value, magic);
  return JS_UNDEFINED;
}

/* Mirrors the C++ side's AddObj/DeleteObj list in a plain, hidden JS array
 * property so every mixed-in input stays reachable(and thus alive) for as
 * long as the Mixer itself is. */
static void
js_sndobjmixer_inputs_push(JSContext* ctx, JSValueConst self, JSValueConst dep) {
  JSAtom atom = JS_NewAtom(ctx, "__inputs");
  JSValue arr = JS_GetProperty(ctx, self, atom);

  if(!JS_IsObject(arr)) {
    JS_FreeValue(ctx, arr);
    arr = JS_NewArray(ctx);
    JS_DefinePropertyValue(ctx, self, atom, JS_DupValue(ctx, arr), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  }

  int64_t len = js_array_length(ctx, arr);
  JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, dep));
  JS_FreeValue(ctx, arr);
  JS_FreeAtom(ctx, atom);
}

static void
js_sndobjmixer_inputs_remove(JSContext* ctx, JSValueConst self, SndObj* dep) {
  JSAtom atom = JS_NewAtom(ctx, "__inputs");
  JSValue arr = JS_GetProperty(ctx, self, atom);

  if(JS_IsObject(arr)) {
    int64_t len = js_array_length(ctx, arr);

    for(int64_t i = 0; i < len; i++) {
      JSValue item = JS_GetPropertyUint32(ctx, arr, i);

      if(js_sndobj_arg(ctx, item) == dep) {
        for(int64_t j = i; j + 1 < len; j++) {
          JSValue next = JS_GetPropertyUint32(ctx, arr, j + 1);
          JS_SetPropertyUint32(ctx, arr, j, next);
        }

        JS_SetPropertyStr(ctx, arr, "length", JS_NewUint32(ctx, len - 1));
        JS_FreeValue(ctx, item);
        break;
      }

      JS_FreeValue(ctx, item);
    }
  } else {
    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  JS_FreeValue(ctx, arr);
  JS_FreeAtom(ctx, atom);
}

static JSValue
js_sndobjeffect_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  SndObjPtr* e;

  if(!(e = static_cast<SndObjPtr*>(JS_GetOpaque2(ctx, this_val, js_sndobjeffect_class_id))))
    return JS_EXCEPTION;

  if(magic < METHOD_LEAF_BASE)
    return js_sndobj_common_method(ctx, e->get(), argc, argv, magic);

  SndObj* obj = e->get();

  switch(magic) {
    case EMETHOD_SET_MAX_AMP: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      ADSR* p = qjsx::require<ADSR>(ctx, obj, "setMaxAmp needs an ADSR");
      if(!p)
        return JS_EXCEPTION;

      p->SetMaxAmp(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SUSTAIN: {
      ADSR* p = qjsx::require<ADSR>(ctx, obj, "sustain needs an ADSR");
      if(!p)
        return JS_EXCEPTION;

      p->Sustain();

      return JS_UNDEFINED;
    }

    case EMETHOD_RELEASE: {
      ADSR* p = qjsx::require<ADSR>(ctx, obj, "release needs an ADSR");
      if(!p)
        return JS_EXCEPTION;

      p->Release();

      return JS_UNDEFINED;
    }

    case EMETHOD_RESTART: {
      ADSR* p = qjsx::require<ADSR>(ctx, obj, "restart needs an ADSR");
      if(!p)
        return JS_EXCEPTION;

      p->Restart();

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_ADSR: {
      float a = 0, d = 0, s = 0, r = 0;

      if(qjsx::args(ctx, argc, argv, a, d, s, r))
        return JS_EXCEPTION;

      ADSR* p = qjsx::require<ADSR>(ctx, obj, "setADSR needs an ADSR");
      if(!p)
        return JS_EXCEPTION;

      p->SetADSR(a, d, s, r);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_DUR: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      ADSR* p = qjsx::require<ADSR>(ctx, obj, "setDur needs an ADSR");
      if(!p)
        return JS_EXCEPTION;

      p->SetDur(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FREQ: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      Reson* p = qjsx::require<Reson>(ctx, obj, "setFreq needs a Reson");
      if(!p)
        return JS_EXCEPTION;

      p->SetFreq(v, mod);

      js_sndobj_retain(ctx, this_val, "__freqMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_BW: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      Reson* p = qjsx::require<Reson>(ctx, obj, "setBW needs a Reson");
      if(!p)
        return JS_EXCEPTION;

      p->SetBW(v, mod);

      js_sndobj_retain(ctx, this_val, "__bwMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_GAIN: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      qjsx::dispatch<Comb, Gain>(obj, [&](auto* p) { p->SetGain(v); });

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_GAIN_M: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      Gain* p = qjsx::require<Gain>(ctx, obj, "setGainM needs a Gain");
      if(!p)
        return JS_EXCEPTION;

      p->SetGainM(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_DB_TO_AMP: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      Gain* p = qjsx::require<Gain>(ctx, obj, "dBToAmp needs a Gain");
      if(!p)
        return JS_EXCEPTION;

      return JS_NewFloat64(ctx, p->dBToAmp(v));
    }

    case EMETHOD_SET_MAX_DELAY_TIME: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      VDelay* p = qjsx::require<VDelay>(ctx, obj, "setMaxDelayTime needs a VDelay");
      if(!p)
        return JS_EXCEPTION;

      p->SetMaxDelayTime(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_DELAY_TIME: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      VDelay* p = qjsx::require<VDelay>(ctx, obj, "setDelayTime needs a VDelay");
      if(!p)
        return JS_EXCEPTION;

      p->SetDelayTime(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FDBGAIN: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      VDelay* p = qjsx::require<VDelay>(ctx, obj, "setFdbgain needs a VDelay");
      if(!p)
        return JS_EXCEPTION;

      p->SetFdbgain(v, mod);

      js_sndobj_retain(ctx, this_val, "__fdbMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FWDGAIN: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      VDelay* p = qjsx::require<VDelay>(ctx, obj, "setFwdgain needs a VDelay");
      if(!p)
        return JS_EXCEPTION;

      p->SetFwdgain(v, mod);

      js_sndobj_retain(ctx, this_val, "__fwdMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_DIRGAIN: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      VDelay* p = qjsx::require<VDelay>(ctx, obj, "setDirgain needs a VDelay");
      if(!p)
        return JS_EXCEPTION;

      p->SetDirgain(v, mod);

      js_sndobj_retain(ctx, this_val, "__dirMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_ADD_OBJ: {
      SndObj* dep = js_sndobj_arg(ctx, argv[0]);
      if(!dep)
        return JS_EXCEPTION;

      Mixer* m = dynamic_cast<Mixer*>(obj);
      if(!m)
        return JS_ThrowTypeError(ctx, "addObj is only valid on a Mixer");

      m->AddObj(dep);
      js_sndobjmixer_inputs_push(ctx, this_val, argv[0]);
      return JS_UNDEFINED;
    }

    case EMETHOD_DELETE_OBJ: {
      SndObj* dep = js_sndobj_arg(ctx, argv[0]);
      if(!dep)
        return JS_EXCEPTION;

      Mixer* m = dynamic_cast<Mixer*>(obj);
      if(!m)
        return JS_ThrowTypeError(ctx, "deleteObj is only valid on a Mixer");

      m->DeleteObj(dep);
      js_sndobjmixer_inputs_remove(ctx, this_val, dep);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FFT_SIZE:
    case EMETHOD_SET_HOP_SIZE: {
      int32_t v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      if(v <= 0)
        return JS_ThrowRangeError(ctx, "size must be positive");

      if(!qjsx::dispatch<PVA, PVS>(obj, [&](auto* p) {
           if(magic == EMETHOD_SET_FFT_SIZE)
             p->SetFFTSize(v);
           else
             p->SetHopSize(v);
         }))
        return JS_ThrowTypeError(ctx, "setFFTSize/setHopSize need a PVA or PVS");

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_WINDOW: {
      Table* table = js_sndobjtable_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);

      if(!table)
        return JS_EXCEPTION;

      if(!qjsx::dispatch<PVA, PVS>(obj, [&](auto* p) { p->SetWindow(table); }))
        return JS_ThrowTypeError(ctx, "setWindow needs a PVA or PVS");

      js_sndobj_retain(ctx, this_val, "__table", argv[0]);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_SCALE: {
      float v = 1;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      PVA* p = qjsx::require<PVA>(ctx, obj, "setScale needs a PVA");
      if(!p)
        return JS_EXCEPTION;

      p->SetScale(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FREQ_MORPH:
    case EMETHOD_SET_AMP_MORPH: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      PVMorph* p = qjsx::require<PVMorph>(ctx, obj, "setFreqMorph/setAmpMorph need a PVMorph");
      if(!p)
        return JS_EXCEPTION;

      if(magic == EMETHOD_SET_FREQ_MORPH)
        p->SetFreqMorph(v, mod);
      else
        p->SetAmpMorph(v, mod);

      js_sndobj_retain(ctx, this_val, magic == EMETHOD_SET_FREQ_MORPH ? "__freqMorphMod" : "__ampMorphMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_BLUR_TIME: {
      float v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      PVBlur* p = qjsx::require<PVBlur>(ctx, obj, "setBlurTime needs a PVBlur");
      if(!p)
        return JS_EXCEPTION;

      p->SetBlurTime(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_PITCH: {
      float v = 1;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      PVTransp* p = qjsx::require<PVTransp>(ctx, obj, "setPitch needs a PVTransp");
      if(!p)
        return JS_EXCEPTION;

      p->SetPitch(v, mod);

      js_sndobj_retain(ctx, this_val, "__pitchMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_MODE: {
      int32_t v = 0;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      PVTransp* p = qjsx::require<PVTransp>(ctx, obj, "setMode needs a PVTransp");
      if(!p)
        return JS_EXCEPTION;

      p->SetMode(v);

      return JS_UNDEFINED;
    }

    case EMETHOD_SET_AMOUNT:
    case EMETHOD_SET_MASK_GAIN: {
      float v = 0;
      SndObj* mod = nullptr;

      if(qjsx::args(ctx, argc, argv, v))
        return JS_EXCEPTION;

      bool hasMod = qjsx::present(argc, argv, 1);

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      if(magic == EMETHOD_SET_AMOUNT) {
        PVFilter* p = qjsx::require<PVFilter>(ctx, obj, "setAmount needs a PVFilter");
        if(!p)
          return JS_EXCEPTION;

        p->SetAmount(v, mod);
      } else {
        PVMask* p = qjsx::require<PVMask>(ctx, obj, "setMaskGain needs a PVMask");
        if(!p)
          return JS_EXCEPTION;

        p->SetMaskGain(v, mod);
      }

      js_sndobj_retain(ctx, this_val, "__amountMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_SPEC_INPUT: {
      SndObj* dep = js_sndobj_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);

      if(!dep)
        return JS_EXCEPTION;

      if(!qjsx::dispatch<PVFilter, PVMask>(obj, [&](auto* p) { p->SetInput2(dep); }))
        return JS_ThrowTypeError(ctx, "setFilterInput/setMaskInput need a PVFilter or PVMask");

      js_sndobj_retain(ctx, this_val, "__input2", argv[0]);
      return JS_UNDEFINED;
    }
  }

  return JS_UNDEFINED;
}

static JSClassDef js_sndobjeffect_class = {
  .class_name = "SndObjEffect",
  .finalizer = qjsx::finalize<SndObjPtr, js_sndobjeffect_class_id>,
};

static const JSCFunctionListEntry js_sndobjeffect_funcs[] = {
  JS_CFUNC_MAGIC_DEF("process", 0, js_sndobjeffect_method, METHOD_PROCESS),
  JS_CFUNC_MAGIC_DEF("output", 1, js_sndobjeffect_method, METHOD_OUTPUT),
  JS_CFUNC_MAGIC_DEF("block", 0, js_sndobjeffect_method, METHOD_BLOCK),
  JS_CFUNC_MAGIC_DEF("enable", 0, js_sndobjeffect_method, METHOD_ENABLE),
  JS_CFUNC_MAGIC_DEF("disable", 0, js_sndobjeffect_method, METHOD_DISABLE),
  JS_CGETSET_MAGIC_DEF("sr", js_sndobjeffect_get, js_sndobjeffect_set, PROP_SR),
  JS_CGETSET_MAGIC_DEF("vecsize", js_sndobjeffect_get, js_sndobjeffect_set, PROP_VECSIZE),
  JS_CGETSET_MAGIC_DEF("error", js_sndobjeffect_get, 0, PROP_ERROR),
  JS_CFUNC_MAGIC_DEF("setMaxAmp", 1, js_sndobjeffect_method, EMETHOD_SET_MAX_AMP),
  JS_CFUNC_MAGIC_DEF("sustain", 0, js_sndobjeffect_method, EMETHOD_SUSTAIN),
  JS_CFUNC_MAGIC_DEF("release", 0, js_sndobjeffect_method, EMETHOD_RELEASE),
  JS_CFUNC_MAGIC_DEF("restart", 0, js_sndobjeffect_method, EMETHOD_RESTART),
  JS_CFUNC_MAGIC_DEF("setADSR", 4, js_sndobjeffect_method, EMETHOD_SET_ADSR),
  JS_CFUNC_MAGIC_DEF("setDur", 1, js_sndobjeffect_method, EMETHOD_SET_DUR),
  JS_CFUNC_MAGIC_DEF("setFreq", 1, js_sndobjeffect_method, EMETHOD_SET_FREQ),
  JS_CFUNC_MAGIC_DEF("setBW", 1, js_sndobjeffect_method, EMETHOD_SET_BW),
  JS_CFUNC_MAGIC_DEF("setGain", 1, js_sndobjeffect_method, EMETHOD_SET_GAIN),
  JS_CFUNC_MAGIC_DEF("setGainM", 1, js_sndobjeffect_method, EMETHOD_SET_GAIN_M),
  JS_CFUNC_MAGIC_DEF("dBToAmp", 1, js_sndobjeffect_method, EMETHOD_DB_TO_AMP),
  JS_CFUNC_MAGIC_DEF("setMaxDelayTime", 1, js_sndobjeffect_method, EMETHOD_SET_MAX_DELAY_TIME),
  JS_CFUNC_MAGIC_DEF("setDelayTime", 1, js_sndobjeffect_method, EMETHOD_SET_DELAY_TIME),
  JS_CFUNC_MAGIC_DEF("setFdbgain", 1, js_sndobjeffect_method, EMETHOD_SET_FDBGAIN),
  JS_CFUNC_MAGIC_DEF("setFwdgain", 1, js_sndobjeffect_method, EMETHOD_SET_FWDGAIN),
  JS_CFUNC_MAGIC_DEF("setDirgain", 1, js_sndobjeffect_method, EMETHOD_SET_DIRGAIN),
  JS_CFUNC_MAGIC_DEF("addObj", 1, js_sndobjeffect_method, EMETHOD_ADD_OBJ),
  JS_CFUNC_MAGIC_DEF("deleteObj", 1, js_sndobjeffect_method, EMETHOD_DELETE_OBJ),
  JS_CGETSET_MAGIC_DEF("objNo", js_sndobjeffect_get, 0, EPROP_OBJ_NO),
  JS_CGETSET_MAGIC_DEF("fftSize", js_sndobjeffect_get, 0, EPROP_FFT_SIZE),
  JS_CGETSET_MAGIC_DEF("hopSize", js_sndobjeffect_get, 0, EPROP_HOP_SIZE),
  JS_CFUNC_MAGIC_DEF("setFFTSize", 1, js_sndobjeffect_method, EMETHOD_SET_FFT_SIZE),
  JS_CFUNC_MAGIC_DEF("setHopSize", 1, js_sndobjeffect_method, EMETHOD_SET_HOP_SIZE),
  JS_CFUNC_MAGIC_DEF("setWindow", 1, js_sndobjeffect_method, EMETHOD_SET_WINDOW),
  JS_CFUNC_MAGIC_DEF("setScale", 1, js_sndobjeffect_method, EMETHOD_SET_SCALE),
  JS_CFUNC_MAGIC_DEF("setFreqMorph", 1, js_sndobjeffect_method, EMETHOD_SET_FREQ_MORPH),
  JS_CFUNC_MAGIC_DEF("setAmpMorph", 1, js_sndobjeffect_method, EMETHOD_SET_AMP_MORPH),
  JS_CFUNC_MAGIC_DEF("setBlurTime", 1, js_sndobjeffect_method, EMETHOD_SET_BLUR_TIME),
  JS_CFUNC_MAGIC_DEF("setPitch", 1, js_sndobjeffect_method, EMETHOD_SET_PITCH),
  JS_CFUNC_MAGIC_DEF("setMode", 1, js_sndobjeffect_method, EMETHOD_SET_MODE),
  JS_CFUNC_MAGIC_DEF("setAmount", 1, js_sndobjeffect_method, EMETHOD_SET_AMOUNT),
  JS_CFUNC_MAGIC_DEF("setMaskGain", 1, js_sndobjeffect_method, EMETHOD_SET_MASK_GAIN),
  JS_CFUNC_MAGIC_DEF("setFilterInput", 1, js_sndobjeffect_method, EMETHOD_SET_SPEC_INPUT),
  JS_CFUNC_MAGIC_DEF("setMaskInput", 1, js_sndobjeffect_method, EMETHOD_SET_SPEC_INPUT),
};

enum {
  INSTANCE_ADSR = 0,
  INSTANCE_RESON,
  INSTANCE_COMB,
  INSTANCE_ALLPASS,
  INSTANCE_VDELAY,
  INSTANCE_GAIN,
  INSTANCE_MIXER,
  INSTANCE_PVA,
  INSTANCE_PVS,
  INSTANCE_PVMORPH,
  INSTANCE_PVMIX,
  INSTANCE_PVBLUR,
  INSTANCE_PVTRANSP,
  INSTANCE_PVFILTER,
  INSTANCE_PVMASK,
};

static const qjsx::ctor_def sndobjeffect_ctors[] = {
  { "ADSR", 9, INSTANCE_ADSR },
  { "Reson", 7, INSTANCE_RESON },
  { "Comb", 5, INSTANCE_COMB },
  { "Allpass", 5, INSTANCE_ALLPASS },
  { "VDelay", 12, INSTANCE_VDELAY },
  { "Gain", 4, INSTANCE_GAIN },
  { "Mixer", 2, INSTANCE_MIXER },
  { "PVA", 6, INSTANCE_PVA },
  { "PVS", 5, INSTANCE_PVS },
  { "PVMorph", 8, INSTANCE_PVMORPH },
  { "PVMix", 4, INSTANCE_PVMIX },
  { "PVBlur", 5, INSTANCE_PVBLUR },
  { "PVTransp", 7, INSTANCE_PVTRANSP },
  { "PVFilter", 6, INSTANCE_PVFILTER },
  { "PVMask", 6, INSTANCE_PVMASK },
};

static JSValue
js_sndobjeffect_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  SndObjPtr e;

  switch(magic) {
    case INSTANCE_ADSR: {
      float att = 0, maxamp = 1, dec = 0, sus = 1, rel = 0, dur = 1, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj* input = nullptr;

      if(qjsx::args(ctx, argc, argv, att, maxamp, dec, sus, rel, dur))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 6) && !(input = js_sndobj_arg(ctx, argv[6])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 7, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new ADSR(att, maxamp, dec, sus, rel, dur, input, vecsize, sr));
      break;
    }

    case INSTANCE_RESON: {
      float fr = 440, bw = 100, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj *input = nullptr, *freqMod = nullptr, *bwMod = nullptr;

      if(qjsx::args(ctx, argc, argv, fr, bw))
        return JS_EXCEPTION;

      if(argc < 3)
        return JS_ThrowTypeError(ctx, "Reson requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[2])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(freqMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 4) && !(bwMod = js_sndobj_arg(ctx, argv[4])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 5, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new Reson(fr, bw, input, freqMod, bwMod, vecsize, sr));
      break;
    }

    case INSTANCE_COMB:
    case INSTANCE_ALLPASS: {
      float gain = 0, dtime = 0.01f, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj* input = nullptr;

      if(qjsx::args(ctx, argc, argv, gain, dtime))
        return JS_EXCEPTION;

      if(argc < 3)
        return JS_ThrowTypeError(ctx, "%s requires an input SndObj", magic == INSTANCE_COMB ? "Comb" : "Allpass");

      if(!(input = js_sndobj_arg(ctx, argv[2])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 3, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      if(magic == INSTANCE_COMB)
        e.reset(new Comb(gain, dtime, input, vecsize, sr));
      else
        e.reset(new Allpass(gain, dtime, input, vecsize, sr));
      break;
    }

    case INSTANCE_VDELAY: {
      float maxdt = 1, dt = 0.1f, fdb = 0, fwd = 1, dir = 0, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj *input = nullptr, *timeMod = nullptr, *fdbMod = nullptr, *fwdMod = nullptr, *dirMod = nullptr;

      if(qjsx::args(ctx, argc, argv, maxdt, dt, fdb, fwd, dir))
        return JS_EXCEPTION;

      if(argc < 6)
        return JS_ThrowTypeError(ctx, "VDelay requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[5])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 6) && !(timeMod = js_sndobj_arg(ctx, argv[6])))
        return JS_EXCEPTION;
      if(qjsx::present(argc, argv, 7) && !(fdbMod = js_sndobj_arg(ctx, argv[7])))
        return JS_EXCEPTION;
      if(qjsx::present(argc, argv, 8) && !(fwdMod = js_sndobj_arg(ctx, argv[8])))
        return JS_EXCEPTION;
      if(qjsx::present(argc, argv, 9) && !(dirMod = js_sndobj_arg(ctx, argv[9])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 10, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new VDelay(maxdt, dt, fdb, fwd, dir, input, timeMod, fdbMod, fwdMod, dirMod, vecsize, sr));
      break;
    }

    case INSTANCE_GAIN: {
      float gain = 0, sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;
      SndObj* input = nullptr;

      if(qjsx::args(ctx, argc, argv, gain))
        return JS_EXCEPTION;

      if(argc < 2)
        return JS_ThrowTypeError(ctx, "Gain requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 2, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new Gain(gain, input, vecsize, sr));
      break;
    }

    case INSTANCE_MIXER: {
      float sr = DEF_SR;
      int32_t vecsize = DEF_VECSIZE;

      if(qjsx::args(ctx, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new Mixer(0, nullptr, vecsize, sr));
      break;
    }

    case INSTANCE_PVA: {
      Table* window = nullptr;
      SndObj* input = nullptr;
      float scale = 1, sr = DEF_SR;
      int32_t fftsize = DEF_FFTSIZE, hopsize = DEF_VECSIZE;

      if(!(window = js_sndobjtable_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED)))
        return JS_EXCEPTION;

      if(argc < 2)
        return JS_ThrowTypeError(ctx, "PVA requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 2, argc, argv, scale, fftsize, hopsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVA(window, input, scale, fftsize, hopsize, sr));
      break;
    }

    case INSTANCE_PVS: {
      Table* window = nullptr;
      SndObj* input = nullptr;
      float sr = DEF_SR;
      int32_t fftsize = DEF_FFTSIZE, hopsize = DEF_VECSIZE;

      if(!(window = js_sndobjtable_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED)))
        return JS_EXCEPTION;

      if(argc < 2)
        return JS_ThrowTypeError(ctx, "PVS requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 2, argc, argv, fftsize, hopsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVS(window, input, fftsize, hopsize, sr));
      break;
    }

    case INSTANCE_PVMORPH: {
      float freqMorph = 0, ampMorph = 0, sr = DEF_SR;
      int32_t vecsize = DEF_FFTSIZE;
      SndObj *in1 = nullptr, *in2 = nullptr, *freqMod = nullptr, *ampMod = nullptr;

      if(qjsx::args(ctx, argc, argv, freqMorph, ampMorph))
        return JS_EXCEPTION;

      if(argc < 4)
        return JS_ThrowTypeError(ctx, "PVMorph requires two input SndObj");

      if(!(in1 = js_sndobj_arg(ctx, argv[2])) || !(in2 = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 4) && !(freqMod = js_sndobj_arg(ctx, argv[4])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 5) && !(ampMod = js_sndobj_arg(ctx, argv[5])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 6, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVMorph(freqMorph, ampMorph, in1, in2, freqMod, ampMod, vecsize, sr));
      break;
    }

    case INSTANCE_PVMIX: {
      float sr = DEF_SR;
      int32_t vecsize = DEF_FFTSIZE;
      SndObj *in1 = nullptr, *in2 = nullptr;

      if(argc < 1)
        return JS_ThrowTypeError(ctx, "PVMix requires an input SndObj");

      if(!(in1 = js_sndobj_arg(ctx, argv[0])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 1) && !(in2 = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 2, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVMix(in1, in2, vecsize, sr));
      break;
    }

    case INSTANCE_PVBLUR: {
      float blurtime = 0, sr = DEF_SR;
      int32_t hopsize = DEF_VECSIZE, vecsize = DEF_FFTSIZE;
      SndObj* input = nullptr;

      if(argc < 1)
        return JS_ThrowTypeError(ctx, "PVBlur requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[0])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 1, argc, argv, blurtime, hopsize, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVBlur(input, blurtime, hopsize, vecsize, sr));
      break;
    }

    case INSTANCE_PVTRANSP: {
      float pitch = 1, sr = DEF_SR;
      int32_t mode = NORMAL_TRANSP, vecsize = DEF_FFTSIZE;
      SndObj *input = nullptr, *pitchMod = nullptr;

      if(argc < 1)
        return JS_ThrowTypeError(ctx, "PVTransp requires an input SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[0])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 1, argc, argv, pitch, mode))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(pitchMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 4, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVTransp(input, pitch, mode, pitchMod, vecsize, sr));
      break;
    }

    case INSTANCE_PVFILTER: {
      float amnt = 1, sr = DEF_SR;
      int32_t vecsize = DEF_FFTSIZE;
      SndObj *input = nullptr, *filspec = nullptr, *amntMod = nullptr;

      if(argc < 2)
        return JS_ThrowTypeError(ctx, "PVFilter requires an input and a filter spectrum SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[0])) || !(filspec = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 2, argc, argv, amnt))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(amntMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 4, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      e.reset(new PVFilter(input, filspec, amnt, amntMod, vecsize, sr));
      break;
    }

    case INSTANCE_PVMASK: {
      float maskgain = 1, sr = DEF_SR;
      int32_t vecsize = DEF_FFTSIZE;
      SndObj *input = nullptr, *mask = nullptr, *gainMod = nullptr;

      if(qjsx::args(ctx, argc, argv, maskgain))
        return JS_EXCEPTION;

      if(argc < 3)
        return JS_ThrowTypeError(ctx, "PVMask requires an input and a mask spectrum SndObj");

      if(!(input = js_sndobj_arg(ctx, argv[1])) || !(mask = js_sndobj_arg(ctx, argv[2])))
        return JS_EXCEPTION;

      if(qjsx::present(argc, argv, 3) && !(gainMod = js_sndobj_arg(ctx, argv[3])))
        return JS_EXCEPTION;

      if(qjsx::args_from(ctx, 4, argc, argv, vecsize, sr))
        return JS_EXCEPTION;

      /* the SndObj constructor of PVMask drops `mask`(it passes the gain
       * modulator as second input), so connect the mask input explicitly */
      PVMask* pm = new PVMask(maskgain, input, mask, gainMod, vecsize, sr);
      pm->SetMaskInput(mask);
      e.reset(pm);
      break;
    }
  }

  JSValue obj = qjsx::wrap(ctx, new_target, js_sndobjeffect_class_id, std::move(e));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(sndobjeffect_ctors, magic));

  switch(magic) {
    case INSTANCE_ADSR:
      if(qjsx::present(argc, argv, 6))
        js_sndobj_retain(ctx, obj, "__input", argv[6]);
      break;
    case INSTANCE_RESON:
      js_sndobj_retain(ctx, obj, "__input", argv[2]);
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__freqMod", argv[3]);
      if(qjsx::present(argc, argv, 4))
        js_sndobj_retain(ctx, obj, "__bwMod", argv[4]);
      break;
    case INSTANCE_COMB:
    case INSTANCE_ALLPASS:
      js_sndobj_retain(ctx, obj, "__input", argv[2]);
      break;
    case INSTANCE_VDELAY:
      js_sndobj_retain(ctx, obj, "__input", argv[5]);
      if(qjsx::present(argc, argv, 6))
        js_sndobj_retain(ctx, obj, "__timeMod", argv[6]);
      if(qjsx::present(argc, argv, 7))
        js_sndobj_retain(ctx, obj, "__fdbMod", argv[7]);
      if(qjsx::present(argc, argv, 8))
        js_sndobj_retain(ctx, obj, "__fwdMod", argv[8]);
      if(qjsx::present(argc, argv, 9))
        js_sndobj_retain(ctx, obj, "__dirMod", argv[9]);
      break;
    case INSTANCE_GAIN:
      js_sndobj_retain(ctx, obj, "__input", argv[1]);
      break;
    case INSTANCE_PVA:
    case INSTANCE_PVS:
      js_sndobj_retain(ctx, obj, "__table", argv[0]);
      js_sndobj_retain(ctx, obj, "__input", argv[1]);
      break;
    case INSTANCE_PVMORPH:
      js_sndobj_retain(ctx, obj, "__input", argv[2]);
      js_sndobj_retain(ctx, obj, "__input2", argv[3]);
      if(qjsx::present(argc, argv, 4))
        js_sndobj_retain(ctx, obj, "__freqMorphMod", argv[4]);
      if(qjsx::present(argc, argv, 5))
        js_sndobj_retain(ctx, obj, "__ampMorphMod", argv[5]);
      break;
    case INSTANCE_PVMIX:
      js_sndobj_retain(ctx, obj, "__input", argv[0]);
      if(qjsx::present(argc, argv, 1))
        js_sndobj_retain(ctx, obj, "__input2", argv[1]);
      break;
    case INSTANCE_PVBLUR:
      js_sndobj_retain(ctx, obj, "__input", argv[0]);
      break;
    case INSTANCE_PVTRANSP:
      js_sndobj_retain(ctx, obj, "__input", argv[0]);
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__pitchMod", argv[3]);
      break;
    case INSTANCE_PVFILTER:
      js_sndobj_retain(ctx, obj, "__input", argv[0]);
      js_sndobj_retain(ctx, obj, "__input2", argv[1]);
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__amountMod", argv[3]);
      break;
    case INSTANCE_PVMASK:
      js_sndobj_retain(ctx, obj, "__input", argv[1]);
      js_sndobj_retain(ctx, obj, "__input2", argv[2]);
      if(qjsx::present(argc, argv, 3))
        js_sndobj_retain(ctx, obj, "__amountMod", argv[3]);
      break;
  }

  return obj;
}

/* ==================== module init ==================== */

static int
js_sndobj_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_sndobjtable_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sndobjtable_class_id, &js_sndobjtable_class);
  sndobjtable_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, sndobjtable_proto, js_sndobjtable_funcs, countof(js_sndobjtable_funcs));
  JS_SetClassProto(ctx, js_sndobjtable_class_id, sndobjtable_proto);

  JS_NewClassID(&js_sndobjgenerator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sndobjgenerator_class_id, &js_sndobjgenerator_class);
  sndobjgenerator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, sndobjgenerator_proto, js_sndobjgenerator_funcs, countof(js_sndobjgenerator_funcs));
  JS_SetClassProto(ctx, js_sndobjgenerator_class_id, sndobjgenerator_proto);
  sndobj_inputs.add(js_sndobjgenerator_class_id);

  JS_NewClassID(&js_sndobjeffect_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sndobjeffect_class_id, &js_sndobjeffect_class);
  sndobjeffect_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, sndobjeffect_proto, js_sndobjeffect_funcs, countof(js_sndobjeffect_funcs));
  JS_SetClassProto(ctx, js_sndobjeffect_class_id, sndobjeffect_proto);
  sndobj_inputs.add(js_sndobjeffect_class_id);

  if(m) {
    qjsx::bind_ctors(ctx, m, (JSCFunctionMagic*)js_sndobjtable_constructor, sndobjtable_proto, sndobjtable_ctors);
    qjsx::bind_ctors(ctx, m, (JSCFunctionMagic*)js_sndobjgenerator_constructor, sndobjgenerator_proto, sndobjgenerator_ctors);
    qjsx::bind_ctors(ctx, m, (JSCFunctionMagic*)js_sndobjeffect_constructor, sndobjeffect_proto, sndobjeffect_ctors);
  }

  return 0;
}

extern "C" VISIBLE void
js_sndobj_add_exports(JSContext* ctx, JSModuleDef* m) {
  qjsx::declare_ctors(ctx, m, sndobjtable_ctors);
  qjsx::declare_ctors(ctx, m, sndobjgenerator_ctors);
  qjsx::declare_ctors(ctx, m, sndobjeffect_ctors);
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_sndobj
#endif

extern "C" VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_sndobj_init)))
    js_sndobj_add_exports(ctx, m);

  return m;
}
