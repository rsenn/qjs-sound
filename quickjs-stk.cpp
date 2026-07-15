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

/* stk::IO (realtime audio/MIDI, streaming, and MIDI file support) */
#include "WvIn.h"
#include "WvOut.h"
#include "RtAudio.h"
#include "RtWvIn.h"
#include "RtWvOut.h"
#include "InetWvIn.h"
#include "InetWvOut.h"
#include "MidiFileIn.h"
#include "RtMidi.h"

#include <memory>

using stk::Stk;

static JSClassID js_stkframes_class_id, js_stk_class_id, js_stkfilter_class_id, js_stkgenerator_class_id, js_stkeffect_class_id, js_stkfm_class_id,
    js_stkinstrmnt_class_id, js_stkfunction_class_id, js_stkwvin_class_id, js_stkwvout_class_id, js_midifilein_class_id, js_rtmidiin_class_id,
    js_rtmidiout_class_id, js_rtaudio_class_id;
static JSValue stkframes_proto, stkframes_ctor, stk_proto, stk_ctor, stkfilter_proto, stkfilter_ctor, stkgenerator_proto, stkgenerator_ctor, stkeffect_proto,
    stkeffect_ctor, stkfm_proto, stkfm_ctor, stkinstrmnt_proto, stkinstrmnt_ctor, stkfunction_proto, stkfunction_ctor,
    twintdrum_proto, tr909bassdrum_proto, tr909percussion_proto,
    stkwvin_proto, rtwvin_proto, inetwvin_proto, stkwvout_proto, rtwvout_proto, inetwvout_proto,
    midifilein_proto, rtmidiin_proto, rtmidiout_proto, rtaudio_proto;

typedef std::shared_ptr<stk::Stk> StkPtr;
typedef std::shared_ptr<stk::StkFrames> StkFramesPtr;
typedef std::shared_ptr<stk::Generator> StkGeneratorPtr;
typedef std::shared_ptr<stk::Filter> StkFilterPtr;
typedef std::shared_ptr<stk::Effect> StkEffectPtr;
typedef std::shared_ptr<stk::FM> StkFMPtr;
typedef std::shared_ptr<stk::Instrmnt> StkInstrmntPtr;
typedef std::shared_ptr<stk::Function> StkFunctionPtr;
typedef std::shared_ptr<stk::WvIn> StkWvInPtr;
typedef std::shared_ptr<stk::WvOut> StkWvOutPtr;

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

static void
array_to_bytes(JSContext* ctx, JSValueConst arr, std::vector<unsigned char>& vec) {
  int64_t len = array_length(ctx, arr);

  for(int64_t i = 0; i < len; i++) {
    JSValue v = JS_GetPropertyUint32(ctx, arr, i);
    uint32_t u;
    JS_ToUint32(ctx, &u, v);
    JS_FreeValue(ctx, v);

    vec.push_back(static_cast<unsigned char>(u));
  }
}

/* stk::StkError and RtMidiError both derive from std::exception; the
 * realtime/streaming I/O constructors below are far more likely to fail in
 * ordinary use (missing device, bad host, no MIDI ports) than the DSP
 * classes bound elsewhere in this file, so their exceptions are translated
 * into JS exceptions instead of being left to propagate as C++ exceptions. */
static JSValue
js_stk_throw(JSContext* ctx, const std::exception& e) {
  return JS_ThrowTypeError(ctx, "%s", e.what());
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
                         "ADSR",
                         "Asymp",
                         "Blit",
                         "BlitSaw",
                         "BlitSquare",
                         "Envelope",
                         "Granulate",
                         "Modulate",
                         "Noise",
                         "SineWave",
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
    .class_name = "Generator",
    .finalizer = js_stkgenerator_finalizer,
};

static const JSCFunctionListEntry js_stkgenerator_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkgenerator_method, METHOD_TICK),
    JS_CGETSET_MAGIC_DEF("channelsOut", js_stkgenerator_get, js_stkgenerator_set, PROP_SAMPLERATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Generator", JS_PROP_CONFIGURABLE),
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
                         "BiQuad",
                         "Delay",
                         "DelayA",
                         "DelayL",
                         "Fir",
                         "FormSwep",
                         "Iir",
                         "OnePole",
                         "OneZero",
                         "PoleZero",
                         "TapDelay",
                         "TwoPole",
                         "TwoZero",
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
    .class_name = "Filter",
    .finalizer = js_stkfilter_finalizer,
};

static const JSCFunctionListEntry js_stkfilter_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkfilter_method, METHOD_FILTER_TICK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Filter", JS_PROP_CONFIGURABLE),
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
                         "Chorus",
                         "Echo",
                         "FreeVerb",
                         "JCRev",
                         "LentPitShift",
                         "NRev",
                         "PRCRev",
                         "PitShift",
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
    .class_name = "Effect",
    .finalizer = js_stkeffect_finalizer,
};

static const JSCFunctionListEntry js_stkeffect_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkeffect_method, METHOD_EFFECT_TICK),
    JS_CFUNC_MAGIC_DEF("setEffectMix", 1, js_stkeffect_method, METHOD_EFFECT_SET_MIX),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_stkeffect_method, METHOD_EFFECT_CLEAR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Effect", JS_PROP_CONFIGURABLE),
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
                         "Cubic",
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
    .class_name = "Function",
    .finalizer = js_stkfunction_finalizer,
};

static const JSCFunctionListEntry js_stkfunction_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkfunction_method, METHOD_FUNCTION_TICK),
    JS_CFUNC_MAGIC_DEF("setA1", 1, js_stkfunction_method, METHOD_FUNCTION_SET_A1),
    JS_CFUNC_MAGIC_DEF("setA2", 1, js_stkfunction_method, METHOD_FUNCTION_SET_A2),
    JS_CFUNC_MAGIC_DEF("setA3", 1, js_stkfunction_method, METHOD_FUNCTION_SET_A3),
    JS_CFUNC_MAGIC_DEF("setGain", 1, js_stkfunction_method, METHOD_FUNCTION_SET_GAIN),
    JS_CFUNC_MAGIC_DEF("setThreshold", 1, js_stkfunction_method, METHOD_FUNCTION_SET_THRESHOLD),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Function", JS_PROP_CONFIGURABLE),
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
  js_set_tostringtag(ctx, obj, "TwinTDrum");
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
    return JS_ThrowTypeError(ctx, "not a TwinTDrum");

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
  js_set_tostringtag(ctx, obj, "Tr909BassDrum");
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

static int
parse_perc_filter_type(JSContext* ctx, JSValueConst v) {
  if(JS_IsUndefined(v))
    return PERC_FILTER_BANDPASS;

  const char* s = JS_ToCString(ctx, v);
  int type = PERC_FILTER_BANDPASS;

  if(s) {
    if(!strcmp(s, "highpass"))
      type = PERC_FILTER_HIGHPASS;
    else if(!strcmp(s, "lowpass"))
      type = PERC_FILTER_LOWPASS;
    JS_FreeCString(ctx, s);
  }

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
    return JS_ThrowTypeError(ctx, "not a Tr909BassDrum");

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

static JSValue
js_tr909percussion_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  StkInstrmntPtr* i = static_cast<StkInstrmntPtr*>(js_mallocz(ctx, sizeof(StkInstrmntPtr)));
  new(i) StkInstrmntPtr(std::make_shared<Tr909Percussion>());

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, tr909percussion_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_stkinstrmnt_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, i);
  js_set_tostringtag(ctx, obj, "Tr909Percussion");
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_PERC_SET_TONE = 0,
  METHOD_PERC_SET_METALLIC,
  METHOD_PERC_SET_NOISE,
  METHOD_PERC_SET_NOISE_FILTER,
  METHOD_PERC_SET_CRUNCH,
  METHOD_PERC_SET_CLAP,
  METHOD_PERC_SET_TUNE,
  METHOD_PERC_TRIGGER,
  METHOD_PERC_RENDER,
};

static JSValue
js_tr909percussion_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkInstrmntPtr* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = static_cast<StkInstrmntPtr*>(JS_GetOpaque2(ctx, this_val, js_stkinstrmnt_class_id))))
    return JS_EXCEPTION;

  Tr909Percussion* d = dynamic_cast<Tr909Percussion*>(p->get());
  if(!d)
    return JS_ThrowTypeError(ctx, "not a Tr909Percussion");

  switch(magic) {
    case METHOD_PERC_SET_TONE: {
      double freq1 = 0, freq2 = 0, mix = 0, decay = 0;
      JS_ToFloat64(ctx, &freq1, argv[0]);
      JS_ToFloat64(ctx, &freq2, argv[1]);
      JS_ToFloat64(ctx, &mix, argv[2]);
      JS_ToFloat64(ctx, &decay, argv[3]);
      d->setTone(freq1, freq2, mix, decay);
      break;
    }
    case METHOD_PERC_SET_METALLIC: {
      double base = 0, mix = 0, decay = 0, brightness = 7000;
      JS_ToFloat64(ctx, &base, argv[0]);
      JS_ToFloat64(ctx, &mix, argv[1]);
      JS_ToFloat64(ctx, &decay, argv[2]);
      if(argc > 3)
        JS_ToFloat64(ctx, &brightness, argv[3]);
      d->setMetallic(base, mix, decay, brightness);
      break;
    }
    case METHOD_PERC_SET_NOISE: {
      double mix = 0, decay = 0;
      JS_ToFloat64(ctx, &mix, argv[0]);
      JS_ToFloat64(ctx, &decay, argv[1]);
      d->setNoise(mix, decay);
      break;
    }
    case METHOD_PERC_SET_NOISE_FILTER: {
      double cutoff = 0, q = 1.0;
      JS_ToFloat64(ctx, &cutoff, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &q, argv[1]);
      d->setNoiseFilter(cutoff, q, argc > 2 ? parse_perc_filter_type(ctx, argv[2]) : PERC_FILTER_BANDPASS);
      break;
    }
    case METHOD_PERC_SET_CRUNCH: {
      double amount = 0;
      JS_ToFloat64(ctx, &amount, argv[0]);
      d->setCrunch(amount, argc > 1 ? parse_drive_type(ctx, argv[1]) : DRIVE_TANH);
      break;
    }
    case METHOD_PERC_SET_CLAP: {
      uint32_t hits = 1;
      double spacing = 0.01;
      JS_ToUint32(ctx, &hits, argv[0]);
      if(argc > 1)
        JS_ToFloat64(ctx, &spacing, argv[1]);
      d->setClap((int)hits, spacing);
      break;
    }
    case METHOD_PERC_SET_TUNE: {
      double t = 1.0;
      JS_ToFloat64(ctx, &t, argv[0]);
      d->setTune(t);
      break;
    }
    case METHOD_PERC_TRIGGER: {
      double v = 1.0;
      if(argc > 0)
        JS_ToFloat64(ctx, &v, argv[0]);
      d->trigger(v);
      break;
    }
    case METHOD_PERC_RENDER: {
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

static const JSCFunctionListEntry js_tr909percussion_funcs[] = {
    JS_CFUNC_MAGIC_DEF("setTone", 4, js_tr909percussion_method, METHOD_PERC_SET_TONE),
    JS_CFUNC_MAGIC_DEF("setMetallic", 3, js_tr909percussion_method, METHOD_PERC_SET_METALLIC),
    JS_CFUNC_MAGIC_DEF("setNoise", 2, js_tr909percussion_method, METHOD_PERC_SET_NOISE),
    JS_CFUNC_MAGIC_DEF("setNoiseFilter", 1, js_tr909percussion_method, METHOD_PERC_SET_NOISE_FILTER),
    JS_CFUNC_MAGIC_DEF("setCrunch", 1, js_tr909percussion_method, METHOD_PERC_SET_CRUNCH),
    JS_CFUNC_MAGIC_DEF("setClap", 1, js_tr909percussion_method, METHOD_PERC_SET_CLAP),
    JS_CFUNC_MAGIC_DEF("setTune", 1, js_tr909percussion_method, METHOD_PERC_SET_TUNE),
    JS_CFUNC_MAGIC_DEF("trigger", 1, js_tr909percussion_method, METHOD_PERC_TRIGGER),
    JS_CFUNC_MAGIC_DEF("render", 2, js_tr909percussion_method, METHOD_PERC_RENDER),
};

/* ============================================================ */
/* stk::WvIn -- RtWvIn, InetWvIn                           */
/* ============================================================ */

enum {
  INSTANCE_WVIN_RTWVIN = 0,
  INSTANCE_WVIN_INETWVIN,
};

static JSValue
js_stkwvin_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr* w = static_cast<StkWvInPtr*>(js_mallocz(ctx, sizeof(StkWvInPtr)));

  try {
    switch(magic) {
      case INSTANCE_WVIN_RTWVIN: {
        uint32_t nChannels = 1;
        double sampleRate = stk::Stk::sampleRate();
        int32_t deviceIndex = 0, bufferFrames = stk::RT_BUFFER_SIZE, nBuffers = 20;

        if(argc > 0)
          JS_ToUint32(ctx, &nChannels, argv[0]);
        if(argc > 1)
          JS_ToFloat64(ctx, &sampleRate, argv[1]);
        if(argc > 2)
          JS_ToInt32(ctx, &deviceIndex, argv[2]);
        if(argc > 3)
          JS_ToInt32(ctx, &bufferFrames, argv[3]);
        if(argc > 4)
          JS_ToInt32(ctx, &nBuffers, argv[4]);

        new(w) StkWvInPtr(std::make_shared<stk::RtWvIn>(nChannels, sampleRate, deviceIndex, bufferFrames, nBuffers));
        break;
      }
      case INSTANCE_WVIN_INETWVIN: {
        int32_t bufferFrames = 1024, nBuffers = 8;

        if(argc > 0)
          JS_ToInt32(ctx, &bufferFrames, argv[0]);
        if(argc > 1)
          JS_ToInt32(ctx, &nBuffers, argv[1]);

        new(w) StkWvInPtr(std::make_shared<stk::InetWvIn>(bufferFrames, nBuffers));
        break;
      }
    }
  } catch(const std::exception& e) {
    js_free(ctx, w);
    return js_stk_throw(ctx, e);
  }

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, magic == INSTANCE_WVIN_RTWVIN ? rtwvin_proto : inetwvin_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_stkwvin_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  js_set_tostringtag(ctx, obj, magic == INSTANCE_WVIN_RTWVIN ? "RtWvIn" : "InetWvIn");
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_WVIN_TICK = 0,
};

static JSValue
js_stkwvin_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = static_cast<StkWvInPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvin_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_WVIN_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        if(argc > 1)
          JS_ToUint32(ctx, &channel, argv[1]);

        (*w)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      if(argc > 0)
        JS_ToUint32(ctx, &channel, argv[0]);

      ret = JS_NewFloat64(ctx, (*w)->tick(channel));
      break;
    }
  }

  return ret;
}

enum {
  PROP_WVIN_CHANNELS_OUT = 0,
  PROP_WVIN_LAST_FRAME,
};

static JSValue
js_stkwvin_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StkWvInPtr* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = static_cast<StkWvInPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvin_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_WVIN_CHANNELS_OUT: {
      ret = JS_NewUint32(ctx, (*w)->channelsOut());
      break;
    }
    case PROP_WVIN_LAST_FRAME: {
      const stk::StkFrames& lf = (*w)->lastFrame();
      unsigned int nch = lf.channels();
      JSValue arr = JS_NewArray(ctx);
      for(unsigned int c = 0; c < nch; c++)
        JS_SetPropertyUint32(ctx, arr, c, JS_NewFloat64(ctx, lf[c]));
      ret = arr;
      break;
    }
  }

  return ret;
}

static void
js_stkwvin_finalizer(JSRuntime* rt, JSValue val) {
  StkWvInPtr* w;

  if((w = static_cast<StkWvInPtr*>(JS_GetOpaque(val, js_stkwvin_class_id)))) {
    w->~StkWvInPtr();
    js_free_rt(rt, w);
  }
}

static JSClassDef js_stkwvin_class = {
    .class_name = "StkWvIn",
    .finalizer = js_stkwvin_finalizer,
};

static const JSCFunctionListEntry js_stkwvin_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkwvin_method, METHOD_WVIN_TICK),
    JS_CGETSET_MAGIC_DEF("channelsOut", js_stkwvin_get, 0, PROP_WVIN_CHANNELS_OUT),
    JS_CGETSET_MAGIC_DEF("lastFrame", js_stkwvin_get, 0, PROP_WVIN_LAST_FRAME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkWvIn", JS_PROP_CONFIGURABLE),
};

/* RtWvIn-only controls (RtWvIn::start/stop are not declared on the
 * WvIn base, so dispatch via dynamic_cast same as the TwinTDrum/Tr909
 * instrument subclasses above). */
enum {
  METHOD_RTWVIN_START = 0,
  METHOD_RTWVIN_STOP,
};

static JSValue
js_rtwvin_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr* w;

  if(!(w = static_cast<StkWvInPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvin_class_id))))
    return JS_EXCEPTION;

  stk::RtWvIn* r = dynamic_cast<stk::RtWvIn*>(w->get());
  if(!r)
    return JS_ThrowTypeError(ctx, "not a RtWvIn");

  switch(magic) {
    case METHOD_RTWVIN_START: r->start(); break;
    case METHOD_RTWVIN_STOP: r->stop(); break;
  }

  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_rtwvin_funcs[] = {
    JS_CFUNC_MAGIC_DEF("start", 0, js_rtwvin_method, METHOD_RTWVIN_START),
    JS_CFUNC_MAGIC_DEF("stop", 0, js_rtwvin_method, METHOD_RTWVIN_STOP),
};

/* InetWvIn-only controls. */
enum {
  METHOD_INETWVIN_LISTEN = 0,
  METHOD_INETWVIN_IS_CONNECTED,
};

static JSValue
js_inetwvin_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = static_cast<StkWvInPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvin_class_id))))
    return JS_EXCEPTION;

  stk::InetWvIn* n = dynamic_cast<stk::InetWvIn*>(w->get());
  if(!n)
    return JS_ThrowTypeError(ctx, "not a InetWvIn");

  switch(magic) {
    case METHOD_INETWVIN_LISTEN: {
      int32_t port = 2006, protocol = stk::Socket::PROTO_TCP;
      uint32_t nChannels = 1, format = stk::Stk::STK_SINT16;

      if(argc > 0)
        JS_ToInt32(ctx, &port, argv[0]);
      if(argc > 1)
        JS_ToUint32(ctx, &nChannels, argv[1]);
      if(argc > 2)
        JS_ToUint32(ctx, &format, argv[2]);
      if(argc > 3)
        JS_ToInt32(ctx, &protocol, argv[3]);

      try {
        n->listen(port, nChannels, format, (stk::Socket::ProtocolType)protocol);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_INETWVIN_IS_CONNECTED: {
      ret = JS_NewBool(ctx, n->isConnected());
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_inetwvin_funcs[] = {
    JS_CFUNC_MAGIC_DEF("listen", 0, js_inetwvin_method, METHOD_INETWVIN_LISTEN),
    JS_CFUNC_MAGIC_DEF("isConnected", 0, js_inetwvin_method, METHOD_INETWVIN_IS_CONNECTED),
};

/* ============================================================ */
/* stk::WvOut -- RtWvOut, InetWvOut                        */
/* ============================================================ */

enum {
  INSTANCE_WVOUT_RTWVOUT = 0,
  INSTANCE_WVOUT_INETWVOUT,
};

static JSValue
js_stkwvout_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkWvOutPtr* w = static_cast<StkWvOutPtr*>(js_mallocz(ctx, sizeof(StkWvOutPtr)));

  try {
    switch(magic) {
      case INSTANCE_WVOUT_RTWVOUT: {
        uint32_t nChannels = 1;
        double sampleRate = stk::Stk::sampleRate();
        int32_t deviceIndex = 0, bufferFrames = stk::RT_BUFFER_SIZE, nBuffers = 20;

        if(argc > 0)
          JS_ToUint32(ctx, &nChannels, argv[0]);
        if(argc > 1)
          JS_ToFloat64(ctx, &sampleRate, argv[1]);
        if(argc > 2)
          JS_ToInt32(ctx, &deviceIndex, argv[2]);
        if(argc > 3)
          JS_ToInt32(ctx, &bufferFrames, argv[3]);
        if(argc > 4)
          JS_ToInt32(ctx, &nBuffers, argv[4]);

        new(w) StkWvOutPtr(std::make_shared<stk::RtWvOut>(nChannels, sampleRate, deviceIndex, bufferFrames, nBuffers));
        break;
      }
      case INSTANCE_WVOUT_INETWVOUT: {
        int32_t packetFrames = 1024;

        if(argc > 0)
          JS_ToInt32(ctx, &packetFrames, argv[0]);

        /* InetWvOut also has an (int port, ...) overload that opens a
         * connection immediately; without this cast to unsigned long an
         * int argument here binds to that overload instead, silently
         * attempting to connect to port `packetFrames`. */
        new(w) StkWvOutPtr(std::make_shared<stk::InetWvOut>(static_cast<unsigned long>(packetFrames)));
        break;
      }
    }
  } catch(const std::exception& e) {
    js_free(ctx, w);
    return js_stk_throw(ctx, e);
  }

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, magic == INSTANCE_WVOUT_RTWVOUT ? rtwvout_proto : inetwvout_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_stkwvout_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, w);
  js_set_tostringtag(ctx, obj, magic == INSTANCE_WVOUT_RTWVOUT ? "RtWvOut" : "InetWvOut");
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_WVOUT_TICK = 0,
  METHOD_WVOUT_CLIP_STATUS,
  METHOD_WVOUT_RESET_CLIP_STATUS,
};

static JSValue
js_stkwvout_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvOutPtr* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = static_cast<StkWvOutPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvout_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_WVOUT_TICK: {
      StkFramesPtr* a;

      if(argc > 0 && (a = static_cast<StkFramesPtr*>(JS_GetOpaque(argv[0], js_stkframes_class_id)))) {
        (*w)->tick(*a->get());
        break;
      }

      double sample = 0;
      JS_ToFloat64(ctx, &sample, argv[0]);
      (*w)->tick(sample);
      break;
    }
    case METHOD_WVOUT_CLIP_STATUS: {
      ret = JS_NewBool(ctx, (*w)->clipStatus());
      break;
    }
    case METHOD_WVOUT_RESET_CLIP_STATUS: {
      (*w)->resetClipStatus();
      break;
    }
  }

  return ret;
}

enum {
  PROP_WVOUT_FRAME_COUNT = 0,
  PROP_WVOUT_TIME,
};

static JSValue
js_stkwvout_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StkWvOutPtr* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = static_cast<StkWvOutPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvout_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_WVOUT_FRAME_COUNT: {
      ret = JS_NewUint32(ctx, (*w)->getFrameCount());
      break;
    }
    case PROP_WVOUT_TIME: {
      ret = JS_NewFloat64(ctx, (*w)->getTime());
      break;
    }
  }

  return ret;
}

static void
js_stkwvout_finalizer(JSRuntime* rt, JSValue val) {
  StkWvOutPtr* w;

  if((w = static_cast<StkWvOutPtr*>(JS_GetOpaque(val, js_stkwvout_class_id)))) {
    w->~StkWvOutPtr();
    js_free_rt(rt, w);
  }
}

static JSClassDef js_stkwvout_class = {
    .class_name = "StkWvOut",
    .finalizer = js_stkwvout_finalizer,
};

static const JSCFunctionListEntry js_stkwvout_funcs[] = {
    JS_CFUNC_MAGIC_DEF("tick", 1, js_stkwvout_method, METHOD_WVOUT_TICK),
    JS_CFUNC_MAGIC_DEF("clipStatus", 0, js_stkwvout_method, METHOD_WVOUT_CLIP_STATUS),
    JS_CFUNC_MAGIC_DEF("resetClipStatus", 0, js_stkwvout_method, METHOD_WVOUT_RESET_CLIP_STATUS),
    JS_CGETSET_MAGIC_DEF("frameCount", js_stkwvout_get, 0, PROP_WVOUT_FRAME_COUNT),
    JS_CGETSET_MAGIC_DEF("time", js_stkwvout_get, 0, PROP_WVOUT_TIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkWvOut", JS_PROP_CONFIGURABLE),
};

/* RtWvOut-only controls. */
enum {
  METHOD_RTWVOUT_START = 0,
  METHOD_RTWVOUT_STOP,
};

static JSValue
js_rtwvout_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvOutPtr* w;

  if(!(w = static_cast<StkWvOutPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvout_class_id))))
    return JS_EXCEPTION;

  stk::RtWvOut* r = dynamic_cast<stk::RtWvOut*>(w->get());
  if(!r)
    return JS_ThrowTypeError(ctx, "not a RtWvOut");

  switch(magic) {
    case METHOD_RTWVOUT_START: r->start(); break;
    case METHOD_RTWVOUT_STOP: r->stop(); break;
  }

  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_rtwvout_funcs[] = {
    JS_CFUNC_MAGIC_DEF("start", 0, js_rtwvout_method, METHOD_RTWVOUT_START),
    JS_CFUNC_MAGIC_DEF("stop", 0, js_rtwvout_method, METHOD_RTWVOUT_STOP),
};

/* InetWvOut-only controls. */
enum {
  METHOD_INETWVOUT_CONNECT = 0,
  METHOD_INETWVOUT_DISCONNECT,
};

static JSValue
js_inetwvout_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvOutPtr* w;

  if(!(w = static_cast<StkWvOutPtr*>(JS_GetOpaque2(ctx, this_val, js_stkwvout_class_id))))
    return JS_EXCEPTION;

  stk::InetWvOut* n = dynamic_cast<stk::InetWvOut*>(w->get());
  if(!n)
    return JS_ThrowTypeError(ctx, "not a InetWvOut");

  switch(magic) {
    case METHOD_INETWVOUT_CONNECT: {
      int32_t port = 0, protocol = stk::Socket::PROTO_TCP;
      uint32_t nChannels = 1, format = stk::Stk::STK_SINT16;
      const char* hostname = "localhost";
      BOOL freeHostname = FALSE;

      JS_ToInt32(ctx, &port, argv[0]);
      if(argc > 1)
        JS_ToInt32(ctx, &protocol, argv[1]);
      if(argc > 2) {
        hostname = JS_ToCString(ctx, argv[2]);
        freeHostname = TRUE;
      }
      if(argc > 3)
        JS_ToUint32(ctx, &nChannels, argv[3]);
      if(argc > 4)
        JS_ToUint32(ctx, &format, argv[4]);

      JSValue exc = JS_UNDEFINED;
      try {
        n->connect(port, (stk::Socket::ProtocolType)protocol, hostname, nChannels, format);
      } catch(const std::exception& e) {
        exc = js_stk_throw(ctx, e);
      }

      if(freeHostname)
        JS_FreeCString(ctx, hostname);

      if(JS_IsException(exc))
        return exc;
      break;
    }
    case METHOD_INETWVOUT_DISCONNECT: {
      n->disconnect();
      break;
    }
  }

  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_inetwvout_funcs[] = {
    JS_CFUNC_MAGIC_DEF("connect", 1, js_inetwvout_method, METHOD_INETWVOUT_CONNECT),
    JS_CFUNC_MAGIC_DEF("disconnect", 0, js_inetwvout_method, METHOD_INETWVOUT_DISCONNECT),
};

/* ============================================================ */
/* stk::MidiFileIn -- MidiFileIn                              */
/* ============================================================ */

static JSValue
js_midifilein_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  const char* fileName = JS_ToCString(ctx, argv[0]);
  if(!fileName)
    return JS_EXCEPTION;

  stk::MidiFileIn* mf = nullptr;
  try {
    mf = new stk::MidiFileIn(fileName);
  } catch(const std::exception& e) {
    JS_FreeCString(ctx, fileName);
    return js_stk_throw(ctx, e);
  }
  JS_FreeCString(ctx, fileName);

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, midifilein_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_midifilein_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, mf);
  js_set_tostringtag(ctx, obj, "MidiFileIn");
  return obj;

fail:
  delete mf;
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_MIDIFILE_REWIND_TRACK = 0,
  METHOD_MIDIFILE_GET_TICK_SECONDS,
  METHOD_MIDIFILE_GET_NEXT_EVENT,
  METHOD_MIDIFILE_GET_NEXT_MIDI_EVENT,
};

static JSValue
js_midifilein_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  stk::MidiFileIn* mf;
  JSValue ret = JS_UNDEFINED;

  if(!(mf = static_cast<stk::MidiFileIn*>(JS_GetOpaque2(ctx, this_val, js_midifilein_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_MIDIFILE_REWIND_TRACK: {
      uint32_t track = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &track, argv[0]);
      try {
        mf->rewindTrack(track);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_MIDIFILE_GET_TICK_SECONDS: {
      uint32_t track = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &track, argv[0]);
      try {
        ret = JS_NewFloat64(ctx, mf->getTickSeconds(track));
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_MIDIFILE_GET_NEXT_EVENT:
    case METHOD_MIDIFILE_GET_NEXT_MIDI_EVENT: {
      uint32_t track = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &track, argv[0]);

      std::vector<unsigned char> event;
      unsigned long deltaTime;
      try {
        deltaTime = magic == METHOD_MIDIFILE_GET_NEXT_EVENT ? mf->getNextEvent(&event, track) : mf->getNextMidiEvent(&event, track);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }

      JSValue data = JS_NewArray(ctx);
      for(size_t i = 0; i < event.size(); i++)
        JS_SetPropertyUint32(ctx, data, i, JS_NewUint32(ctx, event[i]));

      JSValue obj = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, obj, "deltaTime", JS_NewInt64(ctx, deltaTime));
      JS_SetPropertyStr(ctx, obj, "data", data);
      ret = obj;
      break;
    }
  }

  return ret;
}

enum {
  PROP_MIDIFILE_FORMAT = 0,
  PROP_MIDIFILE_NUM_TRACKS,
  PROP_MIDIFILE_DIVISION,
};

static JSValue
js_midifilein_get(JSContext* ctx, JSValueConst this_val, int magic) {
  stk::MidiFileIn* mf;
  JSValue ret = JS_UNDEFINED;

  if(!(mf = static_cast<stk::MidiFileIn*>(JS_GetOpaque2(ctx, this_val, js_midifilein_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_MIDIFILE_FORMAT: ret = JS_NewInt32(ctx, mf->getFileFormat()); break;
    case PROP_MIDIFILE_NUM_TRACKS: ret = JS_NewUint32(ctx, mf->getNumberOfTracks()); break;
    case PROP_MIDIFILE_DIVISION: ret = JS_NewInt32(ctx, mf->getDivision()); break;
  }

  return ret;
}

static void
js_midifilein_finalizer(JSRuntime* rt, JSValue val) {
  stk::MidiFileIn* mf = static_cast<stk::MidiFileIn*>(JS_GetOpaque(val, js_midifilein_class_id));
  delete mf;
}

static JSClassDef js_midifilein_class = {
    .class_name = "MidiFileIn",
    .finalizer = js_midifilein_finalizer,
};

static const JSCFunctionListEntry js_midifilein_funcs[] = {
    JS_CFUNC_MAGIC_DEF("rewindTrack", 0, js_midifilein_method, METHOD_MIDIFILE_REWIND_TRACK),
    JS_CFUNC_MAGIC_DEF("getTickSeconds", 0, js_midifilein_method, METHOD_MIDIFILE_GET_TICK_SECONDS),
    JS_CFUNC_MAGIC_DEF("getNextEvent", 0, js_midifilein_method, METHOD_MIDIFILE_GET_NEXT_EVENT),
    JS_CFUNC_MAGIC_DEF("getNextMidiEvent", 0, js_midifilein_method, METHOD_MIDIFILE_GET_NEXT_MIDI_EVENT),
    JS_CGETSET_MAGIC_DEF("format", js_midifilein_get, 0, PROP_MIDIFILE_FORMAT),
    JS_CGETSET_MAGIC_DEF("numberOfTracks", js_midifilein_get, 0, PROP_MIDIFILE_NUM_TRACKS),
    JS_CGETSET_MAGIC_DEF("division", js_midifilein_get, 0, PROP_MIDIFILE_DIVISION),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MidiFileIn", JS_PROP_CONFIGURABLE),
};

/* ============================================================ */
/* RtMidiIn / RtMidiOut -- RtMidiIn, RtMidiOut              */
/* NOTE: RtAudio.h/RtMidi.h declare their classes in the global   */
/* namespace, not inside namespace stk.                          */
/* ============================================================ */

static JSValue
js_rtmidiin_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  int32_t api = RtMidi::UNSPECIFIED;
  std::string clientName = "RtMidi Input Client";
  uint32_t queueSizeLimit = 100;

  if(argc > 0)
    JS_ToInt32(ctx, &api, argv[0]);
  if(argc > 1) {
    const char* s = JS_ToCString(ctx, argv[1]);
    if(s) {
      clientName = s;
      JS_FreeCString(ctx, s);
    }
  }
  if(argc > 2)
    JS_ToUint32(ctx, &queueSizeLimit, argv[2]);

  RtMidiIn* r = nullptr;
  try {
    r = new RtMidiIn((RtMidi::Api)api, clientName, queueSizeLimit);
  } catch(const std::exception& e) {
    return js_stk_throw(ctx, e);
  }

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, rtmidiin_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_rtmidiin_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, r);
  js_set_tostringtag(ctx, obj, "RtMidiIn");
  return obj;

fail:
  delete r;
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_RTMIDIIN_OPEN_PORT = 0,
  METHOD_RTMIDIIN_OPEN_VIRTUAL_PORT,
  METHOD_RTMIDIIN_CLOSE_PORT,
  METHOD_RTMIDIIN_IS_PORT_OPEN,
  METHOD_RTMIDIIN_GET_PORT_COUNT,
  METHOD_RTMIDIIN_GET_PORT_NAME,
  METHOD_RTMIDIIN_IGNORE_TYPES,
  METHOD_RTMIDIIN_GET_MESSAGE,
  METHOD_RTMIDIIN_GET_CURRENT_API,
};

static JSValue
js_rtmidiin_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  RtMidiIn* r;
  JSValue ret = JS_UNDEFINED;

  if(!(r = static_cast<RtMidiIn*>(JS_GetOpaque2(ctx, this_val, js_rtmidiin_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTMIDIIN_OPEN_PORT: {
      uint32_t portNumber = 0;
      std::string portName = "RtMidi Input";
      if(argc > 0)
        JS_ToUint32(ctx, &portNumber, argv[0]);
      if(argc > 1) {
        const char* s = JS_ToCString(ctx, argv[1]);
        if(s) {
          portName = s;
          JS_FreeCString(ctx, s);
        }
      }
      try {
        r->openPort(portNumber, portName);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_RTMIDIIN_OPEN_VIRTUAL_PORT: {
      std::string portName = "RtMidi Input";
      if(argc > 0) {
        const char* s = JS_ToCString(ctx, argv[0]);
        if(s) {
          portName = s;
          JS_FreeCString(ctx, s);
        }
      }
      try {
        r->openVirtualPort(portName);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_RTMIDIIN_CLOSE_PORT: {
      r->closePort();
      break;
    }
    case METHOD_RTMIDIIN_IS_PORT_OPEN: {
      ret = JS_NewBool(ctx, r->isPortOpen());
      break;
    }
    case METHOD_RTMIDIIN_GET_PORT_COUNT: {
      ret = JS_NewUint32(ctx, r->getPortCount());
      break;
    }
    case METHOD_RTMIDIIN_GET_PORT_NAME: {
      uint32_t portNumber = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &portNumber, argv[0]);
      std::string name;
      try {
        name = r->getPortName(portNumber);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      ret = JS_NewStringLen(ctx, name.c_str(), name.size());
      break;
    }
    case METHOD_RTMIDIIN_IGNORE_TYPES: {
      BOOL sysex = TRUE, time = TRUE, sense = TRUE;
      if(argc > 0)
        sysex = JS_ToBool(ctx, argv[0]);
      if(argc > 1)
        time = JS_ToBool(ctx, argv[1]);
      if(argc > 2)
        sense = JS_ToBool(ctx, argv[2]);
      r->ignoreTypes(sysex, time, sense);
      break;
    }
    case METHOD_RTMIDIIN_GET_MESSAGE: {
      std::vector<unsigned char> message;
      double timeStamp;
      try {
        timeStamp = r->getMessage(&message);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }

      JSValue data = JS_NewArray(ctx);
      for(size_t i = 0; i < message.size(); i++)
        JS_SetPropertyUint32(ctx, data, i, JS_NewUint32(ctx, message[i]));

      JSValue obj = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, obj, "timeStamp", JS_NewFloat64(ctx, timeStamp));
      JS_SetPropertyStr(ctx, obj, "data", data);
      ret = obj;
      break;
    }
    case METHOD_RTMIDIIN_GET_CURRENT_API: {
      ret = JS_NewInt32(ctx, r->getCurrentApi());
      break;
    }
  }

  return ret;
}

static void
js_rtmidiin_finalizer(JSRuntime* rt, JSValue val) {
  RtMidiIn* r = static_cast<RtMidiIn*>(JS_GetOpaque(val, js_rtmidiin_class_id));
  delete r;
}

static JSClassDef js_rtmidiin_class = {
    .class_name = "RtMidiIn",
    .finalizer = js_rtmidiin_finalizer,
};

static const JSCFunctionListEntry js_rtmidiin_funcs[] = {
    JS_CFUNC_MAGIC_DEF("openPort", 0, js_rtmidiin_method, METHOD_RTMIDIIN_OPEN_PORT),
    JS_CFUNC_MAGIC_DEF("openVirtualPort", 0, js_rtmidiin_method, METHOD_RTMIDIIN_OPEN_VIRTUAL_PORT),
    JS_CFUNC_MAGIC_DEF("closePort", 0, js_rtmidiin_method, METHOD_RTMIDIIN_CLOSE_PORT),
    JS_CFUNC_MAGIC_DEF("isPortOpen", 0, js_rtmidiin_method, METHOD_RTMIDIIN_IS_PORT_OPEN),
    JS_CFUNC_MAGIC_DEF("getPortCount", 0, js_rtmidiin_method, METHOD_RTMIDIIN_GET_PORT_COUNT),
    JS_CFUNC_MAGIC_DEF("getPortName", 0, js_rtmidiin_method, METHOD_RTMIDIIN_GET_PORT_NAME),
    JS_CFUNC_MAGIC_DEF("ignoreTypes", 0, js_rtmidiin_method, METHOD_RTMIDIIN_IGNORE_TYPES),
    JS_CFUNC_MAGIC_DEF("getMessage", 0, js_rtmidiin_method, METHOD_RTMIDIIN_GET_MESSAGE),
    JS_CFUNC_MAGIC_DEF("getCurrentApi", 0, js_rtmidiin_method, METHOD_RTMIDIIN_GET_CURRENT_API),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RtMidiIn", JS_PROP_CONFIGURABLE),
};

static JSValue
js_rtmidiout_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  int32_t api = RtMidi::UNSPECIFIED;
  std::string clientName = "RtMidi Output Client";

  if(argc > 0)
    JS_ToInt32(ctx, &api, argv[0]);
  if(argc > 1) {
    const char* s = JS_ToCString(ctx, argv[1]);
    if(s) {
      clientName = s;
      JS_FreeCString(ctx, s);
    }
  }

  RtMidiOut* r = nullptr;
  try {
    r = new RtMidiOut((RtMidi::Api)api, clientName);
  } catch(const std::exception& e) {
    return js_stk_throw(ctx, e);
  }

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, rtmidiout_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_rtmidiout_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, r);
  js_set_tostringtag(ctx, obj, "RtMidiOut");
  return obj;

fail:
  delete r;
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_RTMIDIOUT_OPEN_PORT = 0,
  METHOD_RTMIDIOUT_OPEN_VIRTUAL_PORT,
  METHOD_RTMIDIOUT_CLOSE_PORT,
  METHOD_RTMIDIOUT_IS_PORT_OPEN,
  METHOD_RTMIDIOUT_GET_PORT_COUNT,
  METHOD_RTMIDIOUT_GET_PORT_NAME,
  METHOD_RTMIDIOUT_SEND_MESSAGE,
  METHOD_RTMIDIOUT_GET_CURRENT_API,
};

static JSValue
js_rtmidiout_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  RtMidiOut* r;
  JSValue ret = JS_UNDEFINED;

  if(!(r = static_cast<RtMidiOut*>(JS_GetOpaque2(ctx, this_val, js_rtmidiout_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTMIDIOUT_OPEN_PORT: {
      uint32_t portNumber = 0;
      std::string portName = "RtMidi Output";
      if(argc > 0)
        JS_ToUint32(ctx, &portNumber, argv[0]);
      if(argc > 1) {
        const char* s = JS_ToCString(ctx, argv[1]);
        if(s) {
          portName = s;
          JS_FreeCString(ctx, s);
        }
      }
      try {
        r->openPort(portNumber, portName);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_RTMIDIOUT_OPEN_VIRTUAL_PORT: {
      std::string portName = "RtMidi Output";
      if(argc > 0) {
        const char* s = JS_ToCString(ctx, argv[0]);
        if(s) {
          portName = s;
          JS_FreeCString(ctx, s);
        }
      }
      try {
        r->openVirtualPort(portName);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_RTMIDIOUT_CLOSE_PORT: {
      r->closePort();
      break;
    }
    case METHOD_RTMIDIOUT_IS_PORT_OPEN: {
      ret = JS_NewBool(ctx, r->isPortOpen());
      break;
    }
    case METHOD_RTMIDIOUT_GET_PORT_COUNT: {
      ret = JS_NewUint32(ctx, r->getPortCount());
      break;
    }
    case METHOD_RTMIDIOUT_GET_PORT_NAME: {
      uint32_t portNumber = 0;
      if(argc > 0)
        JS_ToUint32(ctx, &portNumber, argv[0]);
      std::string name;
      try {
        name = r->getPortName(portNumber);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      ret = JS_NewStringLen(ctx, name.c_str(), name.size());
      break;
    }
    case METHOD_RTMIDIOUT_SEND_MESSAGE: {
      std::vector<unsigned char> message;
      array_to_bytes(ctx, argv[0], message);
      try {
        r->sendMessage(&message);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_RTMIDIOUT_GET_CURRENT_API: {
      ret = JS_NewInt32(ctx, r->getCurrentApi());
      break;
    }
  }

  return ret;
}

static void
js_rtmidiout_finalizer(JSRuntime* rt, JSValue val) {
  RtMidiOut* r = static_cast<RtMidiOut*>(JS_GetOpaque(val, js_rtmidiout_class_id));
  delete r;
}

static JSClassDef js_rtmidiout_class = {
    .class_name = "RtMidiOut",
    .finalizer = js_rtmidiout_finalizer,
};

static const JSCFunctionListEntry js_rtmidiout_funcs[] = {
    JS_CFUNC_MAGIC_DEF("openPort", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_OPEN_PORT),
    JS_CFUNC_MAGIC_DEF("openVirtualPort", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_OPEN_VIRTUAL_PORT),
    JS_CFUNC_MAGIC_DEF("closePort", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_CLOSE_PORT),
    JS_CFUNC_MAGIC_DEF("isPortOpen", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_IS_PORT_OPEN),
    JS_CFUNC_MAGIC_DEF("getPortCount", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_GET_PORT_COUNT),
    JS_CFUNC_MAGIC_DEF("getPortName", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_GET_PORT_NAME),
    JS_CFUNC_MAGIC_DEF("sendMessage", 1, js_rtmidiout_method, METHOD_RTMIDIOUT_SEND_MESSAGE),
    JS_CFUNC_MAGIC_DEF("getCurrentApi", 0, js_rtmidiout_method, METHOD_RTMIDIOUT_GET_CURRENT_API),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RtMidiOut", JS_PROP_CONFIGURABLE),
};

/* ============================================================ */
/* RtAudio -- RtAudio                                         */
/* Device enumeration and stream lifecycle only: bridging         */
/* RtAudio's realtime audio callback into the JS engine would     */
/* mean calling back into QuickJS from RtAudio's own audio        */
/* thread, which is not safe (QuickJS is not thread-safe). Use    */
/* RtWvIn/RtWvOut for actual realtime audio I/O instead.    */
/* ============================================================ */

static JSValue
js_rtaudio_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  int32_t api = RtAudio::UNSPECIFIED;
  if(argc > 0)
    JS_ToInt32(ctx, &api, argv[0]);

  RtAudio* a = new RtAudio((RtAudio::Api)api);

  JSValue obj = JS_UNDEFINED, proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto)) {
    JS_FreeValue(ctx, proto);
    proto = JS_DupValue(ctx, rtaudio_proto);
  }

  obj = JS_NewObjectProtoClass(ctx, proto, js_rtaudio_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, a);
  js_set_tostringtag(ctx, obj, "RtAudio");
  return obj;

fail:
  delete a;
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  METHOD_RTAUDIO_GET_CURRENT_API = 0,
  METHOD_RTAUDIO_GET_DEVICE_COUNT,
  METHOD_RTAUDIO_GET_DEVICE_IDS,
  METHOD_RTAUDIO_GET_DEVICE_NAMES,
  METHOD_RTAUDIO_GET_DEVICE_INFO,
  METHOD_RTAUDIO_GET_DEFAULT_OUTPUT_DEVICE,
  METHOD_RTAUDIO_GET_DEFAULT_INPUT_DEVICE,
  METHOD_RTAUDIO_CLOSE_STREAM,
  METHOD_RTAUDIO_START_STREAM,
  METHOD_RTAUDIO_STOP_STREAM,
  METHOD_RTAUDIO_ABORT_STREAM,
  METHOD_RTAUDIO_GET_ERROR_TEXT,
  METHOD_RTAUDIO_IS_STREAM_OPEN,
  METHOD_RTAUDIO_IS_STREAM_RUNNING,
  METHOD_RTAUDIO_GET_STREAM_TIME,
  METHOD_RTAUDIO_SET_STREAM_TIME,
  METHOD_RTAUDIO_GET_STREAM_LATENCY,
  METHOD_RTAUDIO_GET_STREAM_SAMPLE_RATE,
  METHOD_RTAUDIO_SHOW_WARNINGS,
};

static JSValue
js_rtaudio_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  RtAudio* a;
  JSValue ret = JS_UNDEFINED;

  if(!(a = static_cast<RtAudio*>(JS_GetOpaque2(ctx, this_val, js_rtaudio_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTAUDIO_GET_CURRENT_API: {
      ret = JS_NewInt32(ctx, a->getCurrentApi());
      break;
    }
    case METHOD_RTAUDIO_GET_DEVICE_COUNT: {
      ret = JS_NewUint32(ctx, a->getDeviceCount());
      break;
    }
    case METHOD_RTAUDIO_GET_DEVICE_IDS: {
      std::vector<unsigned int> ids = a->getDeviceIds();
      JSValue arr = JS_NewArray(ctx);
      for(size_t i = 0; i < ids.size(); i++)
        JS_SetPropertyUint32(ctx, arr, i, JS_NewUint32(ctx, ids[i]));
      ret = arr;
      break;
    }
    case METHOD_RTAUDIO_GET_DEVICE_NAMES: {
      std::vector<std::string> names = a->getDeviceNames();
      JSValue arr = JS_NewArray(ctx);
      for(size_t i = 0; i < names.size(); i++)
        JS_SetPropertyUint32(ctx, arr, i, JS_NewStringLen(ctx, names[i].c_str(), names[i].size()));
      ret = arr;
      break;
    }
    case METHOD_RTAUDIO_GET_DEVICE_INFO: {
      uint32_t deviceId = 0;
      JS_ToUint32(ctx, &deviceId, argv[0]);
      RtAudio::DeviceInfo info = a->getDeviceInfo(deviceId);

      JSValue obj = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, obj, "id", JS_NewUint32(ctx, info.ID));
      JS_SetPropertyStr(ctx, obj, "name", JS_NewStringLen(ctx, info.name.c_str(), info.name.size()));
      JS_SetPropertyStr(ctx, obj, "outputChannels", JS_NewUint32(ctx, info.outputChannels));
      JS_SetPropertyStr(ctx, obj, "inputChannels", JS_NewUint32(ctx, info.inputChannels));
      JS_SetPropertyStr(ctx, obj, "duplexChannels", JS_NewUint32(ctx, info.duplexChannels));
      JS_SetPropertyStr(ctx, obj, "isDefaultOutput", JS_NewBool(ctx, info.isDefaultOutput));
      JS_SetPropertyStr(ctx, obj, "isDefaultInput", JS_NewBool(ctx, info.isDefaultInput));

      JSValue rates = JS_NewArray(ctx);
      for(size_t i = 0; i < info.sampleRates.size(); i++)
        JS_SetPropertyUint32(ctx, rates, i, JS_NewUint32(ctx, info.sampleRates[i]));
      JS_SetPropertyStr(ctx, obj, "sampleRates", rates);

      JS_SetPropertyStr(ctx, obj, "currentSampleRate", JS_NewUint32(ctx, info.currentSampleRate));
      JS_SetPropertyStr(ctx, obj, "preferredSampleRate", JS_NewUint32(ctx, info.preferredSampleRate));
      JS_SetPropertyStr(ctx, obj, "nativeFormats", JS_NewUint32(ctx, info.nativeFormats));
      ret = obj;
      break;
    }
    case METHOD_RTAUDIO_GET_DEFAULT_OUTPUT_DEVICE: {
      ret = JS_NewUint32(ctx, a->getDefaultOutputDevice());
      break;
    }
    case METHOD_RTAUDIO_GET_DEFAULT_INPUT_DEVICE: {
      ret = JS_NewUint32(ctx, a->getDefaultInputDevice());
      break;
    }
    case METHOD_RTAUDIO_CLOSE_STREAM: {
      a->closeStream();
      break;
    }
    case METHOD_RTAUDIO_START_STREAM: {
      ret = JS_NewInt32(ctx, a->startStream());
      break;
    }
    case METHOD_RTAUDIO_STOP_STREAM: {
      ret = JS_NewInt32(ctx, a->stopStream());
      break;
    }
    case METHOD_RTAUDIO_ABORT_STREAM: {
      ret = JS_NewInt32(ctx, a->abortStream());
      break;
    }
    case METHOD_RTAUDIO_GET_ERROR_TEXT: {
      std::string text = a->getErrorText();
      ret = JS_NewStringLen(ctx, text.c_str(), text.size());
      break;
    }
    case METHOD_RTAUDIO_IS_STREAM_OPEN: {
      ret = JS_NewBool(ctx, a->isStreamOpen());
      break;
    }
    case METHOD_RTAUDIO_IS_STREAM_RUNNING: {
      ret = JS_NewBool(ctx, a->isStreamRunning());
      break;
    }
    case METHOD_RTAUDIO_GET_STREAM_TIME: {
      ret = JS_NewFloat64(ctx, a->getStreamTime());
      break;
    }
    case METHOD_RTAUDIO_SET_STREAM_TIME: {
      double t = 0;
      JS_ToFloat64(ctx, &t, argv[0]);
      a->setStreamTime(t);
      break;
    }
    case METHOD_RTAUDIO_GET_STREAM_LATENCY: {
      ret = JS_NewInt64(ctx, a->getStreamLatency());
      break;
    }
    case METHOD_RTAUDIO_GET_STREAM_SAMPLE_RATE: {
      ret = JS_NewUint32(ctx, a->getStreamSampleRate());
      break;
    }
    case METHOD_RTAUDIO_SHOW_WARNINGS: {
      BOOL value = TRUE;
      if(argc > 0)
        value = JS_ToBool(ctx, argv[0]);
      a->showWarnings(value);
      break;
    }
  }

  return ret;
}

static void
js_rtaudio_finalizer(JSRuntime* rt, JSValue val) {
  RtAudio* a = static_cast<RtAudio*>(JS_GetOpaque(val, js_rtaudio_class_id));
  delete a;
}

static JSClassDef js_rtaudio_class = {
    .class_name = "RtAudio",
    .finalizer = js_rtaudio_finalizer,
};

static const JSCFunctionListEntry js_rtaudio_funcs[] = {
    JS_CFUNC_MAGIC_DEF("getCurrentApi", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_CURRENT_API),
    JS_CFUNC_MAGIC_DEF("getDeviceCount", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_DEVICE_COUNT),
    JS_CFUNC_MAGIC_DEF("getDeviceIds", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_DEVICE_IDS),
    JS_CFUNC_MAGIC_DEF("getDeviceNames", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_DEVICE_NAMES),
    JS_CFUNC_MAGIC_DEF("getDeviceInfo", 1, js_rtaudio_method, METHOD_RTAUDIO_GET_DEVICE_INFO),
    JS_CFUNC_MAGIC_DEF("getDefaultOutputDevice", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_DEFAULT_OUTPUT_DEVICE),
    JS_CFUNC_MAGIC_DEF("getDefaultInputDevice", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_DEFAULT_INPUT_DEVICE),
    JS_CFUNC_MAGIC_DEF("closeStream", 0, js_rtaudio_method, METHOD_RTAUDIO_CLOSE_STREAM),
    JS_CFUNC_MAGIC_DEF("startStream", 0, js_rtaudio_method, METHOD_RTAUDIO_START_STREAM),
    JS_CFUNC_MAGIC_DEF("stopStream", 0, js_rtaudio_method, METHOD_RTAUDIO_STOP_STREAM),
    JS_CFUNC_MAGIC_DEF("abortStream", 0, js_rtaudio_method, METHOD_RTAUDIO_ABORT_STREAM),
    JS_CFUNC_MAGIC_DEF("getErrorText", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_ERROR_TEXT),
    JS_CFUNC_MAGIC_DEF("isStreamOpen", 0, js_rtaudio_method, METHOD_RTAUDIO_IS_STREAM_OPEN),
    JS_CFUNC_MAGIC_DEF("isStreamRunning", 0, js_rtaudio_method, METHOD_RTAUDIO_IS_STREAM_RUNNING),
    JS_CFUNC_MAGIC_DEF("getStreamTime", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_STREAM_TIME),
    JS_CFUNC_MAGIC_DEF("setStreamTime", 1, js_rtaudio_method, METHOD_RTAUDIO_SET_STREAM_TIME),
    JS_CFUNC_MAGIC_DEF("getStreamLatency", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_STREAM_LATENCY),
    JS_CFUNC_MAGIC_DEF("getStreamSampleRate", 0, js_rtaudio_method, METHOD_RTAUDIO_GET_STREAM_SAMPLE_RATE),
    JS_CFUNC_MAGIC_DEF("showWarnings", 1, js_rtaudio_method, METHOD_RTAUDIO_SHOW_WARNINGS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RtAudio", JS_PROP_CONFIGURABLE),
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
                                      // "Generator", 1, JS_CFUNC_constructor, 0);
  stkfilter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkfilter_proto, js_stkfilter_funcs, countof(js_stkfilter_funcs));

  JS_SetClassProto(ctx, js_stkfilter_class_id, stkfilter_proto);

  JSValue ctor;

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "BiQuad", 0, JS_CFUNC_constructor_magic, INSTANCE_BIQUAD);
    JS_SetModuleExport(ctx, m, "BiQuad", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "DelayA", 0, JS_CFUNC_constructor_magic, INSTANCE_DELAY_A);
    JS_SetModuleExport(ctx, m, "DelayA", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "DelayL", 0, JS_CFUNC_constructor_magic, INSTANCE_DELAY_L);
    JS_SetModuleExport(ctx, m, "DelayL", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "Delay", 0, JS_CFUNC_constructor_magic, INSTANCE_DELAY);
    JS_SetModuleExport(ctx, m, "Delay", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "Fir", 1, JS_CFUNC_constructor_magic, INSTANCE_FIR);
    JS_SetModuleExport(ctx, m, "Fir", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "FormSwep", 0, JS_CFUNC_constructor_magic, INSTANCE_FORM_SWEP);
    JS_SetModuleExport(ctx, m, "FormSwep", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "Iir", 0, JS_CFUNC_constructor_magic, INSTANCE_IIR);
    JS_SetModuleExport(ctx, m, "Iir", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "OnePole", 0, JS_CFUNC_constructor_magic, INSTANCE_ONE_POLE);
    JS_SetModuleExport(ctx, m, "OnePole", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "OneZero", 0, JS_CFUNC_constructor_magic, INSTANCE_ONE_ZERO);
    JS_SetModuleExport(ctx, m, "OneZero", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "PoleZero", 0, JS_CFUNC_constructor_magic, INSTANCE_POLE_ZERO);
    JS_SetModuleExport(ctx, m, "PoleZero", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "TapDelay", 0, JS_CFUNC_constructor_magic, INSTANCE_TAP_DELAY);
    JS_SetModuleExport(ctx, m, "TapDelay", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "TwoPole", 0, JS_CFUNC_constructor_magic, INSTANCE_TWO_POLE);
    JS_SetModuleExport(ctx, m, "TwoPole", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfilter_constructor, "TwoZero", 0, JS_CFUNC_constructor_magic, INSTANCE_TWO_ZERO);
    JS_SetModuleExport(ctx, m, "TwoZero", ctor);
  }
  JS_NewClassID(&js_stkgenerator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkgenerator_class_id, &js_stkgenerator_class);

  stkgenerator_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkgenerator_constructor,
                                         // "Filter", 1, JS_CFUNC_constructor, 0);
  stkgenerator_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkgenerator_proto, js_stkgenerator_funcs, countof(js_stkgenerator_funcs));

  JS_SetClassProto(ctx, js_stkgenerator_class_id, stkgenerator_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "ADSR", 0, JS_CFUNC_constructor_magic, INSTANCE_ADSR);
    JS_SetModuleExport(ctx, m, "ADSR", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Asymp", 0, JS_CFUNC_constructor_magic, INSTANCE_ASYMP);
    JS_SetModuleExport(ctx, m, "Asymp", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "BlitSaw", 0, JS_CFUNC_constructor_magic, INSTANCE_BLIT_SAW);
    JS_SetModuleExport(ctx, m, "BlitSaw", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "BlitSquare", 0, JS_CFUNC_constructor_magic, INSTANCE_BLIT_SQUARE);
    JS_SetModuleExport(ctx, m, "BlitSquare", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Blit", 0, JS_CFUNC_constructor_magic, INSTANCE_BLIT);
    JS_SetModuleExport(ctx, m, "Blit", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Envelope", 0, JS_CFUNC_constructor_magic, INSTANCE_ENVELOPE);
    JS_SetModuleExport(ctx, m, "Envelope", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Granulate", 0, JS_CFUNC_constructor_magic, INSTANCE_GRANULATE);
    JS_SetModuleExport(ctx, m, "Granulate", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Modulate", 0, JS_CFUNC_constructor_magic, INSTANCE_MODULATE);
    JS_SetModuleExport(ctx, m, "Modulate", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "Noise", 0, JS_CFUNC_constructor_magic, INSTANCE_NOISE);
    JS_SetModuleExport(ctx, m, "Noise", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "SineWave", 0, JS_CFUNC_constructor_magic, INSTANCE_SINE_WAVE);
    JS_SetModuleExport(ctx, m, "SineWave", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkgenerator_constructor, "SingWave", 0, JS_CFUNC_constructor_magic, INSTANCE_SING_WAVE);
    JS_SetModuleExport(ctx, m, "StkSingWave", ctor);
  }

  JS_NewClassID(&js_stkeffect_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkeffect_class_id, &js_stkeffect_class);

  stkeffect_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkeffect_constructor,
                                      // "Generator", 1, JS_CFUNC_constructor, 0);
  stkeffect_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkeffect_proto, js_stkeffect_funcs, countof(js_stkeffect_funcs));

  JS_SetClassProto(ctx, js_stkeffect_class_id, stkeffect_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "Chorus", 0, JS_CFUNC_constructor_magic, INSTANCE_CHORUS);
    JS_SetModuleExport(ctx, m, "Chorus", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "Echo", 0, JS_CFUNC_constructor_magic, INSTANCE_ECHO);
    JS_SetModuleExport(ctx, m, "Echo", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "FreeVerb", 0, JS_CFUNC_constructor_magic, INSTANCE_FREEVERB);
    JS_SetModuleExport(ctx, m, "FreeVerb", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "JCRev", 0, JS_CFUNC_constructor_magic, INSTANCE_JCREV);
    JS_SetModuleExport(ctx, m, "JCRev", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "LentPitShift", 0, JS_CFUNC_constructor_magic, INSTANCE_LENTPITSHIFT);
    JS_SetModuleExport(ctx, m, "LentPitShift", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "NRev", 0, JS_CFUNC_constructor_magic, INSTANCE_NREV);
    JS_SetModuleExport(ctx, m, "NRev", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "PitShift", 0, JS_CFUNC_constructor_magic, INSTANCE_PITSHIFT);
    JS_SetModuleExport(ctx, m, "PitShift", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkeffect_constructor, "PRCRev", 0, JS_CFUNC_constructor_magic, INSTANCE_PRCREV);
    JS_SetModuleExport(ctx, m, "PRCRev", ctor);

    JS_SetModuleExport(ctx, m, "Effect", stkeffect_ctor);
  }

  stkinstrmnt_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkinstrmnt_constructor,
                                        // "Generator", 1, JS_CFUNC_constructor, 0);
  stkinstrmnt_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkinstrmnt_proto, js_stkinstrmnt_funcs, countof(js_stkinstrmnt_funcs));

  JS_SetClassProto(ctx, js_stkinstrmnt_class_id, stkinstrmnt_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "BandedWG", 0, JS_CFUNC_constructor_magic, INSTANCE_BANDEDWG);
    JS_SetModuleExport(ctx, m, "BandedWG", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "BlowBotl", 0, JS_CFUNC_constructor_magic, INSTANCE_BLOWBOTL);
    JS_SetModuleExport(ctx, m, "BlowBotl", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "BlowHole", 0, JS_CFUNC_constructor_magic, INSTANCE_BLOWHOLE);
    JS_SetModuleExport(ctx, m, "BlowHole", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Bowed", 0, JS_CFUNC_constructor_magic, INSTANCE_BOWED);
    JS_SetModuleExport(ctx, m, "Bowed", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Brass", 0, JS_CFUNC_constructor_magic, INSTANCE_BRASS);
    JS_SetModuleExport(ctx, m, "Brass", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Clarinet", 0, JS_CFUNC_constructor_magic, INSTANCE_CLARINET);
    JS_SetModuleExport(ctx, m, "Clarinet", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Drummer", 0, JS_CFUNC_constructor_magic, INSTANCE_DRUMMER);
    JS_SetModuleExport(ctx, m, "Drummer", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Flute", 0, JS_CFUNC_constructor_magic, INSTANCE_FLUTE);
    JS_SetModuleExport(ctx, m, "Flute", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Mandolin", 0, JS_CFUNC_constructor_magic, INSTANCE_MANDOLIN);
    JS_SetModuleExport(ctx, m, "Mandolin", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Mesh2D", 0, JS_CFUNC_constructor_magic, INSTANCE_MESH2D);
    JS_SetModuleExport(ctx, m, "Mesh2D", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Plucked", 0, JS_CFUNC_constructor_magic, INSTANCE_PLUCKED);
    JS_SetModuleExport(ctx, m, "Plucked", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Recorder", 0, JS_CFUNC_constructor_magic, INSTANCE_RECORDER);
    JS_SetModuleExport(ctx, m, "Recorder", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Resonate", 0, JS_CFUNC_constructor_magic, INSTANCE_RESONATE);
    JS_SetModuleExport(ctx, m, "Resonate", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Saxofony", 0, JS_CFUNC_constructor_magic, INSTANCE_SAXOFONY);
    JS_SetModuleExport(ctx, m, "Saxofony", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Shakers", 0, JS_CFUNC_constructor_magic, INSTANCE_SHAKERS);
    JS_SetModuleExport(ctx, m, "Shakers", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Simple", 0, JS_CFUNC_constructor_magic, INSTANCE_SIMPLE);
    JS_SetModuleExport(ctx, m, "Simple", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Sitar", 0, JS_CFUNC_constructor_magic, INSTANCE_SITAR);
    JS_SetModuleExport(ctx, m, "Sitar", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "StifKarp", 0, JS_CFUNC_constructor_magic, INSTANCE_STIFKARP);
    JS_SetModuleExport(ctx, m, "StifKarp", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "VoicForm", 0, JS_CFUNC_constructor_magic, INSTANCE_VOICFORM);
    JS_SetModuleExport(ctx, m, "VoicForm", ctor);
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkinstrmnt_constructor, "Whistle", 0, JS_CFUNC_constructor_magic, INSTANCE_WHISTLE);
    JS_SetModuleExport(ctx, m, "Whistle", ctor);

    JS_SetModuleExport(ctx, m, "Stk", stk_ctor);
    JS_SetModuleExport(ctx, m, "StkFrames", stkframes_ctor);
    JS_SetModuleExport(ctx, m, "Generator", stkfilter_ctor);
    JS_SetModuleExport(ctx, m, "Filter", stkgenerator_ctor);
  }

  /* TwinTDrum, Tr909BassDrum and Tr909Percussion are ordinary stk::Instrmnt
   * subclasses, so they share js_stkinstrmnt_class_id/finalizer above;
   * only their own prototype (chained onto stkinstrmnt_proto) differs,
   * carrying their extra controls. */
  twintdrum_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, twintdrum_proto, stkinstrmnt_proto);
  JS_SetPropertyFunctionList(ctx, twintdrum_proto, js_twintdrum_funcs, countof(js_twintdrum_funcs));

  tr909bassdrum_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, tr909bassdrum_proto, stkinstrmnt_proto);
  JS_SetPropertyFunctionList(ctx, tr909bassdrum_proto, js_tr909bassdrum_funcs, countof(js_tr909bassdrum_funcs));

  tr909percussion_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, tr909percussion_proto, stkinstrmnt_proto);
  JS_SetPropertyFunctionList(ctx, tr909percussion_proto, js_tr909percussion_funcs, countof(js_tr909percussion_funcs));

  if(m) {
    ctor = JS_NewCFunction2(ctx, js_twintdrum_constructor, "TwinTDrum", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, twintdrum_proto);
    JS_SetModuleExport(ctx, m, "TwinTDrum", ctor);

    ctor = JS_NewCFunction2(ctx, js_tr909bassdrum_constructor, "Tr909BassDrum", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, tr909bassdrum_proto);
    JS_SetModuleExport(ctx, m, "Tr909BassDrum", ctor);

    ctor = JS_NewCFunction2(ctx, js_tr909percussion_constructor, "Tr909Percussion", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, tr909percussion_proto);
    JS_SetModuleExport(ctx, m, "Tr909Percussion", ctor);
  }

  JS_NewClassID(&js_stkfunction_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkfunction_class_id, &js_stkfunction_class);

  stkfunction_ctor = JS_NewObject(ctx);
  stkfunction_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkfunction_proto, js_stkfunction_funcs, countof(js_stkfunction_funcs));

  JS_SetClassProto(ctx, js_stkfunction_class_id, stkfunction_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkfunction_constructor, "Cubic", 0, JS_CFUNC_constructor_magic, INSTANCE_CUBIC);
    JS_SetModuleExport(ctx, m, "Cubic", ctor);

    JS_SetModuleExport(ctx, m, "Function", stkfunction_ctor);
  }

  /* stk::WvIn (RtWvIn, InetWvIn) */
  JS_NewClassID(&js_stkwvin_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkwvin_class_id, &js_stkwvin_class);

  stkwvin_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stkwvin_proto, js_stkwvin_funcs, countof(js_stkwvin_funcs));
  JS_SetClassProto(ctx, js_stkwvin_class_id, stkwvin_proto);

  rtwvin_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, rtwvin_proto, stkwvin_proto);
  JS_SetPropertyFunctionList(ctx, rtwvin_proto, js_rtwvin_funcs, countof(js_rtwvin_funcs));

  inetwvin_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, inetwvin_proto, stkwvin_proto);
  JS_SetPropertyFunctionList(ctx, inetwvin_proto, js_inetwvin_funcs, countof(js_inetwvin_funcs));

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkwvin_constructor, "RtWvIn", 0, JS_CFUNC_constructor_magic, INSTANCE_WVIN_RTWVIN);
    JS_SetConstructor(ctx, ctor, rtwvin_proto);
    JS_SetModuleExport(ctx, m, "RtWvIn", ctor);

    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkwvin_constructor, "InetWvIn", 0, JS_CFUNC_constructor_magic, INSTANCE_WVIN_INETWVIN);
    JS_SetConstructor(ctx, ctor, inetwvin_proto);
    JS_SetModuleExport(ctx, m, "InetWvIn", ctor);
  }

  /* stk::WvOut (RtWvOut, InetWvOut) */
  JS_NewClassID(&js_stkwvout_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkwvout_class_id, &js_stkwvout_class);

  stkwvout_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stkwvout_proto, js_stkwvout_funcs, countof(js_stkwvout_funcs));
  JS_SetClassProto(ctx, js_stkwvout_class_id, stkwvout_proto);

  rtwvout_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, rtwvout_proto, stkwvout_proto);
  JS_SetPropertyFunctionList(ctx, rtwvout_proto, js_rtwvout_funcs, countof(js_rtwvout_funcs));

  inetwvout_proto = JS_NewObject(ctx);
  JS_SetPrototype(ctx, inetwvout_proto, stkwvout_proto);
  JS_SetPropertyFunctionList(ctx, inetwvout_proto, js_inetwvout_funcs, countof(js_inetwvout_funcs));

  if(m) {
    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkwvout_constructor, "RtWvOut", 0, JS_CFUNC_constructor_magic, INSTANCE_WVOUT_RTWVOUT);
    JS_SetConstructor(ctx, ctor, rtwvout_proto);
    JS_SetModuleExport(ctx, m, "RtWvOut", ctor);

    ctor = JS_NewCFunction2(ctx, (JSCFunction*)js_stkwvout_constructor, "InetWvOut", 0, JS_CFUNC_constructor_magic, INSTANCE_WVOUT_INETWVOUT);
    JS_SetConstructor(ctx, ctor, inetwvout_proto);
    JS_SetModuleExport(ctx, m, "InetWvOut", ctor);
  }

  /* stk::MidiFileIn */
  JS_NewClassID(&js_midifilein_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_midifilein_class_id, &js_midifilein_class);

  midifilein_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, midifilein_proto, js_midifilein_funcs, countof(js_midifilein_funcs));
  JS_SetClassProto(ctx, js_midifilein_class_id, midifilein_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, js_midifilein_constructor, "MidiFileIn", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, midifilein_proto);
    JS_SetModuleExport(ctx, m, "MidiFileIn", ctor);
  }

  /* RtMidiIn / RtMidiOut */
  JS_NewClassID(&js_rtmidiin_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_rtmidiin_class_id, &js_rtmidiin_class);

  rtmidiin_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, rtmidiin_proto, js_rtmidiin_funcs, countof(js_rtmidiin_funcs));
  JS_SetClassProto(ctx, js_rtmidiin_class_id, rtmidiin_proto);

  JS_NewClassID(&js_rtmidiout_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_rtmidiout_class_id, &js_rtmidiout_class);

  rtmidiout_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, rtmidiout_proto, js_rtmidiout_funcs, countof(js_rtmidiout_funcs));
  JS_SetClassProto(ctx, js_rtmidiout_class_id, rtmidiout_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, js_rtmidiin_constructor, "RtMidiIn", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, rtmidiin_proto);
    JS_SetModuleExport(ctx, m, "RtMidiIn", ctor);

    ctor = JS_NewCFunction2(ctx, js_rtmidiout_constructor, "RtMidiOut", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, rtmidiout_proto);
    JS_SetModuleExport(ctx, m, "RtMidiOut", ctor);
  }

  /* RtAudio */
  JS_NewClassID(&js_rtaudio_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_rtaudio_class_id, &js_rtaudio_class);

  rtaudio_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, rtaudio_proto, js_rtaudio_funcs, countof(js_rtaudio_funcs));
  JS_SetClassProto(ctx, js_rtaudio_class_id, rtaudio_proto);

  if(m) {
    ctor = JS_NewCFunction2(ctx, js_rtaudio_constructor, "RtAudio", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, rtaudio_proto);
    JS_SetModuleExport(ctx, m, "RtAudio", ctor);
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_stk(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "BiQuad");
  JS_AddModuleExport(ctx, m, "DelayA");
  JS_AddModuleExport(ctx, m, "DelayL");
  JS_AddModuleExport(ctx, m, "Delay");
  JS_AddModuleExport(ctx, m, "Fir");
  JS_AddModuleExport(ctx, m, "FormSwep");
  JS_AddModuleExport(ctx, m, "Iir");
  JS_AddModuleExport(ctx, m, "OnePole");
  JS_AddModuleExport(ctx, m, "OneZero");
  JS_AddModuleExport(ctx, m, "PoleZero");
  JS_AddModuleExport(ctx, m, "TapDelay");
  JS_AddModuleExport(ctx, m, "TwoPole");
  JS_AddModuleExport(ctx, m, "TwoZero");
  JS_AddModuleExport(ctx, m, "ADSR");
  JS_AddModuleExport(ctx, m, "Asymp");
  JS_AddModuleExport(ctx, m, "BlitSaw");
  JS_AddModuleExport(ctx, m, "BlitSquare");
  JS_AddModuleExport(ctx, m, "Blit");
  JS_AddModuleExport(ctx, m, "Envelope");
  JS_AddModuleExport(ctx, m, "Granulate");
  JS_AddModuleExport(ctx, m, "Modulate");
  JS_AddModuleExport(ctx, m, "Noise");
  JS_AddModuleExport(ctx, m, "SineWave");
  JS_AddModuleExport(ctx, m, "StkSingWave");
  JS_AddModuleExport(ctx, m, "BandedWG");
  JS_AddModuleExport(ctx, m, "BlowBotl");
  JS_AddModuleExport(ctx, m, "BlowHole");
  JS_AddModuleExport(ctx, m, "Bowed");
  JS_AddModuleExport(ctx, m, "Brass");
  JS_AddModuleExport(ctx, m, "Clarinet");
  JS_AddModuleExport(ctx, m, "Drummer");
  JS_AddModuleExport(ctx, m, "Flute");
  JS_AddModuleExport(ctx, m, "Mandolin");
  JS_AddModuleExport(ctx, m, "Mesh2D");
  JS_AddModuleExport(ctx, m, "Plucked");
  JS_AddModuleExport(ctx, m, "Recorder");
  JS_AddModuleExport(ctx, m, "Resonate");
  JS_AddModuleExport(ctx, m, "Saxofony");
  JS_AddModuleExport(ctx, m, "Shakers");
  JS_AddModuleExport(ctx, m, "Simple");
  JS_AddModuleExport(ctx, m, "Sitar");
  JS_AddModuleExport(ctx, m, "StifKarp");
  JS_AddModuleExport(ctx, m, "VoicForm");
  JS_AddModuleExport(ctx, m, "Whistle");
  JS_AddModuleExport(ctx, m, "Chorus");
  JS_AddModuleExport(ctx, m, "Echo");
  JS_AddModuleExport(ctx, m, "FreeVerb");
  JS_AddModuleExport(ctx, m, "JCRev");
  JS_AddModuleExport(ctx, m, "LentPitShift");
  JS_AddModuleExport(ctx, m, "NRev");
  JS_AddModuleExport(ctx, m, "PitShift");
  JS_AddModuleExport(ctx, m, "PRCRev");
  JS_AddModuleExport(ctx, m, "Effect");
  JS_AddModuleExport(ctx, m, "TwinTDrum");
  JS_AddModuleExport(ctx, m, "Tr909BassDrum");
  JS_AddModuleExport(ctx, m, "Tr909Percussion");
  JS_AddModuleExport(ctx, m, "Cubic");
  JS_AddModuleExport(ctx, m, "Function");
  JS_AddModuleExport(ctx, m, "Stk");
  JS_AddModuleExport(ctx, m, "StkFrames");
  JS_AddModuleExport(ctx, m, "Generator");
  JS_AddModuleExport(ctx, m, "Filter");
  JS_AddModuleExport(ctx, m, "RtWvIn");
  JS_AddModuleExport(ctx, m, "InetWvIn");
  JS_AddModuleExport(ctx, m, "RtWvOut");
  JS_AddModuleExport(ctx, m, "InetWvOut");
  JS_AddModuleExport(ctx, m, "MidiFileIn");
  JS_AddModuleExport(ctx, m, "RtMidiIn");
  JS_AddModuleExport(ctx, m, "RtMidiOut");
  JS_AddModuleExport(ctx, m, "RtAudio");
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
