// AudioWorkletProcessor around qjs-sound-web.wasm: one processor class, three DSP kinds.
//
//   new AudioWorkletNode(ctx, 'qjs-sound', {
//     numberOfInputs: 1, outputChannelCount: [1],
//     processorOptions: { wasm: <WebAssembly.Module>, kind: 'biquad' | 'reson' | 'plucked', freq, q }
//   });
//   node.port.postMessage({ set: { freq, q } });          // retune the filter
//   node.port.postMessage({ noteOn: { freq, amp } });     // pluck (kind 'plucked')
//
// process() makes one call into wasm per 128-frame block, never one per sample.

// the standalone wasm imports a few WASI/env functions (clock, fd_write ...) that the DSP never uses
const unused = new Proxy({}, { get: () => () => 0 });

function instantiate(module) {
  const instance = new WebAssembly.Instance(module, { env: unused, wasi_snapshot_preview1: unused });
  instance.exports._initialize();
  return instance.exports;
}

const BLOCK = 128;

class QjsSoundProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { wasm, kind = 'biquad', freq = 1000, q = 0.9 } = options.processorOptions;
    const x = (this.x = instantiate(wasm));

    this.kind = kind;
    x.stk_set_sample_rate(sampleRate);

    // input and output blocks live in wasm memory
    this.inPtr = x.qsw_alloc(BLOCK * 4);
    this.outPtr = x.qsw_alloc(BLOCK * 4);
    this.block = x.stk_block_new(BLOCK);

    if(kind == 'biquad') this.dsp = x.stk_biquad_new(freq, q);
    else if(kind == 'reson') this.dsp = x.snd_reson_new(freq, freq * q, BLOCK, sampleRate);
    else if(kind == 'plucked') this.dsp = x.stk_plucked_new(50);
    else throw new Error(`unknown kind '${kind}'`);

    this.port.onmessage = ({ data }) => {
      if(data.set) this.set(data.set.freq, data.set.q);
      if(data.noteOn) x.stk_plucked_note_on(this.dsp, data.noteOn.freq, data.noteOn.amp ?? 0.8);
    };
  }

  set(freq, q) {
    if(this.kind == 'biquad') this.x.stk_biquad_set(this.dsp, freq, q);
    else if(this.kind == 'reson') this.x.snd_reson_set(this.dsp, freq, freq * q);
  }

  process(inputs, outputs) {
    const { x, inPtr, outPtr, dsp, block } = this;
    const out = outputs[0];
    const n = out[0].length;
    const mem = x.memory.buffer; // the view is rebuilt per call: wasm memory may have grown
    const input = new Float32Array(mem, inPtr, n);
    const output = new Float32Array(mem, outPtr, n);

    if(this.kind == 'plucked') {
      x.stk_plucked_process(dsp, block, outPtr, n);
    } else {
      const src = inputs[0] && inputs[0][0];
      if(!src) return true;
      input.set(src);
      if(this.kind == 'biquad') x.stk_biquad_process(dsp, block, inPtr, outPtr, n);
      else x.snd_reson_process(dsp, inPtr, outPtr, n);
    }

    for(const channel of out) channel.set(output);
    return true;
  }
}

registerProcessor('qjs-sound', QjsSoundProcessor);
