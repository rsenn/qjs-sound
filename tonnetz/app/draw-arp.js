/* draws the arpeggiator pane. */

import { app } from './state.js';
import { ARP_STYLES } from '../lib/arp.js';
import { KEYS, NOTES, STEPS, cid, clamp, cname } from '../lib/theory.js';
import { SCALE_OPTS, U, al, pcHue } from './consts.js';

/* The arpeggiator pane: the tonality's chords to pick from, the run style and octave range, and the run that is playing. */
export function drawArp() {
  const p = app.panes.arp, seq = app.player.seq, key = app.harmony.analysis.key, PAD = 8, chH = 24;
  app.arpHits.length = 0;
  app.g.font = `${Math.round(clamp(U * 0.36, 11, 13))}px system-ui, sans-serif`;
  app.g.textBaseline = 'middle';

  const chip = (x, y, w, h, label, { active, hue, mark, action }) => {
    app.g.fillStyle = hue === undefined ? (active ? '#232842' : '#141726') : `hsl(${hue},${active ? 85 : 55}%,${active ? 62 : 32}%)`;
    app.g.beginPath(); app.g.roundRect(x, y, w, h, h / 2); app.g.fill();
    app.g.strokeStyle = active ? '#fff' : '#2a2f4a'; app.g.lineWidth = active ? 2 : 1; app.g.stroke();
    app.g.fillStyle = hue !== undefined && active ? '#07080d' : '#d8def0'; app.g.textAlign = 'center';
    app.g.fillText(label, x + w / 2, y + h / 2 + 1);
    if (mark) { app.g.fillStyle = '#fff'; app.g.beginPath(); app.g.arc(x + w - 7, y + 7, 3, 0, 7); app.g.fill(); }
    app.arpHits.push({ x, y, w, h, action });
  };

  const rowA = p.top + 6;
  const tonality = app.harmony.lock ? `${NOTES[app.harmony.lock.root]} ${SCALE_OPTS[app.scaleIdx].name}` : `${NOTES[key]} major`;
  const title = app.LW > 700 ? `ARPEGGIATOR  ${tonality}` : tonality;
  const styleW = ARP_STYLES.reduce((a, st) => a + app.g.measureText(st).width + 22, 0);
  const need = 2 * PAD + app.g.measureText(title).width + 16 + styleW + app.g.measureText('oct').width + 16 + 4 * 26 + 56;
  /* Too narrow for one line: the title and Save keep the first row, styles and octaves drop to a second. */
  const wrap = need > app.LW, rowS = wrap ? rowA + chH + 6 : rowA;
  let x = app.LW - PAD;
  for (const o of [4, 3, 2, 1]) {
    x -= 26;
    chip(x, rowS, 24, chH, String(o), { active: seq.arp.octaves === o, action: () => seq.setArp({ octaves: o }) });
  }
  app.g.fillStyle = al('#d8def0', 0.5); app.g.textAlign = 'right';
  app.g.fillText('oct', x - 6, rowS + chH / 2 + 1);
  x -= app.g.measureText('oct').width + 16;
  for (const st of [...ARP_STYLES].reverse()) {
    const w = app.g.measureText(st).width + 18;
    x -= w + 4;
    chip(x, rowS, w, chH, st, { active: seq.arp.style === st, action: () => seq.setArp({ style: st }) });
  }
  chip(wrap ? app.LW - PAD - 52 : x - 56, rowA, 52, chH, 'Save', { action: () => app.store.saveArpPreset() });
  app.g.fillStyle = '#d8def0'; app.g.textAlign = 'left';
  app.g.fillText(title, PAD, rowA + chH / 2 + 1);

  const rowB = rowS + chH + 8, ids = [0, 1, 2, 3, 4, 5].map(i => cid(KEYS[key][i], [1, 2, 5].includes(i)));
  const cur = app.harmony.sel.kind === 'tri' ? app.harmony.sel.id : null, next = app.harmony.hints.length ? app.harmony.hints[0].id : null;
  if (cur !== null && !ids.includes(cur)) ids.push(cur);
  const cw = clamp((app.LW - 2 * PAD - 6 * (ids.length - 1)) / ids.length, 36, 72);
  ids.forEach((id, i) => chip(PAD + i * (cw + 6), rowB, cw, 28, cname(id), {
    active: id === cur, hue: pcHue(id >> 1), mark: id === next,
    action: () => app.player.pickChord(app.harmony.locate(id, app.harmony.sel)),
  }));

  const y0 = rowB + 28 + 10, h = p.top + p.h - 6 - y0;
  if (h < 28) return;
  const n = 3 * seq.arp.octaves, colW = (app.LW - 2 * PAD) / STEPS, pill = clamp(h / (n + 1), 3, 8);
  const yOf = d => y0 + h - pill - (n > 1 ? d / (n - 1) : 0) * (h - pill);
  app.g.fillStyle = 'rgba(255,255,255,0.04)'; app.g.fillRect(PAD, y0, app.LW - 2 * PAD, h);
  seq.pattern.forEach((s, k) => {
    const cx = PAD + k * colW, y = yOf(Math.min(s.deg, n - 1));
    if (seq.running && k === app.curStep) { app.g.fillStyle = 'rgba(255,255,255,0.12)'; app.g.fillRect(cx, y0, colW, h); }
    if (s.rest) { app.g.strokeStyle = 'rgba(255,255,255,0.3)'; app.g.lineWidth = 1; app.g.strokeRect(cx + 2.5, y + 0.5, colW - 5, pill - 1); return; }
    app.g.fillStyle = s.accent ? '#ff4fd8' : '#39d5ff';
    app.g.fillRect(cx + 1, y, colW - 2, pill);
    const prev = seq.pattern[k - 1];
    if (s.slide && prev && !prev.rest) {
      app.g.strokeStyle = '#fff'; app.g.lineWidth = 2;
      app.g.beginPath(); app.g.moveTo(cx, yOf(Math.min(prev.deg, n - 1)) + pill / 2); app.g.lineTo(cx + colW / 2, y + pill / 2); app.g.stroke();
    }
  });
}
