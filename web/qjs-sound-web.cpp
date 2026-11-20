// qjs-sound-web: STK and SndObj DSP for the browser, block-wise exports for an
// AudioWorkletProcessor (128 frames per process() call, Float32 in and out).
//
// Every export takes the object it works on; no globals, so several instances
// coexist. Device I/O (RtAudio, sockets, threads) is not compiled.

#include "BiQuad.h"
#include "Plucked.h"
#include "Stk.h"

#include "Reson.h"
#include "SndObj.h"

#include <emscripten.h>

#include <vector>

#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE

namespace {

// stk works on StkFloat (double) frames; the worklet hands over float
struct StkBlock {
  stk::StkFrames frames;
  explicit StkBlock(unsigned n = 128) : frames(0.0, n, 1) {}

  void load(const float* in, unsigned n) {
    frames.resize(n, 1);
    for(unsigned i = 0; i < n; i++)
      frames[i] = in[i];
  }

  void store(float* out, unsigned n) const {
    for(unsigned i = 0; i < n; i++)
      out[i] = static_cast<float>(frames[i]);
  }
};

struct SndResonator {
  SndObj input;
  Reson reson;
  int vecsize;

  SndResonator(float freq, float bw, int vec, float sr)
      : input(0, vec, sr), reson(freq, bw, &input, 0, 0, vec, sr), vecsize(vec) {}
};

} // namespace

EXPORT void stk_set_sample_rate(double sr) { stk::Stk::setSampleRate(sr); }

// biquad resonator (a filter: in -> out)
EXPORT void* stk_biquad_new(double freq, double radius) {
  auto* f = new stk::BiQuad();
  f->setResonance(freq, radius, true);
  return f;
}
EXPORT void stk_biquad_free(void* f) { delete static_cast<stk::BiQuad*>(f); }
EXPORT void stk_biquad_set(void* f, double freq, double radius) {
  static_cast<stk::BiQuad*>(f)->setResonance(freq, radius, true);
}
EXPORT void* stk_block_new(unsigned n) { return new StkBlock(n); }
EXPORT void stk_block_free(void* b) { delete static_cast<StkBlock*>(b); }
EXPORT void stk_biquad_process(void* f, void* b, const float* in, float* out, unsigned n) {
  StkBlock* blk = static_cast<StkBlock*>(b);
  blk->load(in, n);
  static_cast<stk::BiQuad*>(f)->tick(blk->frames);
  blk->store(out, n);
}

// plucked string (an instrument: no input, out only)
EXPORT void* stk_plucked_new(double lowest) { return new stk::Plucked(lowest); }
EXPORT void stk_plucked_free(void* p) { delete static_cast<stk::Plucked*>(p); }
EXPORT void stk_plucked_note_on(void* p, double freq, double amp) {
  static_cast<stk::Plucked*>(p)->noteOn(freq, amp);
}
EXPORT void stk_plucked_process(void* p, void* b, float* out, unsigned n) {
  StkBlock* blk = static_cast<StkBlock*>(b);
  blk->frames.resize(n, 1);
  static_cast<stk::Plucked*>(p)->tick(blk->frames);
  blk->store(out, n);
}

// SndObj Reson (a filter, in -> out)
EXPORT void* snd_reson_new(float freq, float bw, int vecsize, float sr) {
  return new SndResonator(freq, bw, vecsize, sr);
}
EXPORT void snd_reson_free(void* r) { delete static_cast<SndResonator*>(r); }
EXPORT void snd_reson_set(void* r, float freq, float bw) {
  SndResonator* s = static_cast<SndResonator*>(r);
  s->reson.SetFreq(freq);
  s->reson.SetBW(bw);
}
// n must equal the vecsize the object was made with (SndObj works in fixed vectors)
EXPORT int snd_reson_process(void* r, const float* in, float* out, int n) {
  SndResonator* s = static_cast<SndResonator*>(r);
  if(n != s->vecsize)
    return -1;
  s->input.PushIn(const_cast<float*>(in), n);
  s->reson.DoProcess();
  for(int i = 0; i < n; i++)
    out[i] = s->reson.Output(i);
  return 0;
}

EXPORT void* qsw_alloc(unsigned bytes) { return ::operator new(bytes); }
EXPORT void qsw_free(void* p) { ::operator delete(p); }
