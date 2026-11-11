/* the page's mutable state in one object: modules read and write `app.x` instead of a module-level `let`. */

import { TonnetzGeometry } from '../lib/geometry.js';
import { Harmony } from '../lib/harmony.js';

const geo = new TonnetzGeometry();

export const app = {
  /* audio; created by start() */
  ctx: undefined, rack: undefined, player: undefined, master: undefined, mods: [],

  /* canvas size in CSS pixels, device pixel ratio, table height, tonnetz cell size */
  W: undefined, H: undefined, DPR: undefined, tableH: undefined, tonS: undefined,
  /* width of everything but the docked-right tonnetz; equals W otherwise */
  LW: undefined,

  /* the table lives in world coordinates with a fixed module size; `view` maps them to the screen */
  view: { z: 1, x: 0, y: 0 },
  lastBgTap: 0, pinch: null,

  /* panes stack below the table in this order, each under its own splitter; hidden ones keep their height for when they return */
  panes: {
    seq: { want: true, show: true, h: 0, min: 100, split: 0, top: 0 },
    arp: { want: true, show: false, h: 0, min: 130, split: 0, top: 0 },
    keys: { want: false, show: false, h: 0, min: 60, split: 0, top: 0 },
    ton: { want: true, show: true, h: 0, min: 80, split: 0, top: 0, side: false, w: 0 },
  },
  paneList: undefined,
  layoutDirty: false, curStep: -1,

  arpHits: [], trail: [], keysDown: new Map(), ptr: new Map(),
  selected: null, lastTap: { t: 0, m: null },
  hintTris: [], pulse: 0,
  scaleIdx: 0, scaleRoot: 0,
  cache: undefined, lastFrame: 0,

  geo,
  harmony: new Harmony({ locate: (id, from) => geo.findTri(id, from), onChange: () => app.onHarmonyChange() }),
  /* set by the page: recompute the hint triangles when the harmony changes */
  onHarmonyChange() {},
};

app.paneList = [app.panes.seq, app.panes.arp, app.panes.keys, app.panes.ton];
