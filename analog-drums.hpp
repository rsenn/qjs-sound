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

enum {
  TR909_WAVE_SINE = 0,
  TR909_WAVE_TRIANGLE = 1,
};

/* ---- Tr909BassDrum: analog bass drum designer (909-core, mbase-11-ish reach) ----
 *
 * A distorted sine/triangle core with independent, dedicated pitch- and
 * amplitude-decay envelopes -- not ADSR: real analog bass drum circuits
 * are a fast pitch-modulated oscillator plus a single decay stage, and the
 * classic "punch" comes from the pitch settling much faster than the
 * amplitude, not from a multi-stage envelope generator.
 *
 * On top of that 909-style core this adds the controls that separate a
 * one-knob "kick" from a real bass drum designer (Jomox mBase 11 territory):
 * a second, much faster pitch "spike" layered on top of the main drop (the
 * initial transient overshoot analog VCOs get when slammed hard, distinct
 * from the slower tonal settle), a dedicated punch/transient boost on the
 * amplitude stage, a resonant tone filter that tracks the kick's own
 * settling pitch (rather than sitting at a fixed Hz value, which reads as
 * an unrelated second pitched element -- the same mechanism that makes a
 * struck resonant filter sound like a bell elsewhere in this file), and a
 * triangle core option alongside the sine. */
class Tr909BassDrum : public stk::Instrmnt {
public:
  Tr909BassDrum()
      : pitchStart_(400.0), pitchEnd_(55.0), pitchDecay_(0.06), pitchLinear_(false), pitchSpikeAmt_(0.0),
        pitchSpikeTime_(0.005), ampDecay_(0.45), ampLinear_(false), punchAmount_(0.0), punchTime_(0.01),
        drive_(0.35), driveType_(DRIVE_TANH), waveform_(TR909_WAVE_SINE), tone_(6000.0), toneResonance_(0.0),
        clickLevel_(0.5), clickDecay_(0.04), subMix_(0.0), subOctave_(1.0), tune_(1.0), phase_(0.0),
        subPhase_(0.0), pitchEnv_(0.0), pitchCoeff_(1.0), pitchLinStep_(0.0), pitchSpikeEnv_(0.0),
        pitchSpikeCoeff_(1.0), ampEnv_(0.0), ampCoeff_(1.0), ampLinStep_(0.0), punchEnv_(0.0), punchCoeff_(1.0),
        clickEnv_(0.0), clickCoeff_(1.0), velocity_(0.0) {
    toneFilter_.setPole(poleFromCutoff(tone_));
  }

  void setPitchEnvelope(double startFreq, double endFreq, double decayTime, bool linear = false) {
    pitchStart_ = startFreq;
    pitchEnd_ = endFreq > 0 ? endFreq : 1.0;
    pitchDecay_ = std::max(0.001, decayTime);
    pitchLinear_ = linear;
  }
  /* A second, independent pitch overshoot layered on top of the main
   * envelope above, decaying much faster (a handful of ms) -- the analog
   * "double pitch decay" character a plain single-stage pitch envelope
   * can't reach: a near-instant spike on top of the slower tonal drop. */
  void setPitchSpike(double semitones, double timeSeconds) {
    pitchSpikeAmt_ = semitones;
    pitchSpikeTime_ = std::max(0.0005, timeSeconds);
  }
  void setAmpEnvelope(double decayTime, bool linear = false) {
    ampDecay_ = std::max(0.001, decayTime);
    ampLinear_ = linear;
  }
  /* Extra transient gain right at the strike, decaying fast and
   * independently of the main amp envelope -- the "punch" knob dedicated
   * bass drum designers give you instead of just a fast attack stage. */
  void setPunch(double amount, double timeSeconds) {
    punchAmount_ = amount;
    punchTime_ = std::max(0.0005, timeSeconds);
  }
  void setDrive(double amount, int type = DRIVE_TANH) {
    drive_ = amount;
    driveType_ = type;
  }
  void setWaveform(int type) { waveform_ = type; }
  void setTone(double cutoffHz) {
    tone_ = cutoffHz;
    toneFilter_.setPole(poleFromCutoff(cutoffHz));
  }
  /* 0 = plain one-pole rolloff (unchanged default behavior). Above 0 the
   * tone stage adds a resonant peak -- but pinned to a fixed Hz value, that
   * peak reads as a second, unrelated pitched element as soon as the pitch
   * envelope sweeps past or near it (worse with setPitchSpike()), which is
   * exactly what makes a resonant filter sound like a struck bell/cowbell
   * elsewhere in this file (see TwinTDrum). To actually sound like a kick
   * rather than a kick-plus-bell, the resonance instead tracks a multiple
   * of the *current* fundamental each sample (see tick()), capped at the
   * tone_ cutoff -- it reinforces the kick's own pitch as it settles,
   * instead of ringing at some unrelated frequency independent of it. */
  void setToneResonance(double amount) {
    toneResonance_ = std::max(0.0, std::min(0.95, amount));
    if(toneResonance_ <= 0.0)
      resonanceFilter_.setResonance(1000.0, 0.0, true); // force back to identity immediately
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

    pitchSpikeEnv_ = 1.0;
    pitchSpikeCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, pitchSpikeTime_ * sr));

    ampEnv_ = 1.0;
    ampCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, ampDecay_ * sr));
    ampLinStep_ = 1.0 / std::max(1.0, ampDecay_ * sr);

    punchEnv_ = 1.0;
    punchCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, punchTime_ * sr));

    clickEnv_ = 1.0;
    clickCoeff_ = std::pow(1e-3, 1.0 / std::max(1.0, clickDecay_ * sr));
  }

  stk::StkFloat tick(unsigned int channel = 0) override {
    double sr = stk::Stk::sampleRate();
    double baseFreq = pitchEnd_ + (pitchStart_ - pitchEnd_) * pitchEnv_;
    double spikeRatio = pitchSpikeAmt_ != 0.0 ? std::pow(2.0, pitchSpikeAmt_ * pitchSpikeEnv_ / 12.0) : 1.0;
    double freq = baseFreq * spikeRatio * tune_;

    phase_ += 2.0 * M_PI * freq / sr;
    if(phase_ > 2.0 * M_PI)
      phase_ -= 2.0 * M_PI;
    double sample = analog_drive(oscillate(phase_, waveform_), drive_, driveType_);
    sample = toneFilter_.tick(sample);

    if(toneResonance_ > 0.0) {
      /* Track ~3x the *un-spiked* fundamental so the resonant peak follows
       * the kick's own pitch as it settles, capped at the tone_ ceiling.
       * Deliberately ignores the pitch spike so a fast, wide spike doesn't
       * yank the resonance into an unrelated frequency for a few ms. */
      double resoFreq = std::min(tone_, std::max(20.0, baseFreq * 3.0));
      resonanceFilter_.setResonance(resoFreq, toneResonance_, true);
    }
    sample = resonanceFilter_.tick(sample);

    if(subMix_ > 0.0) {
      subPhase_ += 2.0 * M_PI * (freq / subOctave_) / sr;
      if(subPhase_ > 2.0 * M_PI)
        subPhase_ -= 2.0 * M_PI;
      sample += subMix_ * oscillate(subPhase_, waveform_);
    }

    if(clickEnv_ > 1e-4) {
      sample += clickLevel_ * clickEnv_ * noise_.tick();
      clickEnv_ *= clickCoeff_;
    }

    sample *= ampEnv_ * velocity_ * (1.0 + punchAmount_ * punchEnv_);

    pitchEnv_ = pitchLinear_ ? std::max(0.0, pitchEnv_ - pitchLinStep_) : pitchEnv_ * pitchCoeff_;
    pitchSpikeEnv_ *= pitchSpikeCoeff_;
    ampEnv_ = ampLinear_ ? std::max(0.0, ampEnv_ - ampLinStep_) : ampEnv_ * ampCoeff_;
    punchEnv_ *= punchCoeff_;

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

  static double oscillate(double phase, int waveform) {
    switch(waveform) {
      case TR909_WAVE_TRIANGLE: return (2.0 / M_PI) * std::asin(std::sin(phase));
      default: return std::sin(phase);
    }
  }

  double pitchStart_, pitchEnd_, pitchDecay_;
  bool pitchLinear_;
  double pitchSpikeAmt_, pitchSpikeTime_;
  double ampDecay_;
  bool ampLinear_;
  double punchAmount_, punchTime_;
  double drive_;
  int driveType_;
  int waveform_;
  double tone_, toneResonance_;
  double clickLevel_, clickDecay_;
  double subMix_, subOctave_;
  double tune_;

  double phase_, subPhase_;
  double pitchEnv_, pitchCoeff_, pitchLinStep_;
  double pitchSpikeEnv_, pitchSpikeCoeff_;
  double ampEnv_, ampCoeff_, ampLinStep_;
  double punchEnv_, punchCoeff_;
  double clickEnv_, clickCoeff_;
  double velocity_;

  stk::OnePole toneFilter_;
  stk::TwoPole resonanceFilter_;
  stk::Noise noise_;
};
