// portmidi-test: PortMidiStream.openOutput + write + close, the channel
// function, PmDeviceInfo getters, and host-error / device-id getters on a closed stream.
import * as pm from 'portmidi';
import * as tinytest from '../tinytest.js';

const { assert, assertStrictEquals, eq, fail, tests } = tinytest;

tests({
  'initialize/terminate are idempotent'() {
    pm.initialize();
    pm.initialize();
    pm.terminate();
    pm.terminate();
  },

  'countDevices returns a number'() {
    pm.initialize();
    const n = pm.countDevices();
    assert(typeof n === 'number' && n >= 0, 'countDevices: ' + n);
    pm.terminate();
  },

  'getDefaultInputDeviceID / getDefaultOutputDeviceID are numbers'() {
    pm.initialize();
    assert(typeof pm.getDefaultInputDeviceID() === 'number');
    assert(typeof pm.getDefaultOutputDeviceID() === 'number');
    pm.terminate();
  },

  'getDeviceInfo returns a PmDeviceInfo with name/input/output'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return; // no output device in this sandbox
    const info = pm.getDeviceInfo(id);
    assert(typeof info === 'object');
    assert(typeof info.name === 'string');
    assert(typeof info.input === 'boolean');
    assert(typeof info.output === 'boolean');
    assert(typeof info.opened === 'boolean');
    pm.terminate();
  },

  'devices is an exotic array view of PmDeviceInfo objects'() {
    pm.initialize();
    const ds = pm.devices;
    assert(typeof ds === 'object');
    assert(typeof ds.length === 'number');
    if (ds.length) {
      assert(typeof ds[0].name === 'string');
      assert(typeof ds[0].input === 'boolean');
      assert(typeof ds[0].output === 'boolean');
    }
    pm.terminate();
  },

  'time() returns a number'() {
    pm.initialize();
    assert(typeof pm.time() === 'number');
    pm.terminate();
  },

  'getErrorText returns a string'() {
    pm.initialize();
    assert(typeof pm.getErrorText(0) === 'string');
    pm.terminate();
  },

  'channel(n) returns a channel-mask bit (number)'() {
    pm.initialize();
    const mask = pm.channel(0);
    assert(typeof mask === 'number');
    eq(mask, 1 << 0);
    eq(pm.channel(1), 1 << 1);
    eq(pm.channel(15), 1 << 15);
    pm.terminate();
  },

  'PortMidiStream.openOutput opens and isOutput is true'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return;
    const s = pm.PortMidiStream.openOutput(id);
    eq(Object.prototype.toString.call(s), '[object PortMidiStream]');
    eq(s.isOutput, true);
    s.close();
    pm.terminate();
  },

  'PortMidiStream.write returns undefined'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return;
    const s = pm.PortMidiStream.openOutput(id);
    eq(s.write(0x90, 60, 100, 0), undefined);
    eq(s.write(0x80, 60, 100, 0), undefined);
    s.close();
    pm.terminate();
  },

  'PortMidiStream.deviceId and hasHostError after open'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return;
    const s = pm.PortMidiStream.openOutput(id);
    eq(s.deviceId, id);
    eq(s.hasHostError, false);
    s.close();
    pm.terminate();
  },

  'PortMidiStream.close is idempotent'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return;
    const s = pm.PortMidiStream.openOutput(id);
    s.close();
    s.close();
    pm.terminate();
  },

  'PortMidiStream.write after close throws'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return;
    const s = pm.PortMidiStream.openOutput(id);
    s.close();
    let threw = false;
    try {
      s.write(0x90, 60, 100, 0);
    } catch (e) {
      threw = true;
      assert(e instanceof TypeError, 'expected TypeError, got ' + e);
    }
    assert(threw, 'write after close should throw');
    pm.terminate();
  },

  'PortMidiStream.setChannelMask works'() {
    pm.initialize();
    const id = pm.getDefaultOutputDeviceID();
    if (id < 0) return;
    const s = pm.PortMidiStream.openOutput(id);
    s.setChannelMask(pm.channel(0) | pm.channel(1));
    s.close();
    pm.terminate();
  },
});