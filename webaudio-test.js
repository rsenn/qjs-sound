import { AudioDevice, AudioDestinationNode, AudioContext, OscillatorNode } from 'labsound';
import { setTimeout } from 'os';
import { startInteractive } from 'util';

const device = new AudioDevice();

const context = new AudioContext();

const destination = new AudioDestinationNode(context, device);
context.destination = destination;

//const { destination } = context;

console.log(typeof destination == 'object' ? true : destination);

const oscillator = new OscillatorNode(context, { type: 'sawtooth', frequency: 1000, channelCount: 2 });

Object.assign(globalThis, { context, destination, oscillator });

oscillator.connect(destination);
oscillator.start();
setTimeout(() => oscillator.stop(), 1000);

os.kill(os.getpid(), os.SIGUSR1);

//startInteractive()
