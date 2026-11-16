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