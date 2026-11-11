/* adding and removing rack modules. */

import { app } from './state.js';

export function addModule(type, x, y) {
  const m = app.rack.makeModule(type, x, y);
  if (type === 'pad' && app.player.seq.running) app.rack.padSet(m, app.harmony.padVoicing());
  return m;
}

export function removeModule(m) {
  app.rack.removeModule(m);
  if (app.selected === m) app.selected = null;
}
