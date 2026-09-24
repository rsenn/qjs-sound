/* Plays the tonnetz rack through LabSound under qjsm.
   Usage: qjsm tonnetz/cli.js [seconds] [auto|direct]
   Set LABSOUND_MODULE to a labsound.so path to test a local build instead of the installed module. */

import * as std from 'std';
import * as os from 'os';
import { NOTES, cname, triSel, nodeSel, edgeSel } from './lib/theory.js';
import { Harmony } from './lib/harmony.js';
import { Rack } from './lib/synth.js';
import { Player } from './lib/player.js';
import { timers } from './lib/qjs-timers.js';

const { AudioContext } = await import(std.getenv('LABSOUND_MODULE') || 'labsound');

const seconds = +(scriptArgs[1] || 20);
const mode = scriptArgs[2] || 'auto';

const ctx = new AudioContext();
const rack = new Rack(ctx, { timers });
const [vco, vcf, piano, dist, delay] = ['vco', 'vcf', 'fmp', 'dist', 'delay'].map(t => rack.makeModule(t, 0, 0));
rack.connect(vco, vcf); rack.connect(vcf, dist); rack.connect(piano, dist); rack.connect(dist, delay); rack.connect(delay, rack.master);

const harmony = new Harmony();
const player = new Player(harmony, rack, { timers });
await ctx.resume();
player.start();
if (mode === 'direct') player.setMode('direct');

const name = s => (s.kind === 'tri' ? cname(s.id) : s.kind === 'node' ? NOTES[s.root] : `${NOTES[s.pcs[0]]}+${NOTES[s.pcs[1]]}`);
const dB = v => (v > 1e-5 ? (20 * Math.log10(v)).toFixed(1) : '-inf');
const buf = new Float32Array(rack.master.an.fftSize);
const level = () => {
  rack.master.an.getFloatTimeDomainData(buf);
  let pk = 0;
  for (const v of buf) pk = Math.max(pk, Math.abs(v));
  return pk;
};

const directDemo = [triSel(0, 0, true), nodeSel(0, 0), edgeSel([0, 0], [1, 0]), triSel(-1, 0, false)];
let step = 0, peak = 0;
const t0 = os.now();
const tick = () => {
  const elapsed = (os.now() - t0) / 1000;
  peak = Math.max(peak, level());
  if (mode === 'direct') {
    if (step % 2 === 0) player.press(directDemo[(step / 2) % directDemo.length]);
    else player.lift(0);
  }
  console.log(`t=${elapsed.toFixed(0)}s ${mode === 'direct' ? (step % 2 ? 'release' : 'press ') : 'chord'} ${name(harmony.sel)} meter=${dB(level())} dB`);
  step++;
  if (elapsed < seconds) { os.setTimeout(tick, 1000); return; }
  player.stop();
  console.log(`peak=${dB(peak)} dB nodes=${ctx.__nodes.length}`);
  std.exit(peak > 0.01 ? 0 : 1);
};
tick();
