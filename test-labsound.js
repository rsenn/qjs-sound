import { AudioDevice, AudioContext, AudioDestinationNode, OscillatorNode } from 'labsound';

function main() {
  const device = new AudioDevice({}, { sampleRate: 48000, numberOfChannels: 2 });

  const context = new AudioContext(false, true);

  const destinationNode = new AudioDestinationNode(context, device);

  device.destination = destinationNode;
  context.destination = destinationNode;

  const oscillator = new OscillatorNode(context);

  const gain = context.createGain();

  gain.gain = 0.0625;

  oscillator.connect(gain, 0, 0);
  gain.connect(context.destination, 0, 0);

  oscillator.frequency = 440.0;
  oscillator.type = 'Sine';
  oscillator.start(0);

  setTimeout(() => oscillator.stop(), 1000);
  setTimeout(() => {
    Object.assign(globalThis, { device, context, destinationNode, oscillator, gain });
    os.kill(os.getpid(), os.SIGUSR1);
  }, 2000);

  return 0;
}

main();
