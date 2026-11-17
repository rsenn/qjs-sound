// qjsm tests/errors-test.js --list      prints the case names
// qjsm tests/errors-test.js <case>      runs one case, exit 0 = pass
// one process per case, so a crash is reported instead of ending the run.

function assert(cond, msg) {
  if(!cond)
    throw new Error('assertion failed: ' + msg);
}

// the call must throw a TypeError; a call that returns normally has left
// the exception pending (it would surface after the script ended). Build
// the object first: a constructor that throws must not count as a pass.
function throwsTypeError(fn, what) {
  let err;

  try {
    fn();
  } catch(e) {
    err = e;
  }

  assert(err instanceof TypeError, what + ' must throw a TypeError (got ' + (err ? err.name : 'no exception') + ')');
}

const bad = Symbol('bad');
const cases = {};

const sndobj = await import('sndobj').catch(() => null);
if(sndobj) {
  const { HarmTable, Oscili, Buzz, ADSR, Reson, Gain, VDelay } = sndobj;
  const table = () => new HarmTable();
  const input = () => new Oscili(table(), 440, 1);
  const call = (make, method, ...args) => () => {
    const obj = make();
    throwsTypeError(() => obj[method](...args), method);
  };

  cases['sndobj/Oscili.setFreq(Symbol)'] = call(input, 'setFreq', bad);
  cases['sndobj/Oscili.setAmp(Symbol)'] = call(input, 'setAmp', bad);
  cases['sndobj/Oscili.setPhase(Symbol)'] = call(input, 'setPhase', bad);
  cases['sndobj/Oscili.output(Symbol)'] = call(input, 'output', bad);
  cases['sndobj/Buzz.setHarm(Symbol)'] = call(() => new Buzz(), 'setHarm', bad);
  cases['sndobj/ADSR.setADSR(Symbol)'] = call(() => new ADSR(), 'setADSR', bad, 1, 1, 1);
  cases['sndobj/ADSR.setMaxAmp(Symbol)'] = call(() => new ADSR(), 'setMaxAmp', bad);
  cases['sndobj/Reson.setFreq(Symbol)'] = call(() => new Reson(800, 150, input()), 'setFreq', bad);
  cases['sndobj/Gain.setGain(Symbol)'] = call(() => new Gain(-6, input()), 'setGain', bad);
  cases['sndobj/VDelay.setDelayTime(Symbol)'] = call(() => new VDelay(0.1, 0.05, 0.5, 0.5, 0.5, input()), 'setDelayTime', bad);
  cases['sndobj/new Oscili(table, Symbol)'] = () => throwsTypeError(() => new Oscili(table(), bad), 'Oscili constructor');
  cases['sndobj/new Buzz(Symbol)'] = () => throwsTypeError(() => new Buzz(bad), 'Buzz constructor');
  cases['sndobj/new HarmTable(Symbol)'] = () => throwsTypeError(() => new HarmTable(bad), 'HarmTable constructor');
  cases['sndobj/new Oscili() without a table'] = () => throwsTypeError(() => new Oscili(), 'Oscili constructor');
}

const stk = await import('stk').catch(() => null);
if(stk) {
  const { Echo, Chorus, NRev, JCRev, PitShift, StkFrames, SineWave, BiQuad } = stk;

  cases['stk/new Echo(Symbol)'] = () => throwsTypeError(() => new Echo(bad), 'Echo constructor');
  cases['stk/new Chorus(Symbol)'] = () => throwsTypeError(() => new Chorus(bad), 'Chorus constructor');
  cases['stk/new NRev(Symbol)'] = () => throwsTypeError(() => new NRev(bad), 'NRev constructor');
  cases['stk/new JCRev(Symbol)'] = () => throwsTypeError(() => new JCRev(bad), 'JCRev constructor');
  cases['stk/new StkFrames(Symbol)'] = () => throwsTypeError(() => new StkFrames(bad), 'StkFrames constructor');
  const stkcall = (make, method, ...args) => () => {
    const obj = make();
    throwsTypeError(() => obj[method](...args), method);
  };

  cases['stk/NRev.tick(Symbol)'] = stkcall(() => new NRev(1), 'tick', bad);
  cases['stk/PitShift.setEffectMix(Symbol)'] = stkcall(() => new PitShift(), 'setEffectMix', bad);
  cases['stk/BiQuad.tick(Symbol)'] = stkcall(() => new BiQuad(), 'tick', bad);
  cases['stk/SineWave.tick(Symbol)'] = stkcall(() => new SineWave(), 'tick', bad);
}

const labsound = await import('labsound').catch(() => null);
if(labsound) {
  const { AudioContext, OscillatorNode, GainNode, BiquadFilterNode, DelayNode, WaveShaperNode,
          StereoPannerNode, ConvolverNode, AnalyserNode, DynamicsCompressorNode,
          AudioBufferSourceNode, ConstantSourceNode, NoiseNode, IIRFilterNode } = labsound;

  const ctx = () => new AudioContext();
  const osc = () => new OscillatorNode(ctx());

  // constructors with options dicts that should throw TypeError on bad values
  cases['labsound/new OscillatorNode(ctx, { frequency: Symbol() })'] = () => throwsTypeError(() => new OscillatorNode(ctx(), { frequency: bad }), 'OscillatorNode constructor');
  cases['labsound/new OscillatorNode(ctx, { detune: Symbol() })'] = () => throwsTypeError(() => new OscillatorNode(ctx(), { detune: bad }), 'OscillatorNode constructor');
  cases['labsound/new OscillatorNode(ctx, { type: 123 })'] = () => throwsTypeError(() => new OscillatorNode(ctx(), { type: 123 }), 'OscillatorNode constructor');

  cases['labsound/new GainNode(ctx, { gain: Symbol() })'] = () => throwsTypeError(() => new GainNode(ctx(), { gain: bad }), 'GainNode constructor');
  cases['labsound/new BiquadFilterNode(ctx, { frequency: Symbol() })'] = () => throwsTypeError(() => new BiquadFilterNode(ctx(), { frequency: bad }), 'BiquadFilterNode constructor');
  cases['labsound/new BiquadFilterNode(ctx, { Q: Symbol() })'] = () => throwsTypeError(() => new BiquadFilterNode(ctx(), { Q: bad }), 'BiquadFilterNode constructor');
  cases['labsound/new BiquadFilterNode(ctx, { gain: Symbol() })'] = () => throwsTypeError(() => new BiquadFilterNode(ctx(), { gain: bad }), 'BiquadFilterNode constructor');

  cases['labsound/new DelayNode(ctx, { maxDelayTime: Symbol() })'] = () => throwsTypeError(() => new DelayNode(ctx(), { maxDelayTime: bad }), 'DelayNode constructor');
  cases['labsound/new DelayNode(ctx, { delayTime: Symbol() })'] = () => throwsTypeError(() => new DelayNode(ctx(), { delayTime: bad }), 'DelayNode constructor');

  cases['labsound/new StereoPannerNode(ctx, { pan: Symbol() })'] = () => throwsTypeError(() => new StereoPannerNode(ctx(), { pan: bad }), 'StereoPannerNode constructor');

  cases['labsound/new ConvolverNode(ctx, { normalize: 123 })'] = () => throwsTypeError(() => new ConvolverNode(ctx(), { normalize: 123 }), 'ConvolverNode constructor');

  cases['labsound/new AnalyserNode(ctx, { fftSize: Symbol() })'] = () => throwsTypeError(() => new AnalyserNode(ctx(), { fftSize: bad }), 'AnalyserNode constructor');
  cases['labsound/new AnalyserNode(ctx, { minDecibels: Symbol() })'] = () => throwsTypeError(() => new AnalyserNode(ctx(), { minDecibels: bad }), 'AnalyserNode constructor');
  cases['labsound/new AnalyserNode(ctx, { maxDecibels: Symbol() })'] = () => throwsTypeError(() => new AnalyserNode(ctx(), { maxDecibels: bad }), 'AnalyserNode constructor');
  cases['labsound/new AnalyserNode(ctx, { smoothingTimeConstant: Symbol() })'] = () => throwsTypeError(() => new AnalyserNode(ctx(), { smoothingTimeConstant: bad }), 'AnalyserNode constructor');

  cases['labsound/new DynamicsCompressorNode(ctx, { threshold: Symbol() })'] = () => throwsTypeError(() => new DynamicsCompressorNode(ctx(), { threshold: bad }), 'DynamicsCompressorNode constructor');
  cases['labsound/new DynamicsCompressorNode(ctx, { knee: Symbol() })'] = () => throwsTypeError(() => new DynamicsCompressorNode(ctx(), { knee: bad }), 'DynamicsCompressorNode constructor');
  cases['labsound/new DynamicsCompressorNode(ctx, { ratio: Symbol() })'] = () => throwsTypeError(() => new DynamicsCompressorNode(ctx(), { ratio: bad }), 'DynamicsCompressorNode constructor');
  cases['labsound/new DynamicsCompressorNode(ctx, { attack: Symbol() })'] = () => throwsTypeError(() => new DynamicsCompressorNode(ctx(), { attack: bad }), 'DynamicsCompressorNode constructor');
  cases['labsound/new DynamicsCompressorNode(ctx, { release: Symbol() })'] = () => throwsTypeError(() => new DynamicsCompressorNode(ctx(), { release: bad }), 'DynamicsCompressorNode constructor');

  cases['labsound/new AudioBufferSourceNode(ctx, { loop: Symbol() })'] = () => throwsTypeError(() => new AudioBufferSourceNode(ctx(), { loop: bad }), 'AudioBufferSourceNode constructor');
  cases['labsound/new AudioBufferSourceNode(ctx, { playbackRate: Symbol() })'] = () => throwsTypeError(() => new AudioBufferSourceNode(ctx(), { playbackRate: bad }), 'AudioBufferSourceNode constructor');
  cases['labsound/new AudioBufferSourceNode(ctx, { detune: Symbol() })'] = () => throwsTypeError(() => new AudioBufferSourceNode(ctx(), { detune: bad }), 'AudioBufferSourceNode constructor');

  cases['labsound/new ConstantSourceNode(ctx, { offset: Symbol() })'] = () => throwsTypeError(() => new ConstantSourceNode(ctx(), { offset: bad }), 'ConstantSourceNode constructor');

  cases['labsound/new NoiseNode(ctx, { type: 123 })'] = () => throwsTypeError(() => new NoiseNode(ctx(), { type: 123 }), 'NoiseNode constructor');

  cases['labsound/new IIRFilterNode(ctx, { feedforward: 123, feedback: [] })'] = () => throwsTypeError(() => new IIRFilterNode(ctx(), { feedforward: 123, feedback: [] }), 'IIRFilterNode constructor');
  cases['labsound/new IIRFilterNode(ctx, { feedforward: [], feedback: 123 })'] = () => throwsTypeError(() => new IIRFilterNode(ctx(), { feedforward: [], feedback: 123 }), 'IIRFilterNode constructor');

  // method-level bad arguments
  const makeOsc = () => new OscillatorNode(ctx());
  const makeGain = () => new GainNode(ctx());
  const makeBiquad = () => new BiquadFilterNode(ctx());
  const makeDelay = () => new DelayNode(ctx());
  const makeStereoPanner = () => new StereoPannerNode(ctx());
  const makeAnalyser = () => new AnalyserNode(ctx());
  const makeCompressor = () => new DynamicsCompressorNode(ctx());
  const makeAbsource = () => new AudioBufferSourceNode(ctx());
  const makeConstant = () => new ConstantSourceNode(ctx());
  const makeNoise = () => new NoiseNode(ctx());

  const lscall = (make, method, ...args) => () => {
    const obj = make();
    throwsTypeError(() => obj[method](...args), method);
  };

  cases['labsound/OscillatorNode.type(Symbol)'] = lscall(makeOsc, 'type', bad);
  cases['labsound/GainNode.gain.setValueAtTime(Symbol, 0)'] = lscall(makeGain, 'gain.setValueAtTime', bad, 0);
  cases['labsound/BiquadFilterNode.frequency.setValueAtTime(Symbol, 0)'] = lscall(makeBiquad, 'frequency.setValueAtTime', bad, 0);
  cases['labsound/DelayNode.delayTime.setValueAtTime(Symbol, 0)'] = lscall(makeDelay, 'delayTime.setValueAtTime', bad, 0);
  cases['labsound/StereoPannerNode.pan.setValueAtTime(Symbol, 0)'] = lscall(makeStereoPanner, 'pan.setValueAtTime', bad, 0);
  cases['labsound/AnalyserNode.fftSize = Symbol'] = () => throwsTypeError(() => { makeAnalyser().fftSize = bad; }, 'AnalyserNode.fftSize setter');
  cases['labsound/AnalyserNode.minDecibels = Symbol'] = () => throwsTypeError(() => { makeAnalyser().minDecibels = bad; }, 'AnalyserNode.minDecibels setter');
  cases['labsound/AnalyserNode.maxDecibels = Symbol'] = () => throwsTypeError(() => { makeAnalyser().maxDecibels = bad; }, 'AnalyserNode.maxDecibels setter');
  cases['labsound/AnalyserNode.smoothingTimeConstant = Symbol'] = () => throwsTypeError(() => { makeAnalyser().smoothingTimeConstant = bad; }, 'AnalyserNode.smoothingTimeConstant setter');
  cases['labsound/AnalyserNode.getByteFrequencyData(Symbol)'] = lscall(makeAnalyser, 'getByteFrequencyData', bad);
  cases['labsound/DynamicsCompressorNode.threshold.setValueAtTime(Symbol, 0)'] = lscall(makeCompressor, 'threshold.setValueAtTime', bad, 0);
  cases['labsound/AudioBufferSourceNode.loop = Symbol'] = () => throwsTypeError(() => { makeAbsource().loop = bad; }, 'AudioBufferSourceNode.loop setter');
  cases['labsound/AudioBufferSourceNode.playbackRate.setValueAtTime(Symbol, 0)'] = lscall(makeAbsource, 'playbackRate.setValueAtTime', bad, 0);
  cases['labsound/AudioBufferSourceNode.detune.setValueAtTime(Symbol, 0)'] = lscall(makeAbsource, 'detune.setValueAtTime', bad, 0);
  cases['labsound/ConstantSourceNode.offset.setValueAtTime(Symbol, 0)'] = lscall(makeConstant, 'offset.setValueAtTime', bad, 0);
  cases['labsound/NoiseNode.type = Symbol'] = () => throwsTypeError(() => { makeNoise().type = bad; }, 'NoiseNode.type setter');
}

// decision D2: every constructor has a prototype property, so subclassing works
const subclass = (C, args = []) => () => {
  class Sub extends C {}
  assert(new Sub(...args) instanceof C, 'a subclass instance is an instance of the base');
};

if(sndobj) {
  cases['sndobj/class extends Buzz'] = subclass(sndobj.Buzz);
  cases['sndobj/class extends Oscili'] = subclass(sndobj.Oscili, [new sndobj.HarmTable()]);
}

if(stk) {
  cases['stk/class extends ADSR'] = subclass(stk.ADSR);
  cases['stk/class extends NRev'] = subclass(stk.NRev, [1]);
  cases['stk/class extends BiQuad'] = subclass(stk.BiQuad);
}

// the toStringTag of a class is its name
if(stk) {
  for(const name of ['BlitSaw', 'BlitSquare', 'Blit', 'PitShift', 'PRCRev', 'Delay', 'DelayL', 'ADSR', 'SingWave'])
    cases['stk/toStringTag of ' + name] = () => {
      const C = stk[name];
      assert(C, 'stk.' + name + ' exists');
      const args = name == 'Delay' || name == 'DelayL' ? [1, 8] : (name == 'SingWave' ? ['shakers-test.wav'] : []);
      const tag = Object.prototype.toString.call(new C(...args)).slice(8, -1);
      assert(tag == name, 'toStringTag of ' + name + ' is ' + tag);
    };
}

const arg = scriptArgs[1];

if(arg == '--list') {
  console.log(Object.keys(cases).join('\n'));
} else {
  assert(cases[arg], 'unknown case ' + arg);
  cases[arg]();
  console.log('PASS ' + arg);
}