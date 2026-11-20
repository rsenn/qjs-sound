// Runs worklet.js under node (no browser): the processor class is driven by hand.
// Usage: node test.mjs
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import assert from 'node:assert/strict';

globalThis.sampleRate = 44100;
globalThis.AudioWorkletProcessor = class { constructor() { this.port = {}; } };
const processors = {};
globalThis.registerProcessor = (name, cls) => (processors[name] = cls);
await import('./worklet.js');

const wasm = new WebAssembly.Module(readFileSync(new URL('./qjs-sound-web.wasm', import.meta.url)));
const make = (kind, extra = {}) => new processors['qjs-sound']({ processorOptions: { wasm, kind, ...extra } });
const rms = a => Math.sqrt(a.reduce((s, v) => s + v * v, 0) / a.length);

// a sine at `hz` through the processor, RMS of the last second
function run(proc, hz, blocks = 345) {
  const out = [new Float32Array(128)];
  const input = [new Float32Array(128)];
  const tail = [];
  for(let b = 0; b < blocks; b++) {
    for(let i = 0; i < 128; i++) input[0][i] = Math.sin((2 * Math.PI * hz * (b * 128 + i)) / sampleRate);
    proc.process([input], [out]);
    if(b >= blocks - 100) tail.push(...out[0]);
  }
  return rms(tail);
}

for(const kind of ['biquad', 'reson']) {
  const on = run(make(kind, { freq: 1000, q: kind == 'reson' ? 0.05 : 0.98 }), 1000);
  const off = run(make(kind, { freq: 1000, q: kind == 'reson' ? 0.05 : 0.98 }), 4000);
  console.log(`${kind}: rms at resonance ${on.toFixed(4)}, far from it ${off.toFixed(4)}`);
  assert.ok(on > 4 * off, `${kind} must pass its resonance and attenuate 4 kHz`);
}

const plucked = make('plucked');
plucked.x.stk_plucked_note_on(plucked.dsp, 220, 0.8);
const out = [new Float32Array(128)];
const blocks = [];
for(let b = 0; b < 400; b++) { plucked.process([[]], [out]); blocks.push(rms(out[0])); }
console.log(`plucked: rms first block ${blocks[0].toFixed(4)}, last ${blocks[399].toFixed(4)}`);
assert.ok(blocks[0] > 0 && blocks[399] < blocks[0], 'a plucked string decays');

// speed: one 128-frame block per call
for(const kind of ['biquad', 'reson', 'plucked']) {
  const p = make(kind, { freq: 1000, q: 0.5 });
  const input = [[new Float32Array(128).fill(0.1)]];
  const o = [new Float32Array(128)];
  const N = 20000;
  const t = performance.now();
  for(let b = 0; b < N; b++) p.process(input, [o]);
  const per = ((performance.now() - t) / N) * 1000;
  console.log(`${kind}: ${per.toFixed(2)} us per 128-frame block (budget ${((128 / sampleRate) * 1e6).toFixed(0)} us)`);
}
console.log('ok');
