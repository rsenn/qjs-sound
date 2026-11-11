/* draws the piano pane. */

import { app } from './state.js';
import { clamp, mod12 } from '../lib/theory.js';
import { BLACK_KEYS, WHITE_OFFS, pcHue } from './consts.js';
import { keysGeo } from './geom.js';

/* Free mode has no scale to colour, so no key is marked. */
export const scaleSet = () => new Set(app.harmony.lock ? app.harmony.lock.pcs : []);

export function drawKeys(now) {
  const K = keysGeo(), inScale = scaleSet();
  const sounding = new Set(app.trail.filter(n => n.t0 <= now && (n.t1 === null || n.t1 >= now)).map(n => n.midi));
  const paint = (m, x, w, h, black) => {
    const pc = mod12(m), on = inScale.has(pc);
    app.g.fillStyle = on ? `hsl(${pcHue(pc)},${black ? 75 : 85}%,${black ? 40 : 62}%)` : black ? '#14172a' : '#e9ecf5';
    app.g.fillRect(x, K.top, w, h);
    if (!sounding.has(m)) return;
    app.g.strokeStyle = '#fff'; app.g.lineWidth = 3;
    app.g.strokeRect(x + 1.5, K.top + 1.5, w - 3, h - 3);
    app.g.fillStyle = 'rgba(255,255,255,0.35)';
    app.g.fillRect(x, K.top, w, h);
  };
  app.g.font = `${Math.round(clamp(K.ww * 0.4, 8, 11))}px system-ui, sans-serif`;
  app.g.textAlign = 'center'; app.g.textBaseline = 'bottom';
  for (let o = 0; o < K.oct; o++) {
    WHITE_OFFS.forEach((off, wi) => {
      const m = K.base + 12 * o + off, x = (o * 7 + wi) * K.ww;
      paint(m, x + 1, K.ww - 2, K.h - 1, false);
      if (off === 0) {
        app.g.fillStyle = '#07080d';
        app.g.fillText('C' + (Math.floor(m / 12) - 1), x + K.ww / 2, K.top + K.h - 4);
      }
    });
  }
  for (let o = 0; o < K.oct; o++) {
    for (const [pc, wi] of BLACK_KEYS) paint(K.base + 12 * o + pc, (o * 7 + wi) * K.ww - K.ww * 0.3, K.ww * 0.6, K.bh, true);
  }
}
