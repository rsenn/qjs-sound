// stk-tostringtag-order: every stk generator/effect reports its own
// toStringTag, not the previous entry's name.
// BUGS canonical name: stk-tostringtag-array-out-of-enum-order
// Repro: Object.prototype.toString.call(new stk.BlitSaw()) returned
// "[object Blit]" because the ctor_def table was indexed by magic and the
// table order did not match the INSTANCE enum order.

import * as stk from 'stk';
import * as std from 'std';

const cases = [
  { ctor: 'BlitSaw', expected: 'BlitSaw' },
  { ctor: 'BlitSquare', expected: 'BlitSquare' },
  { ctor: 'Blit', expected: 'Blit' },
  { ctor: 'PitShift', expected: 'PitShift' },
  { ctor: 'PRCRev', expected: 'PRCRev' },
];

let allPass = true;

for (const c of cases) {
  const obj = new stk[c.ctor]();
  const tag = Object.prototype.toString.call(obj);
  const expected = `[object ${c.expected}]`;
  if (tag === expected) {
    console.log(`PASS: ${c.ctor} -> ${tag}`);
  } else {
    console.log(`FAIL: ${c.ctor} -> ${tag} (expected ${expected})`);
    allPass = false;
  }
}

if (!allPass) {
  console.log('OVERALL: FAIL');
  std.exit(1);
} else {
  console.log('OVERALL: PASS');
  std.exit(0);
}