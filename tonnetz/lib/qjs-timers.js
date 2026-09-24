/* Browser-shaped timers on top of QuickJS's os module, for Rack and Player under qjsm. */

import * as os from 'os';

let nextId = 1;
const intervals = new Map();

export const timers = {
  setTimeout: (fn, ms) => os.setTimeout(fn, ms),
  /* os.clearTimeout throws on a handle it does not know, where browsers ignore it. */
  clearTimeout: h => { if (h !== undefined) os.clearTimeout(h); },
  setInterval(fn, ms) {
    const id = nextId++;
    const tick = () => {
      intervals.set(id, os.setTimeout(tick, ms));
      fn();
    };
    intervals.set(id, os.setTimeout(tick, ms));
    return id;
  },
  clearInterval(id) {
    const h = intervals.get(id);
    if (h === undefined) return;
    os.clearTimeout(h);
    intervals.delete(id);
  },
};
