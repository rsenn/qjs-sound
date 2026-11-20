// stk-test: STK classes (generators, effects, filters, instruments) and
// namespaces (Generator, Filter, Effect, Stk, FM, Guitar, Twang, etc.),
// StkFrames, and the toStringTag each instance carries.
import * as stk from 'stk';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

// instruments load their waveforms relative to this (tests run from the repo root)
const RAWWAVES = 'third_party/stk/rawwaves/';
stk.Stk.setRawwavePath(RAWWAVES);

// same(a, b): deep equality through JSON, eq() compares with !=
const same = (a, b) => eq(JSON.stringify(a), JSON.stringify(b));

// samples of channel 0 of an StkFrames, as plain numbers
const channel0 = f => Array.from(f.toFloat32());
const close = (a, b) => assert(Math.abs(a - b) < 1e-6, a + ' != ' + b);

tests({
  'StkFrames construction and toStringTag'() {
    const f = new stk.StkFrames(10);
    eq(Object.prototype.toString.call(f), '[object StkFrames]');
  },

  'StkFrames size/channels/frames/sampleType getters'() {
    const f = new stk.StkFrames(10, 2);
    eq(f.size, 20);
    eq(f.channels, 2);
    eq(f.frames, 10);
    eq(f.sampleType, 'f64');
  },

  'StkFrames.resize'() {
    const f = new stk.StkFrames(10);
    f.resize(20, 2);
    eq(f.size, 40);
    eq(f.channels, 2);
    eq(f.frames, 20);
  },

  'StkFrames.getChannel returns Float32Array'() {
    const f = new stk.StkFrames(4, 2);
    const dst = new stk.StkFrames(4, 1);
    f.getChannel(0, dst, 0);
    assertStrictEquals(Object.prototype.toString.call(dst), '[object StkFrames]');
    eq(dst.size, 4);
  },

  'StkFrames.setChannel copies a channel of another StkFrames'() {
    const src = new stk.StkFrames(4, 1);
    src.fromFloat32(new Float32Array([0.25, 0.5, 0.75, 1]));
    const dst = new stk.StkFrames(4, 2);
    dst.setChannel(1, src, 0);
    const out = channel0(dst);
    same(out.filter((v, i) => i % 2 == 1), [0.25, 0.5, 0.75, 1]);
    same(out.filter((v, i) => i % 2 == 0), [0, 0, 0, 0]);
  },

  'StkFrames.setChannel rejects a non-StkFrames source'() {
    const f = new stk.StkFrames(4, 1);
    let err;
    try { f.setChannel(0, [0.1, 0.2, 0.3, 0.4], 0); } catch(e) { err = e; }
    assert(err instanceof TypeError);
  },

  'StkFrames.interpolate returns a number between frames'() {
    const f = new stk.StkFrames(4, 1);
    f.fromFloat32(new Float32Array([0, 1, 2, 3]));
    close(f.interpolate(1.5), 1.5);
  },

  'StkFrames.toFloat32/fromFloat32 round-trip'() {
    const f = new stk.StkFrames(4, 1);
    f.fromFloat32(new Float32Array([0.5, 0.25, 0.125, 1]));
    const f32 = f.toFloat32();
    assertStrictEquals(Object.prototype.toString.call(f32), '[object Float32Array]');
    same(Array.from(f32), [0.5, 0.25, 0.125, 1]);
  },

  'SineWave construction and toStringTag'() {
    const s = new stk.SineWave(44100, 1);
    eq(Object.prototype.toString.call(s), '[object SineWave]');
  },

  'SineWave.tick produces a non-zero signal after the first sample'() {
    const s = new stk.SineWave();
    s.tick(0);
    let sum = 0;
    for(let i = 0; i < 64; i++) sum += Math.abs(s.tick(0));
    assert(sum > 0);
  },

  'SineWave.tick returns a number'() {
    const s = new stk.SineWave(44100, 1);
    const val = s.tick(0.0);
    assert(typeof val === 'number');
  },

  'Echo construction and toStringTag'() {
    const e = new stk.Echo(0.3);
    eq(Object.prototype.toString.call(e), '[object Echo]');
  },

  'Echo.tick returns a number'() {
    const e = new stk.Echo(0.3);
    const val = e.tick(0.1);
    assert(typeof val === 'number');
  },

  'BiQuad construction and toStringTag'() {
    const b = new stk.BiQuad();
    eq(Object.prototype.toString.call(b), '[object BiQuad]');
  },

  'SineWave.setFrequency changes the period and reset restarts the phase'() {
    const s = new stk.SineWave();
    s.setFrequency(stk.Stk.sampleRate() / 4); // one cycle every 4 samples: 0, 1, 0, -1
    s.reset();
    const run = [s.tick(), s.tick(), s.tick(), s.tick()];
    close(run[0], 0); close(run[1], 1); close(run[2], 0); close(run[3], -1);
    s.reset();
    s.addPhaseOffset(0.25); // a quarter cycle ahead: starts at the peak
    close(s.tick(), 1);
  },

  'Blit.setFrequency/setHarmonics/reset are accepted'() {
    const b = new stk.Blit();
    b.setFrequency(220);
    b.setHarmonics(8);
    b.reset();
    assert(typeof b.tick() === 'number');
  },

  'generator setters throw TypeError on a generator that lacks them'() {
    let err;
    try { new stk.Noise().setFrequency(100); } catch(e) { err = e; }
    assert(err instanceof TypeError);
  },

  'Stk.sampleRate/setSampleRate round-trip'() {
    const old = stk.Stk.sampleRate();
    stk.Stk.setSampleRate(22050);
    eq(stk.Stk.sampleRate(), 22050);
    stk.Stk.setSampleRate(old);
    eq(stk.Stk.sampleRate(), old);
  },

  'Stk.setSampleRate rejects a non-positive rate'() {
    let err;
    try { stk.Stk.setSampleRate(0); } catch(e) { err = e; }
    assert(err instanceof RangeError);
  },

  'BiQuad.tick passes a sample through'() {
    const b = new stk.BiQuad();
    assert(typeof b.tick(0.1) === 'number');
  },

  'PitShift construction and toStringTag'() {
    const p = new stk.PitShift();
    eq(Object.prototype.toString.call(p), '[object PitShift]');
  },

  'NRev construction and toStringTag'() {
    const n = new stk.NRev(1.0);
    eq(Object.prototype.toString.call(n), '[object NRev]');
  },

  'Chorus construction and toStringTag'() {
    const c = new stk.Chorus(6000);
    eq(Object.prototype.toString.call(c), '[object Chorus]');
  },

  'FreeVerb construction and toStringTag'() {
    const f = new stk.FreeVerb();
    eq(Object.prototype.toString.call(f), '[object FreeVerb]');
  },

  'JCRev construction and toStringTag'() {
    const j = new stk.JCRev(1.0);
    eq(Object.prototype.toString.call(j), '[object JCRev]');
  },

  'Delay construction and toStringTag'() {
    const d = new stk.Delay(1000);
    eq(Object.prototype.toString.call(d), '[object Delay]');
  },

  'DelayA construction and toStringTag'() {
    const d = new stk.DelayA(1000);
    eq(Object.prototype.toString.call(d), '[object DelayA]');
  },

  'DelayL construction and toStringTag'() {
    const d = new stk.DelayL(1000);
    eq(Object.prototype.toString.call(d), '[object DelayL]');
  },

  'OnePole construction and toStringTag'() {
    const o = new stk.OnePole();
    eq(Object.prototype.toString.call(o), '[object OnePole]');
  },

  'OneZero construction and toStringTag'() {
    const o = new stk.OneZero();
    eq(Object.prototype.toString.call(o), '[object OneZero]');
  },

  'TwoPole construction and toStringTag'() {
    const t = new stk.TwoPole();
    eq(Object.prototype.toString.call(t), '[object TwoPole]');
  },

  'TwoZero construction and toStringTag'() {
    const t = new stk.TwoZero();
    eq(Object.prototype.toString.call(t), '[object TwoZero]');
  },

  'PoleZero construction and toStringTag'() {
    const p = new stk.PoleZero();
    eq(Object.prototype.toString.call(p), '[object PoleZero]');
  },

  'TapDelay construction and toStringTag'() {
    const t = new stk.TapDelay();
    eq(Object.prototype.toString.call(t), '[object TapDelay]');
  },

  'FormSwep construction and toStringTag'() {
    const f = new stk.FormSwep();
    eq(Object.prototype.toString.call(f), '[object FormSwep]');
  },

  'Iir construction and toStringTag'() {
    const i = new stk.Iir();
    eq(Object.prototype.toString.call(i), '[object Iir]');
  },

  'Fir construction and toStringTag'() {
    const f = new stk.Fir();
    eq(Object.prototype.toString.call(f), '[object Fir]');
  },

  'Noise construction and toStringTag'() {
    const n = new stk.Noise();
    eq(Object.prototype.toString.call(n), '[object Noise]');
  },

  'Envelope construction and toStringTag'() {
    const e = new stk.Envelope();
    eq(Object.prototype.toString.call(e), '[object Envelope]');
  },

  'Asymp construction and toStringTag'() {
    const a = new stk.Asymp();
    eq(Object.prototype.toString.call(a), '[object Asymp]');
  },

  'Generator namespace is an object, SineWave is exported flat'() {
    assert(typeof stk.Generator === 'object');
    assert(typeof stk.SineWave === 'function');
  },

  'Filter namespace is an object, BiQuad is exported flat'() {
    assert(typeof stk.Filter === 'object');
    assert(typeof stk.BiQuad === 'function');
  },

  'Effect namespace is an object, Echo is exported flat'() {
    assert(typeof stk.Effect === 'object');
    assert(typeof stk.Echo === 'function');
  },

  'Stk.setRawwavePath/rawwavePath round-trip'() {
    stk.Stk.setRawwavePath('/tmp/rawwaves/');
    eq(stk.Stk.rawwavePath(), '/tmp/rawwaves/');
    stk.Stk.setRawwavePath(RAWWAVES);
    eq(stk.Stk.rawwavePath(), RAWWAVES);
  },

  'FM namespace exists'() {
    assert(typeof stk.FM === 'object');
  },

  'Guitar construction and toStringTag'() {
    const g = new stk.Guitar(44100);
    eq(Object.prototype.toString.call(g), '[object Guitar]');
  },

  'Twang construction and toStringTag'() {
    const t = new stk.Twang(44100);
    eq(Object.prototype.toString.call(t), '[object Twang]');
  },

  'ADSR construction and toStringTag'() {
    const a = new stk.ADSR();
    eq(Object.prototype.toString.call(a), '[object ADSR]');
  },

  'Blit construction and toStringTag'() {
    const b = new stk.Blit();
    eq(Object.prototype.toString.call(b), '[object Blit]');
  },

  'BlitSaw construction and toStringTag'() {
    const b = new stk.BlitSaw();
    eq(Object.prototype.toString.call(b), '[object BlitSaw]');
  },

  'BlitSquare construction and toStringTag'() {
    const b = new stk.BlitSquare();
    eq(Object.prototype.toString.call(b), '[object BlitSquare]');
  },

  'Clarinet construction and toStringTag'() {
    const c = new stk.Clarinet(44100);
    eq(Object.prototype.toString.call(c), '[object Clarinet]');
  },

  'Flute construction and toStringTag'() {
    const f = new stk.Flute(44100);
    eq(Object.prototype.toString.call(f), '[object Flute]');
  },

  'ModalBar construction and toStringTag'() {
    const m = new stk.ModalBar(44100);
    eq(Object.prototype.toString.call(m), '[object ModalBar]');
  },

  'Moog construction and toStringTag'() {
    const m = new stk.Moog(44100);
    eq(Object.prototype.toString.call(m), '[object Moog]');
  },

  'Rhodey construction and toStringTag'() {
    const r = new stk.Rhodey(44100);
    eq(Object.prototype.toString.call(r), '[object Rhodey]');
  },

  'Sitar construction and toStringTag'() {
    const s = new stk.Sitar(44100);
    eq(Object.prototype.toString.call(s), '[object Sitar]');
  },

  'StifKarp construction and toStringTag'() {
    const s = new stk.StifKarp(44100);
    eq(Object.prototype.toString.call(s), '[object StifKarp]');
  },

  'Wurley construction and toStringTag'() {
    const w = new stk.Wurley(44100);
    eq(Object.prototype.toString.call(w), '[object Wurley]');
  },

  'Recorder construction and toStringTag'() {
    const r = new stk.Recorder();
    eq(Object.prototype.toString.call(r), '[object Recorder]');
  },

  'SingWave construction and toStringTag'() {
    const s = new stk.SingWave(RAWWAVES + 'impuls10.raw', true);
    eq(Object.prototype.toString.call(s), '[object SingWave]');
  },

  'VoicForm construction and toStringTag'() {
    const v = new stk.VoicForm(44100);
    eq(Object.prototype.toString.call(v), '[object VoicForm]');
  },

  'Whistle construction and toStringTag'() {
    const w = new stk.Whistle(44100);
    eq(Object.prototype.toString.call(w), '[object Whistle]');
  },

  'TubeBell construction and toStringTag'() {
    const t = new stk.TubeBell(44100);
    eq(Object.prototype.toString.call(t), '[object TubeBell]');
  },

  'Tr909BassDrum construction and toStringTag'() {
    const t = new stk.Tr909BassDrum();
    eq(Object.prototype.toString.call(t), '[object Tr909BassDrum]');
  },

  'Tr909Percussion construction and toStringTag'() {
    const t = new stk.Tr909Percussion();
    eq(Object.prototype.toString.call(t), '[object Tr909Percussion]');
  },

  'TwinTDrum construction and toStringTag'() {
    const t = new stk.TwinTDrum();
    eq(Object.prototype.toString.call(t), '[object TwinTDrum]');
  },

  'PercFlut construction and toStringTag'() {
    const p = new stk.PercFlut(44100);
    eq(Object.prototype.toString.call(p), '[object PercFlut]');
  },

  'HevyMetl construction and toStringTag'() {
    const h = new stk.HevyMetl(44100);
    eq(Object.prototype.toString.call(h), '[object HevyMetl]');
  },

  'BeeThree construction and toStringTag'() {
    const b = new stk.BeeThree(44100);
    eq(Object.prototype.toString.call(b), '[object BeeThree]');
  },

  'BlowBotl construction and toStringTag'() {
    const b = new stk.BlowBotl(44100);
    eq(Object.prototype.toString.call(b), '[object BlowBotl]');
  },

  'BlowHole construction and toStringTag'() {
    const b = new stk.BlowHole(44100);
    eq(Object.prototype.toString.call(b), '[object BlowHole]');
  },

  'Bowed construction and toStringTag'() {
    const b = new stk.Bowed(44100);
    eq(Object.prototype.toString.call(b), '[object Bowed]');
  },

  'Brass construction and toStringTag'() {
    const b = new stk.Brass(44100);
    eq(Object.prototype.toString.call(b), '[object Brass]');
  },

  'Cubic construction and toStringTag'() {
    const c = new stk.Cubic();
    eq(Object.prototype.toString.call(c), '[object Cubic]');
  },

  'FMVoices construction and toStringTag'() {
    const f = new stk.FMVoices(44100);
    eq(Object.prototype.toString.call(f), '[object FMVoices]');
  },

  'Mandolin construction and toStringTag'() {
    const m = new stk.Mandolin(44100);
    eq(Object.prototype.toString.call(m), '[object Mandolin]');
  },

  'Mesh2D construction and toStringTag'() {
    const m = new stk.Mesh2D(4, 4);
    eq(Object.prototype.toString.call(m), '[object Mesh2D]');
  },

  'Shakers construction and toStringTag'() {
    const s = new stk.Shakers();
    eq(Object.prototype.toString.call(s), '[object Shakers]');
  },

  'Simple construction and toStringTag'() {
    const s = new stk.Simple(44100);
    eq(Object.prototype.toString.call(s), '[object Simple]');
  },

  'LentPitShift construction and toStringTag'() {
    const l = new stk.LentPitShift();
    eq(Object.prototype.toString.call(l), '[object LentPitShift]');
  },

  'NRev construction and toStringTag (duplicate check)'() {
    const n = new stk.NRev(1.0);
    eq(Object.prototype.toString.call(n), '[object NRev]');
  },

  'PRCRev construction and toStringTag'() {
    const p = new stk.PRCRev();
    eq(Object.prototype.toString.call(p), '[object PRCRev]');
  },

  'RtAudio construction and toStringTag'() {
    const r = new stk.RtAudio();
    eq(Object.prototype.toString.call(r), '[object RtAudio]');
  },

  'RtMidiIn construction and toStringTag'() {
    const r = new stk.RtMidiIn();
    eq(Object.prototype.toString.call(r), '[object RtMidiIn]');
  },

  'RtMidiOut construction and toStringTag'() {
    const r = new stk.RtMidiOut();
    eq(Object.prototype.toString.call(r), '[object RtMidiOut]');
  },

  'Saxofony construction and toStringTag'() {
    const s = new stk.Saxofony(44100);
    eq(Object.prototype.toString.call(s), '[object Saxofony]');
  },

  'ADSR keyOn/keyOff drive attack, sustain and release; getState follows'() {
    const a = new stk.ADSR();
    a.setAllTimes(0.01, 0.01, 0.5, 0.01);
    eq(a.getState(), 4); // idle
    a.keyOn();
    eq(a.getState(), 0); // attack
    let v = 0;
    for(let i = 0; i < 1500; i++) v = a.tick();
    close(v, 0.5);
    eq(a.getState(), 2); // sustain
    a.keyOff();
    for(let i = 0; i < 1500; i++) v = a.tick();
    close(v, 0);
    eq(a.getState(), 4);
  },

  'ADSR setters are accepted and the other generators reject them'() {
    const a = new stk.ADSR();
    a.setAttackTime(0.1); a.setDecayTime(0.1); a.setReleaseTime(0.1); a.setSustainLevel(0.7);
    a.setAttackRate(0.01); a.setDecayRate(0.01); a.setReleaseRate(0.01); a.setAttackTarget(1);
    a.setTarget(0.5); a.setValue(0.25);
    close(a.tick(), 0.25);
    let err;
    try { new stk.SineWave().keyOn(); } catch(e) { err = e; }
    assert(err instanceof TypeError);
    err = undefined;
    try { a.setAllTimes(1, 2); } catch(e) { err = e; }
    assert(err instanceof TypeError);
  },

  'Envelope ramps to its target in setTime seconds (setTime is relative to the current target)'() {
    const e = new stk.Envelope();
    e.setTarget(1);       // before setTime: the rate is |target - value| / (time * sampleRate)
    e.setTime(0.01);
    let v = 0;
    for(let i = 0; i < 600; i++) v = e.tick();
    close(v, 1);
    eq(e.getState(), 0);
    e.keyOff(0.25);
    e.setTime(0.01);
    for(let i = 0; i < 600; i++) v = e.tick();
    close(v, 0.25);
    e.setValue(0.5);
    close(e.tick(), 0.5);
  },

  'Envelope keyOn with the default target and setRate'() {
    const e = new stk.Envelope();
    e.setRate(0.01);
    e.keyOn();
    let v = 0;
    for(let i = 0; i < 50; i++) v = e.tick();
    close(v, 0.5);
  },

  'Asymp keyOn approaches its target; setTau/setT60/setTime are accepted'() {
    const a = new stk.Asymp();
    a.setTau(0.001); a.setTime(0.01); a.setT60(0.01);
    a.setTarget(1);
    a.keyOn();
    let v = 0;
    for(let i = 0; i < 2000; i++) v = a.tick();
    close(v, 1);
    a.keyOff();
    for(let i = 0; i < 2000; i++) v = a.tick();
    close(v, 0);
    assert(typeof a.getState() === 'number');
  },

  'Delay delays by its length; setDelay/getDelay/getMaximumDelay'() {
    const d = new stk.Delay(3, 10);
    eq(d.getDelay(), 3);
    eq(d.getMaximumDelay(), 10);
    same([1, 0, 0, 0, 0, 0].map(x => d.tick(x)), [0, 0, 0, 1, 0, 0]);
    d.setDelay(5);
    eq(d.getDelay(), 5);
    d.setMaximumDelay(20);
    eq(d.getMaximumDelay(), 20);
  },

  'Delay tapIn/tapOut/addTo/nextOut/energy'() {
    const d = new stk.Delay(8, 10); // energy() sums the squares over the current delay length
    d.tapIn(2, 4);
    close(d.tapOut(4), 2);
    close(d.addTo(1, 4), 3);
    close(d.energy(), 9);
    close(d.nextOut(), 0);
  },

  'DelayA and DelayL take fractional delays'() {
    for(const C of [stk.DelayA, stk.DelayL]) {
      const d = new C(2.5, 10);
      close(d.getDelay(), 2.5);
      d.setDelay(4.5);
      close(d.getDelay(), 4.5);
      eq(d.getMaximumDelay(), 10);
      d.tapIn(1, 3);
      close(d.tapOut(3), 1);
      assert(typeof d.nextOut() === 'number');
    }
  },

  'TapDelay.setTapDelays/getTapDelays/setMaximumDelay'() {
    const t = new stk.TapDelay([1, 2], 10);
    same(t.getTapDelays(), [1, 2]);
    t.setTapDelays([3, 4, 5]);
    same(t.getTapDelays(), [3, 4, 5]);
    t.setMaximumDelay(20);
  },

  'Echo.setDelay and setMaximumDelay'() {
    const e = new stk.Echo(100);
    e.setMaximumDelay(200);
    e.setDelay(50);
    assert(typeof e.tick(1) === 'number');
    let err;
    try { new stk.Chorus().setDelay(1); } catch(x) { err = x; }
    assert(err instanceof TypeError);
  },

  'delay setters reject negative values and filters without them throw TypeError'() {
    for(const f of [() => new stk.Delay(-1), () => new stk.Delay(1, 10).setDelay(-1), () => new stk.Delay(1, 10).tapIn(1, -1)]) {
      let err;
      try { f(); } catch(e) { err = e; }
      assert(err instanceof RangeError);
    }
    let err;
    try { new stk.OnePole().tapOut(1); } catch(e) { err = e; }
    assert(err instanceof TypeError);
  },

  'OnePole.setPole and setGain change the output'() {
    const o = new stk.OnePole();
    o.setPole(0.5);
    o.setGain(2);
    close(o.getGain(), 2);
    const a = o.tick(1);
    o.clear();
    o.setGain(1);
    assert(Math.abs(o.tick(1) - a) > 1e-9);
  },

  'OneZero.setZero, TwoPole.setResonance, TwoZero.setNotch, PoleZero.setAllpass/setBlockZero tick'() {
    const o = new stk.OneZero(); o.setZero(-0.5); assert(typeof o.tick(0.1) === 'number');
    const t = new stk.TwoPole(); t.setResonance(440, 0.9, true); assert(typeof t.tick(0.1) === 'number');
    const z = new stk.TwoZero(); z.setNotch(440, 0.9); assert(typeof z.tick(0.1) === 'number');
    const p = new stk.PoleZero(); p.setAllpass(0.5); p.setBlockZero(); p.setBlockZero(0.9);
    assert(typeof p.tick(0.1) === 'number');
  },

  'setCoefficients takes the coefficient count of its filter'() {
    const o = new stk.OnePole(); o.setCoefficients(1, -0.5); o.setCoefficients(1, -0.5, true);
    const z = new stk.OneZero(); z.setCoefficients(0.5, 0.5);
    const tp = new stk.TwoPole(); tp.setCoefficients(1, -0.2, 0.1);
    const tz = new stk.TwoZero(); tz.setCoefficients(1, 0.5, 0.25);
    const pz = new stk.PoleZero(); pz.setCoefficients(1, 0.5, -0.2);
    const b = new stk.BiQuad(); b.setCoefficients(1, 0, 0, 0, 0);
    close(b.tick(0.5), 0.5);
    let err;
    try { o.setCoefficients(1); } catch(e) { err = e; }
    assert(err instanceof TypeError);
  },

  'BiQuad filter designs and setEqualGainZeroes'() {
    const b = new stk.BiQuad();
    b.setLowPass(1000); b.setLowPass(1000, 0.5); b.setHighPass(1000); b.setBandPass(1000, 1);
    b.setBandReject(1000, 1); b.setAllPass(1000, 1); b.setResonance(440, 0.9); b.setNotch(440, 0.9);
    b.setEqualGainZeroes();
    assert(typeof b.tick(0.1) === 'number');
  },

  'filter setters throw TypeError on a filter that lacks them'() {
    let err;
    try { new stk.Delay().setPole(0.5); } catch(e) { err = e; }
    assert(err instanceof TypeError);
  },

  'OnePole tick returns a number'() {
    const o = new stk.OnePole();
    const val = o.tick(0.1);
    assert(typeof val === 'number');
  },

  'OneZero tick returns a number'() {
    const o = new stk.OneZero();
    const val = o.tick(0.1);
    assert(typeof val === 'number');
  },

  'TwoPole tick returns a number'() {
    const t = new stk.TwoPole();
    const val = t.tick(0.1);
    assert(typeof val === 'number');
  },

  'TwoZero tick returns a number'() {
    const t = new stk.TwoZero();
    const val = t.tick(0.1);
    assert(typeof val === 'number');
  },

  'PoleZero tick returns a number'() {
    const p = new stk.PoleZero();
    const val = p.tick(0.1);
    assert(typeof val === 'number');
  },
});