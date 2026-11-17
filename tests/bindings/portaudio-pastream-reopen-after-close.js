// portaudio-pastream-reopen-after-close: open, close, open again
// BUGS canonical name: pastream-reopen-after-close-segfault
// Repro: new PaStream(0, 2).close(), then a second new PaStream(0, 2) in the
// same process once segfaulted inside libportaudio's ALSA host API. A crash
// fails this test (one process per test); no audio device is a skip.

import * as pa from 'portaudio';
import * as std from 'std';

pa.Pa_Initialize();

let first;
try {
  first = new pa.PaStream(0, 2);
} catch(e) {
  console.log(`SKIP: no audio device (${e.message})`);
  pa.Pa_Terminate();
  std.exit(0);
}
first.close();

const second = new pa.PaStream(0, 2);
second.close();
pa.Pa_Terminate();

console.log('PASS: a second stream opened after the first was closed');
std.exit(0);
