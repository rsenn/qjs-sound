/* draws the table: grid, cables, modules and the master output. */

import { app } from './state.js';
import { TYPES } from '../lib/synth.js';
import { clamp } from '../lib/theory.js';
import { A0, SPAN, U, al, outerR } from './consts.js';
import { handleAt } from './geom.js';

export function analyse(m) {
  m.an.getFloatTimeDomainData(m.buf);
  let pk = 0, sum = 0;
  for (const v of m.buf) { const a = Math.abs(v); if (a > pk) pk = a; sum += v * v; }
  m.pk = pk; m.rms = Math.sqrt(sum / m.buf.length);
}

export const norm = (m, v) => (m.pk > 1e-4 ? v / m.pk : 0);

export function drawCable(m) {
  const t = m.target;
  const dx = t.x - m.x, dy = t.y - m.y, d = Math.hypot(dx, dy);
  const ux = dx / d, uy = dy / d, nx = -uy, ny = ux;
  const s0 = outerR(m), s1 = d - (t === app.master ? U * 1.9 : outerR(t));
  const c0 = TYPES[m.type].color, c1 = t === app.master ? '#ffffff' : TYPES[t.type].color;
  if (s1 > s0) {
    const amp = Math.min(14, 3 + m.rms * 70);
    const grad = app.g.createLinearGradient(m.x, m.y, t.x, t.y);
    grad.addColorStop(0, c0); grad.addColorStop(1, c1);
    app.g.strokeStyle = grad; app.g.lineWidth = 2 + Math.min(3, m.rms * 20); app.g.lineCap = 'round'; app.g.lineJoin = 'round';
    app.g.beginPath();
    const n = clamp(Math.round((s1 - s0) / 4), 40, 160);
    const win = Math.min(m.buf.length, n * 6);
    for (let k = 0; k <= n; k++) {
      const f = k / n, s = s0 + (s1 - s0) * f;
      const off = norm(m, m.buf[Math.floor(f * (win - 1))]) * amp * Math.sin(Math.PI * f);
      const x = m.x + ux * s + nx * off, y = m.y + uy * s + ny * off;
      if (k) app.g.lineTo(x, y); else app.g.moveTo(x, y);
    }
    app.g.stroke();
  }
  const h = handleAt(m), r = U * 0.3;
  app.g.fillStyle = '#0d1020'; app.g.strokeStyle = '#ffffffcc'; app.g.lineWidth = 1.5;
  app.g.beginPath(); app.g.arc(h.x, h.y, r, 0, 7); app.g.fill(); app.g.stroke();
  app.g.beginPath();
  app.g.moveTo(h.x - r * 0.45, h.y - r * 0.45); app.g.lineTo(h.x + r * 0.45, h.y + r * 0.45);
  app.g.moveTo(h.x + r * 0.45, h.y - r * 0.45); app.g.lineTo(h.x - r * 0.45, h.y + r * 0.45);
  app.g.stroke();
}

export function drawRing(c, r, w, v, active, dim) {
  app.g.lineWidth = w; app.g.lineCap = 'round';
  app.g.strokeStyle = al(c, dim ? 0.06 : active ? 0.3 : 0.14);
  app.g.beginPath(); app.g.arc(0, 0, r, A0, A0 + SPAN); app.g.stroke();
  app.g.strokeStyle = al(c, dim ? 0.3 : active ? 1 : 0.7);
  app.g.beginPath(); app.g.arc(0, 0, r, A0, A0 + SPAN * Math.max(v, 0.001)); app.g.stroke();
  const a = A0 + SPAN * v;
  app.g.fillStyle = dim ? '#ffffff55' : '#fff';
  app.g.beginPath(); app.g.arc(Math.cos(a) * r, Math.sin(a) * r, w * (active ? 0.7 : 0.45), 0, 7); app.g.fill();
}

/* The pill sits above the module so the dragging finger never covers the reading. */
export function drawBubble(m, label, value) {
  const fs = Math.round(clamp(U * 0.5, 13, 20));
  app.g.font = `600 ${fs}px system-ui, sans-serif`;
  const text = `${label}  ${value}`, w = app.g.measureText(text).width + fs, h = fs * 1.7;
  const vl = -app.view.x / app.view.z, vr = (app.LW - app.view.x) / app.view.z;
  const x = clamp(m.x, vl + w / 2 + 4, vr - w / 2 - 4), y = Math.max(-app.view.y / app.view.z + 4, m.y - outerR(m) - h - 6);
  app.g.fillStyle = 'rgba(7,8,13,0.92)'; app.g.strokeStyle = '#ffffffcc'; app.g.lineWidth = 1.5;
  app.g.beginPath(); app.g.roundRect(x - w / 2, y, w, h, h / 2); app.g.fill(); app.g.stroke();
  app.g.fillStyle = '#fff'; app.g.textAlign = 'center'; app.g.textBaseline = 'middle';
  app.g.fillText(text, x, y + h / 2 + 1);
}

export function dashRing(x, y, r, alpha, now) {
  app.g.strokeStyle = `rgba(255,255,255,${alpha})`; app.g.lineWidth = 1.5;
  app.g.setLineDash([5, 6]); app.g.lineDashOffset = -now / 40;
  app.g.beginPath(); app.g.arc(x, y, r, 0, 7); app.g.stroke();
  app.g.setLineDash([]);
}

export function drawModule(m, touched, now) {
  const T = TYPES[m.type], c = T.color, n = T.params.length;
  app.g.save(); app.g.translate(m.x, m.y);
  const halo = app.g.createRadialGradient(0, 0, U * 0.5, 0, 0, outerR(m) + U);
  halo.addColorStop(0, al(c, 0.1 + Math.min(0.5, m.rms * 3)));
  halo.addColorStop(1, al(c, 0));
  app.g.fillStyle = halo; app.g.beginPath(); app.g.arc(0, 0, outerR(m) + U, 0, 7); app.g.fill();

  app.g.fillStyle = '#0d1020'; app.g.strokeStyle = m === app.selected ? '#ffffff' : c; app.g.lineWidth = m === app.selected ? 3 : 2;
  app.g.beginPath(); app.g.arc(0, 0, U, 0, 7); app.g.fill(); app.g.stroke();

  app.g.strokeStyle = al(c, 0.9); app.g.lineWidth = 1.5; app.g.lineJoin = 'round';
  app.g.beginPath();
  const pts = 128, r0 = U * 0.62, amp = U * 0.3;
  for (let i = 0; i <= pts; i++) {
    const a = (i / pts) * 2 * Math.PI;
    const r = r0 + norm(m, m.buf[Math.floor((i % pts) / pts * 511)]) * amp;
    if (i) app.g.lineTo(Math.cos(a) * r, Math.sin(a) * r); else app.g.moveTo(Math.cos(a) * r, Math.sin(a) * r);
  }
  app.g.stroke();

  T.params.forEach((q, i) => drawRing(c, U * (1.25 + 0.5 * i), U * 0.34, m.p[i], touched.has(i), touched.size > 0 && !touched.has(i)));

  app.g.fillStyle = al(c, 0.95);
  app.g.font = `600 ${Math.round(U * 0.3)}px system-ui, sans-serif`;
  app.g.textAlign = 'center'; app.g.textBaseline = 'middle';
  app.g.fillText(T.name, 0, 0);

  const fs = Math.round(U * 0.28);
  app.g.font = `${fs}px system-ui, sans-serif`;
  app.g.textBaseline = 'top';
  T.params.forEach((q, i) => {
    app.g.fillStyle = touched.has(i) ? '#fff' : al('#d8def0', 0.55);
    app.g.fillText(`${q.label} ${q.fmt(m.p[i])}`, 0, outerR(m) + 4 + i * fs * 1.2);
  });
  app.g.restore();

  for (const i of touched) drawBubble(m, T.params[i].label, T.params[i].fmt(m.p[i]));
  if (m === app.selected) dashRing(m.x, m.y, U * (1 + 0.5 * n) + 8, 0.9, now);
  else if (app.selected && !app.selected.target && app.rack.canConnect(app.selected, m)) dashRing(m.x, m.y, U * 1.12, 0.35 + 0.25 * Math.sin(now / 200), now);
}

export const dbOf = v => (v > 1e-5 ? 20 * Math.log10(v) : -100);

export function drawMeter() {
  app.master.an.getFloatTimeDomainData(app.master.tb);
  let pk = 0;
  for (const v of app.master.tb) { const a = Math.abs(v); if (a > pk) pk = a; }
  const now = performance.now();
  app.master.level = Math.max(pk, app.master.level * 0.9);
  if (pk >= app.master.hold || now - app.master.holdT > 1200) { app.master.hold = pk; app.master.holdT = now; }

  const w = U * 3, h = Math.max(6, U * 0.2), segs = 24, y = U * 2.15;
  const frac = v => clamp((dbOf(v) + 60) / 60, 0, 1);
  const lit = frac(app.master.level) * segs, holdSeg = Math.min(segs - 1, Math.floor(frac(app.master.hold) * segs));
  const sw = w / segs;
  app.g.save(); app.g.translate(app.master.x, app.master.y);
  for (let k = 0; k < segs; k++) {
    const hue = 130 - (k / segs) * 130;
    const on = k < lit || k === holdSeg;
    app.g.fillStyle = on ? `hsl(${hue},85%,55%)` : 'rgba(255,255,255,0.07)';
    app.g.fillRect(-w / 2 + k * sw + 1, y, sw - 2, h);
  }
  const db = dbOf(app.master.hold);
  app.g.fillStyle = db > -1 ? '#ff5050' : al('#d8def0', 0.7);
  app.g.font = `${Math.round(U * 0.3)}px system-ui, sans-serif`;
  app.g.textAlign = 'center'; app.g.textBaseline = 'top';
  app.g.fillText(db <= -99 ? '-inf dB' : db.toFixed(1) + ' dB', 0, y + h + 4);
  app.g.restore();
}

export function drawMaster(now) {
  app.master.an.getByteFrequencyData(app.master.fr);
  app.g.save(); app.g.translate(app.master.x, app.master.y);
  const bars = 32, r = U * 1.1;
  app.g.lineCap = 'round'; app.g.lineWidth = 4;
  for (let k = 0; k < bars; k++) {
    const a = (k / bars) * 2 * Math.PI - Math.PI / 2;
    const v = app.master.fr[Math.floor(k * 1.4)] / 255;
    app.g.strokeStyle = `hsla(${190 + k * 4},90%,${50 + v * 25}%,${0.35 + v * 0.65})`;
    app.g.beginPath();
    app.g.moveTo(Math.cos(a) * r, Math.sin(a) * r);
    app.g.lineTo(Math.cos(a) * (r + 3 + v * U * 0.7), Math.sin(a) * (r + 3 + v * U * 0.7));
    app.g.stroke();
  }
  app.g.fillStyle = '#0d1020'; app.g.strokeStyle = '#ffffff'; app.g.lineWidth = 2;
  app.g.beginPath(); app.g.arc(0, 0, r, 0, 7); app.g.fill(); app.g.stroke();
  app.g.fillStyle = '#fff'; app.g.font = `600 ${Math.round(U * 0.3)}px system-ui, sans-serif`;
  app.g.textAlign = 'center'; app.g.textBaseline = 'middle';
  app.g.fillText('OUT', 0, 0);
  app.g.restore();
  if (app.selected && !app.selected.target) dashRing(app.master.x, app.master.y, r + 6, 0.35 + 0.25 * Math.sin(now / 200), now);
  drawMeter();
}

/* Too dense to read once zoomed out, and it is redrawn every frame. */
export function drawGrid() {
  const step = U * 0.8;
  if (step * app.view.z < 16) return;
  const x1 = (app.LW - app.view.x) / app.view.z, y1 = (app.tableH - app.view.y) / app.view.z;
  app.g.fillStyle = 'rgba(255,255,255,0.05)';
  for (let y = Math.floor(-app.view.y / app.view.z / step) * step; y < y1; y += step) {
    for (let x = Math.floor(-app.view.x / app.view.z / step) * step; x < x1; x += step) app.g.fillRect(x, y, 1.5, 1.5);
  }
}
