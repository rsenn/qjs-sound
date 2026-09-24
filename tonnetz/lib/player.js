/* Ties harmony, rack and sequencer together and decides what a touch on the tonnetz sounds like.
   'auto': the touch latches a chord and the sequencer wanders on from it.
   'direct': no sequencer; exactly the touched notes sound while a finger is down. */

import { Sequencer } from './sequencer.js';

export class Player {
  constructor(harmony, rack, { timers = globalThis } = {}) {
    this.h = harmony;
    this.rack = rack;
    this.timers = timers;
    this.seq = new Sequencer(harmony, rack);
    this.mode = 'auto';
    this.paused = false;
    this.held = false;
    this.timer = null;

    const prev = harmony.onChange;
    harmony.onChange = () => {
      prev();
      if (this.seq.running) rack.padsSet(harmony.padVoicing());
    };
  }

  start() {
    this.seq.running = true;
    this.seq.relatch();
    this.seq.nextT = this.rack.ctx.currentTime + 0.1;
    this.timer = this.timers.setInterval(() => this.seq.schedule(), 25);
  }

  stop() {
    this.timers.clearInterval(this.timer);
    this.seq.setRunning(false);
  }

  applyRunning() {
    this.seq.setRunning(this.mode === 'auto' && !this.paused);
  }

  setPaused(p) {
    this.paused = p;
    this.applyRunning();
  }

  setMode(mode) {
    if (mode === this.mode) return;
    this.mode = mode;
    this.held = false;
    if (mode === 'auto') this.seq.relatch();
    this.applyRunning();
  }

  press(sel) {
    this.h.setSel(sel);
    this.strike(0.9, false);
  }

  slide(sel) {
    if (sel.key === this.h.sel.key) return;
    this.h.setSel(sel);
    this.strike(0.8, true);
  }

  /* `fingersLeft` counts the tonnetz touches still down after this one lifted. */
  lift(fingersLeft) {
    if (this.mode === 'auto') { this.seq.relatch(); return; }
    if (fingersLeft > 0 || !this.held) return;
    this.held = false;
    this.rack.padsSet([]);
    this.rack.vcoStop();
  }

  strike(vel, sliding) {
    const { h, rack } = this, t = rack.ctx.currentTime + 0.01;
    if (this.mode === 'auto') {
      rack.chordHit(h.pianoVoicing(), t, vel);
      return;
    }
    rack.chordHit(h.directPiano(), t, vel);
    rack.padsSet(h.directPad());
    rack.vcoHold(h.directLead(), t, sliding && this.held);
    this.held = true;
  }
}
