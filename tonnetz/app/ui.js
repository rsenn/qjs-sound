/* the HTML toolbar: buttons, labels, the preset panel and the module menu. */

import { app } from './state.js';
import { ROOT_NAMES, SCALE_OPTS, SPL } from './consts.js';
import { direct } from './geom.js';
import { STYLES, clamp } from '../lib/theory.js';
import { $ } from './dom.js';
import { addModule } from './modules.js';
import { applyPanes, arpMode, refreshHintTris, togglePane } from './layout.js';
import { fitView, toWorld } from './view.js';
import { TYPES } from '../lib/synth.js';
import { fillBluesImprov } from '../lib/improv.js';
import { generateSong, songToChords } from '../lib/song.js';

export const HIDDEN_IN_DIRECT = ['play', 'engine', 'style', 'blues', 'song', 'len', 'bpmg', 'tseq'];

export function syncButtons() {
  $('play').textContent = app.player && app.player.paused ? 'Play' : 'Pause';
  $('mode').textContent = 'Mode: ' + (direct() ? 'direct' : 'auto');
  for (const id of HIDDEN_IN_DIRECT) $(id).style.display = direct() ? 'none' : '';
  if (arpMode()) for (const id of ['style', 'blues']) $(id).style.display = 'none';
  $('songoff').style.display = app.harmony.song && !direct() ? '' : 'none';
  applyPanes();
}

export function syncStyle() {
  if (app.player) $('style').textContent = 'Style: ' + (app.player.seq.edited ? 'custom' : app.player.seq.styleSel);
}

export const togglePause = () => { if (!app.player || direct()) return; app.player.setPaused(!app.player.paused); syncButtons(); };

export const setOctave = o => {
  app.harmony.octave = clamp(o, 0, 5);
  $('octv').textContent = 'oct ' + app.harmony.octave;
  if (app.ctx) app.harmony.recompute();
};

export const setBpm = b => {
  const v = clamp(b, 60, 200);
  if (app.player) app.player.seq.bpm = v;
  $('bpmv').textContent = v + ' bpm';
};

export const STYLE_OPTS = ['auto', ...STYLES], LEN_OPTS = [1, 2, 4];

export function applyScale() {
  const sc = SCALE_OPTS[app.scaleIdx];
  $('scale').textContent = 'Scale: ' + (sc ? sc.name : 'free');
  $('root').textContent = 'Root: ' + ROOT_NAMES[app.scaleRoot];
  $('root').style.display = sc ? '' : 'none';
  app.harmony.setLock(app.scaleRoot, sc && sc.iv);
}

export const MODULE_ORDER = ['fmp', 'fm', 'dist', 'vco', 'vcf', 'pad', 'delay', 'reverb'];

export function syncSeqUi() {
  const seq = app.player.seq;
  syncStyle();
  $('engine').textContent = 'Engine: ' + (seq.mode === 'arp' ? 'arp' : 'steps');
  $('len').textContent = `Chord: ${seq.chordBars} bar${seq.chordBars > 1 ? 's' : ''}`;
  $('bpmv').textContent = seq.bpm + ' bpm';
}

export function renderPresets() {
  const list = $('prelist'), { presets, current: curPreset } = app.store, cur = presets[curPreset];
  list.replaceChildren();
  presets.forEach((p, i) => {
    if (!p) return;
    const row = document.createElement('div');
    row.className = 'prow' + (i === curPreset ? ' cur' : '');
    const num = document.createElement('b'), name = document.createElement('span');
    num.textContent = String(i + 1).padStart(3, '0'); name.textContent = p.name;
    row.append(num, name);
    row.onclick = () => app.store.loadProgram(i);
    list.append(row);
  });
  if (!list.children.length) {
    const none = document.createElement('div');
    none.className = 'pempty'; none.textContent = 'No presets yet: name one below and Save.';
    list.append(none);
  }
  $('prebtn').textContent = 'Preset: ' + (cur ? cur.name : '-') + ' ▾';
  if (cur) $('prename').value = cur.name;
}

/* hooks the DOM-free modules call when something they changed has to show in the toolbar */
app.ui.syncStyle = syncStyle;
app.ui.togglePause = togglePause;
app.ui.applyScale = applyScale;
app.ui.setOctave = setOctave;
app.ui.syncSeqUi = syncSeqUi;
app.ui.syncButtons = syncButtons;
app.ui.renderPresets = renderPresets;
app.ui.layoutChanged = side => {
  $('ctl').style.right = side ? app.panes.ton.w + SPL + 8 + 'px' : '';
  $('ctl').style.maxWidth = side ? Math.round(app.LW * 0.55) + 'px' : '';
  $('add').style.maxWidth = side ? Math.round(app.LW * 0.42) + 'px' : '';
};
app.ui.panesChanged = engine => {
  for (const [id, btn] of [['seq', 'tseq'], ['keys', 'tkeys'], ['ton', 'tton']]) $(btn).classList.toggle('off', !(id === 'seq' ? engine : app.panes[id].show));
  $('tseq').textContent = arpMode() ? 'Arpeggiator' : 'Sequencer';
};

$('octd').onclick = () => setOctave(app.harmony.octave - 1);
$('octu').onclick = () => setOctave(app.harmony.octave + 1);
$('bpmd').onclick = () => app.player && setBpm(app.player.seq.bpm - 4);
$('bpmu').onclick = () => app.player && setBpm(app.player.seq.bpm + 4);
$('play').onclick = togglePause;
$('mode').onclick = () => {
  if (!app.player) return;
  app.player.setMode(direct() ? 'auto' : 'direct');
  syncButtons();
  refreshHintTris();
};
$('style').onclick = () => {
  if (!app.player) return;
  const seq = app.player.seq;
  seq.setStyle(STYLE_OPTS[(STYLE_OPTS.indexOf(seq.styleSel) + 1) % STYLE_OPTS.length]);
  syncStyle();
};
/* Every press deals a new riff; it keeps the scale root if one is locked, else it takes the current chord's root. */
$('engine').onclick = () => {
  if (!app.player) return;
  app.player.seq.setMode(arpMode() ? 'steps' : 'arp');
  syncSeqUi();
  syncButtons();
};

/* Every press deals a new arrangement in the current key and plays it from the top, one chord per bar. */
$('song').onclick = () => {
  if (!app.player) return;
  const tonic = app.harmony.lock ? app.harmony.lock.root : app.harmony.sel.root;
  app.harmony.setSong(songToChords(generateSong({ seed: (Math.random() * 0x7fffffff) | 0, bars: 16 }), tonic));
  app.player.seq.barsInChord = 0;
  syncButtons();
};
$('songoff').onclick = () => { app.harmony.setSong(null); syncButtons(); };

$('blues').onclick = () => {
  if (!app.player) return;
  const root = app.harmony.lock ? app.harmony.lock.root : app.harmony.sel.root;
  fillBluesImprov(app.player.seq, app.harmony, { root, seed: (Math.random() * 0x7fffffff) | 0 });
  app.scaleIdx = SCALE_OPTS.findIndex(s => s && s.name === 'blues');
  app.scaleRoot = root;
  applyScale();
  syncSeqUi();
};
$('fit').onclick =() => { if (app.master) fitView(); };
$('tseq').onclick = () => togglePane(app.panes.seq);
$('tkeys').onclick = () => togglePane(app.panes.keys);
$('tton').onclick = () => togglePane(app.panes.ton);
$('tside').onclick = () => {
  app.panes.ton.side = !app.panes.ton.side;
  $('tside').textContent = 'Dock: ' + (app.panes.ton.side ? 'right' : 'bottom');
  try { localStorage.setItem('tonnetz.dock', app.panes.ton.side ? 'right' : 'bottom'); } catch {}
  applyPanes();
};
try { if (localStorage.getItem('tonnetz.dock') === 'right') { app.panes.ton.side = true; $('tside').textContent = 'Dock: right'; } } catch {}

$('scale').onclick = () => { app.scaleIdx = (app.scaleIdx + 1) % SCALE_OPTS.length; applyScale(); };
$('root').onclick = () => { app.scaleRoot = (app.scaleRoot + 1) % 12; applyScale(); };
applyScale();
$('len').onclick = () => {
  if (!app.player) return;
  const seq = app.player.seq;
  seq.chordBars = LEN_OPTS[(LEN_OPTS.indexOf(seq.chordBars) + 1) % LEN_OPTS.length];
  $('len').textContent = `Chord: ${seq.chordBars} bar${seq.chordBars > 1 ? 's' : ''}`;
};
setOctave(app.harmony.octave);
$('bpmv').textContent = '116 bpm';

for (const type of MODULE_ORDER) {
  const row = document.createElement('div'), dot = document.createElement('i'), name = document.createElement('span');
  row.className = 'prow'; dot.className = 'swatch'; dot.style.background = TYPES[type].color;
  name.textContent = TYPES[type].name;
  row.append(dot, name);
  row.onclick = () => {
    $('modpanel').hidden = true;
    if (!app.ctx) return;
    const at = toWorld({ x: app.LW * (0.15 + Math.random() * 0.4), y: app.tableH * (0.25 + Math.random() * 0.5) });
    addModule(type, at.x, at.y);
  };
  $('modlist').append(row);
}
$('modbtn').onclick = () => { $('modpanel').hidden = !$('modpanel').hidden; };
document.addEventListener('pointerdown', e => { if (!$('mod').contains(e.target)) $('modpanel').hidden = true; });

$('prebtn').onclick = () => { $('prepanel').hidden = !$('prepanel').hidden; };
$('presave').onclick = () => { if (app.player) app.store.savePreset($('prename').value); };
$('predel').onclick = () => app.store.deletePreset();
document.addEventListener('pointerdown', e => { if (!$('pre').contains(e.target)) $('prepanel').hidden = true; });
