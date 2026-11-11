/* pane and module geometry derived from the app state. */

import { app } from './state.js';
import { STEPS, clamp } from '../lib/theory.js';

export const sideOn = () => app.panes.ton.show && app.panes.ton.side;

export const direct = () => !!app.player && app.player.mode === 'direct';

/* the pane's rectangle, in screen pixels */
export const seqRect = () => { const p = app.panes.seq; return { x: 0, y: p.top, w: app.LW, h: p.h }; };

/* grid geometry inside a rectangle { x, y, w, h }; the Chordloom pane by default */
export const seqGeo = (r = seqRect()) => {
  const lw = 26, x0 = r.x + lw + 8, cw = (r.w - lw - 16) / STEPS;
  const th = clamp(Math.round(r.h * 0.1), 12, 18);
  const hh = r.h >= 130 ? 20 : 0;
  const sy = r.y + r.h - th - 4, ay = sy - th - 2, gy0 = r.y + 4 + hh;
  return { x: r.x, y: r.y, w: r.w, lw, x0, cw, th, sy, ay, gy0, hh, rh: (ay - 4 - gy0) / 8 };
};

/* Whole octaves from C, as many as fit at a comfortable key width. */
export const keysGeo = () => {
  const p = app.panes.keys, oct = clamp(Math.floor(app.LW / clamp(app.LW / 5, 140, 210)), 1, 8);
  return { top: p.top, h: p.h, oct, ww: app.LW / (7 * oct), bh: p.h * 0.62, base: 12 * (app.harmony.octave + 1) };
};

export const handleAt = m => ({ x: (m.x + m.target.x) / 2, y: (m.y + m.target.y) / 2 });

export const minTable = () => clamp(app.H * 0.35, 120, 240);
