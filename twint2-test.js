// twint2-test.js — the same Twin-T drum design as TwinTDrum
// (analog-drums.hpp), rebuilt from scratch using only LabSound's WebAudio-
// style node graph, entirely in JS. No native C++ class involved: this is
// a genuine port of the DSP idea, not a wrapper around the STK one.
//
// STK's TwinTDrum models a Twin-T RC notch circuit wired into an inverting
// feedback loop: struck, it rings down at its notch frequency like a tom,
// conga or woodblock. In STK that's a stk::TwoPole resonator pinged with a
// single-sample impulse. The exact same idea maps directly onto a
// WebAudio-style graph:
//
//   impulse (1-sample AudioBuffer) --> BiquadFilterNode(bandpass) --> ...
//
// A bandpass biquad excited by an impulse rings down at its center
// frequency, with the ring length set by Q -- the WebAudio equivalent of
// STK's pole radius. qForDecay() below converts a T60 decay time (seconds)
// into the Q that gives roughly that decay, using the same math STK's
// radiusFor() uses internally (T60 -> pole radius -> here, -> Q instead).
//
// The rest of TwinTDrum's design carries over the same way:
//   - setPitchDrop()  -> BiquadFilterNode.frequency AudioParam automation
//                        (setValueAtTime + exponentialRampToValueAtTime),
//                        started ABOVE the target frequency and dropping
//                        down to it -- the classic analog-tom "boing".
//   - setSecondary()  -> a second, detuned BiquadFilterNode fed the same
//                        impulse, mixed in through a GainNode.
//   - setClick()      -> a NoiseNode burst, gated by a fast GainNode decay,
//                        summed in before the drive stage (matching STK,
//                        where the click is added before analog_drive()).
//   - setDrive()       -> a WaveShaperNode built from the exact same
//                        tanh/cubic/fold formula as analog_drive() in
//                        analog-drums.hpp, ported line-for-line to JS.
//
// The kit below reuses the exact tuning from twintdrum-test.js's (already
// long-release, distinct-pitch-drop-tuned) kit, so the two files render a
// direct A/B comparison of the same instrument design on two different
// synthesis engines.

import { AudioContext, BiquadFilterNode, GainNode, WaveShaperNode, NoiseNode, AudioBufferSourceNode, AudioBuffer } from 'labsound';

const SR = 44100;

/* ---------- analog_drive(), ported line-for-line from analog-drums.hpp ---------- */
function analogDrive(x, amount, type) {
  if(amount <= 0.0)
    return x;

  let driven = x * (1.0 + amount * 8.0);

  switch(type) {
    case 'cubic': {
      if(driven > 1.0)
        driven = 1.0;
      else if(driven < -1.0)
        driven = -1.0;
      return driven - (driven * driven * driven) / 3.0;
    }
    case 'fold': {
      for(let i = 0; i < 16 && (driven > 1.0 || driven < -1.0); i++) {
        if(driven > 1.0)
          driven = 2.0 - driven;
        else if(driven < -1.0)
          driven = -2.0 - driven;
      }
      return driven;
    }
    default: return Math.tanh(driven);
  }
}

function buildDriveCurve(amount, type, n = 2048) {
  const curve = new Float32Array(n);
  for(let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * 2 - 1; // -1..1
    curve[i] = analogDrive(x, amount, type);
  }
  return curve;
}

// T60 (seconds) -> BiquadFilterNode Q, using the same relationship STK's
// radiusFor() uses (r = 10^(-3/(t60*sr))), converted from pole radius to Q
// via the standard narrowband bandpass approximation BW_Hz ~= (1-r)*sr/pi,
// Q = f0/BW_Hz. The sr terms cancel, so Q depends only on frequency and T60.
function qForDecay(freq, t60) {
  return Math.max(0.3, Math.min(500, (Math.PI * freq * t60) / (3 * Math.LN10)));
}

// Gain needed to counteract a "constant peak gain" bandpass biquad's
// amplitude normalization when struck with a single-sample impulse: its
// impulse-response peak is empirically (2*pi*freq/sampleRate)/Q times the
// input amplitude, so this is that ratio's reciprocal. See the comment in
// the TwinT2 constructor for the full explanation.
function excitationGain(freq, q, sampleRate) {
  return (q * sampleRate) / (2 * Math.PI * freq);
}

/* ---------- TwinT2: the Twin-T drum, built from LabSound nodes ---------- */
class TwinT2 {
  constructor(ctx, frequency = 200) {
    this.ctx = ctx;
    this.frequency = frequency;
    this.decay = 0.25;
    this.driveAmount = 0.0;
    this.driveType = 'tanh';
    this.secondaryRatio = 1.5;
    this.secondaryMix = 0.0;
    this.clickAmount = 0.0;
    this.pitchDropSemitones = 0;
    this.pitchDropTime = 0.03;

    this.resonator = new BiquadFilterNode(ctx, { type: 'bandpass', frequency, Q: 1 });
    this.secondary = new BiquadFilterNode(ctx, { type: 'bandpass', frequency: frequency * this.secondaryRatio, Q: 1 });
    this.secondaryGain = new GainNode(ctx, { gain: 0 });
    // Separate excitation-gain stages for each resonator (not one shared
    // gain): a "constant peak gain" bandpass biquad -- like this one, and
    // like the WebAudio spec's bandpass generally -- normalizes for a
    // *continuous* sinusoidal drive, not a single-sample impulse. Struck
    // with a plain impulse, its peak ring-out is only ~(2*pi*freq/sr)/Q of
    // the input amplitude (confirmed empirically: peak*Q/freq is constant
    // and equals 2*pi/sr to 4 significant figures) -- tiny enough, for a
    // low-frequency high-Q tom, to fall below the WaveShaperNode curve's
    // lookup resolution and disappear into quantization noise instead of
    // ringing down audibly. excitationGain() cancels that out so strike()
    // peaks at roughly `amplitude` regardless of tuning/decay, matching
    // the same fix applied to TwinTDrum's TwoPole resonator in
    // analog-drums.hpp (there via normalize=false + sin(w0) scaling; here
    // via a compensating gain stage, since BiquadFilterNode doesn't expose
    // an unnormalized mode).
    this.strikeGain = new GainNode(ctx, { gain: 1 });
    this.secondaryStrikeGain = new GainNode(ctx, { gain: 0 });
    this.shaper = new WaveShaperNode(ctx, { curve: buildDriveCurve(0, 'tanh') });
    this.output = new GainNode(ctx, { gain: 1 });

    this.strikeGain.connect(this.resonator);
    this.resonator.connect(this.shaper);
    this.secondaryStrikeGain.connect(this.secondary);
    this.secondary.connect(this.secondaryGain);
    this.secondaryGain.connect(this.shaper);
    this.shaper.connect(this.output);

    // A shared 1-sample impulse buffer. AudioBufferSourceNode is a
    // single-use source (like every AudioScheduledSourceNode), so each
    // strike() still needs a fresh source node -- but they can all point
    // at this same buffer.
    this._impulse = new AudioBuffer({ numberOfChannels: 1, length: 1, sampleRate: ctx.sampleRate });
    this._impulse.copyToChannel(new Float32Array([1.0]), 0);
  }

  setDecay(t60) { this.decay = t60; return this; }
  setDrive(amount, type = 'tanh') {
    this.driveAmount = amount;
    this.driveType = type;
    this.shaper.curve = buildDriveCurve(amount, type);
    return this;
  }
  setPitchDrop(semitones, time = 0.03) {
    this.pitchDropSemitones = semitones;
    this.pitchDropTime = Math.max(0.0005, time);
    return this;
  }
  setSecondary(ratio, mix) {
    this.secondaryRatio = ratio;
    this.secondaryMix = mix;
    this.secondary.frequency.value = this.frequency * ratio;
    return this;
  }
  setClick(amount) { this.clickAmount = amount; return this; }

  connect(dest) { return this.output.connect(dest); }

  strike(t, amplitude = 1.0) {
    const sr = this.ctx.sampleRate;
    const q = qForDecay(this.frequency, this.decay);
    this.resonator.Q.setValueAtTime(q, t);
    this.resonator.frequency.cancelScheduledValues(t);
    // The impulse lands at the frequency the resonator is actually tuned
    // to at time t, so the excitation-gain compensation has to match that
    // (the peak-frequency overshoot when a pitch drop is active), not the
    // settled frequency it ramps down to afterwards.
    let strikeFreq = this.frequency;
    if(this.pitchDropSemitones !== 0) {
      strikeFreq = this.frequency * Math.pow(2, this.pitchDropSemitones / 12);
      this.resonator.frequency.setValueAtTime(strikeFreq, t);
      this.resonator.frequency.exponentialRampToValueAtTime(this.frequency, t + this.pitchDropTime);
    } else {
      this.resonator.frequency.setValueAtTime(strikeFreq, t);
    }

    const src = new AudioBufferSourceNode(this.ctx, { buffer: this._impulse });
    src.connect(this.strikeGain);
    this.strikeGain.gain.setValueAtTime(amplitude * excitationGain(strikeFreq, q, sr), t);

    if(this.secondaryMix > 0) {
      const f2 = this.frequency * this.secondaryRatio;
      const q2 = qForDecay(f2, this.decay);
      this.secondary.frequency.cancelScheduledValues(t);
      this.secondary.frequency.setValueAtTime(f2, t);
      this.secondary.Q.setValueAtTime(q2, t);
      this.secondaryGain.gain.setValueAtTime(this.secondaryMix, t);
      this.secondaryStrikeGain.gain.setValueAtTime(amplitude * excitationGain(f2, q2, sr), t);
      src.connect(this.secondaryStrikeGain);
    } else {
      this.secondaryGain.gain.setValueAtTime(0, t);
      this.secondaryStrikeGain.gain.setValueAtTime(0, t);
    }
    src.start(t);

    if(this.clickAmount > 0) {
      const noise = new NoiseNode(this.ctx, { type: 'white' });
      const clickGain = new GainNode(this.ctx, { gain: 0 });
      noise.connect(clickGain);
      clickGain.connect(this.shaper);
      clickGain.gain.setValueAtTime(this.clickAmount * amplitude, t);
      clickGain.gain.setTargetAtTime(0.0001, t, 0.006); // ~6ms time constant -- fast, snappy click
      noise.start(t);
      noise.stop(t + 0.15);
    }
  }
}

/* ---------- build the kit: same tuning as twintdrum-test.js ---------- */

async function main() {
  const bpm = 100;
  const step = 60 / bpm / 4; // 16th notes
  const totalSteps = 32;

  const lowTom = { voice: null, tailSeconds: 0.9 * 3 };
  const midTom = { voice: null, tailSeconds: 0.7 * 3 };
  const hiTom = { voice: null, tailSeconds: 0.55 * 3 };
  const cowbell = { voice: null, tailSeconds: 0.4 * 3 };
  const woodblock = { voice: null, tailSeconds: 0.4 };

  // Any source scheduled to start within the offline context's very first
  // processing quantum (128 samples, ~2.9ms) renders completely silent --
  // a LabSound scheduler quirk confirmed by direct probing (a source
  // start()ed at t=0 or anywhere before sample 128 produces no output at
  // all; from sample 128 on it works normally, just delayed to the next
  // quantum boundary). LEAD_IN keeps every scheduled event safely past
  // that dead zone.
  const LEAD_IN = 0.01;

  const longestTail = Math.max(lowTom.tailSeconds, midTom.tailSeconds, hiTom.tailSeconds, cowbell.tailSeconds, woodblock.tailSeconds);
  const totalFrames = Math.ceil((LEAD_IN + totalSteps * step + longestTail) * SR);

  const ctx = new AudioContext(true, true, { numberOfChannels: 1, length: totalFrames, sampleRate: SR });

  lowTom.voice = new TwinT2(ctx, 100);
  lowTom.voice.setDecay(0.9).setDrive(0.12, 'tanh').setPitchDrop(8, 0.09).setClick(0.08);
  lowTom.voice.connect(ctx.destination);

  midTom.voice = new TwinT2(ctx, 165);
  midTom.voice.setDecay(0.7).setDrive(0.16, 'tanh').setPitchDrop(9, 0.075).setClick(0.1);
  midTom.voice.connect(ctx.destination);

  hiTom.voice = new TwinT2(ctx, 240);
  hiTom.voice.setDecay(0.55).setDrive(0.2, 'tanh').setPitchDrop(10, 0.06).setClick(0.12);
  hiTom.voice.connect(ctx.destination);

  cowbell.voice = new TwinT2(ctx, 560);
  cowbell.voice.setDecay(0.4).setDrive(0.6, 'tanh').setSecondary(1.48, 0.8).setClick(0.4);
  cowbell.voice.connect(ctx.destination);

  woodblock.voice = new TwinT2(ctx, 900);
  woodblock.voice.setDecay(0.08).setSecondary(2.0, 0.5).setClick(0.5);
  woodblock.voice.connect(ctx.destination);

  /* ---------- the same solo groove pattern as twintdrum-test.js ---------- */
  const pattern = [
    [lowTom, 1.0], 0, [woodblock, 0.6], 0,
    [midTom, 0.9], [woodblock, 0.4], 0, [hiTom, 0.7],
    [cowbell, 0.8], 0, [woodblock, 0.6], [midTom, 0.5],
    0, [hiTom, 0.9], [woodblock, 0.4], [lowTom, 1.0],
    [midTom, 0.8], 0, [woodblock, 0.6], 0,
    [hiTom, 0.6], [cowbell, 0.9], 0, [woodblock, 0.4],
    [lowTom, 0.9], [midTom, 0.6], 0, [hiTom, 0.8],
    [woodblock, 0.5], [cowbell, 1.0], [lowTom, 0.7], [hiTom, 1.0],
  ];

  // With a graph-scheduled engine there's no per-sample tick() loop --
  // every strike is just scheduled at its absolute time up front, and
  // startRendering() resolves the whole timeline in one shot.
  for(let i = 0; i < totalSteps; i++) {
    const hit = pattern[i % pattern.length];
    if(!hit)
      continue;
    const [voice, velocity] = hit;
    voice.voice.strike(LEAD_IN + i * step, velocity * 0.8);
  }

  const rendered = await ctx.startRendering();

  // The excitation-gain compensation aims each strike at peak~=amplitude in
  // isolation, but overlapping voices (a new hit landing while a previous
  // one is still ringing) can still sum past 0dB -- normalize to a safe
  // target peak before writing, same as twintdrum-test.js.
  const data = rendered.getChannelData(0);
  let peak = 0;
  for(let i = 0; i < data.length; i++)
    peak = Math.max(peak, Math.abs(data[i]));
  const TARGET_PEAK = 0.95;
  if(peak > TARGET_PEAK) {
    const norm = TARGET_PEAK / peak;
    for(let i = 0; i < data.length; i++)
      data[i] *= norm;
    rendered.copyToChannel(data, 0);
  }

  rendered.writeToWav('twint2-test.wav');
  console.log(`Rendered ${(totalFrames / SR).toFixed(2)}s -> twint2-test.wav (peak was ${peak.toFixed(3)})`);
}

main();
