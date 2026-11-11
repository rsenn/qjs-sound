/* draws a step sequencer grid.
 *
 * ```js
 * drawSeq();                                          // Chordloom: the pane
 * drawSeq({ x, y, w, h }, { g, seq, harmony, step }); // any rectangle, any engine
 * ```
 *
 *   object  r  rectangle in screen pixels; default the sequencer pane
 *   object  v  g (context), seq, harmony, step (playing step or -1); default the app's.
 *              seq.swing shifts every second step right by that fraction of a step, so a shuffle shows in the grid
 */

import { app } from './state.js';
import { NOTES, STEPS, clamp, mod12 } from '../lib/theory.js';
import { SCALE_OPTS, U, al } from './consts.js';
import { seqGeo, seqRect } from './geom.js';

export function drawSeq(r = seqRect(), v = {}) {
  const g = v.g || app.g, seq = v.seq || app.player.seq, harmony = v.harmony || app.harmony;
  const step = v.step === undefined ? app.curStep : v.step;
  const S = seqGeo(r), pattern = seq.pattern;
  g.textBaseline = 'middle';
  if (S.hh) {
    g.font = `${Math.round(clamp(U * 0.36, 11, 13))}px system-ui, sans-serif`;
    g.textAlign = 'left'; g.fillStyle = '#d8def0';
    const tonality = harmony.lock ? `${NOTES[harmony.lock.root]} ${SCALE_OPTS[app.scaleIdx].name}` : `${NOTES[harmony.analysis.key]} major`;
    g.fillText(r.w > 700 ? `SEQUENCER  ${tonality}` : tonality, S.x + 8, S.y + 4 + S.hh / 2);
  }
  for (let r = 0; r < 8; r++) {
    const y = S.gy0 + (7 - r) * S.rh, tone = [0, 2, 4].includes(r % 7);
    g.fillStyle = tone ? 'rgba(255,255,255,0.08)' : 'rgba(255,255,255,0.025)';
    g.fillRect(S.x0, y, S.cw * STEPS, S.rh - 1);
    g.font = `${Math.round(clamp(S.rh * 0.75, 8, 11))}px system-ui, sans-serif`;
    g.fillStyle = tone ? '#d8def0' : al('#d8def0', 0.45);
    g.textAlign = 'right';
    g.fillText(NOTES[mod12(harmony.pitchOf(r))], S.x0 - 4, y + S.rh / 2);
  }
  g.lineWidth = 1;
  for (const [alpha, from, every] of [[0.05, 0, 1], [0.28, 0, 4]]) {
    g.strokeStyle = `rgba(255,255,255,${alpha})`;
    g.beginPath();
    for (let k = from; k <= STEPS; k += every) { const x = Math.round(S.x0 + k * S.cw) + 0.5; g.moveTo(x, S.gy0); g.lineTo(x, S.sy + S.th); }
    g.stroke();
  }

  for (let k = 0; k < STEPS; k++) {
    const st = pattern[k], x = S.x0 + k * S.cw + (k % 2 ? (seq.swing || 0) * S.cw : 0), y = S.gy0 + (7 - st.deg) * S.rh;
    if (st.rest) {
      g.strokeStyle = 'rgba(255,255,255,0.3)'; g.lineWidth = 1;
      g.strokeRect(x + 2.5, y + 2.5, S.cw - 5, S.rh - 5);
    } else {
      g.fillStyle = st.accent ? '#ff4fd8' : '#39d5ff';
      g.fillRect(x + 1, y + 1, S.cw - 2, S.rh - 2);
      if (st.slide && k > 0 && !pattern[k - 1].rest) {
        const py = S.gy0 + (7 - pattern[k - 1].deg) * S.rh + S.rh / 2;
        g.strokeStyle = '#fff'; g.lineWidth = 2;
        g.beginPath(); g.moveTo(x - S.cw / 2, py); g.lineTo(x + S.cw / 2, y + S.rh / 2); g.stroke();
      }
    }
    g.fillStyle = 'rgba(255,255,255,0.05)';
    g.fillRect(x + 1, S.ay, S.cw - 2, S.th - 1);
    g.fillRect(x + 1, S.sy, S.cw - 2, S.th - 1);
    if (st.accent) { g.fillStyle = '#ff4fd8'; g.fillRect(x + 1, S.ay, S.cw - 2, S.th - 1); }
    if (st.slide) { g.fillStyle = '#7dffb0'; g.fillRect(x + 1, S.sy, S.cw - 2, S.th - 1); }
    if (seq.running && k === step) {
      g.fillStyle = 'rgba(255,255,255,0.18)';
      g.fillRect(x, S.gy0, S.cw, S.sy + S.th - S.gy0);
      g.fillStyle = '#fff';
      g.fillRect(x, S.gy0 - 3, S.cw, 3);
    }
  }
  g.font = `${Math.round(clamp(S.th * 0.6, 8, 10))}px system-ui, sans-serif`;
  g.textAlign = 'right'; g.fillStyle = al('#d8def0', 0.5);
  g.fillText('ACC', S.x0 - 4, S.ay + S.th / 2);
  g.fillText('SLD', S.x0 - 4, S.sy + S.th / 2);
}
