/* table view: screen/world conversion, zoom and the default and fitted views. */

import { app } from './state.js';
import { U, Z_MAX, Z_MIN, outerR } from './consts.js';
import { clamp } from '../lib/theory.js';

export const toWorld = p => ({ x: (p.x - app.view.x) / app.view.z, y: (p.y - app.view.y) / app.view.z });

export function zoomAt(sx, sy, z) {
  const w = toWorld({ x: sx, y: sy });
  app.view.z = clamp(z, Z_MIN, Z_MAX);
  app.view.x = sx - w.x * app.view.z; app.view.y = sy - w.y * app.view.z;
}

/* Sized so the default patch fills the table the way it did before the view could move. */
export function defaultView() {
  app.view.z = clamp(Math.min(app.LW / 13, (app.tableH - 30) / 7.6), 20, 44) / U;
  app.view.x = app.view.y = 0;
}

export function fitView() {
  const items = [...app.mods, app.master];
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const m of items) {
    const isMod = m !== app.master, r = isMod ? outerR(m) : U * 1.9;
    x0 = Math.min(x0, m.x - r); x1 = Math.max(x1, m.x + r);
    y0 = Math.min(y0, m.y - r - (isMod ? U * 0.9 : 0)); y1 = Math.max(y1, m.y + r + (isMod ? U * 2 : U * 1.5));
  }
  const z = clamp(Math.min(app.LW / (x1 - x0 + U), app.tableH / (y1 - y0 + U)), Z_MIN, 1.6);
  app.view.z = z;
  app.view.x = app.LW / 2 - (x0 + x1) / 2 * z; app.view.y = app.tableH / 2 - (y0 + y1) / 2 * z;
}
