import { AudioDevice, AudioDestinationNode, AudioContext, OscillatorNode } from 'labsound';
import { setTimeout, kill, getpid, SIGUSR1 } from 'os';
import { startInteractive } from 'util';

const context = new AudioContext();
/*const device = new AudioDevice();
const destination = new AudioDestinationNode(context, device);

device.destination = destination;
context.destination = destination;*/

const oscillator = new OscillatorNode(context, { type: 'sawtooth', frequency: 1000, channelCount: 2 });

oscillator.connect(context.destination);
oscillator.frequency = 440;
oscillator.type = 'Sine';

oscillator.start();

setTimeout(() => {
  oscillator.stop();
  kill(getpid(), SIGUSR1);
}, 3000);

Object.assign(globalThis, { context, oscillator });
//startInteractive()
