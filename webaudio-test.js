import { AudioContext, OscillatorNode } from 'labsound';
let context = new AudioContext();

let d = context.destination;

console.log('destination',d);

let o = new OscillatorNode(context, { type: 'sawtooth', frequency: 1000, channelCount: 2 });

o.connect(d);
o.start();
setTimeout(() => o.stop(), 1000);
