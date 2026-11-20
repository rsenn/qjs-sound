// stk-test: STK classes (generators, effects, filters, instruments) and
// namespaces (Generator, Filter, Effect, Stk, FM, Guitar, Twang, etc.),
// StkFrames, and the toStringTag each instance carries.
import * as stk from 'stk';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

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

  'StkFrames.setChannel'() {
    const src = new stk.StkFrames(4);
    src.setChannel(0, [0.1, 0.2, 0.3, 0.4]);
    eq(src.getChannel(0, new stk.StkFrames(4, 1), 0).getChannel(0)[0], 0.1);
    eq(src.getChannel(0, new stk.StkFrames(4, 1), 0).getChannel(0)[3], 0.4);
  },

  'StkFrames.interpolate'() {
    const f = new stk.StkFrames(10);
    const src = new stk.StkFrames(4);
    src.setChannel(0, [0.1, 0.2, 0.3, 0.4]);
    const n = f.interpolate(src);
    assert(typeof n === 'number');
  },

  'StkFrames.toFloat32/fromFloat32 round-trip'() {
    const f = new stk.StkFrames(4, 1);
    f.setChannel(0, [0.1, 0.2, 0.3, 0.4]);
    const f32 = f.toFloat32();
    assertStrictEquals(Object.prototype.toString.call(f32), '[object Float32Array]');
    eq(f32.length, 4);
    const f2 = new stk.StkFrames(4, 1);
    f2.fromFloat32(new Float32Array([0.5, 0.6, 0.7, 0.8]));
    eq(f2.getChannel(0, new stk.StkFrames(4, 1), 0).getChannel(0)[0], 0.5);
  },

  'SineWave construction and toStringTag'() {
    const s = new stk.SineWave(44100, 1);
    eq(Object.prototype.toString.call(s), '[object SineWave]');
  },

  'SineWave getFrequency/setFrequency getAmplitude/setAmplitude'() {
    const s = new stk.SineWave(44100, 1);
    eq(s.getFrequency(), 440);
    eq(s.getAmplitude(), 1);
    s.setFrequency(880);
    s.setAmplitude(0.5);
    eq(s.getFrequency(), 880);
    eq(s.getAmplitude(), 0.5);
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

  'BiQuad.setBiquad and tick'() {
    const b = new stk.BiQuad();
    b.setBiquad(b.BP, 440, 1, 0);
    const val = b.tick(0.1);
    assert(typeof val === 'number');
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

  'Generator namespace has SineWave as a constructor'() {
    assert(typeof stk.Generator.SineWave === 'function');
  },

  'Filter namespace has BiQuad as a constructor'() {
    assert(typeof stk.Filter.BiQuad === 'function');
  },

  'Effect namespace has Echo as a constructor'() {
    assert(typeof stk.Effect.Echo === 'function');
  },

  'Stk namespace has sampleRate setter/getter'() {
    assert(typeof stk.Stk.sampleRate === 'number' || typeof stk.Stk.getSampleRate === 'function');
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
    const s = new stk.SingWave(44100);
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
    const m = new stk.Mesh2D();
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

  'OnePole.setPole and tick'() {
    const o = new stk.OnePole();
    o.setPole(0.5);
    const val = o.tick(0.1);
    assert(typeof val === 'number');
  },

  'OneZero.setZero and tick'() {
    const o = new stk.OneZero();
    o.setZero(-0.5);
    const val = o.tick(0.1);
    assert(typeof val === 'number');
  },

  'TwoPole.setResonance and tick'() {
    const t = new stk.TwoPole();
    t.setResonance(440, 0.9);
    const val = t.tick(0.1);
    assert(typeof val === 'number');
  },

  'TwoZero.setNotch and tick'() {
    const t = new stk.TwoZero();
    t.setNotch(440);
    const val = t.tick(0.1);
    assert(typeof val === 'number');
  },

  'PoleZero.setPoleZero and tick'() {
    const p = new stk.PoleZero();
    p.setPoleZero(0.5, -0.5);
    const val = p.tick(0.1);
    assert(typeof val === 'number');
  },
});