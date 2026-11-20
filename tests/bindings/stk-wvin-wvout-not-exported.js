// stk-wvin-wvout-not-exported: RtWvIn, InetWvIn, RtWvOut and InetWvOut are
// exported as constructors, each with its own prototype on top of WvIn/WvOut.
// Needs no audio device: the classes are only inspected, not constructed.
import * as stk from 'stk';

let failed = 0;
const check = (ok, what) => {
  if(!ok) {
    failed++;
    process.stdout.write('FAILED ' + what + '\n');
  }
};

for(const [name, method] of [['RtWvIn', 'start'], ['InetWvIn', 'listen'], ['RtWvOut', 'start'], ['InetWvOut', 'connect']]) {
  const C = stk[name];

  check(typeof C === 'function', name + ' is a function, got ' + typeof C);
  if(typeof C !== 'function') continue;

  check(typeof C.prototype[method] === 'function', name + '.prototype.' + method);
  check(typeof Object.getPrototypeOf(C.prototype).tick === 'function', name + ' inherits tick from WvIn/WvOut');
}

if(failed) throw new Error(failed + ' check(s) failed');
process.stdout.write('stk-wvin-wvout-not-exported OK\n');
