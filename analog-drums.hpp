#pragma once

#include <algorithm>
#include <cmath>

#include "Instrmnt.h"
#include "Noise.h"
#include "OnePole.h"
#include "TwoPole.h"

/* ============================================================
 * Analog drum voice emulations.
 *
 * STK doesn't model analog drum circuits directly, so these are genuine
 * DSP models of two classic techniques, built out of STK primitives
 * (TwoPole for a ringing resonator, OnePole for tone shaping, Noise for
 * transients) and exposed as ordinary stk::Instrmnt subclasses so they
 * share the existing StkInstrmnt tick()/noteOn()/noteOff() binding in
 * quickjs-stk.cpp (same class id, same finalizer -- only their own
 * prototype, chained onto stkinstrmnt_proto, adds the extra controls
 * below).
 * ============================================================ */

enum {
  DRIVE_TANH = 0,
  DRIVE_CUBIC = 1,
  DRIVE_FOLD = 2,
};

inline double
analog_drive(double x, double amount, int type) {
  if(amount <= 0.0)
    return x;

  double driven = x * (1.0 + amount * 8.0);

  switch(type) {
    case DRIVE_CUBIC: {
      if(driven > 1.0)
        driven = 1.0;
      else if(driven < -1.0)
        driven = -1.0;
      return driven - (driven * driven * driven) / 3.0;
    }
    case DRIVE_FOLD: {
      for(int i = 0; i < 16 && (driven > 1.0 || driven < -1.0); i++) {
        if(driven > 1.0)
          driven = 2.0 - driven;
        else if(driven < -1.0)
          driven = -2.0 - driven;
      }
      return driven;
    }
    default: return tanh(driven);
  }
}

/* ---- TwinTDrum: analog Twin-T oscillator style drum resonator ----
 *
 * A Twin-T RC notch network, wired into an inverting feedback loop,
 * oscillates at its notch frequency; struck, it rings down like a tom,
 * conga or woodblock. We model that ring-down directly as a discretized
 * 2-pole resonator (stk::TwoPole, tuned via setResonance) excited by an
 * impulse -- the digital equivalent of "pinging" the analog circuit. A
 * second, detuned resonator and a noise "click" transient are layered in
 * for timbres a plain twin-T never had (cowbell/agogo-style clusters). */
class TwinTDrum : public stk::Instrmnt {
public:
  TwinTDrum(stk::StkFloat frequency = 200.0)
      : frequency_(frequency), decay_(0.25), drive_(0.0), driveType_(DRIVE_TANH), pitchDropAmt_(0.0),
        pitchDropTime_(0.03), secondaryRatio_(1.5), secondaryMix_(0.0), clickAmount_(0.0), clickEnv_(0.0),
        age_(0.0) {
    refreshResonators();
  }

  void setFrequency(stk::StkFloat frequency) override {
    frequency_ = frequency;
    refreshResonators();
  }
  void setDecay(double t60Seconds) {
    decay_ = std::max(0.001, t60Seconds);
    refreshResonators();
  }
  void setDrive(double amount, int type = DRIVE_TANH) {
    drive_ = amount;
    driveType_ = type;
  }
  void setPitchDrop(double semitones, double timeSeconds) {
    pitchDropAmt_ = semitones;
    pitchDropTime_ = std::max(0.0005, timeSeconds);
  }
  void setSecondary(double ratio, double mix) {
    secondaryRatio_ = ratio;
    secondaryMix_ = mix;
    refreshResonators();
  }
  void setClick(double amount) { clickAmount_ = amount; }

  void noteOn(stk::StkFloat frequency, stk::StkFloat amplitude) override {
    if(frequency > 0.0)
      setFrequency(frequency);
    strike(amplitude);
  }
  void noteOff(stk::StkFloat amplitude) override { setDecay(0.01); }

  void strike(double amplitude = 1.0) {
    age_ = 0.0;

    /* With b0 pinned at 1 (see refreshResonators()), a single-sample impulse
     * through a 2-pole resonator peaks at roughly amplitude / sin(w0) --
     * i.e. it blows up at low frequencies and shrinks at high ones, since
     * that's how much a narrow-band filter amplifies a single sample before
     * decay catches up. Scaling the excitation by sin(w0) cancels that out
     * so strike() peaks at ~amplitude across the tuning range, and (unlike
     * normalize=true) independent of decay_/Q too. */
    double sr = stk::Stk::sampleRate();
    resonator_.tick(amplitude * std::sin(2.0 * M_PI * frequency_ / sr));
    if(secondaryMix_ > 0.0)
      secondary_.tick(amplitude * std::sin(2.0 * M_PI * frequency_ * secondaryRatio_ / sr));
    if(clickAmount_ > 0.0)
      clickEnv_ = 1.0;
  }

  stk::StkFloat tick(unsigned int channel = 0) override {
    if(pitchDropAmt_ != 0.0) {
      double f = currentFrequency();
      /* normalize=false: keep b0 pinned at 1 (see refreshResonators()) so the
       * pitch sweep re-tunes the resonance without rescaling its level. */
      resonator_.setResonance(f, radiusFor(decay_, f), false);
      if(secondaryMix_ > 0.0)
        secondary_.setResonance(f * secondaryRatio_, radiusFor(decay_, f * secondaryRatio_), false);
    }

    double y = resonator_.tick(0.0);
    if(secondaryMix_ > 0.0)
      y += secondaryMix_ * secondary_.tick(0.0);

    if(clickEnv_ > 1e-4) {
      y += clickAmount_ * clickEnv_ * noise_.tick();
      clickEnv_ *= 0.9;
    }

    age_ += 1.0 / stk::Stk::sampleRate();
    lastFrame_[0] = analog_drive(y, drive_, driveType_);
    return lastFrame_[0];
  }

  stk::StkFrames& tick(stk::StkFrames& frames, unsigned int channel = 0) override {
    stk::StkFloat* samples = &frames[channel];
    unsigned int hop = frames.channels();
    for(unsigned int i = 0; i < frames.frames(); i++, samples += hop)
      *samples = tick();
    return frames;
  }

private:
  static double radiusFor(double t60, double freq) {
    double sr = stk::Stk::sampleRate();
    double n = std::max(1.0, t60 * sr);
    return std::pow(10.0, -3.0 / n);
  }

  double currentFrequency() const {
    double env = std::exp(-age_ / pitchDropTime_);
    double ratio = std::pow(2.0, pitchDropAmt_ * env / 12.0);
    return frequency_ * ratio;
  }

  void refreshResonators() {
    /* normalize=false: TwoPole's normalize=true tunes b0 for unity gain
     * under continuous sinusoidal drive, which for a long decay (high Q)
     * shrinks b0 towards zero -- fine for a sustained tone, but it starves
     * a single-sample strike() impulse of energy, making long-decay hits
     * nearly silent. Leaving b0 at its default 1 makes the impulse response
     * amplitude track the strike amplitude directly, independent of decay. */
    resonator_.setResonance(frequency_, radiusFor(decay_, frequency_), false);
    secondary_.setResonance(frequency_ * secondaryRatio_, radiusFor(decay_, frequency_ * secondaryRatio_), false);
  }

  double frequency_, decay_, drive_;
  int driveType_;
  double pitchDropAmt_, pitchDropTime_;
  double secondaryRatio_, secondaryMix_;
  double clickAmount_, clickEnv_;
  double age_;
  stk::TwoPole resonator_, secondary_;
  stk::Noise noise_;
};

/* ---- Tr909BassDrum: analog "909-style" bass drum ----
 *
 * A distorted sine core with independent, dedicated pitch- and
 * amplitude-decay envelopes -- not ADSR: real analog bass drum circuits
 * are a fast pitch-modulated oscillator plus a single decay stage, and the
 * classic "punch" comes from the pitch settling much faster than the
 * amplitude, not from a multi-stage envelope generator. */
class Tr909BassDrum : public stk::Instrmnt {
public:
  Tr909BassDrum()
      : pitchStart_(400.0), pitchEnd_(55.0), pitchDecay_(0.06), pitchLinear_(false), ampDecay_(0.45),
        ampLinear_(false), drive_(0.35), driveType_(DRIVE_TANH), tone_(6000.0), clickLevel_(0.5),
        clickDecay_(0.04), subMix_(0.0), subOctave_(1.0), tune_(1.0), phase_(0.0), subPhase_(0.0),
        pitchEnv_(0.0), pitchCoeff_(1.0), pitchLinStep_(0.0), ampEnv_(0.0), ampCoeff_(1.0), ampLinStep_(0.0),
        clickEnv_(0.0), clickCoeff_(1.0), velocity_(0.0) {
    toneFilter_.setPole(poleFromCutoff(tone_));
  }

  void setPitchEnvelope(double startFreq, double endFreq, double decayTime, bool linear = false) {
    pitchStart_ = startFreq;
    pitchEnd_ = endFreq > 0 ? endFreq : 1.0;
    pitchDecay_ = std::max(0.001, decayTime);
    pitchLinear_ = linear;
  }
  void setAmpEnvelope(double decayTime, bool linear = false) {
    ampDecay_ = std::max(0.001, decayTime);
    ampLinear_ = linear;
  }
  void setDrive(double amount, int type = DRIVE_TANH) {
    drive_ = amount;
    driveType_ = type;
  }
  void setTone(double cutoffHz) {
    tone_ = cutoffHz;
    toneFilter_.setPole(poleFromCutoff(cutoffHz));
  }
  void setClick(double level, double decayTime) {
    clickLevel_ = level;
    clickDecay_ = std::max(0.001, decayTime);
  }
  void setSub(double mix, double octaveOffset = 1.0) {
    subMix_ = mix;
    subOctave_ = octaveOffset <= 0 ? 1.0 : octaveOffset;
  }
  void setTune(double multiplier) { tune_ = multiplier; }

  void noteOn(stk::StkFloat frequency, stk::StkFloat amplitude) override {
    if(frequency > 0.0) {
      double ratio = pitchEnd_ > 0.0 ? pitchStart_ / pitchEnd_ : 2.0;
      pitchEnd_ = frequency;
      pitchStart_ = frequency * ratio;
    }
    trigger(amplitude);
  }
  void noteOff(stk::StkFloat amplitude) override {
    ampEnv_ = 0.0;
    clickEnv_ = 0.0;
  }

  void trigger(double velocity = 1.0) {
    double sr = stk::Stk::sampleRate();
    phase_ = 0.0;
    subPhase_ = 0.0;
    velocity_ = velocity;

    pitchEnv_ = 1.0;
    pitchCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, pitchDecay_ * sr));
    pitchLinStep_ = 1.0 / std::max(1.0, pitchDecay_ * sr);

    ampEnv_ = 1.0;
    ampCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, ampDecay_ * sr));
    ampLinStep_ = 1.0 / std::max(1.0, ampDecay_ * sr);

    clickEnv_ = 1.0;
    clickCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, clickDecay_ * sr));
  }

  stk::StkFloat tick(unsigned int channel = 0) override {
    double sr = stk::Stk::sampleRate();
    double freq = (pitchEnd_ + (pitchStart_ - pitchEnd_) * pitchEnv_) * tune_;

    phase_ += 2.0 * M_PI * freq / sr;
    if(phase_ > 2.0 * M_PI)
      phase_ -= 2.0 * M_PI;
    double sample = analog_drive(std::sin(phase_), drive_, driveType_);
    sample = toneFilter_.tick(sample);

    if(subMix_ > 0.0) {
      subPhase_ += 2.0 * M_PI * (freq / subOctave_) / sr;
      if(subPhase_ > 2.0 * M_PI)
        subPhase_ -= 2.0 * M_PI;
      sample += subMix_ * std::sin(subPhase_);
    }

    if(clickEnv_ > 1e-4) {
      sample += clickLevel_ * clickEnv_ * noise_.tick();
      clickEnv_ *= clickCoeff_;
    }

    sample *= ampEnv_ * velocity_;

    pitchEnv_ = pitchLinear_ ? std::max(0.0, pitchEnv_ - pitchLinStep_) : pitchEnv_ * pitchCoeff_;
    ampEnv_ = ampLinear_ ? std::max(0.0, ampEnv_ - ampLinStep_) : ampEnv_ * ampCoeff_;

    lastFrame_[0] = sample;
    return sample;
  }

  stk::StkFrames& tick(stk::StkFrames& frames, unsigned int channel = 0) override {
    stk::StkFloat* samples = &frames[channel];
    unsigned int hop = frames.channels();
    for(unsigned int i = 0; i < frames.frames(); i++, samples += hop)
      *samples = tick();
    return frames;
  }

private:
  static double poleFromCutoff(double cutoffHz) {
    double sr = stk::Stk::sampleRate();
    double clamped = std::min(std::max(cutoffHz, 20.0), sr * 0.45);
    return std::exp(-2.0 * M_PI * clamped / sr);
  }

  double pitchStart_, pitchEnd_, pitchDecay_;
  bool pitchLinear_;
  double ampDecay_;
  bool ampLinear_;
  double drive_;
  int driveType_;
  double tone_;
  double clickLevel_, clickDecay_;
  double subMix_, subOctave_;
  double tune_;

  double phase_, subPhase_;
  double pitchEnv_, pitchCoeff_, pitchLinStep_;
  double ampEnv_, ampCoeff_, ampLinStep_;
  double clickEnv_, clickCoeff_;
  double velocity_;

  stk::OnePole toneFilter_;
  stk::Noise noise_;
};
