// stk-throw-leaves-error-text: an STK error message does not carry the text
// of earlier errors.
// Repro: try { new MidiFileIn() } catch(e) {}; new Flute() threw
// "MidiFileIn: error opening ...Flute::Flute: argument is less than or equal to zero!"
import * as stk from 'stk';

const message = fn => {
  try {
    fn();
  } catch(e) {
    return e.message;
  }
  throw new Error('did not throw');
};

message(() => new stk.MidiFileIn());          // TypeError path (js_stk_throw)
const second = message(() => new stk.Flute()); // RangeError path (js_throw_stkerror)

if(second.includes('MidiFileIn'))
  throw new Error('stale text in message: ' + second);

// and the other way round, with a second failure of the first kind
message(() => new stk.Flute());
const third = message(() => new stk.MidiFileIn());

if(third.includes('Flute'))
  throw new Error('stale text in message: ' + third);

process.stdout.write('stk-throw-leaves-error-text OK\n');
