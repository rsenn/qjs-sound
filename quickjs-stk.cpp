#include <quickjs.h>
#include <cutils.h>

#include <vector>
#include <memory>
#include <cmath>
#include <cstring>
#include <cinttypes>

#include "defines.h"
#include "js-utils.hpp"
#include "js-typedarray.h"
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
#include "Guitar.h"
#include "Mandolin.h"
#include "Mesh2D.h"
#include "Modal.h"
#include "Plucked.h"
#include "Modal.h"
#include "ModalBar.h"
#include "Moog.h"
#include "Recorder.h"
#include "Resonate.h"
#include "Sampler.h"
#include "Saxofony.h"
#include "Shakers.h"
#include "Simple.h"
#include "Sitar.h"
#include "StifKarp.h"
#include "Twang.h"
#include "VoicForm.h"
#include "Whistle.h"

#include "Sampler.h"
#include "Moog.h"

/* stk::IO(realtime audio/MIDI, streaming, and MIDI file support) */
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

static JSClassID js_stkframes_class_id, js_stk_class_id, js_stkfilter_class_id, js_stkgenerator_class_id, js_stkeffect_class_id,
    js_stkfm_class_id, js_stkinstrmnt_class_id, js_stkfunction_class_id, js_stkwvin_class_id, js_stkwvout_class_id,
    js_midifilein_class_id, js_rtmidiin_class_id, js_rtmidiout_class_id, js_rtaudio_class_id,
    js_stkguitar_class_id, js_stktwang_class_id;
static JSValue stkframes_proto, stkframes_ctor, stk_proto, stk_ctor, stkfilter_proto, stkfilter_ctor, stkgenerator_proto,
    stkgenerator_ctor, stkeffect_proto, stkeffect_ctor, stkfm_proto, stkfm_ctor, stkinstrmnt_proto, stkinstrmnt_ctor,
    stkfunction_proto, stkfunction_ctor, twintdrum_proto, tr909bassdrum_proto, tr909percussion_proto, stkwvin_proto,
    rtwvin_proto, inetwvin_proto, stkwvout_proto, rtwvout_proto, inetwvout_proto, midifilein_proto, rtmidiin_proto,
    rtmidiout_proto, rtaudio_proto, rtaudio_ctor,
    stkguitar_proto, stkguitar_ctor, stktwang_proto, stktwang_ctor;

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
typedef std::shared_ptr<stk::Guitar> StkGuitarPtr;
typedef std::shared_ptr<stk::Twang> StkTwangPtr;

/* stk::StkError and RtMidiError both derive from std::exception; the
 * realtime/streaming I/O constructors below are far more likely to fail in
 * ordinary use(missing device, bad host, no MIDI ports) than the DSP
 * classes bound elsewhere in this file, so their exceptions are translated
 * into JS exceptions instead of being left to propagate as C++ exceptions. */
static JSValue
js_stk_throw(JSContext* ctx, const std::exception& e) {
  return JS_ThrowTypeError(ctx, "%s", e.what());
}

/* Stk::oStream_ is a protected static buffer that handleError() fills but
 * does not clear when it throws, so the next error message would start
 * with the text of every earlier one. */
struct StkBuffer : stk::Stk {
  static void
  clear() {
    oStream_.str(std::string());
  }
};

/* js_throw_stkerror: throws an stk::StkError as a JS exception.
 *
 *   FUNCTION_ARGUMENT -> RangeError, MEMORY_ALLOCATION -> out of memory,
 *   anything else -> InternalError with the STK message
 *
 *   stk::StkError&  err  the caught error (getType is not const)
 *
 *   returns  JS_EXCEPTION
 */
static JSValue
js_throw_stkerror(JSContext* ctx, stk::StkError& err) {
  JSValue ret;

  switch(err.getType()) {
    case stk::StkError::FUNCTION_ARGUMENT: ret = JS_ThrowRangeError(ctx, "%s", err.what()); break;
    case stk::StkError::MEMORY_ALLOCATION: ret = JS_ThrowOutOfMemory(ctx); break;
    default: ret = JS_ThrowInternalError(ctx, "%s", err.what()); break;
  }

  StkBuffer::clear();
  return ret;
}

/* js_stk_count: reads a frame count, rejecting negatives with a RangeError. */
static int
js_stk_count(JSContext* ctx, JSValueConst v, uint32_t* out) {
  int64_t n;

  if(JS_ToInt64(ctx, &n, v))
    return -1;
  if(n < 0 || n > 0x7fffffff) {
    JS_ThrowRangeError(ctx, "frame count out of range");
    return -1;
  }

  *out = n;
  return 0;
}

/* guarded: wraps a magic method dispatcher so a C++ exception thrown by
 * STK or RtMidi becomes a JS exception instead of terminating the process.
 *
 * ```c
 * JS_CFUNC_MAGIC_DEF("noteOn", 2, guarded<js_stkinstrmnt_method>, METHOD_NOTE_ON)
 * ```
 */
template <JSValue (*F)(JSContext*, JSValueConst, int, JSValueConst*, int)>
static JSValue
guarded(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  try {
    return F(ctx, this_val, argc, argv, magic);
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  } catch(const std::exception& e) {
    return js_stk_throw(ctx, e);
  }
}

/* js_stk_channel: reads the optional channel argument of tick().
 *
 *   JSValueConst  v          the argument; undefined keeps channel 0
 *   uint32_t      nchannels  channels available, from StkFrames::channels()
 *   uint32_t*     out        receives the channel
 *
 *   returns  0, or -1 with a RangeError thrown for a channel outside 0..nchannels-1
 */
static int
js_stk_channel(JSContext* ctx, JSValueConst v, uint32_t nchannels, uint32_t* out) {
  int64_t channel = 0;

  if(!JS_IsUndefined(v) && JS_ToInt64(ctx, &channel, v))
    return -1;
  if(channel < 0 || channel >= nchannels) {
    JS_ThrowRangeError(ctx, "channel %" PRId64 " out of range (0..%u)", channel, nchannels ? nchannels - 1 : 0);
    return -1;
  }

  *out = channel;
  return 0;
}

enum {
  PROP_SAMPLERATE = 0,
};

static JSValue
js_stk_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StkPtr* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = qjsx::self<StkPtr>(ctx, this_val, js_stk_class_id)))
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

  if(!(s = qjsx::self<StkPtr>(ctx, this_val, js_stk_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_SAMPLERATE: {
      double rate = 0;
      if(qjsx::args(ctx, 1, &value, rate))
        return JS_EXCEPTION;
      (*s)->setSampleRate(rate);
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSClassDef js_stk_class = {
  .class_name = "Stk",
  .finalizer = qjsx::finalize<StkPtr, js_stk_class_id>,
};

static const JSCFunctionListEntry js_stk_funcs[] = {
  JS_CGETSET_MAGIC_DEF("sampleRate", js_stk_get, js_stk_set, PROP_SAMPLERATE),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Stk", JS_PROP_CONFIGURABLE),
};

/* Several STK classes(the FM family, SingWave, Whistle, Brass, Flute, ...)
 * load .raw wavetable files from disk at construction time, resolved
 * relative to stk::Stk::rawwavePath() (default "../../rawwaves/", relative
 * to the process cwd) - a missing/incorrect path throws a raw stk::StkError
 * that terminates the process(uncaught C++ exception), not a catchable JS
 * error. Expose it so JS can point it at third_party/stk/rawwaves(or
 * wherever rawwaves/ was installed) before constructing such a class. */
static JSValue
js_stk_set_rawwave_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* path = JS_ToCString(ctx, argv[0]);
  if(!path)
    return JS_EXCEPTION;
  stk::Stk::setRawwavePath(path);
  JS_FreeCString(ctx, path);
  return JS_UNDEFINED;
}

static JSValue
js_stk_get_rawwave_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_NewString(ctx, stk::Stk::rawwavePath().c_str());
}

static const JSCFunctionListEntry js_stk_static_funcs[] = {
  JS_CFUNC_DEF("setRawwavePath", 1, js_stk_set_rawwave_path),
  JS_CFUNC_DEF("rawwavePath", 0, js_stk_get_rawwave_path),
};

static JSValue
js_stkframes_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  double value = 0;
  uint32_t nframes = 0, nchannels = 0;
  int i = 0;

  if(argc > 2) {
    if(qjsx::args_from(ctx, 0, argc, argv, value))
      return JS_EXCEPTION;
    i = 1;
  }

  if(argc > i) {
    if(qjsx::args_from(ctx, i, argc, argv, nframes))
      return JS_EXCEPTION;
    i++;
  }

  if(argc > i) {
    if(qjsx::args_from(ctx, i, argc, argv, nchannels))
      return JS_EXCEPTION;
  }

  StkFramesPtr f = (argc > 2)
    ? std::make_shared<stk::StkFrames>(value, nframes, nchannels)
    : std::make_shared<stk::StkFrames>(nframes, nchannels);

  return qjsx::wrap<StkFramesPtr>(ctx, new_target, js_stkframes_class_id, std::move(f));
}

enum {
  METHOD_RESIZE = 0,
  METHOD_INTERPOLATE,
  METHOD_GETCHANNEL,
  METHOD_SETCHANNEL,
  METHOD_TO_FLOAT32,
  METHOD_FROM_FLOAT32,
};

static JSValue
js_stkframes_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkFramesPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = qjsx::self<StkFramesPtr>(ctx, this_val, js_stkframes_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RESIZE: {
      uint32_t nframes, nchannels = 1;
      double value = 0;

      if(qjsx::args_from(ctx, 0, argc, argv, nframes))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, nchannels))
        return JS_EXCEPTION;
      if(argc > 2 && qjsx::args_from(ctx, 2, argc, argv, value))
        return JS_EXCEPTION;

      if(argc > 2)
        (*f)->resize(nframes, nchannels, value);
      else
        (*f)->resize(nframes, nchannels);

      break;
    }
    case METHOD_INTERPOLATE: {
      double frame;
      uint32_t channel = 0;

      if(qjsx::args_from(ctx, 0, argc, argv, frame))
        return JS_EXCEPTION;
      if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*f)->channels(), &channel))
        return JS_EXCEPTION;

      ret = JS_NewFloat64(ctx, (*f)->interpolate(frame, channel));
      break;
    }
    case METHOD_GETCHANNEL: {
      uint32_t channel, dstChannel;
      StkFramesPtr* a;

      if(qjsx::args_from(ctx, 0, argc, argv, channel))
        return JS_EXCEPTION;

      if(!(a = qjsx::opaque<StkFramesPtr>(argv[1], js_stkframes_class_id)))
        return JS_ThrowTypeError(ctx, "argument 2 must be StkFrames");

      if(qjsx::args_from(ctx, 2, argc, argv, dstChannel))
        return JS_EXCEPTION;

      (*f)->getChannel(channel, *a->get(), dstChannel);
      ret = JS_DupValue(ctx, argv[1]);
      break;
    }
    case METHOD_SETCHANNEL: {
      uint32_t channel, srcChannel;
      StkFramesPtr* a;

      if(qjsx::args_from(ctx, 0, argc, argv, channel))
        return JS_EXCEPTION;

      if(!(a = qjsx::opaque<StkFramesPtr>(argv[1], js_stkframes_class_id)))
        return JS_ThrowTypeError(ctx, "argument 2 must be StkFrames");

      if(qjsx::args_from(ctx, 2, argc, argv, srcChannel))
        return JS_EXCEPTION;

      (*f)->setChannel(channel, *a->get(), srcChannel);
      break;
    }
    case METHOD_TO_FLOAT32: {
      /* toFloat32(out?): copies every sample into a Float32Array, narrowing
       * from stk::StkFloat; allocates one when `out` is omitted. */
      size_t n = (*f)->size();

      if(argc > 0 && !JS_IsUndefined(argv[0])) {
        qjsx::array_view<float> view;
        if(qjsx::get_array(ctx, argv[0], view, n))
          return JS_EXCEPTION;

        for(size_t i = 0; i < n; i++)
          view[i] = static_cast<float>((**f)[i]);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      float* dst = static_cast<float*>(js_malloc(ctx, (n ? n : 1) * sizeof(float)));
      if(!dst)
        return JS_EXCEPTION;
      for(size_t i = 0; i < n; i++)
        dst[i] = static_cast<float>((**f)[i]);

      ret = js_typedarray_malloc(ctx, dst, n, TYPEDARRAY_FLOAT32);
      break;
    }
    case METHOD_FROM_FLOAT32: {
      /* fromFloat32(src): copies a Float32Array into the first src.length
       * samples, widening to stk::StkFloat; returns this. */
      qjsx::array_view<float> view;
      if(argc < 1 || qjsx::get_array(ctx, argv[0], view))
        return JS_EXCEPTION;

      size_t n = (*f)->size();
      if(view.size > n)
        return JS_ThrowRangeError(ctx, "source has %zu elements, StkFrames holds %zu", view.size, n);

      for(size_t i = 0; i < view.size; i++)
        (**f)[i] = static_cast<stk::StkFloat>(view[i]);

      ret = JS_DupValue(ctx, this_val);
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
  PROP_SAMPLE_TYPE,
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

  if(!(f = qjsx::self<StkFramesPtr>(ctx, this_val, js_stkframes_class_id)))
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
    case PROP_SAMPLE_TYPE: {
      /* element type of `buffer`: Float64Array for 'f64', Float32Array for 'f32' */
      ret = JS_NewString(ctx, sizeof(stk::StkFloat) == sizeof(double) ? "f64" : "f32");
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

  if(!(f = qjsx::self<StkFramesPtr>(ctx, this_val, js_stkframes_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_DATA_RATE: {
      double rate = 0;
      if(qjsx::args(ctx, 1, &value, rate))
        return JS_EXCEPTION;
      (*f)->setDataRate(rate);
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSClassDef js_stkframes_class = {
  .class_name = "StkFrames",
  .finalizer = qjsx::finalize<StkFramesPtr, js_stkframes_class_id>,
};

static const JSCFunctionListEntry js_stkframes_funcs[] = {
  JS_CFUNC_MAGIC_DEF("resize", 1, guarded<js_stkframes_method>, METHOD_RESIZE),
  JS_CFUNC_MAGIC_DEF("interpolate", 1, guarded<js_stkframes_method>, METHOD_INTERPOLATE),
  JS_CFUNC_MAGIC_DEF("getChannel", 3, guarded<js_stkframes_method>, METHOD_GETCHANNEL),
  JS_CFUNC_MAGIC_DEF("setChannel", 3, guarded<js_stkframes_method>, METHOD_SETCHANNEL),
  JS_CFUNC_MAGIC_DEF("toFloat32", 0, guarded<js_stkframes_method>, METHOD_TO_FLOAT32),
  JS_CFUNC_MAGIC_DEF("fromFloat32", 1, guarded<js_stkframes_method>, METHOD_FROM_FLOAT32),
  JS_CGETSET_MAGIC_DEF("size", js_stkframes_get, 0, PROP_SIZE),
  JS_CGETSET_MAGIC_DEF("empty", js_stkframes_get, 0, PROP_EMPTY),
  JS_CGETSET_MAGIC_DEF("channels", js_stkframes_get, 0, PROP_CHANNELS),
  JS_CGETSET_MAGIC_DEF("frames", js_stkframes_get, 0, PROP_FRAMES),
  JS_CGETSET_MAGIC_DEF("dataRate", js_stkframes_get, js_stkframes_set, PROP_DATA_RATE),
  JS_CGETSET_MAGIC_DEF("buffer", js_stkframes_get, 0, PROP_BUFFER),
  JS_CGETSET_MAGIC_DEF("sampleType", js_stkframes_get, 0, PROP_SAMPLE_TYPE),
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
  { "SingWave", 0, INSTANCE_SING_WAVE },
};

static JSValue
js_stkgenerator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkGeneratorPtr g;
  double arg = 0;

  if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, arg))
    return JS_EXCEPTION;

  try {
    switch(magic) {
      case INSTANCE_ADSR: {
        g = std::make_shared<stk::ADSR>();
        break;
      }
      case INSTANCE_ASYMP: {
        g = std::make_shared<stk::Asymp>();
        break;
      }
      case INSTANCE_BLIT: {
        g = std::make_shared<stk::Blit>(argc > 0 ? arg : 220.0);
        break;
      }
      case INSTANCE_BLIT_SAW: {
        g = std::make_shared<stk::BlitSaw>(argc > 0 ? arg : 220.0);
        break;
      }
      case INSTANCE_BLIT_SQUARE: {
        g = std::make_shared<stk::BlitSquare>(argc > 0 ? arg : 220.0);
        break;
      }
      case INSTANCE_ENVELOPE: {
        g = std::make_shared<stk::Envelope>();
        break;
      }
      case INSTANCE_GRANULATE: {
        if(argc > 1) {
          uint32_t nvoices = 0;
          const char* cstr = JS_ToCString(ctx, argv[1]);
          if(!cstr)
            return JS_EXCEPTION;
          std::string filename(cstr);
          JS_FreeCString(ctx, cstr);
          BOOL raw = FALSE;
          if(qjsx::args_from(ctx, 0, argc, argv, nvoices))
            return JS_EXCEPTION;
          if(argc > 2)
            raw = JS_ToBool(ctx, argv[2]);
          g = std::make_shared<stk::Granulate>(nvoices, filename, raw);
        } else {
          g = std::make_shared<stk::Granulate>();
        }
        break;
      }
      case INSTANCE_MODULATE: {
        g = std::make_shared<stk::Modulate>();
        break;
      }
      case INSTANCE_NOISE: {
        g = std::make_shared<stk::Noise>(argc > 0 ? arg : 0);
        break;
      }
      case INSTANCE_SINE_WAVE: {
        g = std::make_shared<stk::SineWave>();
        break;
      }
      case INSTANCE_SING_WAVE: {
        const char* cstr = JS_ToCString(ctx, argv[0]);
        if(!cstr)
          return JS_EXCEPTION;
        std::string filename(cstr);
        JS_FreeCString(ctx, cstr);
        BOOL raw = FALSE;
        if(argc > 1)
          raw = JS_ToBool(ctx, argv[1]);
        g = std::make_shared<stk::SingWave>(filename, raw);

        break;
      }
    }
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkGeneratorPtr>(ctx, new_target, js_stkgenerator_class_id, std::move(g));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkgenerator_ctors, magic));

  return obj;
}

enum {
  METHOD_TICK = 0,
};

static JSValue
js_stkgenerator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkGeneratorPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = qjsx::self<StkGeneratorPtr>(ctx, this_val, js_stkgenerator_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        (*f)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      /* stk::Generator only declares the StkFrames& overload virtually; each
       * subclass adds its own non-virtual single-sample tick() on top, so
       * dispatch by type for the scalar path. */
      if(js_stk_channel(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, (*f)->channelsOut(), &channel))
        return JS_EXCEPTION;

      stk::Generator* g = f->get();
      double sample = 0;
      bool matched = qjsx::dispatch<stk::Granulate, stk::ADSR, stk::Asymp, stk::BlitSaw, stk::BlitSquare, stk::Blit, stk::Envelope, stk::Modulate, stk::Noise, stk::SineWave, stk::SingWave>(g,
        [&](auto* p) {
          if constexpr(std::is_same_v<decltype(p), stk::Granulate*>)
            sample = p->tick(channel);
          else
            sample = p->tick();
        });

      if(!matched) {
        JS_ThrowTypeError(ctx, "tick requires a generator with a scalar tick()");
        return JS_EXCEPTION;
      }

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

  if(!(g = qjsx::self<StkGeneratorPtr>(ctx, this_val, js_stkgenerator_class_id)))
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

  if(!(g = qjsx::self<StkGeneratorPtr>(ctx, this_val, js_stkgenerator_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSClassDef js_stkgenerator_class = {
  .class_name = "Generator",
  .finalizer = qjsx::finalize<StkGeneratorPtr, js_stkgenerator_class_id>,
};

static const JSCFunctionListEntry js_stkgenerator_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 1, guarded<js_stkgenerator_method>, METHOD_TICK),
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

static JSValue
js_stkfilter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkFilterPtr f;

  try {
    switch(magic) {
      case INSTANCE_DELAY:
      case INSTANCE_DELAY_A:
      case INSTANCE_DELAY_L: {
        double delay = 0, maxDelay = 4095;
        if(qjsx::args_from(ctx, 0, argc, argv, delay))
          return JS_EXCEPTION;
        if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, maxDelay))
          return JS_EXCEPTION;

        switch(magic) {
          case INSTANCE_DELAY: {
            f = std::make_shared<stk::Delay>();
            break;
          }
          case INSTANCE_DELAY_A: {
            f = std::make_shared<stk::DelayA>(delay, maxDelay);
            break;
          }
          case INSTANCE_DELAY_L: {
            f = std::make_shared<stk::DelayL>(delay, maxDelay);
            break;
          }
        }

        break;
      }

      case INSTANCE_FIR: {
        if(argc > 0) {
          std::vector<double> coeff;
          if(qjsx::read_array(ctx, argv[0], coeff))
            return JS_EXCEPTION;
          f = std::make_shared<stk::Fir>(coeff);
        } else {
          f = std::make_shared<stk::Fir>();
        }

        break;
      }
      case INSTANCE_IIR: {
        std::vector<double> acoeff, bcoeff;

        if(argc > 0) {
          if(qjsx::read_array(ctx, argv[0], bcoeff) || (argc > 1 && qjsx::read_array(ctx, argv[1], acoeff)))
            return JS_EXCEPTION;

          f = std::make_shared<stk::Iir>(bcoeff, acoeff);
        } else {
          f = std::make_shared<stk::Iir>();
        }

        break;
      }
      case INSTANCE_TAP_DELAY: {
        std::vector<unsigned long> taps;

        if(argc > 0) {
          uint32_t maxDelay = 4095;
          std::vector<uint32_t> tapValues;

          if(qjsx::read_array(ctx, argv[0], tapValues))
            return JS_EXCEPTION;
          taps.assign(tapValues.begin(), tapValues.end());
          if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, maxDelay))
            return JS_EXCEPTION;

          f = std::make_shared<stk::TapDelay>(taps, maxDelay);
        } else {
          f = std::make_shared<stk::TapDelay>();
        }

        break;
      }

      default: {
        double arg = 0;
        if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, arg))
          return JS_EXCEPTION;

        switch(magic) {
          case INSTANCE_BIQUAD: {
            f = std::make_shared<stk::BiQuad>();
            break;
          }
          case INSTANCE_FORM_SWEP: {
            f = std::make_shared<stk::FormSwep>();
            break;
          }
          case INSTANCE_ONE_POLE: {
            f = std::make_shared<stk::OnePole>(arg);
            break;
          }
          case INSTANCE_ONE_ZERO: {
            f = std::make_shared<stk::OneZero>(arg);
            break;
          }
          case INSTANCE_POLE_ZERO: {
            f = std::make_shared<stk::PoleZero>();
            break;
          }
          case INSTANCE_TWO_POLE: {
            f = std::make_shared<stk::TwoPole>();
            break;
          }
          case INSTANCE_TWO_ZERO: {
            f = std::make_shared<stk::TwoZero>();
            break;
          }
        }

        break;
      }
    }
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkFilterPtr>(ctx, new_target, js_stkfilter_class_id, std::move(f));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkfilter_ctors, magic));

  return obj;
}

enum {
  METHOD_FILTER_TICK = 0,
};

static JSValue
js_stkfilter_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkFilterPtr* f;
  JSValue ret = JS_UNDEFINED;

  if(!(f = qjsx::self<StkFilterPtr>(ctx, this_val, js_stkfilter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FILTER_TICK: {
      StkFramesPtr* a;

      if((a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        uint32_t channel = 0;
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        (*f)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      double input = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, input))
        return JS_EXCEPTION;

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

static JSClassDef js_stkfilter_class = {
  .class_name = "Filter",
  .finalizer = qjsx::finalize<StkFilterPtr, js_stkfilter_class_id>,
};

static const JSCFunctionListEntry js_stkfilter_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 1, guarded<js_stkfilter_method>, METHOD_FILTER_TICK),
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

static JSValue
js_stkeffect_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkEffectPtr e;
  double arg = 0;
  int32_t tmax = stk::RT_BUFFER_SIZE;

  if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, arg))
    return JS_EXCEPTION;
  if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, tmax))
    return JS_EXCEPTION;

  try {
    switch(magic) {
      case INSTANCE_CHORUS: {
        if(argc > 0 && arg <= 0)
          return JS_ThrowRangeError(ctx, "Chorus delay cannot be zero");
        e = std::make_shared<stk::Chorus>(argc > 0 ? arg : 6000);
        break;
      }
      case INSTANCE_ECHO: {
        if(argc > 0 && arg <= 0)
          return JS_ThrowRangeError(ctx, "Echo delay cannot be zero");
        e = std::make_shared<stk::Echo>(argc > 0 ? arg : stk::Stk::sampleRate());
        break;
      }
      case INSTANCE_FREEVERB: {
        e = std::make_shared<stk::FreeVerb>();
        break;
      }
      case INSTANCE_JCREV: {
        e = std::make_shared<stk::JCRev>(argc > 0 ? arg : 1.0);
        break;
      }
      case INSTANCE_LENTPITSHIFT: {
        e = std::make_shared<stk::LentPitShift>(argc > 0 ? arg : 1.0, tmax);
        break;
      }
      case INSTANCE_NREV: {
        e = std::make_shared<stk::NRev>(argc > 0 ? arg : 1.0);
        break;
      }
      case INSTANCE_PITSHIFT: {
        e = std::make_shared<stk::PitShift>();
        break;
      }
      case INSTANCE_PRCREV: {
        e = std::make_shared<stk::PRCRev>(argc > 0 ? arg : 1.0);
        break;
      }
    }
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkEffectPtr>(ctx, new_target, js_stkeffect_class_id, std::move(e));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkeffect_ctors, magic));

  return obj;
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

  if(!(e = qjsx::self<StkEffectPtr>(ctx, this_val, js_stkeffect_class_id)))
    return JS_EXCEPTION;

  stk::Effect* eff = e->get();

  switch(magic) {
    case METHOD_EFFECT_TICK: {
      StkFramesPtr* a;

      if((a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        uint32_t channel = 0;
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        /* Effect subclasses don't share a common virtual tick() either, so
         * dispatch by type same as the scalar path below. */
        qjsx::dispatch<stk::FreeVerb, stk::JCRev, stk::PRCRev, stk::NRev, stk::Chorus, stk::Echo, stk::PitShift, stk::LentPitShift>(eff,
          [&](auto* p) { p->tick(*a->get(), channel); });

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      double in1 = 0, in2 = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, in1))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, in2))
        return JS_EXCEPTION;

      /* Effect subclasses don't share a common tick() signature(some are
       * mono, some stereo, FreeVerb takes two inputs), so dispatch by type
       * and then read back through the(shared) lastFrame() accessor. */
      qjsx::dispatch<stk::FreeVerb, stk::JCRev, stk::PRCRev, stk::NRev, stk::Chorus, stk::Echo, stk::PitShift, stk::LentPitShift>(eff,
        [&](auto* p) {
          if constexpr(std::is_same_v<decltype(p), stk::FreeVerb*>)
            p->tick(in1, in2);
          else
            p->tick(in1);
        });

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
      if(qjsx::args_from(ctx, 0, argc, argv, mix))
        return JS_EXCEPTION;
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

static JSClassDef js_stkeffect_class = {
  .class_name = "Effect",
  .finalizer = qjsx::finalize<StkEffectPtr, js_stkeffect_class_id>,
};

static const JSCFunctionListEntry js_stkeffect_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 1, guarded<js_stkeffect_method>, METHOD_EFFECT_TICK),
  JS_CFUNC_MAGIC_DEF("setEffectMix", 1, guarded<js_stkeffect_method>, METHOD_EFFECT_SET_MIX),
  JS_CFUNC_MAGIC_DEF("clear", 0, guarded<js_stkeffect_method>, METHOD_EFFECT_CLEAR),
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

static const qjsx::ctor_def stkfm_ctors[] = {
  { "BeeThree", 0, INSTANCE_BEETHREE },
  { "FMVoices", 0, INSTANCE_FMVOICES },
  { "HevyMetl", 0, INSTANCE_HEVYMETL },
  { "PercFlut", 0, INSTANCE_PERCFLUT },
  { "Rhodey", 0, INSTANCE_RHODEY },
  { "TubeBell", 0, INSTANCE_TUBEBELL },
  { "Wurley", 0, INSTANCE_WURLEY },
};

static JSValue
js_stkfm_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkFMPtr fm;

  try {
    switch(magic) {
      case INSTANCE_BEETHREE: {
        fm = std::make_shared<stk::BeeThree>();
        break;
      }
      case INSTANCE_FMVOICES: {
        fm = std::make_shared<stk::FMVoices>();
        break;
      }
      case INSTANCE_HEVYMETL: {
        fm = std::make_shared<stk::HevyMetl>();
        break;
      }
      case INSTANCE_PERCFLUT: {
        fm = std::make_shared<stk::PercFlut>();
        break;
      }
      case INSTANCE_RHODEY: {
        fm = std::make_shared<stk::Rhodey>();
        break;
      }
      case INSTANCE_TUBEBELL: {
        fm = std::make_shared<stk::TubeBell>();
        break;
      }
      case INSTANCE_WURLEY: {
        fm = std::make_shared<stk::Wurley>();
        break;
      }
    }
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkFMPtr>(ctx, new_target, js_stkfm_class_id, std::move(fm));

  if(JS_IsException(obj))
    return obj;
  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkfm_ctors, magic));

  return obj;
}

enum {
  METHOD_FM_TICK = 0,
  METHOD_FM_NOTE_ON,
  METHOD_FM_NOTE_OFF,
  METHOD_FM_CONTROL_CHANGE,
};

/* stk::FM extends stk::Instrmnt(FM.h), so tick/noteOn/noteOff/
 * controlChange are the same inherited Instrmnt API js_stkinstrmnt_method
 * dispatches - mirrored here rather than shared since the opaque payload
 * type(StkFMPtr vs StkInstrmntPtr) differs. */
static JSValue
js_stkfm_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkFMPtr* fm;
  JSValue ret = JS_UNDEFINED;

  if(!(fm = qjsx::self<StkFMPtr>(ctx, this_val, js_stkfm_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FM_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        (*fm)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      if(js_stk_channel(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, (*fm)->channelsOut(), &channel))
        return JS_EXCEPTION;

      ret = JS_NewFloat64(ctx, (*fm)->tick(channel));
      break;
    }
    case METHOD_FM_NOTE_ON: {
      double frequency = 0, amplitude = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, frequency))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, amplitude))
        return JS_EXCEPTION;

      (*fm)->noteOn(frequency, amplitude);
      break;
    }
    case METHOD_FM_NOTE_OFF: {
      double amplitude = 0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, amplitude))
        return JS_EXCEPTION;

      (*fm)->noteOff(amplitude);
      break;
    }
    case METHOD_FM_CONTROL_CHANGE: {
      int32_t number = 0;
      double value = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, number))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, value))
        return JS_EXCEPTION;

      (*fm)->controlChange(number, value);
      break;
    }
  }

  return ret;
}

static JSClassDef js_stkfm_class = {
  .class_name = "StkFM",
  .finalizer = qjsx::finalize<StkFMPtr, js_stkfm_class_id>,
};

static const JSCFunctionListEntry js_stkfm_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 0, guarded<js_stkfm_method>, METHOD_FM_TICK),
  JS_CFUNC_MAGIC_DEF("noteOn", 2, guarded<js_stkfm_method>, METHOD_FM_NOTE_ON),
  JS_CFUNC_MAGIC_DEF("noteOff", 1, guarded<js_stkfm_method>, METHOD_FM_NOTE_OFF),
  JS_CFUNC_MAGIC_DEF("controlChange", 2, guarded<js_stkfm_method>, METHOD_FM_CONTROL_CHANGE),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkFM", JS_PROP_CONFIGURABLE),
};

/* stk::Guitar is a digital waveguide string model that does NOT inherit from
 * stk::Instrmnt (its noteOn/noteOff/controlChange take a per-string argument
 * and it has no breath/pressure controls), so it gets its own class here
 * rather than joining the stkinstrmnt family. */
typedef std::shared_ptr<stk::Guitar> StkGuitarPtr;

enum {
  INSTANCE_GUITAR = 0,
};

static JSValue
js_stkguitar_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkGuitarPtr g;
  uint32_t nStrings = 6;
  const char* bodyfile = "";

  if(argc > 0 && qjsx::present(argc, argv, 0))
    if(qjsx::args_from(ctx, 0, argc, argv, nStrings))
      return JS_EXCEPTION;
  if(argc > 1) {
    bodyfile = JS_ToCString(ctx, argv[1]);
    if(!bodyfile)
      return JS_EXCEPTION;
  }

  try {
    g = std::make_shared<stk::Guitar>(nStrings, bodyfile);
  } catch(stk::StkError& err) {
    if(argc > 1)
      JS_FreeCString(ctx, bodyfile);
    return js_throw_stkerror(ctx, err);
  }
  if(argc > 1)
    JS_FreeCString(ctx, bodyfile);

  JSValue obj = qjsx::wrap<StkGuitarPtr>(ctx, new_target, js_stkguitar_class_id, std::move(g));

  if(JS_IsException(obj))
    return obj;
  js_set_tostringtag(ctx, obj, "Guitar");
  return obj;
}

enum {
  METHOD_GUITAR_TICK = 0,
  METHOD_GUITAR_NOTE_ON,
  METHOD_GUITAR_NOTE_OFF,
  METHOD_GUITAR_CONTROL_CHANGE,
};

static JSValue
js_stkguitar_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkGuitarPtr* g;
  JSValue ret = JS_UNDEFINED;

  if(!(g = qjsx::self<StkGuitarPtr>(ctx, this_val, js_stkguitar_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_GUITAR_TICK: {
      double input = 0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, input))
        return JS_EXCEPTION;

      ret = JS_NewFloat64(ctx, (*g)->tick(input));
      break;
    }
    case METHOD_GUITAR_NOTE_ON: {
      double frequency = 0, amplitude = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, frequency))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, amplitude))
        return JS_EXCEPTION;

      (*g)->noteOn(frequency, amplitude);
      break;
    }
    case METHOD_GUITAR_NOTE_OFF: {
      double amplitude = 0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, amplitude))
        return JS_EXCEPTION;

      (*g)->noteOff(amplitude);
      break;
    }
    case METHOD_GUITAR_CONTROL_CHANGE: {
      int32_t number = 0;
      double value = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, number))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, value))
        return JS_EXCEPTION;

      (*g)->controlChange(number, value);
      break;
    }
  }

  return ret;
}

static JSClassDef js_stkguitar_class = {
  .class_name = "Guitar",
  .finalizer = qjsx::finalize<StkGuitarPtr, js_stkguitar_class_id>,
};

static const JSCFunctionListEntry js_stkguitar_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 0, guarded<js_stkguitar_method>, METHOD_GUITAR_TICK),
  JS_CFUNC_MAGIC_DEF("noteOn", 2, guarded<js_stkguitar_method>, METHOD_GUITAR_NOTE_ON),
  JS_CFUNC_MAGIC_DEF("noteOff", 1, guarded<js_stkguitar_method>, METHOD_GUITAR_NOTE_OFF),
  JS_CFUNC_MAGIC_DEF("controlChange", 2, guarded<js_stkguitar_method>, METHOD_GUITAR_CONTROL_CHANGE),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Guitar", JS_PROP_CONFIGURABLE),
};

static const qjsx::ctor_def stk_guitar_ctors[] = {
  { "Guitar", 0, INSTANCE_GUITAR },
};

/* stk::Twang inherits from Stk (not Instrmnt), so it gets its own class like Guitar. */
typedef std::shared_ptr<stk::Twang> StkTwangPtr;

enum {
  INSTANCE_TWANG = 0,
};

static JSValue
js_stktwang_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkTwangPtr t;
  double arg = 50.0;
  if(argc > 0 && qjsx::present(argc, argv, 0))
    if(qjsx::args_from(ctx, 0, argc, argv, arg))
      return JS_EXCEPTION;

  try {
    t = std::make_shared<stk::Twang>(arg);
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkTwangPtr>(ctx, new_target, js_stktwang_class_id, std::move(t));

  if(JS_IsException(obj))
    return obj;
  js_set_tostringtag(ctx, obj, "Twang");
  return obj;
}

enum {
  METHOD_TWANG_TICK = 0,
  METHOD_TWANG_SET_FREQUENCY,
  METHOD_TWANG_SET_PLUCK_POSITION,
  METHOD_TWANG_SET_LOOP_GAIN,
};

static JSValue
js_stktwang_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkTwangPtr* t;
  JSValue ret = JS_UNDEFINED;

  if(!(t = qjsx::self<StkTwangPtr>(ctx, this_val, js_stktwang_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_TWANG_TICK: {
      double input = 0;
      if(argc > 0 && qjsx::present(argc, argv, 0))
        if(qjsx::args_from(ctx, 0, argc, argv, input))
          return JS_EXCEPTION;

      ret = JS_NewFloat64(ctx, (*t)->tick(input));
      break;
    }
    case METHOD_TWANG_SET_FREQUENCY: {
      double frequency = 0;
      if(qjsx::args(ctx, argc, argv, frequency))
        return JS_EXCEPTION;
      (*t)->setFrequency(frequency);
      break;
    }
    case METHOD_TWANG_SET_PLUCK_POSITION: {
      double position = 0;
      if(qjsx::args(ctx, argc, argv, position))
        return JS_EXCEPTION;
      (*t)->setPluckPosition(position);
      break;
    }
    case METHOD_TWANG_SET_LOOP_GAIN: {
      double loopGain = 0;
      if(qjsx::args(ctx, argc, argv, loopGain))
        return JS_EXCEPTION;
      (*t)->setLoopGain(loopGain);
      break;
    }
  }

  return ret;
}

static void
js_stktwang_finalizer(JSRuntime* rt, JSValue val) {
  StkTwangPtr* t;

  if((t = static_cast<StkTwangPtr*>(JS_GetOpaque(val, js_stktwang_class_id)))) {
    t->~StkTwangPtr();
    js_free_rt(rt, t);
  }
}

static JSClassDef js_stktwang_class = {
  .class_name = "Twang",
  .finalizer = js_stktwang_finalizer,
};

static const JSCFunctionListEntry js_stktwang_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 0, guarded<js_stktwang_method>, METHOD_TWANG_TICK),
  JS_CFUNC_MAGIC_DEF("setFrequency", 1, guarded<js_stktwang_method>, METHOD_TWANG_SET_FREQUENCY),
  JS_CFUNC_MAGIC_DEF("setPluckPosition", 1, guarded<js_stktwang_method>, METHOD_TWANG_SET_PLUCK_POSITION),
  JS_CFUNC_MAGIC_DEF("setLoopGain", 1, guarded<js_stktwang_method>, METHOD_TWANG_SET_LOOP_GAIN),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Twang", JS_PROP_CONFIGURABLE),
};

static const qjsx::ctor_def stk_twang_ctors[] = {
  { "Twang", 0, INSTANCE_TWANG },
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
  INSTANCE_MODALBAR,
  INSTANCE_MOOG,
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
  { "ModalBar", 0, INSTANCE_MODALBAR },
  { "Moog", 0, INSTANCE_MOOG },
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

static JSValue
js_stkinstrmnt_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkInstrmntPtr i;
  double arg = 0;
  if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, arg))
    return JS_EXCEPTION;

  try {
    switch(magic) {
      case INSTANCE_BANDEDWG: {
        i = std::make_shared<stk::BandedWG>();
        break;
      }
      case INSTANCE_BLOWBOTL: {
        i = std::make_shared<stk::BlowBotl>();
        break;
      }
      case INSTANCE_BLOWHOLE: {
        i = std::make_shared<stk::BlowHole>(arg);
        break;
      }
      case INSTANCE_BOWED: {
        i = std::make_shared<stk::Bowed>(argc > 0 ? arg : 8.0);
        break;
      }
      case INSTANCE_BRASS: {
        i = std::make_shared<stk::Brass>(argc > 0 ? arg : 8.0);
        break;
      }
      case INSTANCE_CLARINET: {
        i = std::make_shared<stk::Clarinet>(argc > 0 ? arg : 8.0);
        break;
      }
      case INSTANCE_DRUMMER: {
        i = std::make_shared<stk::Drummer>();
        break;
      }
      case INSTANCE_FLUTE: {
        i = std::make_shared<stk::Flute>(arg);
        break;
      }
      case INSTANCE_MANDOLIN: {
        i = std::make_shared<stk::Mandolin>(arg);
        break;
      }
      case INSTANCE_MESH2D: {
        uint32_t nx = 0, ny = 0;
        if(qjsx::args_from(ctx, 0, argc, argv, nx) || qjsx::args_from(ctx, 1, argc, argv, ny))
          return JS_EXCEPTION;
        i = std::make_shared<stk::Mesh2D>(nx, ny);
        break;
      }
      case INSTANCE_MODALBAR: {
        i = std::make_shared<stk::ModalBar>();
        break;
      }
      case INSTANCE_MOOG: {
        i = std::make_shared<stk::Moog>();
        break;
      }
      case INSTANCE_PLUCKED: {
        i = std::make_shared<stk::Plucked>(argc > 0 ? arg : 10.0);
        break;
      }
      case INSTANCE_RECORDER: {
        i = std::make_shared<stk::Recorder>();
        break;
      }
      case INSTANCE_RESONATE: {
        i = std::make_shared<stk::Resonate>();
        break;
      }
      // case INSTANCE_SAMPLER: { i = std::make_shared<stk::Sampler>(); break; }
      case INSTANCE_SAXOFONY: {
        i = std::make_shared<stk::Saxofony>(arg);
        break;
      }
      case INSTANCE_SHAKERS: {
        i = std::make_shared<stk::Shakers>(argc > 0 ? arg : 0);
        break;
      }
      case INSTANCE_SIMPLE: {
        i = std::make_shared<stk::Simple>();
        break;
      }
      case INSTANCE_SITAR: {
        i = std::make_shared<stk::Sitar>(argc > 0 ? arg : 8.0);
        break;
      }
      case INSTANCE_STIFKARP: {
        i = std::make_shared<stk::StifKarp>(argc > 0 ? arg : 10.0);
        break;
      }
      case INSTANCE_VOICFORM: {
        i = std::make_shared<stk::VoicForm>();
        break;
      }
      case INSTANCE_WHISTLE: {
        i = std::make_shared<stk::Whistle>();
        break;
      }
    }
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkInstrmntPtr>(ctx, new_target, js_stkinstrmnt_class_id, std::move(i));

  if(JS_IsException(obj))
    return obj;
  return obj;
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

  if(!(i = qjsx::self<StkInstrmntPtr>(ctx, this_val, js_stkinstrmnt_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_INSTRMNT_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        (*i)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      if(js_stk_channel(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, (*i)->channelsOut(), &channel))
        return JS_EXCEPTION;

      ret = JS_NewFloat64(ctx, (*i)->tick(channel));
      break;
    }
    case METHOD_INSTRMNT_NOTE_ON: {
      double frequency = 0, amplitude = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, frequency))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, amplitude))
        return JS_EXCEPTION;

      (*i)->noteOn(frequency, amplitude);
      break;
    }
    case METHOD_INSTRMNT_NOTE_OFF: {
      double amplitude = 0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, amplitude))
        return JS_EXCEPTION;

      (*i)->noteOff(amplitude);
      break;
    }
    case METHOD_INSTRMNT_CONTROL_CHANGE: {
      int32_t number = 0;
      double value = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, number))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, value))
        return JS_EXCEPTION;

      (*i)->controlChange(number, value);
      break;
    }
  }

  return ret;
}

static JSClassDef js_stkinstrmnt_class = {
  .class_name = "StkInstrmnt",
  .finalizer = qjsx::finalize<StkInstrmntPtr, js_stkinstrmnt_class_id>,
};

static const JSCFunctionListEntry js_stkinstrmnt_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 0, guarded<js_stkinstrmnt_method>, METHOD_INSTRMNT_TICK),
  JS_CFUNC_MAGIC_DEF("noteOn", 2, guarded<js_stkinstrmnt_method>, METHOD_INSTRMNT_NOTE_ON),
  JS_CFUNC_MAGIC_DEF("noteOff", 1, guarded<js_stkinstrmnt_method>, METHOD_INSTRMNT_NOTE_OFF),
  JS_CFUNC_MAGIC_DEF("controlChange", 2, guarded<js_stkinstrmnt_method>, METHOD_INSTRMNT_CONTROL_CHANGE),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkInstrmnt", JS_PROP_CONFIGURABLE),
};

enum {
  INSTANCE_CUBIC = 0,
};

static const qjsx::ctor_def stkfunction_ctors[] = {
  { "Cubic", 0, INSTANCE_CUBIC },
};

static JSValue
js_stkfunction_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkFunctionPtr g;

  try {
    switch(magic) {
      case INSTANCE_CUBIC: {
        g = std::make_shared<stk::Cubic>();
        break;
      }
    }
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkFunctionPtr>(ctx, new_target, js_stkfunction_class_id, std::move(g));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkfunction_ctors, magic));

  return obj;
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

  if(!(g = qjsx::self<StkFunctionPtr>(ctx, this_val, js_stkfunction_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_FUNCTION_TICK: {
      StkFramesPtr* a;

      if((a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        uint32_t channel = 0;
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        /* stk::Function doesn't declare a virtual StkFrames& tick(); Cubic is
         * presently the only bound subclass, so dispatch to its overload
         * directly, same as the Cubic-specific controls below. */
        stk::Cubic* cubic = qjsx::require<stk::Cubic>(ctx, g->get(), "tick needs a Cubic");
        if(!cubic)
          return JS_EXCEPTION;
        cubic->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      double input = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, input))
        return JS_EXCEPTION;

      ret = JS_NewFloat64(ctx, (*g)->tick(input));
      break;
    }
    /* Cubic-specific shaping controls(Cubic is presently the only bound
     * stk::Function subclass). */
    case METHOD_FUNCTION_SET_A1:
    case METHOD_FUNCTION_SET_A2:
    case METHOD_FUNCTION_SET_A3:
    case METHOD_FUNCTION_SET_GAIN:
    case METHOD_FUNCTION_SET_THRESHOLD: {
      double value = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, value))
        return JS_EXCEPTION;

      stk::Cubic* cubic = qjsx::require<stk::Cubic>(ctx, g->get(), "setA1/setA2/setA3/setGain/setThreshold needs a Cubic");
      if(!cubic)
        return JS_EXCEPTION;

      switch(magic) {
        case METHOD_FUNCTION_SET_A1: cubic->setA1(value); break;
        case METHOD_FUNCTION_SET_A2: cubic->setA2(value); break;
        case METHOD_FUNCTION_SET_A3: cubic->setA3(value); break;
        case METHOD_FUNCTION_SET_GAIN: cubic->setGain(value); break;
        case METHOD_FUNCTION_SET_THRESHOLD: cubic->setThreshold(value); break;
      }

      break;
    }
  }

  return ret;
}

static JSClassDef js_stkfunction_class = {
  .class_name = "Function",
  .finalizer = qjsx::finalize<StkFunctionPtr, js_stkfunction_class_id>,
};

static const JSCFunctionListEntry js_stkfunction_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 1, guarded<js_stkfunction_method>, METHOD_FUNCTION_TICK),
  JS_CFUNC_MAGIC_DEF("setA1", 1, guarded<js_stkfunction_method>, METHOD_FUNCTION_SET_A1),
  JS_CFUNC_MAGIC_DEF("setA2", 1, guarded<js_stkfunction_method>, METHOD_FUNCTION_SET_A2),
  JS_CFUNC_MAGIC_DEF("setA3", 1, guarded<js_stkfunction_method>, METHOD_FUNCTION_SET_A3),
  JS_CFUNC_MAGIC_DEF("setGain", 1, guarded<js_stkfunction_method>, METHOD_FUNCTION_SET_GAIN),
  JS_CFUNC_MAGIC_DEF("setThreshold", 1, guarded<js_stkfunction_method>, METHOD_FUNCTION_SET_THRESHOLD),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Function", JS_PROP_CONFIGURABLE),
};

/* TwinTDrum, Tr909BassDrum, the DRIVE_* enum and analog_drive() live in
 * analog-drums.hpp -- see that file for the DSP model notes. */
#include "analog-drums.hpp"

static JSValue
js_twintdrum_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  double frequency = 200.0;
  if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, frequency))
    return JS_EXCEPTION;

  StkInstrmntPtr i;

  try {
    i = std::make_shared<TwinTDrum>(frequency);
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkInstrmntPtr>(ctx, new_target, js_stkinstrmnt_class_id, std::move(i));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, "TwinTDrum");
  return obj;
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

  if(!(p = qjsx::self<StkInstrmntPtr>(ctx, this_val, js_stkinstrmnt_class_id)))
    return JS_EXCEPTION;

  TwinTDrum* d = qjsx::require<TwinTDrum>(ctx, p->get(), "not a TwinTDrum");
  if(!d)
    return JS_EXCEPTION;

  double a = 0, b = 0;
  if(qjsx::args_from(ctx, 0, argc, argv, a, b))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_TWINT_SET_FREQUENCY: d->setFrequency(a); break;
    case METHOD_TWINT_SET_DECAY: d->setDecay(a); break;
    case METHOD_TWINT_SET_DRIVE: d->setDrive(a, argc > 1 ? (int)b : DRIVE_TANH); break;
    case METHOD_TWINT_SET_PITCH_DROP: d->setPitchDrop(a, argc > 1 ? b : 0.03); break;
    case METHOD_TWINT_SET_SECONDARY: d->setSecondary(a, argc > 1 ? b : 0.0); break;
    case METHOD_TWINT_SET_CLICK: d->setClick(a); break;
    case METHOD_TWINT_STRIKE: d->strike(argc > 0 ? a : 1.0); break;
    case METHOD_TWINT_RENDER: {
      if(a < 0 || a > 0x7fffffff)
        return JS_ThrowRangeError(ctx, "frame count out of range");
      uint32_t n = (uint32_t)a;
      double amp = argc > 1 ? b : 1.0;
      StkFramesPtr f = std::make_shared<stk::StkFrames>(n, 1);
      stk::StkFrames& fr = *f;
      d->strike(amp);
      for(uint32_t k = 0; k < n; k++)
        fr[k] = d->tick();
      ret = qjsx::wrap_proto<StkFramesPtr>(ctx, stkframes_proto, js_stkframes_class_id, std::move(f));
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_twintdrum_funcs[] = {
  JS_CFUNC_MAGIC_DEF("setFrequency", 1, guarded<js_twintdrum_method>, METHOD_TWINT_SET_FREQUENCY),
  JS_CFUNC_MAGIC_DEF("setDecay", 1, guarded<js_twintdrum_method>, METHOD_TWINT_SET_DECAY),
  JS_CFUNC_MAGIC_DEF("setDrive", 1, guarded<js_twintdrum_method>, METHOD_TWINT_SET_DRIVE),
  JS_CFUNC_MAGIC_DEF("setPitchDrop", 2, guarded<js_twintdrum_method>, METHOD_TWINT_SET_PITCH_DROP),
  JS_CFUNC_MAGIC_DEF("setSecondary", 2, guarded<js_twintdrum_method>, METHOD_TWINT_SET_SECONDARY),
  JS_CFUNC_MAGIC_DEF("setClick", 1, guarded<js_twintdrum_method>, METHOD_TWINT_SET_CLICK),
  JS_CFUNC_MAGIC_DEF("strike", 1, guarded<js_twintdrum_method>, METHOD_TWINT_STRIKE),
  JS_CFUNC_MAGIC_DEF("render", 2, guarded<js_twintdrum_method>, METHOD_TWINT_RENDER),
};

static JSValue
js_tr909bassdrum_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  StkInstrmntPtr i;

  try {
    i = std::make_shared<Tr909BassDrum>();
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkInstrmntPtr>(ctx, new_target, js_stkinstrmnt_class_id, std::move(i));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, "Tr909BassDrum");
  return obj;
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

  if(!(p = qjsx::self<StkInstrmntPtr>(ctx, this_val, js_stkinstrmnt_class_id)))
    return JS_EXCEPTION;

  Tr909BassDrum* d = qjsx::require<Tr909BassDrum>(ctx, p->get(), "not a Tr909BassDrum");
  if(!d)
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_TR909_SET_PITCH_ENV: {
      double start = 0, end = 0, decay = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, start, end, decay))
        return JS_EXCEPTION;
      d->setPitchEnvelope(start, end, decay, argc > 3 ? parse_curve_linear(ctx, argv[3]) : false);
      break;
    }
    case METHOD_TR909_SET_PITCH_SPIKE: {
      double semitones = 0, time = 0.005;
      if(qjsx::args_from(ctx, 0, argc, argv, semitones))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, time))
        return JS_EXCEPTION;
      d->setPitchSpike(semitones, time);
      break;
    }
    case METHOD_TR909_SET_AMP_ENV: {
      double decay = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, decay))
        return JS_EXCEPTION;
      d->setAmpEnvelope(decay, argc > 1 ? parse_curve_linear(ctx, argv[1]) : false);
      break;
    }
    case METHOD_TR909_SET_PUNCH: {
      double amount = 0, time = 0.01;
      if(qjsx::args_from(ctx, 0, argc, argv, amount))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, time))
        return JS_EXCEPTION;
      d->setPunch(amount, time);
      break;
    }
    case METHOD_TR909_SET_DRIVE: {
      double amount = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, amount))
        return JS_EXCEPTION;
      d->setDrive(amount, argc > 1 ? parse_drive_type(ctx, argv[1]) : DRIVE_TANH);
      break;
    }
    case METHOD_TR909_SET_WAVEFORM: {
      d->setWaveform(parse_tr909_waveform(ctx, argv[0]));
      break;
    }
    case METHOD_TR909_SET_TONE: {
      double cutoff = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, cutoff))
        return JS_EXCEPTION;
      d->setTone(cutoff);
      break;
    }
    case METHOD_TR909_SET_TONE_RESONANCE: {
      double amount = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, amount))
        return JS_EXCEPTION;
      d->setToneResonance(amount);
      break;
    }
    case METHOD_TR909_SET_CLICK: {
      double level = 0, decay = 0.04;
      if(qjsx::args_from(ctx, 0, argc, argv, level))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, decay))
        return JS_EXCEPTION;
      d->setClick(level, decay);
      break;
    }
    case METHOD_TR909_SET_SUB: {
      double mix = 0, octave = 1.0;
      if(qjsx::args_from(ctx, 0, argc, argv, mix))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, octave))
        return JS_EXCEPTION;
      d->setSub(mix, octave);
      break;
    }
    case METHOD_TR909_SET_TUNE: {
      double t = 1.0;
      if(qjsx::args_from(ctx, 0, argc, argv, t))
        return JS_EXCEPTION;
      d->setTune(t);
      break;
    }
    case METHOD_TR909_TRIGGER: {
      double v = 1.0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, v))
        return JS_EXCEPTION;
      d->trigger(v);
      break;
    }
    case METHOD_TR909_RENDER: {
      uint32_t n = 0;
      double v = 1.0;
      if(argc < 1 || js_stk_count(ctx, argv[0], &n))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, v))
        return JS_EXCEPTION;

      StkFramesPtr f = std::make_shared<stk::StkFrames>(n, 1);
      stk::StkFrames& fr = *f;
      d->trigger(v);
      for(uint32_t k = 0; k < n; k++)
        fr[k] = d->tick();
      ret = qjsx::wrap_proto<StkFramesPtr>(ctx, stkframes_proto, js_stkframes_class_id, std::move(f));
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_tr909bassdrum_funcs[] = {
  JS_CFUNC_MAGIC_DEF("setPitchEnvelope", 4, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_PITCH_ENV),
  JS_CFUNC_MAGIC_DEF("setPitchSpike", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_PITCH_SPIKE),
  JS_CFUNC_MAGIC_DEF("setAmpEnvelope", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_AMP_ENV),
  JS_CFUNC_MAGIC_DEF("setPunch", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_PUNCH),
  JS_CFUNC_MAGIC_DEF("setDrive", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_DRIVE),
  JS_CFUNC_MAGIC_DEF("setWaveform", 1, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_WAVEFORM),
  JS_CFUNC_MAGIC_DEF("setTone", 1, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_TONE),
  JS_CFUNC_MAGIC_DEF("setToneResonance", 1, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_TONE_RESONANCE),
  JS_CFUNC_MAGIC_DEF("setClick", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_CLICK),
  JS_CFUNC_MAGIC_DEF("setSub", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_SUB),
  JS_CFUNC_MAGIC_DEF("setTune", 1, guarded<js_tr909bassdrum_method>, METHOD_TR909_SET_TUNE),
  JS_CFUNC_MAGIC_DEF("trigger", 1, guarded<js_tr909bassdrum_method>, METHOD_TR909_TRIGGER),
  JS_CFUNC_MAGIC_DEF("render", 2, guarded<js_tr909bassdrum_method>, METHOD_TR909_RENDER),
};

static JSValue
js_tr909percussion_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  StkInstrmntPtr i;

  try {
    i = std::make_shared<Tr909Percussion>();
  } catch(stk::StkError& err) {
    return js_throw_stkerror(ctx, err);
  }

  JSValue obj = qjsx::wrap<StkInstrmntPtr>(ctx, new_target, js_stkinstrmnt_class_id, std::move(i));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, "Tr909Percussion");
  return obj;
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

  if(!(p = qjsx::self<StkInstrmntPtr>(ctx, this_val, js_stkinstrmnt_class_id)))
    return JS_EXCEPTION;

  Tr909Percussion* d = qjsx::require<Tr909Percussion>(ctx, p->get(), "not a Tr909Percussion");
  if(!d)
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_PERC_SET_TONE: {
      double freq1 = 0, freq2 = 0, mix = 0, decay = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, freq1, freq2, mix, decay))
        return JS_EXCEPTION;
      d->setTone(freq1, freq2, mix, decay);
      break;
    }
    case METHOD_PERC_SET_METALLIC: {
      double base = 0, mix = 0, decay = 0, brightness = 7000;
      if(qjsx::args_from(ctx, 0, argc, argv, base, mix, decay))
        return JS_EXCEPTION;
      if(argc > 3 && qjsx::args_from(ctx, 3, argc, argv, brightness))
        return JS_EXCEPTION;
      d->setMetallic(base, mix, decay, brightness);
      break;
    }
    case METHOD_PERC_SET_NOISE: {
      double mix = 0, decay = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, mix, decay))
        return JS_EXCEPTION;
      d->setNoise(mix, decay);
      break;
    }
    case METHOD_PERC_SET_NOISE_FILTER: {
      double cutoff = 0, q = 1.0;
      if(qjsx::args_from(ctx, 0, argc, argv, cutoff))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, q))
        return JS_EXCEPTION;
      d->setNoiseFilter(cutoff, q, argc > 2 ? parse_perc_filter_type(ctx, argv[2]) : PERC_FILTER_BANDPASS);
      break;
    }
    case METHOD_PERC_SET_CRUNCH: {
      double amount = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, amount))
        return JS_EXCEPTION;
      d->setCrunch(amount, argc > 1 ? parse_drive_type(ctx, argv[1]) : DRIVE_TANH);
      break;
    }
    case METHOD_PERC_SET_CLAP: {
      uint32_t hits = 1;
      double spacing = 0.01;
      if(qjsx::args_from(ctx, 0, argc, argv, hits))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, spacing))
        return JS_EXCEPTION;
      d->setClap((int)hits, spacing);
      break;
    }
    case METHOD_PERC_SET_TUNE: {
      double t = 1.0;
      if(qjsx::args_from(ctx, 0, argc, argv, t))
        return JS_EXCEPTION;
      d->setTune(t);
      break;
    }
    case METHOD_PERC_TRIGGER: {
      double v = 1.0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, v))
        return JS_EXCEPTION;
      d->trigger(v);
      break;
    }
    case METHOD_PERC_RENDER: {
      uint32_t n = 0;
      double v = 1.0;
      if(argc < 1 || js_stk_count(ctx, argv[0], &n))
        return JS_EXCEPTION;
      if(argc > 1 && qjsx::args_from(ctx, 1, argc, argv, v))
        return JS_EXCEPTION;

      StkFramesPtr f = std::make_shared<stk::StkFrames>(n, 1);
      stk::StkFrames& fr = *f;
      d->trigger(v);
      for(uint32_t k = 0; k < n; k++)
        fr[k] = d->tick();
      ret = qjsx::wrap_proto<StkFramesPtr>(ctx, stkframes_proto, js_stkframes_class_id, std::move(f));
      break;
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_tr909percussion_funcs[] = {
  JS_CFUNC_MAGIC_DEF("setTone", 4, guarded<js_tr909percussion_method>, METHOD_PERC_SET_TONE),
  JS_CFUNC_MAGIC_DEF("setMetallic", 3, guarded<js_tr909percussion_method>, METHOD_PERC_SET_METALLIC),
  JS_CFUNC_MAGIC_DEF("setNoise", 2, guarded<js_tr909percussion_method>, METHOD_PERC_SET_NOISE),
  JS_CFUNC_MAGIC_DEF("setNoiseFilter", 1, guarded<js_tr909percussion_method>, METHOD_PERC_SET_NOISE_FILTER),
  JS_CFUNC_MAGIC_DEF("setCrunch", 1, guarded<js_tr909percussion_method>, METHOD_PERC_SET_CRUNCH),
  JS_CFUNC_MAGIC_DEF("setClap", 1, guarded<js_tr909percussion_method>, METHOD_PERC_SET_CLAP),
  JS_CFUNC_MAGIC_DEF("setTune", 1, guarded<js_tr909percussion_method>, METHOD_PERC_SET_TUNE),
  JS_CFUNC_MAGIC_DEF("trigger", 1, guarded<js_tr909percussion_method>, METHOD_PERC_TRIGGER),
  JS_CFUNC_MAGIC_DEF("render", 2, guarded<js_tr909percussion_method>, METHOD_PERC_RENDER),
};

/* ============================================================ */
/* stk::WvIn -- RtWvIn, InetWvIn                           */
/* ============================================================ */

enum {
  INSTANCE_WVIN_RTWVIN = 0,
  INSTANCE_WVIN_INETWVIN,
};

static const qjsx::ctor_def stkwvin_ctors[] = {
  { "RtWvIn", 0, INSTANCE_WVIN_RTWVIN },
  { "InetWvIn", 0, INSTANCE_WVIN_INETWVIN },
};

static JSValue
js_stkwvin_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr w;

  try {
    switch(magic) {
      case INSTANCE_WVIN_RTWVIN: {
        uint32_t nChannels = 1;
        double sampleRate = stk::Stk::sampleRate();
        int32_t deviceIndex = 0, bufferFrames = stk::RT_BUFFER_SIZE, nBuffers = 20;

        if(qjsx::args_from(ctx, 0, argc, argv, nChannels, sampleRate, deviceIndex, bufferFrames, nBuffers))
          return JS_EXCEPTION;

        w = std::make_shared<stk::RtWvIn>(nChannels, sampleRate, deviceIndex, bufferFrames, nBuffers);
        break;
      }
      case INSTANCE_WVIN_INETWVIN: {
        int32_t bufferFrames = 1024, nBuffers = 8;

        if(qjsx::args_from(ctx, 0, argc, argv, bufferFrames, nBuffers))
          return JS_EXCEPTION;

        w = std::make_shared<stk::InetWvIn>(bufferFrames, nBuffers);
        break;
      }
    }
  } catch(const std::exception& e) {
    return js_stk_throw(ctx, e);
  }

  JSValue obj = qjsx::wrap<StkWvInPtr>(ctx, new_target, js_stkwvin_class_id, std::move(w));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkwvin_ctors, magic));
  return obj;
}

enum {
  METHOD_WVIN_TICK = 0,
};

static JSValue
js_stkwvin_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr* w;
  JSValue ret = JS_UNDEFINED;

  if(!(w = qjsx::self<StkWvInPtr>(ctx, this_val, js_stkwvin_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_WVIN_TICK: {
      StkFramesPtr* a;
      uint32_t channel = 0;

      if(argc > 0 && (a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        if(js_stk_channel(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, (*a->get()).channels(), &channel))
          return JS_EXCEPTION;

        (*w)->tick(*a->get(), channel);

        ret = JS_DupValue(ctx, argv[0]);
        break;
      }

      if(js_stk_channel(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, (*w)->channelsOut(), &channel))
        return JS_EXCEPTION;

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

  if(!(w = qjsx::self<StkWvInPtr>(ctx, this_val, js_stkwvin_class_id)))
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

static JSClassDef js_stkwvin_class = {
  .class_name = "StkWvIn",
  .finalizer = qjsx::finalize<StkWvInPtr, js_stkwvin_class_id>,
};

static const JSCFunctionListEntry js_stkwvin_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 1, guarded<js_stkwvin_method>, METHOD_WVIN_TICK),
  JS_CGETSET_MAGIC_DEF("channelsOut", js_stkwvin_get, 0, PROP_WVIN_CHANNELS_OUT),
  JS_CGETSET_MAGIC_DEF("lastFrame", js_stkwvin_get, 0, PROP_WVIN_LAST_FRAME),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StkWvIn", JS_PROP_CONFIGURABLE),
};

/* RtWvIn-only controls(RtWvIn::start/stop are not declared on the
 * WvIn base, so dispatch via dynamic_cast same as the TwinTDrum/Tr909
 * instrument subclasses above). */
enum {
  METHOD_RTWVIN_START = 0,
  METHOD_RTWVIN_STOP,
};

static JSValue
js_rtwvin_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvInPtr* w;

  if(!(w = qjsx::self<StkWvInPtr>(ctx, this_val, js_stkwvin_class_id)))
    return JS_EXCEPTION;

  stk::RtWvIn* r = qjsx::require<stk::RtWvIn>(ctx, w->get(), "not a RtWvIn");
  if(!r)
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTWVIN_START: r->start(); break;
    case METHOD_RTWVIN_STOP: r->stop(); break;
  }

  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_rtwvin_funcs[] = {
  JS_CFUNC_MAGIC_DEF("start", 0, guarded<js_rtwvin_method>, METHOD_RTWVIN_START),
  JS_CFUNC_MAGIC_DEF("stop", 0, guarded<js_rtwvin_method>, METHOD_RTWVIN_STOP),
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

  if(!(w = qjsx::self<StkWvInPtr>(ctx, this_val, js_stkwvin_class_id)))
    return JS_EXCEPTION;

  stk::InetWvIn* n = qjsx::require<stk::InetWvIn>(ctx, w->get(), "not a InetWvIn");
  if(!n)
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_INETWVIN_LISTEN: {
      int32_t port = 2006, protocol = stk::Socket::PROTO_TCP;
      uint32_t nChannels = 1, format = stk::Stk::STK_SINT16;

      if(qjsx::args_from(ctx, 0, argc, argv, port, nChannels, format, protocol))
        return JS_EXCEPTION;

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
  JS_CFUNC_MAGIC_DEF("listen", 0, guarded<js_inetwvin_method>, METHOD_INETWVIN_LISTEN),
  JS_CFUNC_MAGIC_DEF("isConnected", 0, guarded<js_inetwvin_method>, METHOD_INETWVIN_IS_CONNECTED),
};

/* ============================================================ */
/* stk::WvOut -- RtWvOut, InetWvOut                        */
/* ============================================================ */

enum {
  INSTANCE_WVOUT_RTWVOUT = 0,
  INSTANCE_WVOUT_INETWVOUT,
};

static const qjsx::ctor_def stkwvout_ctors[] = {
  { "RtWvOut", 0, INSTANCE_WVOUT_RTWVOUT },
  { "InetWvOut", 0, INSTANCE_WVOUT_INETWVOUT },
};

static JSValue
js_stkwvout_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], int magic) {
  StkWvOutPtr w;

  try {
    switch(magic) {
      case INSTANCE_WVOUT_RTWVOUT: {
        uint32_t nChannels = 1;
        double sampleRate = stk::Stk::sampleRate();
        int32_t deviceIndex = 0, bufferFrames = stk::RT_BUFFER_SIZE, nBuffers = 20;

        if(qjsx::args_from(ctx, 0, argc, argv, nChannels, sampleRate, deviceIndex, bufferFrames, nBuffers))
          return JS_EXCEPTION;

        w = std::make_shared<stk::RtWvOut>(nChannels, sampleRate, deviceIndex, bufferFrames, nBuffers);
        break;
      }
      case INSTANCE_WVOUT_INETWVOUT: {
        int32_t packetFrames = 1024;

        if(qjsx::args_from(ctx, 0, argc, argv, packetFrames))
          return JS_EXCEPTION;

        /* InetWvOut also has an(int port, ...) overload that opens a
         * connection immediately; without this cast to unsigned long an
         * int argument here binds to that overload instead, silently
         * attempting to connect to port `packetFrames`. */
        w = std::make_shared<stk::InetWvOut>(static_cast<unsigned long>(packetFrames));
        break;
      }
    }
  } catch(const std::exception& e) {
    return js_stk_throw(ctx, e);
  }

  JSValue obj = qjsx::wrap<StkWvOutPtr>(ctx, new_target, js_stkwvout_class_id, std::move(w));

  if(JS_IsException(obj))
    return obj;

  js_set_tostringtag(ctx, obj, qjsx::ctor_name(stkwvout_ctors, magic));
  return obj;
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

  if(!(w = qjsx::self<StkWvOutPtr>(ctx, this_val, js_stkwvout_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_WVOUT_TICK: {
      StkFramesPtr* a;

      if(argc > 0 && (a = qjsx::opaque<StkFramesPtr>(argv[0], js_stkframes_class_id))) {
        (*w)->tick(*a->get());
        break;
      }

      double sample = 0;
      if(qjsx::args_from(ctx, 0, argc, argv, sample))
        return JS_EXCEPTION;
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

  if(!(w = qjsx::self<StkWvOutPtr>(ctx, this_val, js_stkwvout_class_id)))
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

static JSClassDef js_stkwvout_class = {
  .class_name = "StkWvOut",
  .finalizer = qjsx::finalize<StkWvOutPtr, js_stkwvout_class_id>,
};

static const JSCFunctionListEntry js_stkwvout_funcs[] = {
  JS_CFUNC_MAGIC_DEF("tick", 1, guarded<js_stkwvout_method>, METHOD_WVOUT_TICK),
  JS_CFUNC_MAGIC_DEF("clipStatus", 0, guarded<js_stkwvout_method>, METHOD_WVOUT_CLIP_STATUS),
  JS_CFUNC_MAGIC_DEF("resetClipStatus", 0, guarded<js_stkwvout_method>, METHOD_WVOUT_RESET_CLIP_STATUS),
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

  if(!(w = qjsx::self<StkWvOutPtr>(ctx, this_val, js_stkwvout_class_id)))
    return JS_EXCEPTION;

  stk::RtWvOut* r = qjsx::require<stk::RtWvOut>(ctx, w->get(), "not a RtWvOut");
  if(!r)
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTWVOUT_START: r->start(); break;
    case METHOD_RTWVOUT_STOP: r->stop(); break;
  }

  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_rtwvout_funcs[] = {
  JS_CFUNC_MAGIC_DEF("start", 0, guarded<js_rtwvout_method>, METHOD_RTWVOUT_START),
  JS_CFUNC_MAGIC_DEF("stop", 0, guarded<js_rtwvout_method>, METHOD_RTWVOUT_STOP),
};

/* InetWvOut-only controls. */
enum {
  METHOD_INETWVOUT_CONNECT = 0,
  METHOD_INETWVOUT_DISCONNECT,
};

static JSValue
js_inetwvout_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StkWvOutPtr* w;

  if(!(w = qjsx::self<StkWvOutPtr>(ctx, this_val, js_stkwvout_class_id)))
    return JS_EXCEPTION;

  stk::InetWvOut* n = qjsx::require<stk::InetWvOut>(ctx, w->get(), "not a InetWvOut");
  if(!n)
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_INETWVOUT_CONNECT: {
      int32_t port = 0, protocol = stk::Socket::PROTO_TCP;
      uint32_t nChannels = 1, format = stk::Stk::STK_SINT16;
      std::string hostname = "localhost";

      if(qjsx::args_from(ctx, 0, argc, argv, port, protocol))
        return JS_EXCEPTION;
      if(argc > 2 && qjsx::with(ctx, argv[2], [&](const char* s) {
        hostname = s;
      }))
        return JS_EXCEPTION;
      if(argc > 3 && qjsx::args_from(ctx, 3, argc, argv, nChannels))
        return JS_EXCEPTION;
      if(argc > 4 && qjsx::args_from(ctx, 4, argc, argv, format))
        return JS_EXCEPTION;

      JSValue exc = JS_UNDEFINED;
      try {
        n->connect(port, (stk::Socket::ProtocolType)protocol, hostname.c_str(), nChannels, format);
      } catch(const std::exception& e) {
        exc = js_stk_throw(ctx, e);
      }

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
  JS_CFUNC_MAGIC_DEF("connect", 1, guarded<js_inetwvout_method>, METHOD_INETWVOUT_CONNECT),
  JS_CFUNC_MAGIC_DEF("disconnect", 0, guarded<js_inetwvout_method>, METHOD_INETWVOUT_DISCONNECT),
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

  JSValue proto = JS_DupValue(ctx, midifilein_proto);
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_midifilein_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    delete mf;
    return obj;
  }

  JS_SetOpaque(obj, mf);
  js_set_tostringtag(ctx, obj, "MidiFileIn");
  return obj;
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

  if(!(mf = qjsx::self<stk::MidiFileIn>(ctx, this_val, js_midifilein_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_MIDIFILE_REWIND_TRACK: {
      uint32_t track = 0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, track))
        return JS_EXCEPTION;
      try {
        mf->rewindTrack(track);
      } catch(const std::exception& e) {
        return js_stk_throw(ctx, e);
      }
      break;
    }
    case METHOD_MIDIFILE_GET_TICK_SECONDS: {
      uint32_t track = 0;
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, track))
        return JS_EXCEPTION;
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
      if(argc > 0 && qjsx::args_from(ctx, 0, argc, argv, track))
        return JS_EXCEPTION;

      std::vector<unsigned char> event;
      unsigned long deltaTime;
      try {
        deltaTime = magic == METHOD_MIDIFILE_GET_NEXT_EVENT ? mf->getNextEvent(&event, track)
                                                            : mf->getNextMidiEvent(&event, track);
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

  if(!(mf = qjsx::self<stk::MidiFileIn>(ctx, this_val, js_midifilein_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_MIDIFILE_FORMAT: ret = JS_NewInt32(ctx, mf->getFileFormat()); break;
    case PROP_MIDIFILE_NUM_TRACKS: ret = JS_NewUint32(ctx, mf->getNumberOfTracks()); break;
    case PROP_MIDIFILE_DIVISION: ret = JS_NewInt32(ctx, mf->getDivision()); break;
  }

  return ret;
}

static JSClassDef js_midifilein_class = {
  .class_name = "MidiFileIn",
  .finalizer = qjsx::finalize_delete<stk::MidiFileIn, js_midifilein_class_id>,
};

static const JSCFunctionListEntry js_midifilein_funcs[] = {
  JS_CFUNC_MAGIC_DEF("rewindTrack", 0, guarded<js_midifilein_method>, METHOD_MIDIFILE_REWIND_TRACK),
  JS_CFUNC_MAGIC_DEF("getTickSeconds", 0, guarded<js_midifilein_method>, METHOD_MIDIFILE_GET_TICK_SECONDS),
  JS_CFUNC_MAGIC_DEF("getNextEvent", 0, guarded<js_midifilein_method>, METHOD_MIDIFILE_GET_NEXT_EVENT),
  JS_CFUNC_MAGIC_DEF("getNextMidiEvent", 0, guarded<js_midifilein_method>, METHOD_MIDIFILE_GET_NEXT_MIDI_EVENT),
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

  if(qjsx::args_from(ctx, 0, argc, argv, api))
    return JS_EXCEPTION;
  if(argc > 1) {
    const char* s = JS_ToCString(ctx, argv[1]);
    if(!s)
      return JS_EXCEPTION;
    clientName = s;
    JS_FreeCString(ctx, s);
  }
  if(argc > 2 && qjsx::args_from(ctx, 2, argc, argv, queueSizeLimit))
    return JS_EXCEPTION;

  RtMidiIn* r = nullptr;
  try {
    r = new RtMidiIn((RtMidi::Api)api, clientName, queueSizeLimit);
  } catch(const std::exception& e) {
    return js_stk_throw(ctx, e);
  }

  JSValue proto = JS_DupValue(ctx, rtmidiin_proto);
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_rtmidiin_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    delete r;
    return obj;
  }

  JS_SetOpaque(obj, r);
  js_set_tostringtag(ctx, obj, "RtMidiIn");
  return obj;
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

  if(!(r = qjsx::self<RtMidiIn>(ctx, this_val, js_rtmidiin_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTMIDIIN_OPEN_PORT: {
      uint32_t portNumber = 0;
      std::string portName = "RtMidi Input";
      if(qjsx::args_from(ctx, 0, argc, argv, portNumber))
        return JS_EXCEPTION;
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
      if(qjsx::args_from(ctx, 0, argc, argv, portNumber))
        return JS_EXCEPTION;
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

static JSClassDef js_rtmidiin_class = {
  .class_name = "RtMidiIn",
  .finalizer = qjsx::finalize_delete<RtMidiIn, js_rtmidiin_class_id>,
};

static const JSCFunctionListEntry js_rtmidiin_funcs[] = {
  JS_CFUNC_MAGIC_DEF("openPort", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_OPEN_PORT),
  JS_CFUNC_MAGIC_DEF("openVirtualPort", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_OPEN_VIRTUAL_PORT),
  JS_CFUNC_MAGIC_DEF("closePort", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_CLOSE_PORT),
  JS_CFUNC_MAGIC_DEF("isPortOpen", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_IS_PORT_OPEN),
  JS_CFUNC_MAGIC_DEF("getPortCount", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_GET_PORT_COUNT),
  JS_CFUNC_MAGIC_DEF("getPortName", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_GET_PORT_NAME),
  JS_CFUNC_MAGIC_DEF("ignoreTypes", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_IGNORE_TYPES),
  JS_CFUNC_MAGIC_DEF("getMessage", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_GET_MESSAGE),
  JS_CFUNC_MAGIC_DEF("getCurrentApi", 0, guarded<js_rtmidiin_method>, METHOD_RTMIDIIN_GET_CURRENT_API),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RtMidiIn", JS_PROP_CONFIGURABLE),
};

static JSValue
js_rtmidiout_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  int32_t api = RtMidi::UNSPECIFIED;
  std::string clientName = "RtMidi Output Client";

  if(qjsx::args_from(ctx, 0, argc, argv, api))
    return JS_EXCEPTION;
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

  JSValue proto = JS_DupValue(ctx, rtmidiout_proto);
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_rtmidiout_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    delete r;
    return obj;
  }

  JS_SetOpaque(obj, r);
  js_set_tostringtag(ctx, obj, "RtMidiOut");
  return obj;
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

  if(!(r = qjsx::self<RtMidiOut>(ctx, this_val, js_rtmidiout_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_RTMIDIOUT_OPEN_PORT: {
      uint32_t portNumber = 0;
      std::string portName = "RtMidi Output";
      if(qjsx::args_from(ctx, 0, argc, argv, portNumber))
        return JS_EXCEPTION;
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
      if(qjsx::args_from(ctx, 0, argc, argv, portNumber))
        return JS_EXCEPTION;
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
      if(argc < 1 || qjsx::read_array(ctx, argv[0], message))
        return JS_EXCEPTION;
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

static JSClassDef js_rtmidiout_class = {
  .class_name = "RtMidiOut",
  .finalizer = qjsx::finalize_delete<RtMidiOut, js_rtmidiout_class_id>,
};

static const JSCFunctionListEntry js_rtmidiout_funcs[] = {
  JS_CFUNC_MAGIC_DEF("openPort", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_OPEN_PORT),
  JS_CFUNC_MAGIC_DEF("openVirtualPort", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_OPEN_VIRTUAL_PORT),
  JS_CFUNC_MAGIC_DEF("closePort", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_CLOSE_PORT),
  JS_CFUNC_MAGIC_DEF("isPortOpen", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_IS_PORT_OPEN),
  JS_CFUNC_MAGIC_DEF("getPortCount", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_GET_PORT_COUNT),
  JS_CFUNC_MAGIC_DEF("getPortName", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_GET_PORT_NAME),
  JS_CFUNC_MAGIC_DEF("sendMessage", 1, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_SEND_MESSAGE),
  JS_CFUNC_MAGIC_DEF("getCurrentApi", 0, guarded<js_rtmidiout_method>, METHOD_RTMIDIOUT_GET_CURRENT_API),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RtMidiOut", JS_PROP_CONFIGURABLE),
};

/* ============================================================ */
/* RtAudio -- RtAudio                                         */
/* Device enumeration and stream lifecycle only: bridging         */
/* RtAudio's realtime audio callback into the JS engine would     */
/* mean calling back into QuickJS from RtAudio's own audio        */
/* thread, which is not safe(QuickJS is not thread-safe). Use    */
/* RtWvIn/RtWvOut for actual realtime audio I/O instead.    */
/* ============================================================ */

static JSValue
js_rtaudio_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  int32_t api = RtAudio::UNSPECIFIED;
  if(qjsx::args_from(ctx, 0, argc, argv, api))
    return JS_EXCEPTION;

  RtAudio* a = new RtAudio((RtAudio::Api)api);

  JSValue proto = JS_DupValue(ctx, rtaudio_proto);
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_rtaudio_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    delete a;
    return obj;
  }

  JS_SetOpaque(obj, a);
  js_set_tostringtag(ctx, obj, "RtAudio");
  return obj;
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

  if(!(a = qjsx::self<RtAudio>(ctx, this_val, js_rtaudio_class_id)))
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
      if(qjsx::args_from(ctx, 0, argc, argv, deviceId))
        return JS_EXCEPTION;
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
      if(qjsx::args_from(ctx, 0, argc, argv, t))
        return JS_EXCEPTION;
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

static JSClassDef js_rtaudio_class = {
  .class_name = "RtAudio",
  .finalizer = qjsx::finalize_delete<RtAudio, js_rtaudio_class_id>,
};

static const JSCFunctionListEntry js_rtaudio_funcs[] = {
  JS_CFUNC_MAGIC_DEF("getCurrentApi", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_CURRENT_API),
  JS_CFUNC_MAGIC_DEF("getDeviceCount", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_DEVICE_COUNT),
  JS_CFUNC_MAGIC_DEF("getDeviceIds", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_DEVICE_IDS),
  JS_CFUNC_MAGIC_DEF("getDeviceNames", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_DEVICE_NAMES),
  JS_CFUNC_MAGIC_DEF("getDeviceInfo", 1, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_DEVICE_INFO),
  JS_CFUNC_MAGIC_DEF("getDefaultOutputDevice", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_DEFAULT_OUTPUT_DEVICE),
  JS_CFUNC_MAGIC_DEF("getDefaultInputDevice", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_DEFAULT_INPUT_DEVICE),
  JS_CFUNC_MAGIC_DEF("closeStream", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_CLOSE_STREAM),
  JS_CFUNC_MAGIC_DEF("startStream", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_START_STREAM),
  JS_CFUNC_MAGIC_DEF("stopStream", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_STOP_STREAM),
  JS_CFUNC_MAGIC_DEF("abortStream", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_ABORT_STREAM),
  JS_CFUNC_MAGIC_DEF("getErrorText", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_ERROR_TEXT),
  JS_CFUNC_MAGIC_DEF("isStreamOpen", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_IS_STREAM_OPEN),
  JS_CFUNC_MAGIC_DEF("isStreamRunning", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_IS_STREAM_RUNNING),
  JS_CFUNC_MAGIC_DEF("getStreamTime", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_STREAM_TIME),
  JS_CFUNC_MAGIC_DEF("setStreamTime", 1, guarded<js_rtaudio_method>, METHOD_RTAUDIO_SET_STREAM_TIME),
  JS_CFUNC_MAGIC_DEF("getStreamLatency", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_STREAM_LATENCY),
  JS_CFUNC_MAGIC_DEF("getStreamSampleRate", 0, guarded<js_rtaudio_method>, METHOD_RTAUDIO_GET_STREAM_SAMPLE_RATE),
  JS_CFUNC_MAGIC_DEF("showWarnings", 1, guarded<js_rtaudio_method>, METHOD_RTAUDIO_SHOW_WARNINGS),
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RtAudio", JS_PROP_CONFIGURABLE),
};

int
js_stk_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_stk_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stk_class_id, &js_stk_class);

  stk_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stk_constructor,
                                // "Stk", 1, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, stk_ctor, js_stk_static_funcs, countof(js_stk_static_funcs));
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

  if(m)
    qjsx::bind_ctors(ctx, m, js_stkfilter_constructor, stkfilter_proto, stkfilter_ctors);

  JS_NewClassID(&js_stkgenerator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkgenerator_class_id, &js_stkgenerator_class);

  stkgenerator_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkgenerator_constructor,
                                         // "Filter", 1, JS_CFUNC_constructor, 0);
  stkgenerator_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkgenerator_proto, js_stkgenerator_funcs, countof(js_stkgenerator_funcs));

  JS_SetClassProto(ctx, js_stkgenerator_class_id, stkgenerator_proto);

  if(m)
    qjsx::bind_ctors(ctx, m, js_stkgenerator_constructor, stkgenerator_proto, stkgenerator_ctors);

  JS_NewClassID(&js_stkeffect_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkeffect_class_id, &js_stkeffect_class);

  stkeffect_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkeffect_constructor,
                                      // "Generator", 1, JS_CFUNC_constructor, 0);
  stkeffect_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkeffect_proto, js_stkeffect_funcs, countof(js_stkeffect_funcs));

  JS_SetClassProto(ctx, js_stkeffect_class_id, stkeffect_proto);

  if(m) {
    qjsx::bind_ctors(ctx, m, js_stkeffect_constructor, stkeffect_proto, stkeffect_ctors);

    JS_SetModuleExport(ctx, m, "Effect", stkeffect_ctor);
  }

  JS_NewClassID(&js_stkfm_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkfm_class_id, &js_stkfm_class);

  stkfm_ctor = JS_NewObject(ctx);
  stkfm_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkfm_proto, js_stkfm_funcs, countof(js_stkfm_funcs));

  JS_SetClassProto(ctx, js_stkfm_class_id, stkfm_proto);

  if(m) {
    qjsx::bind_ctors(ctx, m, js_stkfm_constructor, stkfm_proto, stkfm_ctors);

    JS_SetModuleExport(ctx, m, "FM", stkfm_ctor);
  }

  JS_NewClassID(&js_stkguitar_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stkguitar_class_id, &js_stkguitar_class);

  stkguitar_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stkguitar_proto, js_stkguitar_funcs, countof(js_stkguitar_funcs));
  JS_SetClassProto(ctx, js_stkguitar_class_id, stkguitar_proto);

  if(m) {
    stkguitar_ctor = JS_NewCFunctionMagic(ctx, js_stkguitar_constructor, "Guitar", 0, JS_CFUNC_constructor_magic, INSTANCE_GUITAR);
    JS_DefinePropertyValueStr(ctx, stkguitar_ctor, "prototype", JS_DupValue(ctx, stkguitar_proto), 0);
    JS_SetModuleExport(ctx, m, "Guitar", stkguitar_ctor);
  }

  JS_NewClassID(&js_stktwang_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stktwang_class_id, &js_stktwang_class);

  stktwang_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stktwang_proto, js_stktwang_funcs, countof(js_stktwang_funcs));
  JS_SetClassProto(ctx, js_stktwang_class_id, stktwang_proto);

  if(m) {
    stktwang_ctor = JS_NewCFunctionMagic(ctx, js_stktwang_constructor, "Twang", 0, JS_CFUNC_constructor_magic, INSTANCE_TWANG);
    JS_DefinePropertyValueStr(ctx, stktwang_ctor, "prototype", JS_DupValue(ctx, stktwang_proto), 0);
    JS_SetModuleExport(ctx, m, "Twang", stktwang_ctor);
  }

  stkinstrmnt_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_stkinstrmnt_constructor,
                                        // "Generator", 1, JS_CFUNC_constructor, 0);
  stkinstrmnt_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, stkinstrmnt_proto, js_stkinstrmnt_funcs, countof(js_stkinstrmnt_funcs));

  JS_SetClassProto(ctx, js_stkinstrmnt_class_id, stkinstrmnt_proto);

  if(m) {
    qjsx::bind_ctors(ctx, m, js_stkinstrmnt_constructor, stkinstrmnt_proto, stkinstrmnt_ctors);

    JS_SetModuleExport(ctx, m, "Stk", stk_ctor);
    JS_SetModuleExport(ctx, m, "StkFrames", stkframes_ctor);
    JS_SetModuleExport(ctx, m, "Generator", stkfilter_ctor);
    JS_SetModuleExport(ctx, m, "Filter", stkgenerator_ctor);
  }

  /* TwinTDrum, Tr909BassDrum and Tr909Percussion are ordinary stk::Instrmnt
   * subclasses, so they share js_stkinstrmnt_class_id/finalizer above;
   * only their own prototype(chained onto stkinstrmnt_proto) differs,
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
    qjsx::bind_ctors(ctx, m, js_stkfunction_constructor, stkfunction_proto, stkfunction_ctors);
    JS_SetModuleExport(ctx, m, "Function", stkfunction_ctor);
  }

  /* stk::WvIn(RtWvIn, InetWvIn) */
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
    qjsx::bind_ctors(ctx, m, js_stkwvin_constructor, rtwvin_proto, stkwvin_ctors);
    JS_SetModuleExport(ctx, m, "RtWvIn", JS_NewString(ctx, qjsx::ctor_name(stkwvin_ctors, INSTANCE_WVIN_RTWVIN)));
    JS_SetModuleExport(ctx, m, "InetWvIn", JS_NewString(ctx, qjsx::ctor_name(stkwvin_ctors, INSTANCE_WVIN_INETWVIN)));
  }

  /* stk::WvOut(RtWvOut, InetWvOut) */
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
    qjsx::bind_ctors(ctx, m, js_stkwvout_constructor, rtwvout_proto, stkwvout_ctors);
    JS_SetModuleExport(ctx, m, "RtWvOut", JS_NewString(ctx, qjsx::ctor_name(stkwvout_ctors, INSTANCE_WVOUT_RTWVOUT)));
    JS_SetModuleExport(ctx, m, "InetWvOut", JS_NewString(ctx, qjsx::ctor_name(stkwvout_ctors, INSTANCE_WVOUT_INETWVOUT)));
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

  rtaudio_ctor = JS_NewObject(ctx);
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
js_stk_add_exports(JSContext* ctx, JSModuleDef* m) {
  qjsx::declare_ctors(ctx, m, stkfilter_ctors);
  qjsx::declare_ctors(ctx, m, stkgenerator_ctors);
  qjsx::declare_ctors(ctx, m, stkinstrmnt_ctors);
  qjsx::declare_ctors(ctx, m, stkeffect_ctors);
  qjsx::declare_ctors(ctx, m, stkfm_ctors);

  JS_AddModuleExport(ctx, m, "Effect");
  JS_AddModuleExport(ctx, m, "Guitar");
  JS_AddModuleExport(ctx, m, "Twang");
  JS_AddModuleExport(ctx, m, "FM");
  JS_AddModuleExport(ctx, m, "TwinTDrum");
  JS_AddModuleExport(ctx, m, "Tr909BassDrum");
  JS_AddModuleExport(ctx, m, "Tr909Percussion");
  JS_AddModuleExport(ctx, m, "Cubic");
  JS_AddModuleExport(ctx, m, "Function");
  JS_AddModuleExport(ctx, m, "Stk");
  JS_AddModuleExport(ctx, m, "StkFrames");
  JS_AddModuleExport(ctx, m, "Generator");
  JS_AddModuleExport(ctx, m, "Filter");
  JS_AddModuleExport(ctx, m, "MidiFileIn");
  JS_AddModuleExport(ctx, m, "RtMidiIn");
  JS_AddModuleExport(ctx, m, "RtMidiOut");
  JS_AddModuleExport(ctx, m, "RtAudio");
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_stk
#endif

extern "C" VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_stk_init))) {
#ifdef USE_STK
    js_stk_add_exports(ctx, m);
#endif
  }

  return m;
}
