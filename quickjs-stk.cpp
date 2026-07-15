#include <quickjs.h>
#include <cutils.h>

#include <vector>
#include <memory>
#include <cmath>
#include <cstring>

#include "defines.h"
#include "Stk.h"
#include "Generator.h"
#include "Filter.h"
#include "Effect.h"
#include "FM.h"
#include "Instrmnt.h"

/* stk::Filter */
#include "BiQuad.h"
#include "DelayA.h"
#include "DelayL.h"
#include "Delay.h"
#include "Fir.h"
#include "FormSwep.h"
#include "Iir.h"
#include "OnePole.h"
#include "OneZero.h"
#include "PoleZero.h"
#include "TapDelay.h"
#include "TwoPole.h"
#include "TwoZero.h"

/* stk::Function */
#include "Function.h"
#include "Cubic.h"

/* stk::Generator */
#include "ADSR.h"
#include "Asymp.h"
#include "BlitSaw.h"
#include "BlitSquare.h"
#include "Blit.h"
#include "Envelope.h"
#include "Granulate.h"
#include "Modulate.h"
#include "Noise.h"
#include "SineWave.h"
#include "SingWave.h"

/* stk::Effect */
#include "Chorus.h"
#include "Echo.h"
#include "FreeVerb.h"
#include "JCRev.h"
#include "LentPitShift.h"
#include "NRev.h"
#include "PRCRev.h"
#include "PitShift.h"

/* stk::FM */
#include "BeeThree.h"
#include "FMVoices.h"
#include "HevyMetl.h"
#include "PercFlut.h"
#include "Rhodey.h"
#include "TubeBell.h"
#include "Wurley.h"

/* stk::Instr */
#include "BandedWG.h"
#include "BlowBotl.h"
#include "BlowHole.h"
#include "Bowed.h"
#include "Brass.h"
#include "Clarinet.h"
#include "Drummer.h"
#include "FM.h"
#include "Flute.h"
#include "Mandolin.h"
#include "Mesh2D.h"
#include "Modal.h"
#include "Plucked.h"
#include "Recorder.h"
#include "Resonate.h"
#include "Sampler.h"
#include "Saxofony.h"
#include "Shakers.h"
#include "Simple.h"
#include "Sitar.h"
#include "StifKarp.h"
#include "VoicForm.h"
#include "Whistle.h"

#include "Sampler.h"
#include "Moog.h"

#include <memory>

using stk::Stk;

static JSClassID js_stkframes_class_id, js_stk_class_id, js_stkfilter_class_id, js_stkgenerator_class_id, js_stkeffect_class_id, js_stkfm_class_id,
    js_stkinstrmnt_class_id, js_stkfunction_class_id;
static JSValue stkframes_proto, stkframes_ctor, stk_proto, stk_ctor, stkfilter_proto, stkfilter_ctor, stkgenerator_proto, stkgenerator_ctor, stkeffect_proto,
    stkeffect_ctor, stkfm_proto, stkfm_ctor, stkinstrmnt_proto, stkinstrmnt_ctor, stkfunction_proto, stkfunction_ctor,
    twintdrum_proto, tr909bassdrum_proto;

typedef std::shared_ptr<stk::Stk> StkPtr;
typedef std::shared_ptr<stk::StkFrames> StkFramesPtr;
typedef std::shared_ptr<stk::Generator> StkGeneratorPtr;
typedef std::shared_ptr<stk::Filter> StkFilterPtr;
typedef std::shared_ptr<stk::Effect> StkEffectPtr;
typedef std::shared_ptr<stk::FM> StkFMPtr;
typedef std::shared_ptr<stk::Instrmnt> StkInstrmntPtr;
typedef std::shared_ptr<stk::Function> StkFunctionPtr;

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

static int64_t
array_length(JSContext* ctx, JSValueConst arr) {
  int64_t len = -1;
  JSValue lprop = JS_GetPropertyStr(ctx, arr, "length");

  if(!JS_IsException(lprop))
    JS_ToInt64(ctx, &len, lprop);

  JS_FreeValue(ctx, lprop);
  return len;
}

static void
array_to_vector(JSContext* ctx, JSValueConst arr, std::vector<double>& vec) {
  int64_t len = array_length(ctx, arr);

  for(int64_t i = 0; i < len; i++) {
    JSValue v = JS_GetPropertyUint32(ctx, arr, i);
    double f;
    JS_ToFloat64(ctx, &f, v);
    JS_FreeValue(ctx, v);

    vec.push_back(f);
  }
}

static void
array_to_vector(JSContext* ctx, JSValueConst arr, std::vector<unsigned long>& vec) {
  int64_t len = array_length(ctx, arr);

  for(int64_t i = 0; i < len; i++) {
    JSValue v = JS_GetPropertyUint32(ctx, arr, i);
    uint32_t u;
    JS_ToUint32(ctx, &u, v);
    JS_FreeValue(ctx, v);

    vec.push_back(u);
  }
}

static JSValue
js_stk_wrap(JSContext* ctx, Stk* s) {
  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_NewObjectProtoClass(ctx, stk_proto, js_stk_class_id);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, s);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_SAMPLERATE = 0,
};

static JSValue
js_stk_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StkPtr* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = static_cast<StkPtr*>(JS_GetOpaque2(ctx, this_val, js_stk_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*s)->sampleRate());
      break;
    }
  }

  return ret;
}

static JSValue
js_stk_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  StkPtr* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = static_cast<StkPtr*>(JS_GetOpaque2(ctx, this_val, js_stk_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_SAMPLERATE: {
      double rate;
      JS_ToFloat64(ctx, &rate, value);
      (*s)->setSampleRate(rate);
      break;
    }
  }

  return ret;
}

static void
js_stk_finalizer(JSRuntime* rt, JSValue val) {
  StkPtr* s;

  if((s = static_cast<StkPtr*>(JS_GetOpaque(val, js_stk_class_id)))) {
    s->~StkPtr();
    js_free_rt(rt, s);
  }
}

static JSClassDef js_stk_class = {
    .class_name = "Stk",
    .finalizer = js_stk_finalizer,
};

static const JSCFunctionListEntry js_stk_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sampleRate", js_stk_get, js_stk_set, PROP_SAMPLERATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Stk", JS_PROP_CONFIGURABLE),
};

static JSValue
js_stkframes_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  double value;
  uint32_t nframes = 0, nchannels = 0;
  int i = 0;

  if(argc > 2)
    JS_ToFloat64(ctx, &value, argv[i++]);

  if(argc > i)
    JS_ToUint32(ctx, &nframes, argv[i]);

  ++i;
  if(argc > i)
    JS_ToUint32(ctx, &nchannels, argv[i]);

  StkFramesPtr* f = static_cast<StkFramesPtr*>(js_mallocz(ctx, sizeof(StkFramesPtr)));

  if(argc > 2)
    new(f) StkFramesPtr(std::make_shared<stk::StkFrames>(value, nframes, nchannels));
  else
    new(f) StkFramesPtr(std::make_shared<stk::StkFrames>(nframes, nchannels));

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkframes_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkframes_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, f);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_RESIZE = 0,
  METHOD_INTERPOLATE,
  METHOD_GETCHANNEL,
  METHOD_SETCHANNEL,
};

static JSValue
js_stkframes_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkFramesPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = static_cast<StkFramesPtr*>(JS_GetOpaque2(ctx, this_val, js_stkframes_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RESIZE: {
      uint32_t nframes, nchannels = 1;

      JS_ToUint32(ctx, &nframes, argv[0]);
      if(argc > 1)
        JS_ToUint32(ctx, &nchannels, argv[1]);

      if(argc > 2) {
        double value;
        JS_ToFloat64(ctx, &value, argv[2]);
        (*f)->resize(nframes, nchannels, value);
      } else {
        (*f)->resize(nframes, nchannels);
      }

      break;
    }
    case METHOD_INTERPOLATE: {
      double frame;
      uint32_t channel = 0;
      JS_ToFloat64(ctx, &frame, argv[0]);
      if(argc > 1)
        JS_ToUint32(ctx, &channel, argv[1]);

      ret = JS_NewFloat64(ctx, (*f)->interpolate(frame, channel));
      break;
    }
    case METHOD_GETCHANNEL: {
      uint32_t channel, dstChannel;
      StkFramesPtr* a;

      JS_ToUint32(ctx, &channel, argv[0]);

      if(!(a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[1], js_stkframes_class_id))))
        return JS_ThrowTypeError(ctx, "argument 2 must be StkFrames");

      JS_ToUint32(ctx, &dstChannel, argv[2]);

      (*f)->getChannel(channel, *a->get(), dstChannel);
      ret = JS_DupValue(ctx, argv[1]);
      break;
    }
    case METHOD_SETCHANNEL: {
      uint32_t channel, srcChannel;
      StkFramesPtr* a;

      JS_ToUint32(ctx, &channel, argv[0]);

      if(!(a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[1], js_stkframes_class_id))))
        return JS_ThrowTypeError(ctx, "argument 2 must be StkFrames");

      JS_ToUint32(ctx, &srcChannel, argv[2]);

      (*f)->setChannel(channel, *a->get(), srcChannel);
      break;
    }
  }

  return ret;
}

enum {
  PROP_SIZE = 0,
  PROP_EMPTY,
  PROP_CHANNELS,
  PROP_FRAMES,
  PROP_DATA_RATE,
  PROP_BUFFER,
};

static void
js_stkframes_free_buf(JSRuntime* rt, void* opaque, void* ptr) {
  StkFramesPtr* f = static_cast<StkFramesPtr*>(opaque);
  f->~StkFramesPtr();
  js_free_rt(rt, f);
}

static JSValue
js_stkframes_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StkFramesPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = static_cast<StkFramesPtr*>(JS_GetOpaque2(ctx, this_val, js_stkframes_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_SIZE: {
      ret = JS_NewUint32(ctx, (*f)->size());
      break;
    }
    case PROP_EMPTY: {
      ret = JS_NewBool(ctx, (*f)->empty());
      break;
    }
    case PROP_CHANNELS: {
      ret = JS_NewUint32(ctx, (*f)->channels());
      break;
    }
    case PROP_FRAMES: {
      ret = JS_NewUint32(ctx, (*f)->frames());
      break;
    }
    case PROP_DATA_RATE: {
      ret = JS_NewFloat64(ctx, (*f)->dataRate());
      break;
    }
    case PROP_BUFFER: {
      stk::StkFloat* ptr = &((*(*f))[0]);
      size_t len = (*f)->size();

      StkFramesPtr* opaque = static_cast<StkFramesPtr*>(js_mallocz(ctx, sizeof(StkFramesPtr)));

      new(opaque) StkFramesPtr(*f);

      ret = JS_NewArrayBuffer(ctx, reinterpret_cast<uint8_t*>(ptr), sizeof(stk::StkFloat) * len, js_stkframes_free_buf, opaque, FALSE);
      break;
    }
  }

  return ret;
}

static JSValue
js_stkframes_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  StkFramesPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = static_cast<StkFramesPtr*>(JS_GetOpaque2(ctx, this_val, js_stkframes_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_DATA_RATE: {
      double rate;
      JS_ToFloat64(ctx, &rate, value);
      (*f)->setDataRate(rate);
      break;
    }
  }

  return ret;
}

static void
js_stkframes_finalizer(JSRuntime* rt, JSValue val) {
  StkFramesPtr* f;

  if((f = static_cast<StkFramesPtr*>(JS_GetOpaque(val, js_stkframes_class_id)))) {
    f->~StkFramesPtr();
    js_free_rt(rt, f);
  }
}

static JSClassDef js_stkframes_class = {
    .class_name = "StkFrames",
    .finalizer = js_stkframes_finalizer,
};

static const JSCFunctionListEntry js_stkframes_funcs[] = {
    JS_CFUNC_MAGIC_DEF("resize", 1, js_stkframes_method, METHOD_RESIZE),
    JS_CFUNC_MAGIC_DEF("interpolate", 1, js_stkframes_method, METHOD_INTERPOLATE),
    JS_CFUNC_MAGIC_DEF("getChannel", 3, js_stkframes_method, METHOD_GETCHANNEL),
    JS_CFUNC_MAGIC_DEF("setChannel", 3, js_stkframes_method, METHOD_SETCHANNEL),
    JS_CGETSET_MAGIC_DEF("size", js_stkframes_get, 0, PROP_SIZE),
    JS_CGETSET_MAGIC_DEF("empty", js_stkframes_get, 0, PROP_EMPTY),
    JS_CGETSET_MAGIC_DEF("channels", js_stkframes_get, 0, PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("frames", js_stkframes_get, 0, PROP_FRAMES),
    JS_CGETSET_MAGIC_DEF("dataRate", js_stkframes_get, js_stkframes_set, PROP_DATA_RATE),
    JS_CGETSET_MAGIC_DEF("buffer", js_stkframes_get, 0, PROP_BUFFER),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkFrames", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_ADSR = 0,
  INSTANCE_ASYMP,
  INSTANCE_BLIT_SAW,
  INSTANCE_BLIT_SQUARE,
  INSTANCE_BLIT,
  INSTANCE_ENVELOPE,
  INSTANCE_GRANULATE,
  INSTANCE_MODULATE,
  INSTANCE_NOISE,
  INSTANCE_SINE_WAVE,
  INSTANCE_SING_WAVE,
};

static JSValue
js_stkgenerator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkGeneratorPtr* g = static_cast<StkGeneratorPtr*>(js_mallocz(ctx, sizeof(StkGeneratorPtr)));
  double arg = 0;

  if(argc > 0)
    JS_ToFloat64(ctx, &arg, argv[0]);

  switch(magic) {
    case INSTANCE_ADSR: {
      *g = std::make_shared<stk::ADSR>();
      break;
    }
    case INSTANCE_ASYMP: {
      *g = std::make_shared<stk::Asymp>();
      break;
    }
    case INSTANCE_BLIT: {
      *g = std::make_shared<stk::Blit>(argc > 0 ? arg : 220.0);
      break;
    }
    case INSTANCE_BLIT_SAW: {
      *g = std::make_shared<stk::BlitSaw>(argc > 0 ? arg : 220.0);
      break;
    }
    case INSTANCE_BLIT_SQUARE: {
      *g = std::make_shared<stk::BlitSquare>(argc > 0 ? arg : 220.0);
      break;
    }
    case INSTANCE_ENVELOPE: {
      *g = std::make_shared<stk::Envelope>();
      break;
    }
    case INSTANCE_GRANULATE: {
      if(argc > 1) {
        uint32_t nvoices;
        const char* filename = JS_ToCString(ctx, argv[1]);
        BOOL raw = FALSE;
        JS_ToUint32(ctx, &nvoices, argv[0]);
        if(argc > 2)
          raw = JS_ToBool(ctx, argv[2]);
        *g = std::make_shared<stk::Granulate>(nvoices, filename, raw);
        JS_FreeCString(ctx, filename);
      } else {
        *g = std::make_shared<stk::Granulate>();
      }
      break;
    }
    case INSTANCE_MODULATE: {
      *g = std::make_shared<stk::Modulate>();
      break;
    }
    case INSTANCE_NOISE: {
      *g = std::make_shared<stk::Noise>(argc > 0 ? arg : 0);
      break;
    }
    case INSTANCE_SINE_WAVE: {
      *g = std::make_shared<stk::SineWave>();
      break;
    }
    case INSTANCE_SING_WAVE: {
      const char* filename = JS_ToCString(ctx, argv[0]);
      BOOL raw = FALSE;
      if(argc > 1)
        raw = JS_ToBool(ctx, argv[1]);
      *g = std::make_shared<stk::SingWave>(filename, raw);
      JS_FreeCString(ctx, filename);

      break;
    }
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkgenerator_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkgenerator_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, g);

  js_set_tostringtag(ctx,
                     obj,
                     ((const char*[]){
                         "StkADSR",
                         "StkAsymp",
                         "StkBlit",
                         "StkBlitSaw",
                         "StkBlitSquare",
                         "StkEnvelope",
                         "StkGranulate",
                         "StkModulate",
                         "StkNoise",
                         "StkSineWave",
                         "StkSingWave",
                     })[magic]);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_TICK = 0,
};

static JSValue
js_stkgenerator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkGeneratorPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = static_cast<StkGeneratorPtr*>(JS_GetOpaque2(ctx, this_val, js_stkgenerator_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        if(argc > 1)
          JS_ToUint32(ctx, &channel, argv[1]);

        (*f)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      /* stk::Generator only declares the StkFrames& overload virtually; each
       * subclass adds its own non-virtual single-sample tick() on top, so
       * dispatch by type for the scalar path. */
      if(argc > 0)
        JS_ToUint32(ctx, &channel, argv[0]);

      stk::Generator* g = f->get();
      double sample = 0;
      if(auto* p = dynamic_cast<stk::Granulate*>(g))
        sample = p->tick(channel);
      else if(auto* p = dynamic_cast<stk::ADSR*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::Asymp*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::BlitSaw*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::BlitSquare*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::Blit*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::Envelope*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::Modulate*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::Noise*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::SineWave*>(g))
        sample = p->tick();
      else if(auto* p = dynamic_cast<stk::SingWave*>(g))
        sample = p->tick();

      ret = JS_NewFloat64(ctx, sample);
      break;
    }
  }

  return ret;
}

enum {
  PROP_CHANNELS_OUT = 0,
};

static JSValue
js_stkgenerator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StkGeneratorPtr* g;
  JSValue ret = JS_UNDEFINED;

  if(!(g = static_cast<StkGeneratorPtr*>(JS_GetOpaque2(ctx, this_val, js_stkgenerator_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_CHANNELS_OUT: {
      ret = JS_NewUint32(ctx, (*g)->channelsOut());
      break;
    }
  }

  return ret;
}

static JSValue
js_stkgenerator_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  StkGeneratorPtr* g;
  JSValue ret = JS_UNDEFINED;

  if(!(g = static_cast<StkGeneratorPtr*>(JS_GetOpaque2(ctx, this_val, js_stkgenerator_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
js_stkgenerator_finalizer(JSRuntime* rt, JSValue val) {
  StkGeneratorPtr* s;

  if((s = static_cast<StkGeneratorPtr*>(JS_GetOpaque(val, js_stkgenerator_class_id)))) {
    s->~StkGeneratorPtr();
    js_free_rt(rt, s);
  }
}

static JSClassDef js_stkgenerator_class = {
    .class_name = "StkGenerator",
    .finalizer = js_stkgenerator_finalizer,
};

static const JSCFunctionListEntry js_stkgenerator_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkgenerator_method, METHOD_TICK),
    JS_CGETSET_MAGIC_DEF("channelsOut", js_stkgenerator_get, js_stkgenerator_set, PROP_SAMPLERATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkGenerator", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_BIQUAD = 0,
  INSTANCE_DELAY,
  INSTANCE_DELAY_A,
  INSTANCE_DELAY_L,
  INSTANCE_FIR,
  INSTANCE_FORM_SWEP,
  INSTANCE_IIR,
  INSTANCE_ONE_POLE,
  INSTANCE_ONE_ZERO,
  INSTANCE_POLE_ZERO,
  INSTANCE_TAP_DELAY,
  INSTANCE_TWO_POLE,
  INSTANCE_TWO_ZERO,
};

static JSValue
js_stkfilter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkFilterPtr* f = static_cast<StkFilterPtr*>(js_mallocz(ctx, sizeof(StkFilterPtr)));

  switch(magic) {
    case INSTANCE_DELAY:
    case INSTANCE_DELAY_A:
    case INSTANCE_DELAY_L: {
      double delay = 0, maxDelay = 4095;
      if(argc > 0)
        JS_ToFloat64(ctx, &delay, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &maxDelay, argv[1]);

      switch(magic) {
        case INSTANCE_DELAY: {
          *f = std::make_shared<stk::Delay>();
          break;
        }
        case INSTANCE_DELAY_A: {
          *f = std::make_shared<stk::DelayA>(delay, maxDelay);
          break;
        }
        case INSTANCE_DELAY_L: {
          *f = std::make_shared<stk::DelayL>(delay, maxDelay);
          break;
        }
      }

      break;
    }

    case INSTANCE_FIR: {
      if(argc > 0) {
        std::vector<double> coeff;
        array_to_vector(ctx, argv[0], coeff);
        *f = std::make_shared<stk::Fir>(coeff);
      } else {
        *f = std::make_shared<stk::Fir>();
      }

      break;
    }
    case INSTANCE_IIR: {
      std::vector<double> acoeff, bcoeff;

      if(argc > 0) {
        array_to_vector(ctx, argv[0], bcoeff);
        if(argc > 1)
          array_to_vector(ctx, argv[1], acoeff);

        *f = std::make_shared<stk::Iir>(bcoeff, acoeff);
      } else {
        *f = std::make_shared<stk::Iir>();
      }

      break;
    }
    case INSTANCE_TAP_DELAY: {
      std::vector<unsigned long> taps;

      if(argc > 0) {
        uint32_t maxDelay = 4095;

        array_to_vector(ctx, argv[0], taps);
        if(argc > 1)
          JS_ToUint32(ctx, &maxDelay, argv[1]);

        *f = std::make_shared<stk::TapDelay>(taps, maxDelay);
      } else {
        *f = std::make_shared<stk::TapDelay>();
      }

      break;
    }

    default: {
      double arg = 0;
      if(argc > 0)
        JS_ToFloat64(ctx, &arg, argv[0]);

      switch(magic) {
        case INSTANCE_BIQUAD: {
          *f = std::make_shared<stk::BiQuad>();
          break;
        }
        case INSTANCE_FORM_SWEP: {
          *f = std::make_shared<stk::FormSwep>();
          break;
        }
        case INSTANCE_ONE_POLE: {
          *f = std::make_shared<stk::OnePole>(arg);
          break;
        }
        case INSTANCE_ONE_ZERO: {
          *f = std::make_shared<stk::OneZero>(arg);
          break;
        }
        case INSTANCE_POLE_ZERO: {
          *f = std::make_shared<stk::PoleZero>();
          break;
        }
        case INSTANCE_TWO_POLE: {
          *f = std::make_shared<stk::TwoPole>();
          break;
        }
        case INSTANCE_TWO_ZERO: {
          *f = std::make_shared<stk::TwoZero>();
          break;
        }
      }

      break;
    }
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkfilter_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkfilter_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, f);

  js_set_tostringtag(ctx,
                     obj,
                     ((const char*[]){
                         "StkBiQuad",
                         "StkDelay",
                         "StkDelayA",
                         "StkDelayL",
                         "StkFir",
                         "StkFormSwep",
                         "StkIir",
                         "StkOnePole",
                         "StkOneZero",
                         "StkPoleZero",
                         "StkTapDelay",
                         "StkTwoPole",
                         "StkTwoZero",
                     })[magic]);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_FILTER_TICK = 0,
};

static JSValue
js_stkfilter_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkFilterPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = static_cast<StkFilterPtr*>(JS_GetOpaque2(ctx, this_val, js_stkfilter_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FILTER_TICK: {
      StkFramesPtr* a;

      if((a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        uint32_t channel = 0;
        if(argc > 1)
          JS_ToUint32(ctx, &channel, argv[1]);

        (*f)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      double input = 0;
      JS_ToFloat64(ctx, &input, argv[0]);

      /* Filter subclasses only expose a polymorphic StkFrames& tick(); route a
       * single sample through a one-frame buffer so any filter type works here. */
      stk::StkFrames frame(1, 1);
      frame[0] = input;
      (*f)->tick(frame, 0);

      ret = JS_NewFloat64(ctx, frame[0]);
      break;
    }
  }

  return ret;
}

static void
js_stkfilter_finalizer(JSRuntime* rt, JSValue val) {
  StkFilterPtr* f;

  if((f = static_cast<StkFilterPtr*>(JS_GetOpaque(val, js_stkfilter_class_id)))) {
    f->~StkFilterPtr();
    js_free_rt(rt, f);
  }
}

static JSClassDef js_stkfilter_class = {
    .class_name = "StkFilter",
    .finalizer = js_stkfilter_finalizer,
};

static const JSCFunctionListEntry js_stkfilter_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkfilter_method, METHOD_FILTER_TICK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkFilter", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_CHORUS = 0,
  INSTANCE_ECHO,
  INSTANCE_FREEVERB,
  INSTANCE_JCREV,
  INSTANCE_LENTPITSHIFT,
  INSTANCE_NREV,
  INSTANCE_PITSHIFT,
  INSTANCE_PRCREV,
};

static JSValue
js_stkeffect_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkEffectPtr* e = static_cast<StkEffectPtr*>(js_mallocz(ctx, sizeof(StkEffectPtr)));
  double arg = 0;
  if(argc > 0)
    JS_ToFloat64(ctx, &arg, argv[0]);

  switch(magic) {
    case INSTANCE_CHORUS: {
      *e = std::make_shared<stk::Chorus>(argc > 0 ? arg : 6000);
      break;
    }
    case INSTANCE_ECHO: {
      *e = std::make_shared<stk::Echo>(argc > 0 ? arg : stk::Stk::sampleRate());
      break;
    }
    case INSTANCE_FREEVERB: {
      *e = std::make_shared<stk::FreeVerb>();
      break;
    }
    case INSTANCE_JCREV: {
      *e = std::make_shared<stk::JCRev>(argc > 0 ? arg : 1.0);
      break;
    }
    case INSTANCE_LENTPITSHIFT: {
      int32_t tmax = stk::RT_BUFFER_SIZE;
      if(argc > 1)
        JS_ToInt32(ctx, &tmax, argv[1]);
      *e = std::make_shared<stk::LentPitShift>(argc > 0 ? arg : 1.0, tmax);
      break;
    }
    case INSTANCE_NREV: {
      *e = std::make_shared<stk::NRev>(argc > 0 ? arg : 1.0);
      break;
    }
    case INSTANCE_PITSHIFT: {
      *e = std::make_shared<stk::PitShift>();
      break;
    }
    case INSTANCE_PRCREV: {
      *e = std::make_shared<stk::PRCRev>(argc > 0 ? arg : 1.0);
      break;
    }
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkeffect_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkeffect_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, e);

  js_set_tostringtag(ctx,
                     obj,
                     ((const char*[]){
                         "StkChorus",
                         "StkEcho",
                         "StkFreeVerb",
                         "StkJCRev",
                         "StkLentPitShift",
                         "StkNRev",
                         "StkPRCRev",
                         "StkPitShift",
                     })[magic]);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_EFFECT_TICK = 0,
  METHOD_EFFECT_SET_MIX,
  METHOD_EFFECT_CLEAR,
};

static JSValue
js_stkeffect_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkEffectPtr* e;
  JSValue ret = JS_UNDEFINED;

  if(!(e = static_cast<StkEffectPtr*>(JS_GetOpaque2(ctx, this_val, js_stkeffect_class_id))))
    return JS_EXCEPTION;

  stk::Effect* eff = e->get();

  switch(magic) {
    case METHOD_EFFECT_TICK: {
      StkFramesPtr* a;

      if((a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        uint32_t channel = 0;
        if(argc > 1)
          JS_ToUint32(ctx, &channel, argv[1]);

        /* Effect subclasses don't share a common virtual tick() either, so
         * dispatch by type same as the scalar path below. */
        if(auto* p = dynamic_cast<stk::FreeVerb*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::JCRev*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::PRCRev*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::NRev*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::Chorus*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::Echo*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::PitShift*>(eff))
          p->tick(*a->get(), channel);
        else if(auto* p = dynamic_cast<stk::LentPitShift*>(eff))
          p->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      double in1 = 0, in2 = 0;
      JS_ToFloat64(ctx, &in1, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &in2, argv[1]);

      /* Effect subclasses don't share a common tick() signature (some are
       * mono, some stereo, FreeVerb takes two inputs), so dispatch by type
       * and then read back through the (shared) lastFrame() accessor. */
      if(auto* p = dynamic_cast<stk::FreeVerb*>(eff))
        p->tick(in1, in2);
      else if(auto* p = dynamic_cast<stk::JCRev*>(eff))
        p->tick(in1);
      else if(auto* p = dynamic_cast<stk::PRCRev*>(eff))
        p->tick(in1);
      else if(auto* p = dynamic_cast<stk::NRev*>(eff))
        p->tick(in1);
      else if(auto* p = dynamic_cast<stk::Chorus*>(eff))
        p->tick(in1);
      else if(auto* p = dynamic_cast<stk::Echo*>(eff))
        p->tick(in1);
      else if(auto* p = dynamic_cast<stk::PitShift*>(eff))
        p->tick(in1);
      else if(auto* p = dynamic_cast<stk::LentPitShift*>(eff))
        p->tick(in1);

      const stk::StkFrames& last = eff->lastFrame();
      unsigned int nch = eff->channelsOut();

      if(nch > 1) {
        JSValue arr = JS_NewArray(ctx);
        for(unsigned int c = 0; c < nch; c++)
          JS_SetPropertyUint32(ctx, arr, c, JS_NewFloat64(ctx, last[c]));
        ret = arr;
      } else {
        ret = JS_NewFloat64(ctx, last[0]);
      }

      break;
    }
    case METHOD_EFFECT_SET_MIX: {
      double mix = 0;
      JS_ToFloat64(ctx, &mix, argv[0]);
      eff->setEffectMix(mix);
      break;
    }
    case METHOD_EFFECT_CLEAR: {
      eff->clear();
      break;
    }
  }

  return ret;
}

static void
js_stkeffect_finalizer(JSRuntime* rt, JSValue val) {
  StkEffectPtr* e;

  if((e = static_cast<StkEffectPtr*>(JS_GetOpaque(val, js_stkeffect_class_id)))) {
    e->~StkEffectPtr();
    js_free_rt(rt, e);
  }
}

static JSClassDef js_stkeffect_class = {
    .class_name = "StkEffect",
    .finalizer = js_stkeffect_finalizer,
};

static const JSCFunctionListEntry js_stkeffect_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkeffect_method, METHOD_EFFECT_TICK),
    JS_CFUNC_MAGIC_DEF("setEffectMix", 1, js_stkeffect_method, METHOD_EFFECT_SET_MIX),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_stkeffect_method, METHOD_EFFECT_CLEAR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkEffect", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_BEETHREE = 0,
  INSTANCE_FMVOICES,
  INSTANCE_HEVYMETL,
  INSTANCE_PERCFLUT,
  INSTANCE_RHODEY,
  INSTANCE_TUBEBELL,
  INSTANCE_WURLEY,
};

static JSValue
js_stkfm_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkFMPtr* fm = static_cast<StkFMPtr*>(js_mallocz(ctx, sizeof(StkFMPtr)));
  double arg = 0;
  if(argc > 0)
    JS_ToFloat64(ctx, &arg, argv[0]);

  switch(magic) {}

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkfm_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkfm_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, fm);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_stkfm_finalizer(JSRuntime* rt, JSValue val) {
  StkFMPtr* fm;

  if((fm = static_cast<StkFMPtr*>(JS_GetOpaque(val, js_stkfm_class_id)))) {
    fm->~StkFMPtr();
    js_free_rt(rt, fm);
  }
}

static JSClassDef js_stkfm_class = {
    .class_name = "StkFM",
    .finalizer = js_stkfm_finalizer,
};

static const JSCFunctionListEntry js_stkfm_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkFM", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_BANDEDWG = 0,
  INSTANCE_BLOWBOTL,
  INSTANCE_BLOWHOLE,
  INSTANCE_BOWED,
  INSTANCE_BRASS,
  INSTANCE_CLARINET,
  INSTANCE_DRUMMER,
  INSTANCE_FLUTE,
  INSTANCE_MANDOLIN,
  INSTANCE_MESH2D,
  INSTANCE_PLUCKED,
  INSTANCE_RECORDER,
  INSTANCE_RESONATE,
  INSTANCE_SAXOFONY,
  INSTANCE_SHAKERS,
  INSTANCE_SIMPLE,
  INSTANCE_SITAR,
  INSTANCE_STIFKARP,
  INSTANCE_VOICFORM,
  INSTANCE_WHISTLE,
};

static JSValue
js_stkinstrmnt_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkInstrmntPtr* i = static_cast<StkInstrmntPtr*>(js_mallocz(ctx, sizeof(StkInstrmntPtr)));
  double arg = 0;
  if(argc > 0)
    JS_ToFloat64(ctx, &arg, argv[0]);

  switch(magic) {
    case INSTANCE_BANDEDWG: {
      *i = std::make_shared<stk::BandedWG>();
      break;
    }
    case INSTANCE_BLOWBOTL: {
      *i = std::make_shared<stk::BlowBotl>();
      break;
    }
    case INSTANCE_BLOWHOLE: {
      *i = std::make_shared<stk::BlowHole>(arg);
      break;
    }
    case INSTANCE_BOWED: {
      *i = std::make_shared<stk::Bowed>(argc > 0 ? arg : 8.0);
      break;
    }
    case INSTANCE_BRASS: {
      *i = std::make_shared<stk::Brass>(argc > 0 ? arg : 8.0);
      break;
    }
    case INSTANCE_CLARINET: {
      *i = std::make_shared<stk::Clarinet>(argc > 0 ? arg : 8.0);
      break;
    }
    case INSTANCE_DRUMMER: {
      *i = std::make_shared<stk::Drummer>();
      break;
    }
    case INSTANCE_FLUTE: {
      *i = std::make_shared<stk::Flute>(arg);
      break;
    }
    case INSTANCE_MANDOLIN: {
      *i = std::make_shared<stk::Mandolin>(arg);
      break;
    }
    case INSTANCE_MESH2D: {
      uint32_t nx, ny;
      JS_ToUint32(ctx, &nx, argv[0]);
      JS_ToUint32(ctx, &ny, argv[1]);
      *i = std::make_shared<stk::Mesh2D>(nx, ny);
      break;
    }
    // case INSTANCE_MODAL: { *i = std::make_shared<stk::Modal>(argc > 0 ? arg :
    // 4); break; }
    case INSTANCE_PLUCKED: {
      *i = std::make_shared<stk::Plucked>(argc > 0 ? arg : 10.0);
      break;
    }
    case INSTANCE_RECORDER: {
      *i = std::make_shared<stk::Recorder>();
      break;
    }
    case INSTANCE_RESONATE: {
      *i = std::make_shared<stk::Resonate>();
      break;
    }
    // case INSTANCE_SAMPLER: { *i = std::make_shared<stk::Sampler>(); break; }
    case INSTANCE_SAXOFONY: {
      *i = std::make_shared<stk::Saxofony>(arg);
      break;
    }
    case INSTANCE_SHAKERS: {
      *i = std::make_shared<stk::Shakers>(argc > 0 ? arg : 0);
      break;
    }
    case INSTANCE_SIMPLE: {
      *i = std::make_shared<stk::Simple>();
      break;
    }
    case INSTANCE_SITAR: {
      *i = std::make_shared<stk::Sitar>(argc > 0 ? arg : 8.0);
      break;
    }
    case INSTANCE_STIFKARP: {
      *i = std::make_shared<stk::StifKarp>(argc > 0 ? arg : 10.0);
      break;
    }
    case INSTANCE_VOICFORM: {
      *i = std::make_shared<stk::VoicForm>();
      break;
    }
    case INSTANCE_WHISTLE: {
      *i = std::make_shared<stk::Whistle>();
      break;
    }
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkinstrmnt_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkinstrmnt_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, i);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_INSTRMNT_TICK = 0,
  METHOD_INSTRMNT_NOTE_ON,
  METHOD_INSTRMNT_NOTE_OFF,
  METHOD_INSTRMNT_CONTROL_CHANGE,
};

static JSValue
js_stkinstrmnt_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkInstrmntPtr* i;
  JSValue ret = JS_UNDEFINED;

  if(!(i = static_cast<StkInstrmntPtr*>(JS_GetOpaque2(ctx, this_val, js_stkinstrmnt_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_INSTRMNT_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        if(argc > 1)
          JS_ToUint32(ctx, &channel, argv[1]);

        (*i)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      if(argc > 0)
        JS_ToUint32(ctx, &channel, argv[0]);

      ret = JS_NewFloat64(ctx, (*i)->tick(channel));
      break;
    }
    case METHOD_INSTRMNT_NOTE_ON: {
      double frequency = 0, amplitude = 0;
      JS_ToFloat64(ctx, &frequency, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &amplitude, argv[1]);

      (*i)->noteOn(frequency, amplitude);
      break;
    }
    case METHOD_INSTRMNT_NOTE_OFF: {
      double amplitude = 0;
      if(argc > 0)
        JS_ToFloat64(ctx, &amplitude, argv[0]);

      (*i)->noteOff(amplitude);
      break;
    }
    case METHOD_INSTRMNT_CONTROL_CHANGE: {
      int32_t number = 0;
      double value = 0;
      JS_ToInt32(ctx, &number, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &value, argv[1]);

      (*i)->controlChange(number, value);
      break;
    }
  }

  return ret;
}

static void
js_stkinstrmnt_finalizer(JSRuntime* rt, JSValue val) {
  StkInstrmntPtr* i;

  if((i = static_cast<StkInstrmntPtr*>(JS_GetOpaque(val, js_stkinstrmnt_class_id)))) {
    i->~StkInstrmntPtr();
    js_free_rt(rt, i);
  }
}

static JSClassDef js_stkinstrmnt_class = {
    .class_name = "StkInstrmnt",
    .finalizer = js_stkinstrmnt_finalizer,
};

static const JSCFunctionListEntry js_stkinstrmnt_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 0, js_stkinstrmnt_method, METHOD_INSTRMNT_TICK),
    JS_CFUNC_MAGIC_DEF("noteOn", 2, js_stkinstrmnt_method, METHOD_INSTRMNT_NOTE_ON),
    JS_CFUNC_MAGIC_DEF("noteOff", 1, js_stkinstrmnt_method, METHOD_INSTRMNT_NOTE_OFF),
    JS_CFUNC_MAGIC_DEF("controlChange", 2, js_stkinstrmnt_method, METHOD_INSTRMNT_CONTROL_CHANGE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkInstrmnt", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_CUBIC = 0,
};

static JSValue
js_stkfunction_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkFunctionPtr* g = static_cast<StkFunctionPtr*>(js_mallocz(ctx, sizeof(StkFunctionPtr)));

  switch(magic) {
    case INSTANCE_CUBIC: {
      *g = std::make_shared<stk::Cubic>();
      break;
    }
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, stkfunction_proto);
  }

  /* using new_target to get the prototype is necessary when the class is
   * extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stkfunction_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, g);

  js_set_tostringtag(ctx,
                     obj,
                     ((const char*[]){
                         "StkCubic",
                     })[magic]);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_FUNCTION_TICK = 0,
  METHOD_FUNCTION_SET_A1,
  METHOD_FUNCTION_SET_A2,
  METHOD_FUNCTION_SET_A3,
  METHOD_FUNCTION_SET_GAIN,
  METHOD_FUNCTION_SET_THRESHOLD,
};

static JSValue
js_stkfunction_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkFunctionPtr* g;
  JSValue ret = JS_UNDEFINED;

  if(!(g = static_cast<StkFunctionPtr*>(JS_GetOpaque2(ctx, this_val, js_stkfunction_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FUNCTION_TICK: {
      StkFramesPtr* a;

      if((a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        uint32_t channel = 0;
        if(argc > 1)
          JS_ToUint32(ctx, &channel, argv[1]);

        /* stk::Function doesn't declare a virtual StkFrames& tick(); Cubic is
         * presently the only bound subclass, so dispatch to its overload
         * directly, same as the Cubic-specific controls below. */
        if(auto* cubic = dynamic_cast<stk::Cubic*>(g->get()))
          cubic->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      double input = 0;
      JS_ToFloat64(ctx, &input, argv[0]);

      ret = JS_NewFloat64(ctx, (*g)->tick(input));
      break;
    }
    /* Cubic-specific shaping controls (Cubic is presently the only bound
     * stk::Function subclass). */
    case METHOD_FUNCTION_SET_A1:
    case METHOD_FUNCTION_SET_A2:
    case METHOD_FUNCTION_SET_A3:
    case METHOD_FUNCTION_SET_GAIN:
    case METHOD_FUNCTION_SET_THRESHOLD: {
      double value = 0;
      JS_ToFloat64(ctx, &value, argv[0]);

      if(auto* cubic = dynamic_cast<stk::Cubic*>(g->get())) {
        switch(magic) {
          case METHOD_FUNCTION_SET_A1: cubic->setA1(value); break;
          case METHOD_FUNCTION_SET_A2: cubic->setA2(value); break;
          case METHOD_FUNCTION_SET_A3: cubic->setA3(value); break;
          case METHOD_FUNCTION_SET_GAIN: cubic->setGain(value); break;
          case METHOD_FUNCTION_SET_THRESHOLD: cubic->setThreshold(value); break;
        }
      }

      break;
    }
  }

  return ret;
}

static void
js_stkfunction_finalizer(JSRuntime* rt, JSValue val) {
  StkFunctionPtr* g;

  if((g = static_cast<StkFunctionPtr*>(JS_GetOpaque(val, js_stkfunction_class_id)))) {
    g->~StkFunctionPtr();
    js_free_rt(rt, g);
  }
}

static JSClassDef js_stkfunction_class = {
    .class_name = "StkFunction",
    .finalizer = js_stkfunction_finalizer,
};

static const JSCFunctionListEntry js_stkfunction_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkfunction_method, METHOD_FUNCTION_TICK),
    JS_CFUNC_MAGIC_DEF("setA1", 1, js_stkfunction_method, METHOD_FUNCTION_SET_A1),
    JS_CFUNC_MAGIC_DEF("setA2", 1, js_stkfunction_method, METHOD_FUNCTION_SET_A2),
    JS_CFUNC_MAGIC_DEF("setA3", 1, js_stkfunction_method, METHOD_FUNCTION_SET_A3),
    JS_CFUNC_MAGIC_DEF("setGain", 1, js_stkfunction_method, METHOD_FUNCTION_SET_GAIN),
    JS_CFUNC_MAGIC_DEF("setThreshold", 1, js_stkfunction_method, METHOD_FUNCTION_SET_THRESHOLD),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkFunction", JS_PROP_CONFIGURABLE),
};

/* TwinTDrum, Tr909BassDrum, the DRIVE_* enum and analog_drive() live in
 * analog-drums.hpp -- see that file for the DSP model notes. */
#include "analog-drums.hpp"

static JSValue
js_new_stkframes(JSContext* ctx, unsigned int nFrames, unsigned int nChannels) {
  StkFramesPtr* f = static_cast<StkFramesPtr*>(js_mallocz(ctx, sizeof(StkFramesPtr)));
  new(f) StkFramesPtr(std::make_shared<stk::StkFrames>(nFrames, nChannels));

  JSValue obj = JS_NewObjectProtoClass(ctx, stkframes_proto, js_stkframes_class_id);
  if(JS_IsException(obj)) {
    f->~StkFramesPtr();
    js_free(ctx, f);
    return JS_EXCEPTION;
  }
  JS_SetOpaque(obj, f);
  return obj;
}

static JSValue
js_twintdrum_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  double frequency = 200.0;
  if(argc > 0)
    JS_ToFloat64(ctx, &frequency, argv[0]);

  StkInstrmntPtr* i = static_cast<StkInstrmntPtr*>(js_mallocz(ctx, sizeof(StkInstrmntPtr)));
  new(i) StkInstrmntPtr(std::make_shared<TwinTDrum>(frequency));

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, twintdrum_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_stkinstrmnt_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, i);
  js_set_tostringtag(ctx, obj, "StkTwinTDrum");
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_TWINT_SET_FREQUENCY = 0,
  METHOD_TWINT_SET_DECAY,
  METHOD_TWINT_SET_DRIVE,
  METHOD_TWINT_SET_PITCH_DROP,
  METHOD_TWINT_SET_SECONDARY,
  METHOD_TWINT_SET_CLICK,
  METHOD_TWINT_STRIKE,
  METHOD_TWINT_RENDER,
};

static JSValue
js_twintdrum_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkInstrmntPtr* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = static_cast<StkInstrmntPtr*>(JS_GetOpaque2(ctx, this_val, js_stkinstrmnt_class_id))))
    return JS_EXCEPTION;

  TwinTDrum* d = dynamic_cast<TwinTDrum*>(p->get());
  if(!d)
    return JS_ThrowTypeError(ctx, "not a StkTwinTDrum");

  double a = 0, b = 0;
  if(argc > 0)
    JS_ToFloat64(ctx, &a, argv[0]);
  if(argc > 1)
    JS_ToFloat64(ctx, &b, argv[1]);

  switch(magic) {
    case METHOD_TWINT_SET_FREQUENCY: d->setFrequency(a); break;
    case METHOD_TWINT_SET_DECAY: d->setDecay(a); break;
    case METHOD_TWINT_SET_DRIVE: d->setDrive(a, argc > 1 ? (int)b : DRIVE_TANH); break;
    case METHOD_TWINT_SET_PITCH_DROP: d->setPitchDrop(a, argc > 1 ? b : 0.03); break;
    case METHOD_TWINT_SET_SECONDARY: d->setSecondary(a, argc > 1 ? b : 0.0); break;
    case METHOD_TWINT_SET_CLICK: d->setClick(a); break;
    case METHOD_TWINT_STRIKE: d->strike(argc > 0 ? a : 1.0); break;
    case METHOD_TWINT_RENDER: {
      uint32_t n = (uint32_t)a;
      double amp = argc > 1 ? b : 1.0;
      JSValue frames = js_new_stkframes(ctx, n, 1);
      if(JS_IsException(frames))
        return frames;
      StkFramesPtr* fp = static_cast<StkFramesPtr*>(JS_GetOpaque(frames, js_stkframes_class_id));
      stk::StkFrames& fr = **fp;
      d->strike(amp);
      for(uint32_t k = 0; k < n; k++)
        fr[k] = d->tick();
      ret = frames;
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_twintdrum_funcs[] = {
    JS_CFUNC_MAGIC_DEF("setFrequency", 1, js_twintdrum_method, METHOD_TWINT_SET_FREQUENCY),
    JS_CFUNC_MAGIC_DEF("setDecay", 1, js_twintdrum_method, METHOD_TWINT_SET_DECAY),
    JS_CFUNC_MAGIC_DEF("setDrive", 1, js_twintdrum_method, METHOD_TWINT_SET_DRIVE),
    JS_CFUNC_MAGIC_DEF("setPitchDrop", 2, js_twintdrum_method, METHOD_TWINT_SET_PITCH_DROP),
    JS_CFUNC_MAGIC_DEF("setSecondary", 2, js_twintdrum_method, METHOD_TWINT_SET_SECONDARY),
    JS_CFUNC_MAGIC_DEF("setClick", 1, js_twintdrum_method, METHOD_TWINT_SET_CLICK),
    JS_CFUNC_MAGIC_DEF("strike", 1, js_twintdrum_method, METHOD_TWINT_STRIKE),
    JS_CFUNC_MAGIC_DEF("render", 2, js_twintdrum_method, METHOD_TWINT_RENDER),
};

static JSValue
js_tr909bassdrum_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  StkInstrmntPtr* i = static_cast<StkInstrmntPtr*>(js_mallocz(ctx, sizeof(StkInstrmntPtr)));
  new(i) StkInstrmntPtr(std::make_shared<Tr909BassDrum>());

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, tr909bassdrum_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_stkinstrmnt_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, i);
  js_set_tostringtag(ctx, obj, "StkTr909BassDrum");
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static int
parse_drive_type(JSContext* ctx, JSValueConst v) {
  if(JS_IsUndefined(v))
    return DRIVE_TANH;

  const char* s = JS_ToCString(ctx, v);
  int type = DRIVE_TANH;

  if(s) {
    if(!strcmp(s, "cubic"))
      type = DRIVE_CUBIC;
    else if(!strcmp(s, "fold"))
      type = DRIVE_FOLD;
    JS_FreeCString(ctx, s);
  }

  return type;
}

static bool
parse_curve_linear(JSContext* ctx, JSValueConst v) {
  if(JS_IsUndefined(v))
    return false;

  const char* s = JS_ToCString(ctx, v);
  bool linear = s && !strcmp(s, "linear");

  if(s)
    JS_FreeCString(ctx, s);

  return linear;
}

static int
parse_tr909_waveform(JSContext* ctx, JSValueConst v) {
  if(JS_IsUndefined(v))
    return TR909_WAVE_SINE;

  const char* s = JS_ToCString(ctx, v);
  int type = (s && !strcmp(s, "triangle")) ? TR909_WAVE_TRIANGLE : TR909_WAVE_SINE;

  if(s)
    JS_FreeCString(ctx, s);

  return type;
}

enum {
  METHOD_TR909_SET_PITCH_ENV = 0,
  METHOD_TR909_SET_PITCH_SPIKE,
  METHOD_TR909_SET_AMP_ENV,
  METHOD_TR909_SET_PUNCH,
  METHOD_TR909_SET_DRIVE,
  METHOD_TR909_SET_WAVEFORM,
  METHOD_TR909_SET_TONE,
  METHOD_TR909_SET_TONE_RESONANCE,
  METHOD_TR909_SET_CLICK,
  METHOD_TR909_SET_SUB,
  METHOD_TR909_SET_TUNE,
  METHOD_TR909_TRIGGER,
  METHOD_TR909_RENDER,
};

static JSValue
js_tr909bassdrum_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkInstrmntPtr* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = static_cast<StkInstrmntPtr*>(JS_GetOpaque2(ctx, this_val, js_stkinstrmnt_class_id))))
    return JS_EXCEPTION;

  Tr909BassDrum* d = dynamic_cast<Tr909BassDrum*>(p->get());
  if(!d)
    return JS_ThrowTypeError(ctx, "not a StkTr909BassDrum");

  switch(magic) {
    case METHOD_TR909_SET_PITCH_ENV: {
      double start = 0, end = 0, decay = 0;
      JS_ToFloat64(ctx, &start, argv[0]);
      JS_ToFloat64(ctx, &end, argv[1]);
      JS_ToFloat64(ctx, &decay, argv[2]);
      d->setPitchEnvelope(start, end, decay, argc > 3 ? parse_curve_linear(ctx, argv[3]) : false);
      break;
    }
    case METHOD_TR909_SET_PITCH_SPIKE: {
      double semitones = 0, time = 0.005;
      JS_ToFloat64(ctx, &semitones, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &time, argv[1]);
      d->setPitchSpike(semitones, time);
      break;
    }
    case METHOD_TR909_SET_AMP_ENV: {
      double decay = 0;
      JS_ToFloat64(ctx, &decay, argv[0]);
      d->setAmpEnvelope(decay, argc > 1 ? parse_curve_linear(ctx, argv[1]) : false);
      break;
    }
    case METHOD_TR909_SET_PUNCH: {
      double amount = 0, time = 0.01;
      JS_ToFloat64(ctx, &amount, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &time, argv[1]);
      d->setPunch(amount, time);
      break;
    }
    case METHOD_TR909_SET_DRIVE: {
      double amount = 0;
      JS_ToFloat64(ctx, &amount, argv[0]);
      d->setDrive(amount, argc > 1 ? parse_drive_type(ctx, argv[1]) : DRIVE_TANH);
      break;
    }
    case METHOD_TR909_SET_WAVEFORM: {
      d->setWaveform(parse_tr909_waveform(ctx, argv[0]));
      break;
    }
    case METHOD_TR909_SET_TONE: {
      double cutoff = 0;
      JS_ToFloat64(ctx, &cutoff, argv[0]);
      d->setTone(cutoff);
      break;
    }
    case METHOD_TR909_SET_TONE_RESONANCE: {
      double amount = 0;
      JS_ToFloat64(ctx, &amount, argv[0]);
      d->setToneResonance(amount);
      break;
    }
    case METHOD_TR909_SET_CLICK: {
      double level = 0, decay = 0.04;
      JS_ToFloat64(ctx, &level, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &decay, argv[1]);
      d->setClick(level, decay);
      break;
    }
    case METHOD_TR909_SET_SUB: {
      double mix = 0, octave = 1.0;
      JS_ToFloat64(ctx, &mix, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &octave, argv[1]);
      d->setSub(mix, octave);
      break;
    }
    case METHOD_TR909_SET_TUNE: {
      double t = 1.0;
      JS_ToFloat64(ctx, &t, argv[0]);
      d->setTune(t);
      break;
    }
    case METHOD_TR909_TRIGGER: {
      double v = 1.0;
      if(argc > 0)
        JS_ToFloat64(ctx, &v, argv[0]);
      d->trigger(v);
      break;
    }
    case METHOD_TR909_RENDER: {
      uint32_t n = 0;
      double v = 1.0;
      JS_ToUint32(ctx, &n, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &v, argv[1]);

      JSValue frames = js_new_stkframes(ctx, n, 1);
      if(JS_IsException(frames))
        return frames;
      StkFramesPtr* fp = static_cast<StkFramesPtr*>(JS_GetOpaque(frames, js_stkframes_class_id));
      stk::StkFrames& fr = **fp;
      d->trigger(v);
      for(uint32_t k = 0; k < n; k++)
        fr[k] = d->tick();
      ret = frames;
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_tr909bassdrum_funcs[] = {
    JS_CFUNC_MAGIC_DEF("setPitchEnvelope", 4, js_tr909bassdrum_method, METHOD_TR909_SET_PITCH_ENV),
    JS_CFUNC_MAGIC_DEF("setPitchSpike", 2, js_tr909bassdrum_method, METHOD_TR909_SET_PITCH_SPIKE),
    JS_CFUNC_MAGIC_DEF("setAmpEnvelope", 2, js_tr909bassdrum_method, METHOD_TR909_SET_AMP_ENV),
    JS_CFUNC_MAGIC_DEF("setPunch", 2, js_tr909bassdrum_method, METHOD_TR909_SET_PUNCH),
    JS_CFUNC_MAGIC_DEF("setDrive", 2, js_tr909bassdrum_method, METHOD_TR909_SET_DRIVE),
    JS_CFUNC_MAGIC_DEF("setWaveform", 1, js_tr909bassdrum_method, METHOD_TR909_SET_WAVEFORM),
    JS_CFUNC_MAGIC_DEF("setTone", 1, js_tr909bassdrum_method, METHOD_TR909_SET_TONE),
    JS_CFUNC_MAGIC_DEF("setToneResonance", 1, js_tr909bassdrum_method, METHOD_TR909_SET_TONE_RESONANCE),
    JS_CFUNC_MAGIC_DEF("setClick", 2, js_tr909bassdrum_method, METHOD_TR909_SET_CLICK),
    JS_CFUNC_MAGIC_DEF("setSub", 2, js_tr909bassdrum_method, METHOD_TR909_SET_SUB),
    JS_CFUNC_MAGIC_DEF("setTune", 1, js_tr909bassdrum_method, METHOD_TR909_SET_TUNE),
    JS_CFUNC_MAGIC_DEF("trigger", 1, js_tr909bassdrum_method, METHOD_TR909_TRIGGER),
    JS_CFUNC_MAGIC_DEF("render", 2, js_tr909bassdrum_method, METHOD_TR909_RENDER),
};

int
js_stk_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_stk_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stk_class_id, &js_stk_class);

  stk_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stk_constructor,
                                // "Stk", 1, JS_CFUNC_constructor, 0);
  stk_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stk_proto, js_stk_funcs, countof(js_stk_funcs));

  JS_SetClassProto(ctx, js_stk_class_id, stk_proto);

  JS_NewClassID(&js_stkframes_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkframes_class_id, &js_stkframes_class);

  stkframes_ctor = JS_NewCFunction2(ctx, js_stkframes_constructor, "StkFrames", 1, JS_CFUNC_constructor, 0);
  stkframes_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkframes_proto, js_stkframes_funcs, countof(js_stkframes_funcs));

  JS_SetClassProto(ctx, js_stkframes_class_id, stkframes_proto);

  JS_NewClassID(&js_stkfilter_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkfilter_class_id, &js_stkfilter_class);

  stkfilter_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkfilter_constructor,
                                      // "StkGenerator", 1, JS_CFUNC_constructor, 0);
  stkfilter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkfilter_proto, js_stkfilter_funcs, countof(js_stkfilter_funcs));

  JS_SetClassProto(ctx, js_stkfilter_class_id, stkfilter_proto);

  JSValue ctor;

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "BiQuad", 0, JS_CFUNC_constructor_magic, INSTANCE_BIQUAD);
    JS_SetModuleExport(ctx, m, "StkBiQuad", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "DelayA", 0, JS_CFUNC_constructor_magic, INSTANCE_DELAY_A);
    JS_SetModuleExport(ctx, m, "StkDelayA", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "DelayL", 0, JS_CFUNC_constructor_magic, INSTANCE_DELAY_L);
    JS_SetModuleExport(ctx, m, "StkDelayL", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "Delay", 0, JS_CFUNC_constructor_magic, INSTANCE_DELAY);
    JS_SetModuleExport(ctx, m, "StkDelay", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "Fir", 1, JS_CFUNC_constructor_magic, INSTANCE_FIR);
    JS_SetModuleExport(ctx, m, "StkFir", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "FormSwep", 0, JS_CFUNC_constructor_magic, INSTANCE_FORM_SWEP);
    JS_SetModuleExport(ctx, m, "StkFormSwep", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "Iir", 0, JS_CFUNC_constructor_magic, INSTANCE_IIR);
    JS_SetModuleExport(ctx, m, "StkIir", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "OnePole", 0, JS_CFUNC_constructor_magic, INSTANCE_ONE_POLE);
    JS_SetModuleExport(ctx, m, "StkOnePole", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "OneZero", 0, JS_CFUNC_constructor_magic, INSTANCE_ONE_ZERO);
    JS_SetModuleExport(ctx, m, "StkOneZero", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "PoleZero", 0, JS_CFUNC_constructor_magic, INSTANCE_POLE_ZERO);
    JS_SetModuleExport(ctx, m, "StkPoleZero", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "TapDelay", 0, JS_CFUNC_constructor_magic, INSTANCE_TAP_DELAY);
    JS_SetModuleExport(ctx, m, "StkTapDelay", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "TwoPole", 0, JS_CFUNC_constructor_magic, INSTANCE_TWO_POLE);
    JS_SetModuleExport(ctx, m, "StkTwoPole", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "TwoZero", 0, JS_CFUNC_constructor_magic, INSTANCE_TWO_ZERO);
    JS_SetModuleExport(ctx, m, "StkTwoZero", ctor);
  }
  JS_NewClassID(&js_stkgenerator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkgenerator_class_id, &js_stkgenerator_class);

  stkgenerator_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkgenerator_constructor,
                                         // "StkFilter", 1, JS_CFUNC_constructor, 0);
  stkgenerator_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkgenerator_proto, js_stkgenerator_funcs, countof(js_stkgenerator_funcs));

  JS_SetClassProto(ctx, js_stkgenerator_class_id, stkgenerator_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "ADSR", 0, JS_CFUNC_constructor_magic, INSTANCE_ADSR);
    JS_SetModuleExport(ctx, m, "StkADSR", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Asymp", 0, JS_CFUNC_constructor_magic, INSTANCE_ASYMP);
    JS_SetModuleExport(ctx, m, "StkAsymp", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "BlitSaw", 0, JS_CFUNC_constructor_magic, INSTANCE_BLIT_SAW);
    JS_SetModuleExport(ctx, m, "StkBlitSaw", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "BlitSquare", 0, JS_CFUNC_constructor_magic, INSTANCE_BLIT_SQUARE);
    JS_SetModuleExport(ctx, m, "StkBlitSquare", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Blit", 0, JS_CFUNC_constructor_magic, INSTANCE_BLIT);
    JS_SetModuleExport(ctx, m, "StkBlit", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Envelope", 0, JS_CFUNC_constructor_magic, INSTANCE_ENVELOPE);
    JS_SetModuleExport(ctx, m, "StkEnvelope", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Granulate", 0, JS_CFUNC_constructor_magic, INSTANCE_GRANULATE);
    JS_SetModuleExport(ctx, m, "StkGranulate", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Modulate", 0, JS_CFUNC_constructor_magic, INSTANCE_MODULATE);
    JS_SetModuleExport(ctx, m, "StkModulate", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Noise", 0, JS_CFUNC_constructor_magic, INSTANCE_NOISE);
    JS_SetModuleExport(ctx, m, "StkNoise", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "SineWave", 0, JS_CFUNC_constructor_magic, INSTANCE_SINE_WAVE);
    JS_SetModuleExport(ctx, m, "StkSineWave", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "SingWave", 0, JS_CFUNC_constructor_magic, INSTANCE_SING_WAVE);
    JS_SetModuleExport(ctx, m, "StkStkSingWave", ctor);
  }

  JS_NewClassID(&js_stkeffect_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkeffect_class_id, &js_stkeffect_class);

  stkeffect_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkeffect_constructor,
                                      // "StkGenerator", 1, JS_CFUNC_constructor, 0);
  stkeffect_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkeffect_proto, js_stkeffect_funcs, countof(js_stkeffect_funcs));

  JS_SetClassProto(ctx, js_stkeffect_class_id, stkeffect_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "Chorus", 0, JS_CFUNC_constructor_magic, INSTANCE_CHORUS);
    JS_SetModuleExport(ctx, m, "StkChorus", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "Echo", 0, JS_CFUNC_constructor_magic, INSTANCE_ECHO);
    JS_SetModuleExport(ctx, m, "StkEcho", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "FreeVerb", 0, JS_CFUNC_constructor_magic, INSTANCE_FREEVERB);
    JS_SetModuleExport(ctx, m, "StkFreeVerb", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "JCRev", 0, JS_CFUNC_constructor_magic, INSTANCE_JCREV);
    JS_SetModuleExport(ctx, m, "StkJCRev", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "LentPitShift", 0, JS_CFUNC_constructor_magic, INSTANCE_LENTPITSHIFT);
    JS_SetModuleExport(ctx, m, "StkLentPitShift", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "NRev", 0, JS_CFUNC_constructor_magic, INSTANCE_NREV);
    JS_SetModuleExport(ctx, m, "StkNRev", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "PitShift", 0, JS_CFUNC_constructor_magic, INSTANCE_PITSHIFT);
    JS_SetModuleExport(ctx, m, "StkPitShift", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "PRCRev", 0, JS_CFUNC_constructor_magic, INSTANCE_PRCREV);
    JS_SetModuleExport(ctx, m, "StkPRCRev", ctor);

    JS_SetModuleExport(ctx, m, "StkEffect", stkeffect_ctor);
  }

  stkinstrmnt_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkinstrmnt_constructor,
                                        // "StkGenerator", 1, JS_CFUNC_constructor, 0);
  stkinstrmnt_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkinstrmnt_proto, js_stkinstrmnt_funcs, countof(js_stkinstrmnt_funcs));

  JS_SetClassProto(ctx, js_stkinstrmnt_class_id, stkinstrmnt_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "BandedWG", 0, JS_CFUNC_constructor_magic, INSTANCE_BANDEDWG);
    JS_SetModuleExport(ctx, m, "StkBandedWG", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "BlowBotl", 0, JS_CFUNC_constructor_magic, INSTANCE_BLOWBOTL);
    JS_SetModuleExport(ctx, m, "StkBlowBotl", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "BlowHole", 0, JS_CFUNC_constructor_magic, INSTANCE_BLOWHOLE);
    JS_SetModuleExport(ctx, m, "StkBlowHole", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Bowed", 0, JS_CFUNC_constructor_magic, INSTANCE_BOWED);
    JS_SetModuleExport(ctx, m, "StkBowed", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Brass", 0, JS_CFUNC_constructor_magic, INSTANCE_BRASS);
    JS_SetModuleExport(ctx, m, "StkBrass", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Clarinet", 0, JS_CFUNC_constructor_magic, INSTANCE_CLARINET);
    JS_SetModuleExport(ctx, m, "StkClarinet", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Drummer", 0, JS_CFUNC_constructor_magic, INSTANCE_DRUMMER);
    JS_SetModuleExport(ctx, m, "StkDrummer", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Flute", 0, JS_CFUNC_constructor_magic, INSTANCE_FLUTE);
    JS_SetModuleExport(ctx, m, "StkFlute", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Mandolin", 0, JS_CFUNC_constructor_magic, INSTANCE_MANDOLIN);
    JS_SetModuleExport(ctx, m, "StkMandolin", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Mesh2D", 0, JS_CFUNC_constructor_magic, INSTANCE_MESH2D);
    JS_SetModuleExport(ctx, m, "StkMesh2D", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Plucked", 0, JS_CFUNC_constructor_magic, INSTANCE_PLUCKED);
    JS_SetModuleExport(ctx, m, "StkPlucked", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Recorder", 0, JS_CFUNC_constructor_magic, INSTANCE_RECORDER);
    JS_SetModuleExport(ctx, m, "StkRecorder", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Resonate", 0, JS_CFUNC_constructor_magic, INSTANCE_RESONATE);
    JS_SetModuleExport(ctx, m, "StkResonate", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Saxofony", 0, JS_CFUNC_constructor_magic, INSTANCE_SAXOFONY);
    JS_SetModuleExport(ctx, m, "StkSaxofony", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Shakers", 0, JS_CFUNC_constructor_magic, INSTANCE_SHAKERS);
    JS_SetModuleExport(ctx, m, "StkShakers", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Simple", 0, JS_CFUNC_constructor_magic, INSTANCE_SIMPLE);
    JS_SetModuleExport(ctx, m, "StkSimple", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Sitar", 0, JS_CFUNC_constructor_magic, INSTANCE_SITAR);
    JS_SetModuleExport(ctx, m, "StkSitar", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "StifKarp", 0, JS_CFUNC_constructor_magic, INSTANCE_STIFKARP);
    JS_SetModuleExport(ctx, m, "StkStifKarp", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "VoicForm", 0, JS_CFUNC_constructor_magic, INSTANCE_VOICFORM);
    JS_SetModuleExport(ctx, m, "StkVoicForm", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Whistle", 0, JS_CFUNC_constructor_magic, INSTANCE_WHISTLE);
    JS_SetModuleExport(ctx, m, "StkWhistle", ctor);

    JS_SetModuleExport(ctx, m, "Stk", stk_ctor);
    JS_SetModuleExport(ctx, m, "StkFrames", stkframes_ctor);
    JS_SetModuleExport(ctx, m, "StkGenerator", stkfilter_ctor);
    JS_SetModuleExport(ctx, m, "StkFilter", stkgenerator_ctor);
  }

  /* TwinTDrum and Tr909BassDrum are ordinary stk::Instrmnt subclasses, so
   * they share js_stkinstrmnt_class_id/finalizer above; only their own
   * prototype (chained onto stkinstrmnt_proto) differs, carrying their
   * extra controls. */
  twintdrum_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, twintdrum_proto, stkinstrmnt_proto);
  JS_SetPropertyFunctionList(ctx, twintdrum_proto, js_twintdrum_funcs, countof(js_twintdrum_funcs));

  tr909bassdrum_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, tr909bassdrum_proto, stkinstrmnt_proto);
  JS_SetPropertyFunctionList(ctx, tr909bassdrum_proto, js_tr909bassdrum_funcs, countof(js_tr909bassdrum_funcs));

  if(m) {
    ctor = JS_NewCFunction2(ctx, js_twintdrum_constructor, "TwinTDrum", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, twintdrum_proto);
    JS_SetModuleExport(ctx, m, "StkTwinTDrum", ctor);

    ctor = JS_NewCFunction2(ctx, js_tr909bassdrum_constructor, "Tr909BassDrum", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, tr909bassdrum_proto);
    JS_SetModuleExport(ctx, m, "StkTr909BassDrum", ctor);
  }

  JS_NewClassID(&js_stkfunction_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkfunction_class_id, &js_stkfunction_class);

  stkfunction_ctor = JS_NewObject(ctx);
  stkfunction_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkfunction_proto, js_stkfunction_funcs, countof(js_stkfunction_funcs));

  JS_SetClassProto(ctx, js_stkfunction_class_id, stkfunction_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfunction_constructor, "Cubic", 0, JS_CFUNC_constructor_magic, INSTANCE_CUBIC);
    JS_SetModuleExport(ctx, m, "StkCubic", ctor);

    JS_SetModuleExport(ctx, m, "StkFunction", stkfunction_ctor);
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_stk(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "StkBiQuad");
  JS_AddModuleExport(ctx, m, "StkDelayA");
  JS_AddModuleExport(ctx, m, "StkDelayL");
  JS_AddModuleExport(ctx, m, "StkDelay");
  JS_AddModuleExport(ctx, m, "StkFir");
  JS_AddModuleExport(ctx, m, "StkFormSwep");
  JS_AddModuleExport(ctx, m, "StkIir");
  JS_AddModuleExport(ctx, m, "StkOnePole");
  JS_AddModuleExport(ctx, m, "StkOneZero");
  JS_AddModuleExport(ctx, m, "StkPoleZero");
  JS_AddModuleExport(ctx, m, "StkTapDelay");
  JS_AddModuleExport(ctx, m, "StkTwoPole");
  JS_AddModuleExport(ctx, m, "StkTwoZero");
  JS_AddModuleExport(ctx, m, "StkADSR");
  JS_AddModuleExport(ctx, m, "StkAsymp");
  JS_AddModuleExport(ctx, m, "StkBlitSaw");
  JS_AddModuleExport(ctx, m, "StkBlitSquare");
  JS_AddModuleExport(ctx, m, "StkBlit");
  JS_AddModuleExport(ctx, m, "StkEnvelope");
  JS_AddModuleExport(ctx, m, "StkGranulate");
  JS_AddModuleExport(ctx, m, "StkModulate");
  JS_AddModuleExport(ctx, m, "StkNoise");
  JS_AddModuleExport(ctx, m, "StkSineWave");
  JS_AddModuleExport(ctx, m, "StkStkSingWave");
  JS_AddModuleExport(ctx, m, "StkBandedWG");
  JS_AddModuleExport(ctx, m, "StkBlowBotl");
  JS_AddModuleExport(ctx, m, "StkBlowHole");
  JS_AddModuleExport(ctx, m, "StkBowed");
  JS_AddModuleExport(ctx, m, "StkBrass");
  JS_AddModuleExport(ctx, m, "StkClarinet");
  JS_AddModuleExport(ctx, m, "StkDrummer");
  JS_AddModuleExport(ctx, m, "StkFlute");
  JS_AddModuleExport(ctx, m, "StkMandolin");
  JS_AddModuleExport(ctx, m, "StkMesh2D");
  JS_AddModuleExport(ctx, m, "StkPlucked");
  JS_AddModuleExport(ctx, m, "StkRecorder");
  JS_AddModuleExport(ctx, m, "StkResonate");
  JS_AddModuleExport(ctx, m, "StkSaxofony");
  JS_AddModuleExport(ctx, m, "StkShakers");
  JS_AddModuleExport(ctx, m, "StkSimple");
  JS_AddModuleExport(ctx, m, "StkSitar");
  JS_AddModuleExport(ctx, m, "StkStifKarp");
  JS_AddModuleExport(ctx, m, "StkVoicForm");
  JS_AddModuleExport(ctx, m, "StkWhistle");
  JS_AddModuleExport(ctx, m, "StkChorus");
  JS_AddModuleExport(ctx, m, "StkEcho");
  JS_AddModuleExport(ctx, m, "StkFreeVerb");
  JS_AddModuleExport(ctx, m, "StkJCRev");
  JS_AddModuleExport(ctx, m, "StkLentPitShift");
  JS_AddModuleExport(ctx, m, "StkNRev");
  JS_AddModuleExport(ctx, m, "StkPitShift");
  JS_AddModuleExport(ctx, m, "StkPRCRev");
  JS_AddModuleExport(ctx, m, "StkEffect");
  JS_AddModuleExport(ctx, m, "StkTwinTDrum");
  JS_AddModuleExport(ctx, m, "StkTr909BassDrum");
  JS_AddModuleExport(ctx, m, "StkCubic");
  JS_AddModuleExport(ctx, m, "StkFunction");
  JS_AddModuleExport(ctx, m, "Stk");
  JS_AddModuleExport(ctx, m, "StkFrames");
  JS_AddModuleExport(ctx, m, "StkGenerator");
  JS_AddModuleExport(ctx, m, "StkFilter");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_stk_init))) {
#ifdef USE_STK
    js_init_module_stk(ctx, m);
#endif
  }

  return m;
}
