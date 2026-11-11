/* Chordloom's entry point: wires the DOM-free modules to the page, starts audio on the first touch and runs the frame loop. */

import { Rack } from '../lib/synth.js';
import { Player } from '../lib/player.js';
import { app } from './state.js';
import { U } from './consts.js';
import { addModule } from './modules.js';
import { attachDomInput } from './dom-input.js';
import { frame } from './frame.js';
import { refreshHintTris, resize } from './layout.js';
import { store } from './session.js';
import { $ } from './dom.js';
import './ui.js';

const cv = document.getElementById('cv');
app.g = cv.getContext('2d');
app.makeCanvas = () => document.createElement('canvas');
app.store = store;
app.onHarmonyChange = refreshHintTris;
attachDomInput(cv);

/* sizes the canvas to the window; layout itself does not know about the DOM */
function onResize() {
  resize(innerWidth, innerHeight, devicePixelRatio);
  cv.width = app.W * app.DPR; cv.height = app.H * app.DPR;
}

/* ---------- start ---------- */

function start() {
  app.ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'playback' });
  app.rack = new Rack(app.ctx);
  app.mods = app.rack.mods;
  app.master = app.rack.master;
  app.player = new Player(app.harmony, app.rack);
  app.master.x = app.LW / app.view.z - U * 2.6;
  app.master.y = app.tableH / app.view.z / 2;
  app.store.loadPresets();
  app.ui.renderPresets();

  if (!app.store.restoreSession()) {
    const types = ['vco', 'vcf', 'fmp', 'dist', 'delay'], usable = app.master.x - U * 3.2;
    const [vco, vcf, piano, dist, delay] = types.map((type, k) => addModule(type, usable * (k + 0.5) / types.length, app.master.y));
    app.rack.connect(vco, vcf); app.rack.connect(vcf, dist); app.rack.connect(piano, dist); app.rack.connect(dist, delay); app.rack.connect(delay, app.master);
  }
  app.store.markSaved();
  setInterval(app.store.saveSession, 1000);
  setInterval(app.store.autosavePreset, 30000);
  addEventListener('pagehide', app.store.saveSession);

  app.player.start();
  app.ctx.resume();
  $('start').remove();
}

$('start').addEventListener('click', start, { once: true });
addEventListener('resize', onResize);
if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('sw.js').catch(() => {});
  navigator.serviceWorker.addEventListener('message', e => {
    if (e.data !== 'updated' || $('upd')) return;
    const b = document.createElement('button');
    b.id = 'upd'; b.textContent = 'New version ready: tap to reload';
    b.style.cssText = 'position:fixed;left:50%;bottom:calc(12px + env(safe-area-inset-bottom));transform:translateX(-50%);z-index:20;border-color:#7dffb0';
    b.onclick = () => location.reload();
    document.body.append(b);
  });
}
try { indexedDB.deleteDatabase('tonnetz'); } catch (e) { /* older versions stored a pattern and a song there; both features are gone */ }
onResize();
requestAnimationFrame(frame);
