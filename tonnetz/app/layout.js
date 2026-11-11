/* pane layout: sizes, splitters, visibility; DOM changes go through the app.ui hooks. */

import { app } from './state.js';
import { SPL, stripH } from './consts.js';
import { buildCache } from './draw-tonnetz.js';
import { direct, minTable, sideOn } from './geom.js';
import { defaultView } from './view.js';
import { clamp, cname } from '../lib/theory.js';

export function refreshHintTris() {
  app.hintTris = [];
  if (!app.geo.width || direct()) return;
  app.harmony.hints.forEach((h, rank) => {
    for (const c of app.geo.copiesOf(h.id)) app.hintTris.push({ ...c, rank, name: (h.back ? '↩' : '') + cname(h.id) });
  });
}

/* When the panes don't fit, the lower ones give up height first, but never below their minimum. */
export function fitPanes() {
  const vis = app.paneList.filter(p => p.show);
  let over = vis.reduce((a, p) => a + Math.max(p.h, p.min) + SPL, 0) - (app.H - stripH - minTable());
  for (const p of vis) {
    p.h = Math.max(p.h, p.min);
    const cut = clamp(over, 0, p.h - p.min);
    p.h -= cut; over -= cut;
  }
}

export function relayout() {
  const side = sideOn();
  app.panes.ton.w = clamp(app.panes.ton.w, 160, Math.max(160, app.W - 300));
  app.LW = side ? app.W - app.panes.ton.w - SPL : app.W;
  app.ui.layoutChanged(side);
  app.paneList = [app.panes.seq, app.panes.arp, app.panes.keys, ...(side ? [] : [app.panes.ton])];
  fitPanes();
  app.tableH = app.H - stripH - app.paneList.reduce((a, p) => a + (p.show ? SPL + p.h : 0), 0);
  let y = app.tableH + stripH;
  for (const p of app.paneList) {
    if (p.show) { p.split = y; p.top = y + SPL; y = p.top + p.h; } else p.split = p.top = app.H;
  }
  if (side) {
    app.panes.ton.split = app.panes.ton.top = app.H;
    app.tonS = clamp(Math.min(app.H / 5, app.panes.ton.w / 3.5), 20, 140);
    app.geo.layout(app.H, app.tonS, (app.panes.ton.w - 3.35 * app.tonS) / 2, { x: app.LW + SPL, y: 0 });
  } else {
    app.tonS = clamp(Math.min(app.W / 5, app.panes.ton.h / 3.35), 20, app.W / 5);
    app.geo.layout(app.W, app.tonS, app.panes.ton.top + (app.panes.ton.h - 3.35 * app.tonS) / 2);
  }
  refreshHintTris();
  buildCache();
}

export function resize(w, h, dpr) {
  const oh = app.H;
  app.W = w; app.H = h; app.DPR = Math.min(dpr || 1, 1.5);
  if (oh) for (const p of app.paneList) p.h = Math.round(p.h * app.H / oh);
  else {
    app.panes.seq.h = app.panes.arp.h = clamp(Math.round(app.H * 0.24), 110, 220);
    app.panes.keys.h = clamp(Math.round(app.H * 0.15), 90, 150);
    app.panes.ton.w = clamp(Math.round(app.W * 0.28), 200, 360);
    app.panes.ton.h = Math.round(Math.min(app.W / 8, app.H * 0.4 / 3.35) * 3.35);
  }
  relayout();
  if (!oh) defaultView();
}

/* Direct mode plays no pattern, so the sequencer hides itself there and returns with the user's own choice on going back to auto. */
export const arpMode = () => !!app.player && app.player.seq.mode === 'arp';

/* The arpeggiator pane takes the sequencer grid's place while the engine is in arp mode; both share one show/hide choice. */
export function applyPanes() {
  const engine = app.panes.seq.want && !direct();
  app.panes.seq.show = engine && !arpMode();
  app.panes.arp.show = engine && arpMode();
  app.panes.keys.show = app.panes.keys.want;
  app.panes.ton.show = app.panes.ton.want;
  app.ui.panesChanged(engine);
  app.layoutDirty = true;
}

export function togglePane(p) {
  p.want = !p.want;
  applyPanes();
}
