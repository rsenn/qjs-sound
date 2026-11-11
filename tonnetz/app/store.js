/* session and preset storage, DOM-free: the page hands in how to capture and apply a state, and where to keep it. */

const MAX_PRESETS = 128, STOCK_VERSION = '2';
const AUTOSAVE_MS = 10 * 60 * 1000, AUTOSAVE_PREFIX = 'autosave ', AUTOSAVE_KEEP = 5;

/* createStore({ storage, prefix, capture, apply, stock, ready, onChange })
 *
 *   Storage   storage   localStorage-shaped; may throw when blocked or full
 *   string    prefix    key prefix: `${prefix}.rack`, `.presets`, `.stockv`
 *   function  capture   () => state object (one snapshot: session and preset alike)
 *   function  apply     (state) => boolean, false when the state is unusable
 *   function  stock     () => stock presets, [{ name, state }]
 *   function  ready     () => boolean, presets are only loaded or autosaved once true
 *   function  onChange  () => void, called when the preset list or the current slot changed
 */
export function createStore({ storage, prefix = 'tonnetz', capture, apply, stock, ready = () => true, onChange = () => {} }) {
  const SESSION_KEY = prefix + '.rack', PRESET_KEY = prefix + '.presets', STOCK_KEY = prefix + '.stockv';
  let savedSession = null, presets = [], cur = -1;
  let lastSave = Date.now(), lastJson = null;

  /* Panning and zooming alone don't count as work worth saving. */
  const presetJson = () => JSON.stringify({ ...capture(), view: null });

  function saveSession() {
    const json = JSON.stringify(capture());
    if (json === savedSession) return;
    try { storage.setItem(SESSION_KEY, json); savedSession = json; } catch (e) { /* storage may be blocked or full */ }
  }

  function restoreSession() {
    try { return apply(JSON.parse(storage.getItem(SESSION_KEY))); } catch (e) { return false; }
  }

  function storePresets() {
    try { storage.setItem(PRESET_KEY, JSON.stringify(presets)); } catch (e) { /* storage may be blocked or full */ }
  }

  const trim = () => { while (presets.length && !presets[presets.length - 1]) presets.pop(); };

  /* A preset's slot is its MIDI program number, so deleting leaves a hole instead of renumbering the ones after it. */
  function loadPresets() {
    try {
      const raw = storage.getItem(PRESET_KEY);
      const a = JSON.parse(raw);
      presets = (Array.isArray(a) ? a : []).slice(0, MAX_PRESETS).map(p => (p && typeof p.name === 'string' && p.state ? p : null));
      /* Bump STOCK_VERSION when a stock preset changes: slots still holding a stock name or nothing are refreshed, other slots are the user's. */
      if (raw === null || storage.getItem(STOCK_KEY) !== STOCK_VERSION) {
        const st = stock();
        st.forEach((s, i) => { if (!presets[i] || st.some(t => t.name === presets[i].name)) presets[i] = s; });
        storage.setItem(STOCK_KEY, STOCK_VERSION);
        storePresets();
      }
    } catch (e) {
      presets = [];
    }
  }

  function loadProgram(n) {
    const p = presets[n];
    if (!p || !ready() || !apply(p.state)) return false;
    cur = n;
    onChange();
    return true;
  }

  function savePreset(name) {
    name = name.trim() || 'Preset ' + (presets.filter(Boolean).length + 1);
    let i = presets.findIndex(p => p && p.name === name);
    if (i < 0) i = presets.indexOf(null);
    if (i < 0) i = presets.length;
    if (i >= MAX_PRESETS) return;
    presets[i] = { name, state: capture() };
    cur = i;
    lastSave = Date.now(); lastJson = presetJson();
    storePresets();
    onChange();
  }

  /* Only the newest AUTOSAVE_KEEP autosaves survive, so they can't eat all the preset slots. */
  function autosavePreset() {
    if (!ready() || Date.now() - lastSave < AUTOSAVE_MS) return;
    const json = presetJson();
    if (json === lastJson) return;
    const d = new Date(), pad = n => String(n).padStart(2, '0');
    const name = `${AUTOSAVE_PREFIX}${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
    const autos = () => presets.map((p, i) => [p, i]).filter(([p]) => p && p.name.startsWith(AUTOSAVE_PREFIX)).sort((a, b) => (a[0].at || 0) - (b[0].at || 0));
    while (autos().length >= AUTOSAVE_KEEP) presets[autos()[0][1]] = null;
    let i = presets.indexOf(null);
    if (i < 0) i = presets.length;
    if (i >= MAX_PRESETS) return;
    presets[i] = { name, at: Date.now(), state: capture() };
    trim();
    lastSave = Date.now(); lastJson = json;
    storePresets();
    onChange();
  }

  function saveArpPreset() {
    let n = 1;
    while (presets.some(p => p && p.name === 'Arp ' + n)) n++;
    savePreset('Arp ' + n);
  }

  function deletePreset() {
    if (!presets[cur]) return;
    presets[cur] = null;
    trim();
    cur = -1;
    storePresets();
    onChange();
  }

  return {
    get presets() { return presets; },
    get current() { return cur; },
    /* marks the present state as already saved, so autosave waits for a change */
    markSaved() { lastJson = presetJson(); },
    saveSession, restoreSession, loadPresets, loadProgram, savePreset, autosavePreset, saveArpPreset, deletePreset,
  };
}
