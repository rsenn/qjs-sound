/* draws the status strip between table and panes. */

import { app } from './state.js';
import { TYPES } from '../lib/synth.js';
import { MODES, NOTES, clamp, cname } from '../lib/theory.js';
import { SCALE_OPTS, U, al, nameOf, stripH } from './consts.js';
import { direct } from './geom.js';

export function drawStrip() {
  const fs = Math.round(clamp(U * 0.34, 11, 14)), y = app.tableH + stripH / 2 + 1;
  app.g.font = `${fs}px system-ui, sans-serif`;
  app.g.textBaseline = 'middle'; app.g.textAlign = 'left'; app.g.fillStyle = '#d8def0';
  const { sel, analysis, hints } = app.harmony;
  const mode = MODES[analysis.mode] || '';
  const scale = app.harmony.lock ? `scale ${NOTES[app.harmony.lock.root]} ${SCALE_OPTS[app.scaleIdx].name}` : `${NOTES[sel.root]} ${mode}${app.LW > 560 ? ` (${NOTES[analysis.key]} major)` : ''}`;
  app.g.fillText(`${nameOf(sel)}  ${scale}`, 8, y);
  app.g.textAlign = 'right'; app.g.fillStyle = al('#d8def0', 0.7);
  if (direct()) { app.g.fillText('direct play', app.LW - 8, y); return; }
  const next = hints.length ? 'next: ' + hints.map(h => (h.back ? '↩' : '') + cname(h.id)).join('  ') : '';
  const seq = app.player && app.player.seq, song = app.harmony.song;
  const bars = song ? `song bar ${(song.bar + song.chords.length - 1) % song.chords.length + 1}/${song.chords.length}` : `bar ${Math.max(1, seq ? seq.barsInChord : 0)}/${seq ? seq.chordBars : 2}`;
  app.g.fillText(`${next}   ${bars}`, app.LW - 8, y);
}

export function statusText() {
  if (app.selected && !app.selected.target) return `${TYPES[app.selected.type].name} selected: tap a module or OUT to send its output there`;
  if (app.selected) return `${TYPES[app.selected.type].name} is patched: tap the x on its cable to disconnect, or tap another module`;
  return '';
}
