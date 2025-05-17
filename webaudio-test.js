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
oscillator.start();

setTimeout(() => oscillator.stop(), 1000);

kill(getpid(), SIGUSR1);
//Object.assign(globalThis, { context, destination, oscillator });
//startInteractive()
