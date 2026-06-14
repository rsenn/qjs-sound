const isBrowser = typeof globalThis.window !== 'undefined';

async function main() {
  const { AudioContext, OscillatorNode } = isBrowser ? globalThis : await import('labsound');
  const setTimeout = isBrowser ? globalThis.setTimeout : (await import('os')).setTimeout;

  const context = new AudioContext();
  const d = context.destination;

  const o = new OscillatorNode(context, { type: 'sawtooth', frequency: 1000, channelCount: 2 });
  o.connect(d);
  o.start();
  setTimeout(() => o.stop(), 1000);
}

main();
