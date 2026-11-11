/* draws the step sequencer pane. */

import { app } from './state.js';
import { NOTES, STEPS, clamp, mod12 } from '../lib/theory.js';
import { SCALE_OPTS, U, al } from './consts.js';
import { seqGeo } from './geom.js';

export function drawSeq() {
  const S = seqGeo(), seq = app.player.seq, pattern = seq.pattern;
  app.g.textBaseline = 'middle';
  if (S.hh) {
    app.g.font = `${Math.round(clamp(U * 0.36, 11, 13))}px system-ui, sans-serif`;
    app.g.textAlign = 'left'; app.g.fillStyle = '#d8def0';
    const tonality = app.harmony.lock ? `${NOTES[app.harmony.lock.root]} ${SCALE_OPTS[app.scaleIdx].name}` : `${NOTES[app.harmony.analysis.key]} major`;
    app.g.fillText(app.LW > 700 ? `SEQUENCER  ${tonality}` : tonality, 8, app.panes.seq.top + 4 + S.hh / 2);
  }
  for (let r = 0; r < 8; r++) {
    const y = S.gy0 + (7 - r) * S.rh, tone = [0, 2, 4].includes(r % 7);
    app.g.fillStyle = tone ? 'rgba(255,255,255,0.08)' : 'rgba(255,255,255,0.025)';
    app.g.fillRect(S.x0, y, S.cw * STEPS, S.rh - 1);
    app.g.font = `${Math.round(clamp(S.rh * 0.75, 8, 11))}px system-ui, sans-serif`;
    app.g.fillStyle = tone ? '#d8def0' : al('#d8def0', 0.45);
    app.g.textAlign = 'right';
    app.g.fillText(NOTES[mod12(app.harmony.pitchOf(r))], S.x0 - 4, y + S.rh / 2);
  }
  app.g.lineWidth = 1;
  for (const [alpha, from, every] of [[0.05, 0, 1], [0.28, 0, 4]]) {
    app.g.strokeStyle = `rgba(255,255,255,${alpha})`;
    app.g.beginPath();
    for (let k = from; k <= STEPS; k += every) { const x = Math.round(S.x0 + k * S.cw) + 0.5; app.g.moveTo(x, S.gy0); app.g.lineTo(x, S.sy + S.th); }
    app.g.stroke();
  }

  for (let k = 0; k < STEPS; k++) {
    const st = pattern[k], x = S.x0 + k * S.cw, y = S.gy0 + (7 - st.deg) * S.rh;
    if (st.rest) {
      app.g.strokeStyle = 'rgba(255,255,255,0.3)'; app.g.lineWidth = 1;
      app.g.strokeRect(x + 2.5, y + 2.5, S.cw - 5, S.rh - 5);
    } else {
      app.g.fillStyle = st.accent ? '#ff4fd8' : '#39d5ff';
      app.g.fillRect(x + 1, y + 1, S.cw - 2, S.rh - 2);
      if (st.slide && k > 0 && !pattern[k - 1].rest) {
        const py = S.gy0 + (7 - pattern[k - 1].deg) * S.rh + S.rh / 2;
        app.g.strokeStyle = '#fff'; app.g.lineWidth = 2;
        app.g.beginPath(); app.g.moveTo(x - S.cw / 2, py); app.g.lineTo(x + S.cw / 2, y + S.rh / 2); app.g.stroke();
      }
    }
    app.g.fillStyle = 'rgba(255,255,255,0.05)';
    app.g.fillRect(x + 1, S.ay, S.cw - 2, S.th - 1);
    app.g.fillRect(x + 1, S.sy, S.cw - 2, S.th - 1);
    if (st.accent) { app.g.fillStyle = '#ff4fd8'; app.g.fillRect(x + 1, S.ay, S.cw - 2, S.th - 1); }
    if (st.slide) { app.g.fillStyle = '#7dffb0'; app.g.fillRect(x + 1, S.sy, S.cw - 2, S.th - 1); }
    if (seq.running && k === app.curStep) {
      app.g.fillStyle = 'rgba(255,255,255,0.18)';
      app.g.fillRect(x, S.gy0, S.cw, S.sy + S.th - S.gy0);
      app.g.fillStyle = '#fff';
      app.g.fillRect(x, S.gy0 - 3, S.cw, 3);
    }
  }
  app.g.font = `${Math.round(clamp(S.th * 0.6, 8, 10))}px system-ui, sans-serif`;
  app.g.textAlign = 'right'; app.g.fillStyle = al('#d8def0', 0.5);
  app.g.fillText('ACC', S.x0 - 4, S.ay + S.th / 2);
  app.g.fillText('SLD', S.x0 - 4, S.sy + S.th / 2);
}
