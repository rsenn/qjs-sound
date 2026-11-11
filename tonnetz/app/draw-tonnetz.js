/* draws the tonnetz lattice, its offscreen cache and the touch flashes. */

import { app } from './state.js';
import { NOTES, mod12 } from '../lib/theory.js';
import { SPL, pcHue, stripH } from './consts.js';
import { sideOn } from './geom.js';

export function tracePath(c, pts) {
  c.beginPath();
  c.moveTo(pts[0][0], pts[0][1]); c.lineTo(pts[1][0], pts[1][1]); c.lineTo(pts[2][0], pts[2][1]); c.closePath();
}

export function paintNode(c, i, j, on) {
  const [x, y] = app.geo.np(i, j), pc = mod12(7 * i + 4 * j);
  c.fillStyle = on ? `hsl(${pcHue(pc)},90%,62%)` : `hsla(${pcHue(pc)},45%,35%,0.9)`;
  c.beginPath(); c.arc(x, y, app.geo.s * (on ? 0.29 : 0.24), 0, 7); c.fill();
  if (on) { c.strokeStyle = '#fff'; c.lineWidth = 2; c.stroke(); }
  c.fillStyle = on ? '#07080d' : 'rgba(255,255,255,0.8)';
  c.fillText(NOTES[pc], x, y + 1);
}

export function buildCache() {
  app.cache = app.makeCanvas();
  app.cache.width = Math.round(app.W * app.DPR); app.cache.height = Math.round(app.H * app.DPR);
  const c = app.cache.getContext('2d');
  c.setTransform(app.DPR, 0, 0, app.DPR, 0, 0);
  c.fillStyle = '#07080d'; c.fillRect(0, 0, app.W, app.H);

  c.fillStyle = '#090b13'; c.fillRect(0, app.tableH, app.LW, stripH);
  c.strokeStyle = '#2a2f4a'; c.lineWidth = 1;
  c.beginPath();
  c.moveTo(0, app.tableH + 0.5); c.lineTo(app.LW, app.tableH + 0.5);
  c.stroke();
  for (const p of app.paneList) {
    if (!p.show) continue;
    c.fillStyle = '#10131f'; c.fillRect(0, p.split, app.LW, SPL);
    c.fillStyle = p === app.panes.ton ? '#0b0d16' : '#090b13'; c.fillRect(0, p.top, app.LW, p.h);
    c.fillStyle = '#4a5178';
    for (let k = -2; k <= 2; k++) c.fillRect(Math.round(app.LW / 2 + k * 10) - 2, p.split + SPL / 2 - 1, 4, 2);
    c.beginPath();
    c.moveTo(0, p.split + 0.5); c.lineTo(app.LW, p.split + 0.5);
    c.moveTo(0, p.top - 0.5); c.lineTo(app.LW, p.top - 0.5);
    c.stroke();
  }
  if (sideOn()) {
    c.fillStyle = '#10131f'; c.fillRect(app.LW, 0, SPL, app.H);
    c.fillStyle = '#0b0d16'; c.fillRect(app.LW + SPL, 0, app.panes.ton.w, app.H);
    c.fillStyle = '#4a5178';
    for (let k = -2; k <= 2; k++) c.fillRect(app.LW + SPL / 2 - 1, Math.round(app.H / 2 + k * 10) - 2, 2, 4);
    c.beginPath();
    c.moveTo(app.LW + 0.5, 0); c.lineTo(app.LW + 0.5, app.H);
    c.moveTo(app.LW + SPL - 0.5, 0); c.lineTo(app.LW + SPL - 0.5, app.H);
    c.stroke();
  }
  if (!app.panes.ton.show) return;

  const { imin, imax } = app.geo.latticeRange();
  c.lineJoin = 'round';
  c.fillStyle = 'rgba(255,255,255,0.025)'; c.strokeStyle = 'rgba(255,255,255,0.09)';
  for (let j = 0; j < 3; j++) for (let i = imin; i <= imax; i++) for (const up of [true, false]) {
    tracePath(c, app.geo.triPts(i, j, up)); c.fill(); c.stroke();
  }
  c.textAlign = 'center'; c.textBaseline = 'middle';
  c.font = `600 ${Math.round(app.geo.s * 0.2)}px system-ui, sans-serif`;
  for (let j = 0; j <= 3; j++) for (let i = imin; i <= imax + 1; i++) {
    const [x, y] = app.geo.np(i, j);
    if (x > -app.geo.s && x < app.W + app.geo.s && y > -app.geo.s && y < app.H + app.geo.s) paintNode(c, i, j, false);
  }
}

export const FLASH_MS = 450;

/* Every sounding or just-released note lights all copies of its pitch class on the lattice, fading out after release. */
export function drawFlashes(now) {
  const lit = new Array(12).fill(0);
  for (const n of app.trail) {
    if (n.t0 > now) continue;
    const a = n.t1 === null || now <= n.t1 ? 1 : 1 - (now - n.t1) / FLASH_MS;
    if (a > 0) lit[mod12(n.midi)] = Math.max(lit[mod12(n.midi)], a);
  }
  if (!lit.some(a => a > 0)) return;
  const { imin, imax } = app.geo.latticeRange();
  for (let j = 0; j <= 3; j++) for (let i = imin; i <= imax + 1; i++) {
    const pc = mod12(7 * i + 4 * j), a = lit[pc];
    if (a <= 0) continue;
    const [x, y] = app.geo.np(i, j);
    if (x < -app.geo.s || x > app.W + app.geo.s || y < -app.geo.s || y > app.H + app.geo.s) continue;
    const hue = pcHue(pc);
    app.g.fillStyle = `hsla(${hue},95%,70%,${0.45 * a})`;
    app.g.beginPath(); app.g.arc(x, y, app.geo.s * (0.2 + 0.04 * a), 0, 7); app.g.fill();
    app.g.fillStyle = `hsla(${hue},95%,75%,${a})`;
    app.g.beginPath(); app.g.arc(x, y, app.geo.s * 0.18, 0, 7); app.g.fill();
    app.g.strokeStyle = `rgba(255,255,255,${a})`; app.g.lineWidth = 2; app.g.stroke();
    app.g.fillStyle = '#07080d';
    app.g.textAlign = 'center'; app.g.textBaseline = 'middle';
    app.g.font = `600 ${Math.round(app.geo.s * 0.2)}px system-ui, sans-serif`;
    app.g.fillText(NOTES[pc], x, y + 1);
  }
}

export function drawTonnetz(now) {
  const sel = app.harmony.sel;

  for (const h of app.hintTris) {
    const pts = app.geo.triPts(h.i, h.j, h.up);
    const a = h.rank === 0 ? 0.55 + 0.35 * Math.sin(now / 220) : 0.3;
    app.g.fillStyle = `rgba(255,255,255,${a * 0.12})`;
    app.g.strokeStyle = `rgba(255,255,255,${a})`; app.g.lineWidth = h.rank === 0 ? 2.5 : 1.5;
    app.g.setLineDash([4, 4]);
    tracePath(app.g, pts); app.g.fill(); app.g.stroke();
    app.g.setLineDash([]);
    app.g.fillStyle = `rgba(255,255,255,${h.rank === 0 ? 0.9 : 0.6})`;
    app.g.font = `600 ${Math.round(app.geo.s * 0.22)}px system-ui, sans-serif`;
    app.g.textAlign = 'center'; app.g.textBaseline = 'middle';
    app.g.fillText(h.name, (pts[0][0] + pts[1][0] + pts[2][0]) / 3, (pts[0][1] + pts[1][1] + pts[2][1]) / 3);
  }

  if (sel.kind === 'tri') {
    const { i, j, up } = sel.tri, hue = pcHue(sel.root);
    app.g.fillStyle = `hsla(${hue},80%,55%,${0.3 + 0.35 * app.pulse})`;
    app.g.strokeStyle = `hsla(${hue},90%,70%,0.9)`; app.g.lineWidth = 1.5;
    tracePath(app.g, app.geo.triPts(i, j, up)); app.g.fill(); app.g.stroke();
  } else if (sel.kind === 'edge') {
    const [a, b] = sel.edge.map(n => app.geo.np(...n));
    app.g.strokeStyle = `hsla(${pcHue(sel.root)},90%,65%,${0.6 + 0.4 * app.pulse})`; app.g.lineWidth = 5; app.g.lineCap = 'round';
    app.g.beginPath(); app.g.moveTo(a[0], a[1]); app.g.lineTo(b[0], b[1]); app.g.stroke();
  }
  app.g.textAlign = 'center'; app.g.textBaseline = 'middle';
  app.g.font = `600 ${Math.round(app.geo.s * 0.2)}px system-ui, sans-serif`;
  for (const k of sel.nodes) {
    const n = /^n(-?\d+),(-?\d+)$/.exec(k);
    paintNode(app.g, +n[1], +n[2], true);
  }
  drawFlashes(now);
}
