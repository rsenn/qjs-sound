/* what a saved session or preset contains, and the store that keeps them. */

import { app } from './state.js';
import { SCALE_OPTS, U, Z_MAX, Z_MIN } from './consts.js';
import { createStore } from './store.js';
import { fitView } from './view.js';
import { stockPresets } from '../lib/presets.js';
import { clamp, mod12 } from '../lib/theory.js';

export const store = createStore({
  storage: localStorage, capture: captureState, apply: applyState, ready: () => !!app.player, onChange: () => app.ui.renderPresets(),
  stock: () => stockPresets({ x0: 0, x1: app.master.x - U * 3.2, y: app.master.y }),
});

/* One snapshot serves the autosaved session and every preset: rack (parameters, wiring, positions), table view, sequencer, scale lock. */
export function captureState() {
  return {
    v: 2,
    mods: app.rack.serialize(), master: { x: app.master.x, y: app.master.y }, view: { ...app.view },
    seq: app.player.seq.getState(), scale: { idx: app.scaleIdx, root: app.scaleRoot }, octave: app.harmony.octave,
    song: app.harmony.song ? app.harmony.song.chords : null,
  };
}

/* Stored data may come from another version or be corrupt, so each part is validated on its own and bad parts are skipped. */
export function applyState(st) {
  if (!st || st.v !== 2 || !Array.isArray(st.mods)) return false;
  app.selected = null;
  app.rack.clear();
  if (st.mods.length && !app.rack.restore(st.mods)) return false;
  const M = st.master, V = st.view, S = st.scale;
  if (M && Number.isFinite(M.x) && Number.isFinite(M.y)) { app.master.x = M.x; app.master.y = M.y; }
  if (V && [V.z, V.x, V.y].every(Number.isFinite)) { app.view.z = clamp(V.z, Z_MIN, Z_MAX); app.view.x = V.x; app.view.y = V.y; }
  else if (st.fit) fitView();
  app.player.seq.setState(st.seq);
  const song = Array.isArray(st.song) && st.song.length <= 128 && st.song.every(id => Number.isInteger(id) && id >= 0 && id < 24);
  app.harmony.setSong(song ? st.song : null);
  if (S && Number.isInteger(S.idx) && S.idx >= 0 && S.idx < SCALE_OPTS.length && Number.isInteger(S.root)) {
    app.scaleIdx = S.idx; app.scaleRoot = mod12(S.root);
    app.ui.applyScale();
  }
  if (Number.isFinite(st.octave)) app.ui.setOctave(st.octave);
  app.ui.syncSeqUi();
  app.ui.syncButtons();
  if (app.player.seq.running) app.rack.padsSet(app.harmony.padVoicing());
  return true;
}
