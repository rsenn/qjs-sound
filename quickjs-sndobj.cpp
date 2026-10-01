#include <quickjs.h>
#include <cutils.h>

#include <vector>
#include <memory>
#include <cstring>

#include "defines.h"
#include "quickjs-cpp.hpp"

#include "SndObj.h"
#include "Table.h"
#include "HarmTable.h"

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

static JSClassID js_sndobjtable_class_id, js_sndobjgenerator_class_id, js_sndobjeffect_class_id;
static JSValue sndobjtable_proto, sndobjgenerator_proto, sndobjeffect_proto;

typedef std::unique_ptr<Table> SndObjTablePtr;
typedef std::unique_ptr<SndObj> SndObjPtr;

static JSAtom
js_symbol_tostringtag(JSContext* ctx) {
  JSValue g = JS_GetGlobalObject(ctx);
  JSValue sym = JS_GetPropertyStr(ctx, g, "Symbol");
  JSValue tst = JS_GetPropertyStr(ctx, sym, "toStringTag");
  JS_FreeValue(ctx, sym);
  JS_FreeValue(ctx, g);
  JSAtom ret = JS_ValueToAtom(ctx, tst);
  JS_FreeValue(ctx, tst);
  return ret;
}

static void
js_set_tostringtag(JSContext* ctx, JSValueConst obj, const char* name) {
  JSAtom tst = js_symbol_tostringtag(ctx);
  JSValue str = JS_NewString(ctx, name);
  JS_DeleteProperty(ctx, obj, tst, 0);
  JS_DefinePropertyValue(ctx, obj, tst, str, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, tst);
}

/* SndObj stores raw, non-owning pointers to its inputs/modulators (see
 * SndObj.h). Retaining a strong (hidden, non-enumerable) JS reference on the
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
  void* p;

  if((p = JS_GetOpaque(v, js_sndobjgenerator_class_id)))
    return static_cast<SndObjPtr*>(p)->get();

  if((p = JS_GetOpaque(v, js_sndobjeffect_class_id)))
    return static_cast<SndObjPtr*>(p)->get();

  JS_ThrowTypeError(ctx, "expected an sndobj generator or effect instance");
  return nullptr;
}

static Table*
js_sndobjtable_arg(JSContext* ctx, JSValueConst v) {
  void* p;

  if((p = JS_GetOpaque(v, js_sndobjtable_class_id)))
    return static_cast<SndObjTablePtr*>(p)->get();

  JS_ThrowTypeError(ctx, "expected a Table instance");
  return nullptr;
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
        return JS_ThrowInternalError(ctx, "SndObj::DoProcess failed (error %d)", obj->GetError());
    
      return JS_NewBool(ctx, ok != 0);
    }

    case METHOD_OUTPUT: {
      int32_t pos = 0;

      if(argc > 0)
        JS_ToInt32(ctx, &pos, argv[0]);

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
      double sr = 0;
      JS_ToFloat64(ctx, &sr, value);
      obj->SetSr((float)sr);
      break;
    }

    case PROP_VECSIZE: {
      int32_t vs = 0;
      JS_ToInt32(ctx, &vs, value);
      obj->SetVectorSize(vs);
      break;
    }
  }
}

/* ==================== Family A: Table (HarmTable) ==================== */

enum {
  TMETHOD_LOOKUP = 0,
  TMETHOD_TO_ARRAY,
  TMETHOD_SET_HARM,
  TMETHOD_SET_PHASE,
  TMETHOD_MAKE_TABLE,
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

      if(argc > 0)
        JS_ToInt32(ctx, &pos, argv[0]);
      
      return JS_NewFloat64(ctx, table->Lookup(pos));
    }

    case TMETHOD_TO_ARRAY: {
      return qjsx::new_array<float>(ctx, table->GetTable(), (size_t)table->GetLen());
    }

    case TMETHOD_SET_HARM: {
      int32_t harm = 0, type = SINE;

      if(argc > 0)
        JS_ToInt32(ctx, &harm, argv[0]);

      if(argc > 1)
        JS_ToInt32(ctx, &type, argv[1]);

      if(auto* p = dynamic_cast<HarmTable*>(table))
        p->SetHarm(harm, type);

      return JS_UNDEFINED;
    }

    case TMETHOD_SET_PHASE: {
      double phase = 0;

      if(argc > 0)
        JS_ToFloat64(ctx, &phase, argv[0]);

      if(auto* p = dynamic_cast<HarmTable*>(table))
        p->SetPhase((float)phase);

      return JS_UNDEFINED;
    }

    case TMETHOD_MAKE_TABLE: {
      return JS_NewBool(ctx, table->MakeTable() != 0);
    }
  }

  return JS_UNDEFINED;
}

static void
js_sndobjtable_finalizer(JSRuntime* rt, JSValue val) {
  SndObjTablePtr* t;

  if((t = static_cast<SndObjTablePtr*>(JS_GetOpaque(val, js_sndobjtable_class_id)))) {
    t->~SndObjTablePtr();
    js_free_rt(rt, t);
  }
}

static JSClassDef js_sndobjtable_class = {
    .class_name = "Table",
    .finalizer = js_sndobjtable_finalizer,
};

static const JSCFunctionListEntry js_sndobjtable_funcs[] = {
    JS_CGETSET_MAGIC_DEF("len", js_sndobjtable_get, 0, TPROP_LEN),
    JS_CFUNC_MAGIC_DEF("lookup", 1, js_sndobjtable_method, TMETHOD_LOOKUP),
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_sndobjtable_method, TMETHOD_TO_ARRAY),
    JS_CFUNC_MAGIC_DEF("setHarm", 2, js_sndobjtable_method, TMETHOD_SET_HARM),
    JS_CFUNC_MAGIC_DEF("setPhase", 1, js_sndobjtable_method, TMETHOD_SET_PHASE),
    JS_CFUNC_MAGIC_DEF("makeTable", 0, js_sndobjtable_method, TMETHOD_MAKE_TABLE),
    JS_PROP_INT32_DEF("SINE", SINE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SAW", SAW, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SQUARE", SQUARE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BUZZ", BUZZ, JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_HARM_TABLE = 0,
};

static JSValue
js_sndobjtable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  SndObjTablePtr* t = static_cast<SndObjTablePtr*>(js_mallocz(ctx, sizeof(SndObjTablePtr)));
  if(!t)
    return JS_EXCEPTION;

  new(t) SndObjTablePtr();

  switch(magic) {
    case INSTANCE_HARM_TABLE: {
      if(argc == 0) {
        t->reset(new HarmTable());
      } else {
        int64_t len = 4096;
        int32_t harm = 1, type = SINE;
        double phase = 0;

        JS_ToInt64(ctx, &len, argv[0]);

        if(argc > 1)
          JS_ToInt32(ctx, &harm, argv[1]);

        if(argc > 2)
          JS_ToInt32(ctx, &type, argv[2]);

        if(argc > 3)
          JS_ToFloat64(ctx, &phase, argv[3]);

        t->reset(new HarmTable((long)len, harm, type, (float)phase));
      }

      break;
    }
  }

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, sndobjtable_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_sndobjtable_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, t);
  js_set_tostringtag(ctx, obj, ((const char*[]){"HarmTable"})[magic]);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/* ================ Family B: generators (Oscili/Buzz/Randi) ================ */

enum {
  GMETHOD_SET_FREQ = METHOD_LEAF_BASE,
  GMETHOD_SET_AMP,
  GMETHOD_SET_TABLE,
  GMETHOD_SET_PHASE,
  GMETHOD_SET_HARM,
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

    case GMETHOD_SET_AMP: {
      double amp = 0;
      JS_ToFloat64(ctx, &amp, argv[0]);
      bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
      SndObj* mod = nullptr;

      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;
      
      if(auto* p = dynamic_cast<Oscil*>(obj))
        p->SetAmp((float)amp, mod);
      else if(auto* p = dynamic_cast<Buzz*>(obj))
        p->SetAmp((float)amp, mod);
      else if(auto* p = dynamic_cast<Rand*>(obj))
        p->SetAmp((float)amp, mod);

      js_sndobj_retain(ctx, this_val, "__ampMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case GMETHOD_SET_TABLE: {
      Table* table = js_sndobjtable_arg(ctx, argv[0]);

      if(!table)
        return JS_EXCEPTION;

      if(auto* p = dynamic_cast<Oscil*>(obj))
        p->SetTable(table);

      js_sndobj_retain(ctx, this_val, "__table", argv[0]);
      return JS_UNDEFINED;
    }

    case GMETHOD_SET_PHASE: {
      double phase = 0;

      JS_ToFloat64(ctx, &phase, argv[0]);

      if(auto* p = dynamic_cast<Oscil*>(obj))
        p->SetPhase((float)phase);

      return JS_UNDEFINED;
    }

    case GMETHOD_SET_HARM: {
      int32_t harm = 1;

      JS_ToInt32(ctx, &harm, argv[0]);

      if(auto* p = dynamic_cast<Buzz*>(obj))
        p->SetHarm(harm);

      return JS_UNDEFINED;
    }
  }

  return JS_UNDEFINED;
}

static void
js_sndobjgenerator_finalizer(JSRuntime* rt, JSValue val) {
  SndObjPtr* g;

  if((g = static_cast<SndObjPtr*>(JS_GetOpaque(val, js_sndobjgenerator_class_id)))) {
    g->~SndObjPtr();
    js_free_rt(rt, g);
  }
}

static JSClassDef js_sndobjgenerator_class = {
    .class_name = "SndObjGenerator",
    .finalizer = js_sndobjgenerator_finalizer,
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
};

enum {
  INSTANCE_OSCILI = 0,
  INSTANCE_BUZZ,
  INSTANCE_RANDI,
};

static JSValue
js_sndobjgenerator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  SndObjPtr* g = static_cast<SndObjPtr*>(js_mallocz(ctx, sizeof(SndObjPtr)));
  if(!g)
    return JS_EXCEPTION;

  new(g) SndObjPtr();

  switch(magic) {
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

    case INSTANCE_BUZZ: {
      double fr = 440, amp = 1;
      int32_t harms = 1;
    
      if(argc > 0)
        JS_ToFloat64(ctx, &fr, argv[0]);
    
      if(argc > 1)
        JS_ToFloat64(ctx, &amp, argv[1]);
    
      if(argc > 2)
        JS_ToInt32(ctx, &harms, argv[2]);
    
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
    
      g->reset(new Buzz((float)fr, (float)amp, (short)harms, freqMod, ampMod, vecsize, (float)sr));
      break;
    }

    case INSTANCE_RANDI: {
      double fr = 1, amp = 1;
    
      if(argc > 0)
        JS_ToFloat64(ctx, &fr, argv[0]);
    
      if(argc > 1)
        JS_ToFloat64(ctx, &amp, argv[1]);
    
      SndObj *freqMod = nullptr, *ampMod = nullptr;
    
      if(argc > 2 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]) && !(freqMod = js_sndobj_arg(ctx, argv[2])))
        goto argfail;
    
      if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]) && !(ampMod = js_sndobj_arg(ctx, argv[3])))
        goto argfail;
    
      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;
    
      if(argc > 4)
        JS_ToInt32(ctx, &vecsize, argv[4]);
    
      if(argc > 5)
        JS_ToFloat64(ctx, &sr, argv[5]);
    
      g->reset(new Randi((float)fr, (float)amp, freqMod, ampMod, vecsize, (float)sr));
      break;
    }
  }

  {
    JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
 
    if(JS_IsException(proto))
      goto fail;
 
    if(!JS_IsObject(proto)) {
      JS_FreeValue(ctx, proto);
      proto = JS_DupValue(ctx, sndobjgenerator_proto);
    }

    obj = JS_NewObjectProtoClass(ctx, proto, js_sndobjgenerator_class_id);
    JS_FreeValue(ctx, proto);
    if(JS_IsException(obj))
      goto fail;

    JS_SetOpaque(obj, g);
    js_set_tostringtag(ctx, obj, ((const char*[]){"Oscili", "Buzz", "Randi"})[magic]);

    switch(magic) {
      case INSTANCE_OSCILI:
        if(argc > 0)
          js_sndobj_retain(ctx, obj, "__table", argv[0]);
        if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
          js_sndobj_retain(ctx, obj, "__freqMod", argv[3]);
        if(argc > 4 && !JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4]))
          js_sndobj_retain(ctx, obj, "__ampMod", argv[4]);
        break;
      case INSTANCE_BUZZ:
        if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
          js_sndobj_retain(ctx, obj, "__freqMod", argv[3]);
        if(argc > 4 && !JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4]))
          js_sndobj_retain(ctx, obj, "__ampMod", argv[4]);
        break;
      case INSTANCE_RANDI:
        if(argc > 2 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]))
          js_sndobj_retain(ctx, obj, "__freqMod", argv[2]);
        if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
          js_sndobj_retain(ctx, obj, "__ampMod", argv[3]);
        break;
    }

    return obj;

  fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

argfail:
  g->~SndObjPtr();
  js_free(ctx, g);
  return JS_EXCEPTION;
}

/* ========== Family C: effects (ADSR/Reson/Comb/Allpass/VDelay/Gain/Mixer) ========== */

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
};

enum {
  EPROP_OBJ_NO = PROP_LEAF_BASE,
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
 * property so every mixed-in input stays reachable (and thus alive) for as
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
 
  uint32_t len = 0;
  JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
  JS_ToUint32(ctx, &len, lenv);
  JS_FreeValue(ctx, lenv);
  JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, dep));
  JS_FreeValue(ctx, arr);
  JS_FreeAtom(ctx, atom);
}

static void
js_sndobjmixer_inputs_remove(JSContext* ctx, JSValueConst self, SndObj* dep) {
  JSAtom atom = JS_NewAtom(ctx, "__inputs");
  JSValue arr = JS_GetProperty(ctx, self, atom);
 
  if(JS_IsObject(arr)) {
    uint32_t len = 0;
    JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
 
    JS_ToUint32(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);
   
    for(uint32_t i = 0; i < len; i++) {
      JSValue item = JS_GetPropertyUint32(ctx, arr, i);
     
      if(js_sndobj_arg(ctx, item) == dep) {
        for(uint32_t j = i; j + 1 < len; j++) {
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
      double v = 0;
     
      JS_ToFloat64(ctx, &v, argv[0]);
     
      if(auto* p = dynamic_cast<ADSR*>(obj))
        p->SetMaxAmp((float)v);
     
      return JS_UNDEFINED;
    }

    case EMETHOD_SUSTAIN: {
      if(auto* p = dynamic_cast<ADSR*>(obj))
        p->Sustain();
     
      return JS_UNDEFINED;
    }

    case EMETHOD_RELEASE: {
      if(auto* p = dynamic_cast<ADSR*>(obj))
        p->Release();
     
      return JS_UNDEFINED;
    }

    case EMETHOD_RESTART: {
      if(auto* p = dynamic_cast<ADSR*>(obj))
        p->Restart();
    
      return JS_UNDEFINED;
    }

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

    case EMETHOD_SET_DUR: {
      double v = 0;
    
      JS_ToFloat64(ctx, &v, argv[0]);
    
      if(auto* p = dynamic_cast<ADSR*>(obj))
        p->SetDur((float)v);
    
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FREQ: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
      bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
      SndObj* mod = nullptr;
    
      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;
    
      if(auto* p = dynamic_cast<Reson*>(obj))
        p->SetFreq((float)v, mod);
    
      js_sndobj_retain(ctx, this_val, "__freqMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_BW: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
      bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
      SndObj* mod = nullptr;
    
      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;
    
      if(auto* p = dynamic_cast<Reson*>(obj))
        p->SetBW((float)v, mod);
    
      js_sndobj_retain(ctx, this_val, "__bwMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_GAIN: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
   
      if(auto* p = dynamic_cast<Comb*>(obj))
        p->SetGain((float)v);
      else if(auto* p = dynamic_cast<Gain*>(obj))
        p->SetGain((float)v);
    
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_GAIN_M: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
   
      if(auto* p = dynamic_cast<Gain*>(obj))
        p->SetGainM((float)v);
   
      return JS_UNDEFINED;
    }

    case EMETHOD_DB_TO_AMP: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
      double r = 0;
   
      if(auto* p = dynamic_cast<Gain*>(obj))
        r = p->dBToAmp((float)v);
   
      return JS_NewFloat64(ctx, r);
    }

    case EMETHOD_SET_MAX_DELAY_TIME: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
    
      if(auto* p = dynamic_cast<VDelay*>(obj))
        p->SetMaxDelayTime((float)v);
    
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_DELAY_TIME: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
    
      if(auto* p = dynamic_cast<VDelay*>(obj))
        p->SetDelayTime((float)v);
    
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FDBGAIN: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
      bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
      SndObj* mod = nullptr;
 
      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;
 
      if(auto* p = dynamic_cast<VDelay*>(obj))
        p->SetFdbgain((float)v, mod);
 
      js_sndobj_retain(ctx, this_val, "__fdbMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_FWDGAIN: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
      bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
      SndObj* mod = nullptr;
    
      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;
    
      if(auto* p = dynamic_cast<VDelay*>(obj))
        p->SetFwdgain((float)v, mod);
     
      js_sndobj_retain(ctx, this_val, "__fwdMod", hasMod ? argv[1] : JS_UNDEFINED);
      return JS_UNDEFINED;
    }

    case EMETHOD_SET_DIRGAIN: {
      double v = 0;
      JS_ToFloat64(ctx, &v, argv[0]);
      bool hasMod = argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
      SndObj* mod = nullptr;
     
      if(hasMod && !(mod = js_sndobj_arg(ctx, argv[1])))
        return JS_EXCEPTION;
    
      if(auto* p = dynamic_cast<VDelay*>(obj))
        p->SetDirgain((float)v, mod);
    
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
  }

  return JS_UNDEFINED;
}

static void
js_sndobjeffect_finalizer(JSRuntime* rt, JSValue val) {
  SndObjPtr* e;

  if((e = static_cast<SndObjPtr*>(JS_GetOpaque(val, js_sndobjeffect_class_id)))) {
    e->~SndObjPtr();
    js_free_rt(rt, e);
  }
}

static JSClassDef js_sndobjeffect_class = {
    .class_name = "SndObjEffect",
    .finalizer = js_sndobjeffect_finalizer,
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
};

enum {
  INSTANCE_ADSR = 0,
  INSTANCE_RESON,
  INSTANCE_COMB,
  INSTANCE_ALLPASS,
  INSTANCE_VDELAY,
  INSTANCE_GAIN,
  INSTANCE_MIXER,
};

static JSValue
js_sndobjeffect_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  SndObjPtr* e = static_cast<SndObjPtr*>(js_mallocz(ctx, sizeof(SndObjPtr)));
  if(!e)
    return JS_EXCEPTION;
  new(e) SndObjPtr();

  switch(magic) {
    case INSTANCE_ADSR: {
      double att = 0, maxamp = 1, dec = 0, sus = 1, rel = 0, dur = 1;

      if(argc > 0)
        JS_ToFloat64(ctx, &att, argv[0]);

      if(argc > 1)
        JS_ToFloat64(ctx, &maxamp, argv[1]);

      if(argc > 2)
        JS_ToFloat64(ctx, &dec, argv[2]);

      if(argc > 3)
        JS_ToFloat64(ctx, &sus, argv[3]);

      if(argc > 4)
        JS_ToFloat64(ctx, &rel, argv[4]);

      if(argc > 5)
        JS_ToFloat64(ctx, &dur, argv[5]);
     
      SndObj* input = nullptr;

      if(argc > 6 && !JS_IsUndefined(argv[6]) && !JS_IsNull(argv[6]) && !(input = js_sndobj_arg(ctx, argv[6])))
        goto argfail;

      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;

      if(argc > 7)
        JS_ToInt32(ctx, &vecsize, argv[7]);

      if(argc > 8)
        JS_ToFloat64(ctx, &sr, argv[8]);

      e->reset(new ADSR((float)att, (float)maxamp, (float)dec, (float)sus, (float)rel, (float)dur, input, vecsize, (float)sr));
      break;
    }

    case INSTANCE_RESON: {
      double fr = 440, bw = 100;

      if(argc > 0)
        JS_ToFloat64(ctx, &fr, argv[0]);

      if(argc > 1)
        JS_ToFloat64(ctx, &bw, argv[1]);

      if(argc < 3) {
        JS_ThrowTypeError(ctx, "Reson requires an input SndObj");
        goto argfail;
      }

      SndObj* input = js_sndobj_arg(ctx, argv[2]);

      if(!input)
        goto argfail;

      SndObj *freqMod = nullptr, *bwMod = nullptr;

      if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]) && !(freqMod = js_sndobj_arg(ctx, argv[3])))
        goto argfail;

      if(argc > 4 && !JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4]) && !(bwMod = js_sndobj_arg(ctx, argv[4])))
        goto argfail;

      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;

      if(argc > 5)
        JS_ToInt32(ctx, &vecsize, argv[5]);

      if(argc > 6)
        JS_ToFloat64(ctx, &sr, argv[6]);

      e->reset(new Reson((float)fr, (float)bw, input, freqMod, bwMod, vecsize, (float)sr));
      break;
    }

    case INSTANCE_COMB:
    case INSTANCE_ALLPASS: {
      double gain = 0, dtime = 0.01;

      if(argc > 0)
        JS_ToFloat64(ctx, &gain, argv[0]);

      if(argc > 1)
        JS_ToFloat64(ctx, &dtime, argv[1]);

      if(argc < 3) {
        JS_ThrowTypeError(ctx, "%s requires an input SndObj", magic == INSTANCE_COMB ? "Comb" : "Allpass");
        goto argfail;
      }

      SndObj* input = js_sndobj_arg(ctx, argv[2]);
      if(!input)
        goto argfail;

      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;

      if(argc > 3)
        JS_ToInt32(ctx, &vecsize, argv[3]);

      if(argc > 4)
        JS_ToFloat64(ctx, &sr, argv[4]);

      if(magic == INSTANCE_COMB)
        e->reset(new Comb((float)gain, (float)dtime, input, vecsize, (float)sr));
      else
        e->reset(new Allpass((float)gain, (float)dtime, input, vecsize, (float)sr));
      break;
    }

    case INSTANCE_VDELAY: {
      double maxdt = 1, dt = 0.1, fdb = 0, fwd = 1, dir = 0;

      if(argc > 0)
        JS_ToFloat64(ctx, &maxdt, argv[0]);

      if(argc > 1)
        JS_ToFloat64(ctx, &dt, argv[1]);

      if(argc > 2)
        JS_ToFloat64(ctx, &fdb, argv[2]);

      if(argc > 3)
        JS_ToFloat64(ctx, &fwd, argv[3]);

      if(argc > 4)
        JS_ToFloat64(ctx, &dir, argv[4]);

      if(argc < 6) {
        JS_ThrowTypeError(ctx, "VDelay requires an input SndObj");
        goto argfail;
      }
      SndObj* input = js_sndobj_arg(ctx, argv[5]);
      if(!input)
        goto argfail;

      SndObj *timeMod = nullptr, *fdbMod = nullptr, *fwdMod = nullptr, *dirMod = nullptr;

      if(argc > 6 && !JS_IsUndefined(argv[6]) && !JS_IsNull(argv[6]) && !(timeMod = js_sndobj_arg(ctx, argv[6])))
        goto argfail;

      if(argc > 7 && !JS_IsUndefined(argv[7]) && !JS_IsNull(argv[7]) && !(fdbMod = js_sndobj_arg(ctx, argv[7])))
        goto argfail;

      if(argc > 8 && !JS_IsUndefined(argv[8]) && !JS_IsNull(argv[8]) && !(fwdMod = js_sndobj_arg(ctx, argv[8])))
        goto argfail;

      if(argc > 9 && !JS_IsUndefined(argv[9]) && !JS_IsNull(argv[9]) && !(dirMod = js_sndobj_arg(ctx, argv[9])))
        goto argfail;

      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;

      if(argc > 10)
        JS_ToInt32(ctx, &vecsize, argv[10]);

      if(argc > 11)
        JS_ToFloat64(ctx, &sr, argv[11]);

      e->reset(new VDelay((float)maxdt, (float)dt, (float)fdb, (float)fwd, (float)dir, input, timeMod, fdbMod, fwdMod, dirMod, vecsize, (float)sr));
      break;
    }

    case INSTANCE_GAIN: {
      double gain = 0;
      if(argc > 0)
        JS_ToFloat64(ctx, &gain, argv[0]);

      if(argc < 2) {
        JS_ThrowTypeError(ctx, "Gain requires an input SndObj");
        goto argfail;
      }

      SndObj* input = js_sndobj_arg(ctx, argv[1]);
      if(!input)
        goto argfail;

      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;

      if(argc > 2)
        JS_ToInt32(ctx, &vecsize, argv[2]);

      if(argc > 3)
        JS_ToFloat64(ctx, &sr, argv[3]);

      e->reset(new Gain((float)gain, input, vecsize, (float)sr));
      break;
    }

    case INSTANCE_MIXER: {
      int32_t vecsize = DEF_VECSIZE;
      double sr = DEF_SR;

      if(argc > 0)
        JS_ToInt32(ctx, &vecsize, argv[0]);

      if(argc > 1)
        JS_ToFloat64(ctx, &sr, argv[1]);

      e->reset(new Mixer(0, nullptr, vecsize, (float)sr));
      break;
    }
  }

  {
    JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if(JS_IsException(proto))
      goto fail;

    if(!JS_IsObject(proto)) {
      JS_FreeValue(ctx, proto);
      proto = JS_DupValue(ctx, sndobjeffect_proto);
    }

    obj = JS_NewObjectProtoClass(ctx, proto, js_sndobjeffect_class_id);
    JS_FreeValue(ctx, proto);
    if(JS_IsException(obj))
      goto fail;

    JS_SetOpaque(obj, e);
    js_set_tostringtag(ctx, obj, ((const char*[]){"ADSR", "Reson", "Comb", "Allpass", "VDelay", "Gain", "Mixer"})[magic]);

    switch(magic) {
      case INSTANCE_ADSR:
        if(argc > 6 && !JS_IsUndefined(argv[6]) && !JS_IsNull(argv[6]))
          js_sndobj_retain(ctx, obj, "__input", argv[6]);
        break;
      case INSTANCE_RESON:
        js_sndobj_retain(ctx, obj, "__input", argv[2]);
        if(argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
          js_sndobj_retain(ctx, obj, "__freqMod", argv[3]);
        if(argc > 4 && !JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4]))
          js_sndobj_retain(ctx, obj, "__bwMod", argv[4]);
        break;
      case INSTANCE_COMB:
      case INSTANCE_ALLPASS:
        js_sndobj_retain(ctx, obj, "__input", argv[2]);
        break;
      case INSTANCE_VDELAY:
        js_sndobj_retain(ctx, obj, "__input", argv[5]);
        if(argc > 6 && !JS_IsUndefined(argv[6]) && !JS_IsNull(argv[6]))
          js_sndobj_retain(ctx, obj, "__timeMod", argv[6]);
        if(argc > 7 && !JS_IsUndefined(argv[7]) && !JS_IsNull(argv[7]))
          js_sndobj_retain(ctx, obj, "__fdbMod", argv[7]);
        if(argc > 8 && !JS_IsUndefined(argv[8]) && !JS_IsNull(argv[8]))
          js_sndobj_retain(ctx, obj, "__fwdMod", argv[8]);
        if(argc > 9 && !JS_IsUndefined(argv[9]) && !JS_IsNull(argv[9]))
          js_sndobj_retain(ctx, obj, "__dirMod", argv[9]);
        break;
      case INSTANCE_GAIN:
        js_sndobj_retain(ctx, obj, "__input", argv[1]);
        break;
    }

    return obj;

  fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

argfail:
  e->~SndObjPtr();
  js_free(ctx, e);
  return JS_EXCEPTION;
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

  JS_NewClassID(&js_sndobjeffect_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sndobjeffect_class_id, &js_sndobjeffect_class);
  sndobjeffect_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, sndobjeffect_proto, js_sndobjeffect_funcs, countof(js_sndobjeffect_funcs));
  JS_SetClassProto(ctx, js_sndobjeffect_class_id, sndobjeffect_proto);

  if(m) {
    JSValue ctor;

    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjtable_constructor, "HarmTable", 4, JS_CFUNC_constructor_magic, INSTANCE_HARM_TABLE);
    JS_SetModuleExport(ctx, m, "HarmTable", ctor);

    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjgenerator_constructor, "Oscili", 7, JS_CFUNC_constructor_magic, INSTANCE_OSCILI);
    JS_SetModuleExport(ctx, m, "Oscili", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjgenerator_constructor, "Buzz", 7, JS_CFUNC_constructor_magic, INSTANCE_BUZZ);
    JS_SetModuleExport(ctx, m, "Buzz", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjgenerator_constructor, "Randi", 6, JS_CFUNC_constructor_magic, INSTANCE_RANDI);
    JS_SetModuleExport(ctx, m, "Randi", ctor);

    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "ADSR", 9, JS_CFUNC_constructor_magic, INSTANCE_ADSR);
    JS_SetModuleExport(ctx, m, "ADSR", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "Reson", 7, JS_CFUNC_constructor_magic, INSTANCE_RESON);
    JS_SetModuleExport(ctx, m, "Reson", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "Comb", 5, JS_CFUNC_constructor_magic, INSTANCE_COMB);
    JS_SetModuleExport(ctx, m, "Comb", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "Allpass", 5, JS_CFUNC_constructor_magic, INSTANCE_ALLPASS);
    JS_SetModuleExport(ctx, m, "Allpass", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "VDelay", 12, JS_CFUNC_constructor_magic, INSTANCE_VDELAY);
    JS_SetModuleExport(ctx, m, "VDelay", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "Gain", 4, JS_CFUNC_constructor_magic, INSTANCE_GAIN);
    JS_SetModuleExport(ctx, m, "Gain", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_sndobjeffect_constructor, "Mixer", 2, JS_CFUNC_constructor_magic, INSTANCE_MIXER);
    JS_SetModuleExport(ctx, m, "Mixer", ctor);
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_sndobj(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "HarmTable");
  JS_AddModuleExport(ctx, m, "Oscili");
  JS_AddModuleExport(ctx, m, "Buzz");
  JS_AddModuleExport(ctx, m, "Randi");
  JS_AddModuleExport(ctx, m, "ADSR");
  JS_AddModuleExport(ctx, m, "Reson");
  JS_AddModuleExport(ctx, m, "Comb");
  JS_AddModuleExport(ctx, m, "Allpass");
  JS_AddModuleExport(ctx, m, "VDelay");
  JS_AddModuleExport(ctx, m, "Gain");
  JS_AddModuleExport(ctx, m, "Mixer");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_sndobj_init)))
    js_init_module_sndobj(ctx, m);

  return m;
}
