import { AudioContext, OscillatorNode } from 'labsound';
import { setTimeout } from 'os';
import { startInteractive } from 'util';

let context = new AudioContext();

let { destination } = context;

console.log(typeof context.destination == 'object' ? true : context.destination);

let oscillator = new OscillatorNode(context, { type: 'sawtooth', frequency: 1000, channelCount: 2 });

Object.assign(globalThis, { context, destination, oscillator });

oscillator.connect(destination);
oscillator.start();
setTimeout(() => oscillator.stop(), 1000);

os.kill(os.getpid(), os.SIGUSR1);

//startInteractive()
