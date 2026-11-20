// labsound-test: LabSound's Web Audio API shaped bindings - construction,
// AudioParam getters/setters, node connections, and offline rendering.
// The tests use OfflineAudioContext to avoid needing a real audio device.
import * as lab from 'labsound';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

// AudioParam and AudioBuffer values are float32: compare with a tolerance
const close = (a, b) => assert(Math.abs(a - b) < 1e-6, a + ' != ' + b);

tests({
  'OfflineAudioContext construction and toStringTag'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    eq(Object.prototype.toString.call(ctx), '[object OfflineAudioContext]');
  },

  'OfflineAudioContext basic properties'() {
    const ctx = new lab.OfflineAudioContext(2, 512, 48000);
    eq(ctx.length, 512);
    eq(ctx.sampleRate, 48000);
    eq(ctx.state, 'suspended');
    assert(typeof ctx.currentTime === 'number');
    eq(Object.prototype.toString.call(ctx.destination), '[object AudioDestinationNode]');
    eq(Object.prototype.toString.call(ctx.listener), '[object AudioListener]');
  },

  'OscillatorNode construction and toStringTag'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const osc = new lab.OscillatorNode(ctx);
    eq(Object.prototype.toString.call(osc), '[object OscillatorNode]');
  },

  'OscillatorNode frequency/detune/type getters and setters'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const osc = new lab.OscillatorNode(ctx);
    eq(osc.frequency.value, 440);
    eq(osc.detune.value, 0);
    eq(osc.type, 'sine');
    osc.frequency.value = 880;
    osc.detune.value = 100;
    osc.type = 'square';
    eq(osc.frequency.value, 880);
    eq(osc.detune.value, 100);
    eq(osc.type, 'square');
  },

  'OscillatorNode start/stop'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const osc = new lab.OscillatorNode(ctx);
    osc.start(0);
    osc.stop(0.02);
  },

  'GainNode construction and gain AudioParam'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const g = new lab.GainNode(ctx);
    eq(Object.prototype.toString.call(g), '[object GainNode]');
    eq(typeof g.gain, 'object');
    eq(g.gain.value, 1);
    g.gain.value = 0.5;
    eq(g.gain.value, 0.5);
  },

  'AudioNode.connect disconnects and chains'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const osc = new lab.OscillatorNode(ctx);
    const g = new lab.GainNode(ctx);
    osc.connect(g);
    g.connect(ctx.destination);
    osc.disconnect();
    g.disconnect();
  },

  'OfflineAudioContext.startRendering returns a Promise resolving to AudioBuffer'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const osc = new lab.OscillatorNode(ctx);
    osc.type = 'sine';
    osc.frequency.value = 440;
    osc.start(0);
    osc.stop(0.02);
    const g = new lab.GainNode(ctx);
    g.gain.value = 0.5;
    osc.connect(g);
    g.connect(ctx.destination);
    return ctx.startRendering().then(rb => {
      assertStrictEquals(Object.prototype.toString.call(rb), '[object AudioBuffer]');
      eq(rb.numberOfChannels, 1);
      eq(rb.length, 1024);
      eq(rb.sampleRate, 44100);
      const data = rb.getChannelData(0);
      eq(data.length, 1024);
      let mx = 0;
      for (let i = 0; i < data.length; i++) {
        if (Math.abs(data[i]) > mx) mx = Math.abs(data[i]);
      }
      assert(mx > 0, 'rendered buffer has signal');
      assert(mx <= 0.5 + 1e-4, 'gain limited to 0.5');
    });
  },

  'AudioBuffer construction with options object'() {
    const ab = new lab.AudioBuffer({ numberOfChannels: 2, length: 1024, sampleRate: 44100 });
    eq(Object.prototype.toString.call(ab), '[object AudioBuffer]');
    eq(ab.numberOfChannels, 2);
    eq(ab.length, 1024);
    eq(ab.sampleRate, 44100);
  },

  'AudioBuffer.getChannelData returns Float32Array'() {
    const ab = new lab.AudioBuffer({ numberOfChannels: 2, length: 1024, sampleRate: 44100 });
    const ch = ab.getChannelData(0);
    assertStrictEquals(Object.prototype.toString.call(ch), '[object Float32Array]');
    eq(ch.length, 1024);
  },

  'AudioBuffer.getChannelData throws on bad index'() {
    const ab = new lab.AudioBuffer({ numberOfChannels: 1, length: 1024, sampleRate: 44100 });
    let threw = false;
    try {
      ab.getChannelData(5);
    } catch (e) {
      threw = true;
      assert(e instanceof RangeError, 'expected RangeError, got ' + e);
    }
    assert(threw, 'getChannelData(5) on 1-ch buffer should throw');
  },

  'AudioBuffer.copyToChannel works'() {
    const ab = new lab.AudioBuffer({ numberOfChannels: 2, length: 1024, sampleRate: 44100 });
    ab.copyToChannel(new Float32Array([0.1, 0.2, 0.3]), 0, 0);
    close(ab.getChannelData(0)[0], 0.1);
    close(ab.getChannelData(0)[1], 0.2);
    close(ab.getChannelData(0)[2], 0.3);
  },

  'BiquadFilterNode construction and AudioParam getters'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const bq = new lab.BiquadFilterNode(ctx);
    eq(Object.prototype.toString.call(bq), '[object BiquadFilterNode]');
    eq(bq.type, 'lowpass');
    eq(bq.frequency.value, 350);
    eq(bq.Q.value, 1);
    eq(bq.gain.value, 0);
    bq.type = 'highpass';
    eq(bq.type, 'highpass');
  },

  'DelayNode delayTime AudioParam'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const d = new lab.DelayNode(ctx);
    eq(Object.prototype.toString.call(d), '[object DelayNode]');
    eq(typeof d.delayTime, 'object');
    eq(d.delayTime.value, 0);
    d.delayTime.value = 0.05;
    close(d.delayTime.value, 0.05);
  },

  'StereoPannerNode pan AudioParam'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const p = new lab.StereoPannerNode(ctx);
    eq(Object.prototype.toString.call(p), '[object StereoPannerNode]');
    eq(typeof p.pan, 'object');
    eq(p.pan.value, 0);
    p.pan.value = -0.5;
    eq(p.pan.value, -0.5);
  },

  'DynamicsCompressorNode AudioParams'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const c = new lab.DynamicsCompressorNode(ctx);
    eq(Object.prototype.toString.call(c), '[object DynamicsCompressorNode]');
    eq(typeof c.threshold, 'object');
    eq(typeof c.knee, 'object');
    eq(typeof c.ratio, 'object');
    eq(typeof c.attack, 'object');
    eq(typeof c.release, 'object');
    eq(c.threshold.value, -24);
    eq(c.knee.value, 30);
    eq(c.ratio.value, 12);
  },

  'AnalyserNode properties'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const a = new lab.AnalyserNode(ctx);
    eq(Object.prototype.toString.call(a), '[object AnalyserNode]');
    eq(a.fftSize, 1024);
    eq(a.smoothingTimeConstant, 0.8);
    eq(a.minDecibels, -100);
    eq(a.maxDecibels, -30);
  },

  'ConstantSourceNode offset AudioParam'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const c = new lab.ConstantSourceNode(ctx);
    eq(Object.prototype.toString.call(c), '[object ConstantSourceNode]');
    eq(typeof c.offset, 'object');
    eq(c.offset.value, 1);
  },

  'ConvolverNode buffer getter/setter'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const c = new lab.ConvolverNode(ctx);
    eq(Object.prototype.toString.call(c), '[object ConvolverNode]');
    eq(c.buffer, null);
    const ab = new lab.AudioBuffer({ numberOfChannels: 1, length: 1024, sampleRate: 44100 });
    c.buffer = ab;
    // the getter hands back a wrapper of the same buffer, not the same object
    eq(c.buffer.length, ab.length);
    eq(c.buffer.sampleRate, ab.sampleRate);
  },

  'IIRFilterNode construction with coefficient arrays'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const iir = new lab.IIRFilterNode(ctx, { feedforward: [1, 0, 0, 0, 0, 0], feedback: [1, 0, 0, 0, 0, 0] });
    eq(Object.prototype.toString.call(iir), '[object IIRFilterNode]');
  },

  'NoiseNode type getter'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const n = new lab.NoiseNode(ctx);
    eq(Object.prototype.toString.call(n), '[object NoiseNode]');
    eq(n.type, 'white');
  },

  'WaveShaperNode curve and oversample'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const w = new lab.WaveShaperNode(ctx);
    eq(Object.prototype.toString.call(w), '[object WaveShaperNode]');
    eq(w.curve, undefined);
    eq(w.oversample, 'none');
  },

  'AudioDestinationNode'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    eq(Object.prototype.toString.call(ctx.destination), '[object AudioDestinationNode]');
  },

  'AudioListener'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    eq(Object.prototype.toString.call(ctx.listener), '[object AudioListener]');
  },

  'AudioDevice'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const dev = new lab.AudioDevice(ctx);
    eq(Object.prototype.toString.call(dev), '[object AudioDevice]');
  },

  'AudioBufferSourceNode'() {
    const ctx = new lab.OfflineAudioContext(1, 1024, 44100);
    const src = new lab.AudioBufferSourceNode(ctx);
    eq(Object.prototype.toString.call(src), '[object AudioBufferSourceNode]');
    eq(src.loop, false);
    eq(src.detune.value, 0);
    eq(src.playbackRate.value, 1);
  },
});