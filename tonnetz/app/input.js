/* pointer and keyboard input on normalized events, no DOM: pointers are { id, x, y }, keys are codes. */

import { app } from './state.js';
import { A0, BLACK_KEYS, SPAN, SPL, U, WHITE_OFFS, Z_MAX, Z_MIN } from './consts.js';
import { handleAt, keysGeo, minTable, seqGeo, sideOn } from './geom.js';
import { paintStep, seqDrag, seqPress } from './seq-edit.js';
import { TYPES } from '../lib/synth.js';
import { STEPS, clamp } from '../lib/theory.js';
import { removeModule } from './modules.js';
import { fitView, toWorld, zoomAt } from './view.js';

/* Black keys overlap the white ones, so they get first claim on the upper part of the keyboard. */
export const keysMidiAt = p => {
  const K = keysGeo(), o = clamp(Math.floor(p.x / (7 * K.ww)), 0, K.oct - 1), xi = p.x - o * 7 * K.ww;
  if (p.y - K.top < K.bh) {
    for (const [pc, wi] of BLACK_KEYS) if (Math.abs(xi - wi * K.ww) < K.ww * 0.3) return K.base + 12 * o + pc;
  }
  return K.base + 12 * o + WHITE_OFFS[clamp(Math.floor(xi / K.ww), 0, 6)];
};

/* Notes from the piano and the computer keyboard are snapped to the locked scale, the same as everything else the player sounds. */
export function startNote(midi, sliding) {
  const m = app.harmony.snapMidi(midi), entry = { midi: m, t0: performance.now(), t1: null };
  app.player.noteOn(m, sliding);
  app.trail.push(entry);
  return entry;
}

export function endNote(entry) { if (entry) entry.t1 = performance.now(); }

/* Physical key positions, so QWERTY and QWERTZ layouts play the same notes: two rows, the number/home rows above them for sharps. */
export const KEY_NOTES = (() => {
  const map = {};
  const put = (codes, base, semis) => codes.forEach((c, i) => { if (c) map[c] = base + semis[i]; });
  const white = [0, 2, 4, 5, 7, 9, 11, 12, 14, 16];
  put(['KeyZ', 'KeyX', 'KeyC', 'KeyV', 'KeyB', 'KeyN', 'KeyM', 'Comma', 'Period', 'Slash'], 12, white);
  put(['KeyS', 'KeyD', null, 'KeyG', 'KeyH', 'KeyJ', null, 'KeyL', 'Semicolon'], 12, [1, 3, 0, 6, 8, 10, 0, 13, 15]);
  put(['KeyQ', 'KeyW', 'KeyE', 'KeyR', 'KeyT', 'KeyY', 'KeyU', 'KeyI', 'KeyO', 'KeyP'], 24, white);
  put(['Digit2', 'Digit3', null, 'Digit5', 'Digit6', 'Digit7', null, 'Digit9', 'Digit0'], 24, [1, 3, 0, 6, 8, 10, 0, 13, 15]);
  return map;
})();

export const splitAt = p => p.x < app.LW && app.paneList.find(q => q.show && p.y >= q.split - 4 && p.y < q.top + 4);

export const sideSplitAt = p => sideOn() && p.x >= app.LW - 4 && p.x < app.LW + SPL + 4;

/* Dragging a splitter moves the boundary above its pane: the pane above (or the table) gives or takes the difference. */
export function dragSplit(s, p) {
  const want = s.y0 - p.y, room = s.prev ? s.prev.h0 - s.prev.min : s.table0 - minTable();
  /* Once the space above is used up, growing takes from the pane below instead of stalling. */
  const fromAbove = clamp(want, 0, room);
  const fromBelow = s.next ? clamp(want - fromAbove, 0, s.next.h0 - s.next.min) : 0;
  const d = want < 0 ? Math.max(want, s.pane.min - s.pane.h0) : fromAbove + fromBelow;
  s.pane.h = s.pane.h0 + d;
  if (s.prev) s.prev.h = s.prev.h0 - (want < 0 ? d : fromAbove);
  if (s.next) s.next.h = s.next.h0 - fromBelow;
  app.layoutDirty = true;
}

/* The module whose centre is closest wins, so overlapping rings of neighbours never steal a touch from the nearer one. */
export function hitModule(p) {
  let best = null, bd = Infinity;
  for (const m of app.mods) {
    const d = Math.hypot(p.x - m.x, p.y - m.y);
    let zone = -1;
    if (d >= U) {
      zone = Math.floor((d - U) / (U * 0.5));
      if (zone >= TYPES[m.type].params.length) continue;
    }
    if (d <= bd) { bd = d; best = { m, zone }; }
  }
  return best;
}

/* The knob follows the finger's angle directly. In the gap at the bottom of the arc the value holds the end it came from,
   so a sweep can never wrap from max to min. */
export function setRing(s, p) {
  const dx = p.x - s.m.x, dy = p.y - s.m.y;
  if (Math.hypot(dx, dy) < U * 0.5) return;
  let t = ((Math.atan2(dy, dx) - A0) % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI) / SPAN;
  if (t > 1) t = s.m.p[s.key] > 0.5 ? 1 : 0;
  s.m.p[s.key] = t;
  app.rack.apply(s.m);
}

export function hitHandle(p) {
  for (const m of app.mods) {
    if (!m.target) continue;
    const h = handleAt(m);
    if (Math.hypot(p.x - h.x, p.y - h.y) < U * 0.5) return m;
  }
  return null;
}

export function tapModule(m) {
  const now = performance.now();
  if (app.lastTap.m === m && now - app.lastTap.t < 350) { removeModule(m); app.lastTap = { t: 0, m: null }; return; }
  app.lastTap = { t: now, m };
  if (app.selected && app.selected !== m && !app.selected.target && app.rack.canConnect(app.selected, m)) {
    app.rack.connect(app.selected, m);
    app.selected = null;
  } else {
    app.selected = app.selected === m ? null : m;
  }
}

export const panPointers = () => [...app.ptr.values()].filter(s => s.kind === 'pan');

/* One finger on the background pans; a second one turns it into a pinch that zooms about the midpoint. */
export function beginGesture() {
  const ps = panPointers();
  for (const s of ps) { s.ox = s.x; s.oy = s.y; s.vx = app.view.x; s.vy = app.view.y; }
  if (ps.length !== 2) { app.pinch = null; return; }
  const [a, b] = ps;
  app.pinch = { d0: Math.hypot(a.x - b.x, a.y - b.y) || 1, z0: app.view.z, w: toWorld({ x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }) };
  a.moved = b.moved = true;
}

export function moveGesture(s, p) {
  s.x = p.x; s.y = p.y;
  const ps = panPointers();
  if (ps.length === 2 && app.pinch) {
    const [a, b] = ps, mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    app.view.z = clamp(app.pinch.z0 * Math.hypot(a.x - b.x, a.y - b.y) / app.pinch.d0, Z_MIN, Z_MAX);
    app.view.x = mx - app.pinch.w.x * app.view.z; app.view.y = my - app.pinch.w.y * app.view.z;
  } else {
    if (Math.hypot(s.x - s.ox, s.y - s.oy) > 6) s.moved = true;
    app.view.x = s.vx + s.x - s.ox; app.view.y = s.vy + s.y - s.oy;
  }
}

export const tonTouches = () => [...app.ptr.values()].filter(s => s.kind === 'ton').length;

/* a pointer came up or was cancelled. */
export function pointerUp(e) {
  if (app.ctx && app.ctx.state !== 'running') app.ctx.resume();
  const s = app.ptr.get(e.id);
  if (!s) return;
  app.ptr.delete(e.id);
  if (s.kind === 'ton') app.player.lift(tonTouches());
  else if (s.kind === 'keys') { endNote(s.entry); noteOffIfIdle(); }
  else if (s.kind === 'pan') {
    beginGesture();
    if (!s.moved && performance.now() - s.t0 < 300) {
      app.selected = null;
      const now = performance.now();
      if (now - app.lastBgTap < 350) fitView();
      app.lastBgTap = now;
    }
  }
  else if (s.kind === 'move' && !s.moved && performance.now() - s.t0 < 300) tapModule(s.m);
}

/* Preventing keyup too stops a focused button from also firing its own click on space. */
export const noteOffIfIdle = () => {
  if (!app.keysDown.size && ![...app.ptr.values()].some(s => s.kind === 'keys')) app.player.noteOff();
};

/* a pointer went down at e.x, e.y: starts the gesture that fits what is under it. */
export function pointerDown(e) {
  if (!app.ctx) return;
  if (app.ctx.state !== 'running') app.ctx.resume();
  const p = e;
  if (sideSplitAt(p)) {
    app.ptr.set(e.id, { kind: 'vsplit', x0: p.x, w0: app.panes.ton.w });
    return;
  }
  const sp = splitAt(p);
  if (sp) {
    for (const q of app.paneList) q.h0 = q.h;
    const prev = app.paneList.slice(0, app.paneList.indexOf(sp)).reverse().find(q => q.show) || null;
    const next = app.paneList.slice(app.paneList.indexOf(sp) + 1).find(q => q.show) || null;
    app.ptr.set(e.id, { kind: 'split', pane: sp, prev, next, y0: p.y, table0: app.tableH });
    return;
  }
  if (sideOn() ? p.x >= app.LW + SPL : app.panes.ton.show && p.y >= app.panes.ton.top) {
    app.ptr.set(e.id, { kind: 'ton' });
    app.player.press(app.geo.hit(p));
    if (app.player.live) app.pulse = 1;
    return;
  }
  if (app.panes.keys.show && p.y >= app.panes.keys.top) {
    const s = { kind: 'keys', midi: keysMidiAt(p) };
    s.entry = startNote(s.midi, false);
    app.ptr.set(e.id, s);
    return;
  }
  if (app.panes.arp.show && p.y >= app.panes.arp.top) {
    const hit = app.arpHits.find(h => p.x >= h.x && p.x < h.x + h.w && p.y >= h.y && p.y < h.y + h.h);
    if (hit) hit.action();
    return;
  }
  if (app.panes.seq.show && p.y >= app.panes.seq.top) {
    const S = seqGeo(), seq = app.player.seq, s = seqPress(S, seq, p);
    if (!s) return;
    app.ptr.set(e.id, s);
    paintStep(s, s.col, p, S, seq);
    app.ui.syncStyle();
    return;
  }
  if (p.y >= app.tableH) return;
  const wp = toWorld(p);
  const cable = hitHandle(wp);
  if (cable) { app.rack.disconnect(cable); return; }
  const hit = hitModule(wp);
  if (!hit) {
    if (Math.hypot(wp.x - app.master.x, wp.y - app.master.y) < U * 1.5) {
      if (app.selected && !app.selected.target) { app.rack.connect(app.selected, app.master); app.selected = null; }
      return;
    }
    app.ptr.set(e.id, { kind: 'pan', x: p.x, y: p.y, t0: performance.now(), moved: false });
    beginGesture();
    return;
  }
  const { m, zone } = hit;
  app.mods.splice(app.mods.indexOf(m), 1); app.mods.push(m);
  if (zone < 0) {
    app.ptr.set(e.id, { kind: 'move', m, dx: m.x - wp.x, dy: m.y - wp.y, x0: p.x, y0: p.y, t0: performance.now(), moved: false });
  } else {
    const s = { kind: 'ring', m, key: zone };
    app.ptr.set(e.id, s);
    setRing(s, wp);
  }
}

/* a pointer moved; returns the cursor to show when no gesture is running, else undefined. */
export function pointerMove(e) {
  const s = app.ptr.get(e.id);
  if (!s) return sideSplitAt(e) ? 'col-resize' : splitAt(e) ? 'row-resize' : '';
  const p = e;
  if (s.kind === 'vsplit') {
    app.panes.ton.w = s.w0 + s.x0 - p.x;
    app.layoutDirty = true;
  } else if (s.kind === 'split') {
    dragSplit(s, p);
  } else if (s.kind === 'keys') {
    const m = app.harmony.snapMidi(keysMidiAt(p));
    if (m !== s.entry.midi) { endNote(s.entry); s.entry = startNote(m, true); }
  } else if (s.kind === 'seq') {
    seqDrag(seqGeo(), app.player.seq, s, p);
  } else if (s.kind === 'ton') {
    const before = app.harmony.sel.key;
    app.player.slide(app.geo.hit(p));
    if (app.player.live && app.harmony.sel.key !== before) app.pulse = 1;
  } else if (s.kind === 'pan') {
    moveGesture(s, p);
  } else if (s.kind === 'move') {
    if (Math.hypot(p.x - s.x0, p.y - s.y0) > 8) s.moved = true;
    const wp = toWorld(p);
    s.m.x = wp.x + s.dx; s.m.y = wp.y + s.dy;
  } else {
    setRing(s, toWorld(p));
  }
}

/* zooms the table about the pointer; returns true when the event was used. */
export function wheel(e) {
  if (e.y >= app.tableH || e.x >= app.LW) return false;
  zoomAt(e.x, e.y, app.view.z * Math.exp(-e.dy * 0.0015));
  return true;
}

/* drops every running gesture and sounding note, e.g. when the window loses focus. */
export function cancelAll() {
  app.ptr.clear();
  if (!app.player) return;
  app.player.lift(0);
  for (const k of app.keysDown.values()) endNote(k);
  app.keysDown.clear();
  app.player.noteOff();
}

/* a key went down: e is { code, repeat, mod }; returns true when the key was used (the page then swallows it). */
export function keyDown(e) {
  if (e.code === 'Space') {
    if (!e.repeat) app.ui.togglePause();
    return true;
  }
  const note = KEY_NOTES[e.code];
  if (note === undefined || !app.player || e.mod) return false;
  if (e.repeat || app.keysDown.has(e.code)) return true;
  if (app.ctx.state !== 'running') app.ctx.resume();
  app.keysDown.set(e.code, startNote(12 * (app.harmony.octave + 1) + note, app.keysDown.size > 0));
  return true;
}

/* a key came up: e is { code }. */
export function keyUp(e) {
  if (!app.keysDown.has(e.code)) return;
  endNote(app.keysDown.get(e.code));
  app.keysDown.delete(e.code);
  noteOffIfIdle();
}
