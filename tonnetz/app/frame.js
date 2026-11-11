/* one animation frame: advances the visuals and draws every pane. */

import { app } from './state.js';
import { SPL, U } from './consts.js';
import { drawArp } from './draw-arp.js';
import { drawKeys } from './draw-keys.js';
import { drawSeq } from './draw-seq.js';
import { drawStrip, statusText } from './draw-strip.js';
import { analyse, drawCable, drawGrid, drawMaster, drawModule } from './draw-table.js';
import { drawTonnetz } from './draw-tonnetz.js';
import { relayout } from './layout.js';

export function frame() {
  requestAnimationFrame(frame);
  const now = performance.now();
  if (now - app.lastFrame < 30) return;
  app.lastFrame = now;
  if (app.layoutDirty) { app.layoutDirty = false; relayout(); }
  app.g.setTransform(app.DPR, 0, 0, app.DPR, 0, 0);
  app.g.drawImage(app.cache, 0, 0, app.W, app.H);
  if (!app.ctx) return;

  const q = app.player.seq.stepQueue;
  while (q.length && q[0].t <= app.ctx.currentTime) {
    const e = q.shift();
    app.curStep = e.k;
    if (e.note) {
      app.pulse = 1;
      const t0 = now - (app.ctx.currentTime - e.t) * 1000;
      app.trail.push({ midi: e.midi, t0, t1: t0 + e.dur * 1000 });
    }
  }
  while (app.trail.length && app.trail[0].t1 !== null && now - app.trail[0].t1 > 1000) app.trail.shift();
  app.pulse *= 0.92;

  for (const m of app.mods) analyse(m);
  app.g.save();
  app.g.beginPath(); app.g.rect(0, 0, app.LW, app.tableH); app.g.clip();
  app.g.translate(app.view.x, app.view.y); app.g.scale(app.view.z, app.view.z);
  drawGrid();
  for (const m of app.mods) if (m.target) drawCable(m);
  drawMaster(now);
  for (const m of app.mods) {
    const touched = new Set();
    for (const s of app.ptr.values()) if (s.kind === 'ring' && s.m === m) touched.add(s.key);
    drawModule(m, touched, now);
  }
  app.g.restore();
  app.g.fillStyle = 'rgba(216,222,240,0.5)';
  app.g.font = `${Math.round(U * 0.3)}px system-ui, sans-serif`;
  app.g.textAlign = 'center'; app.g.textBaseline = 'bottom';
  app.g.fillText(statusText(), app.LW / 2, 34);
  drawStrip();
  if (app.panes.seq.show) drawSeq();
  if (app.panes.arp.show) drawArp();
  if (app.panes.keys.show) drawKeys(now);
  if (app.panes.ton.show) drawTonnetz(now);
  for (const s of app.ptr.values()) {
    if (s.kind !== 'split') continue;
    app.g.fillStyle = 'rgba(255,255,255,0.14)';
    app.g.fillRect(0, s.pane.split, app.LW, SPL);
  }
}
