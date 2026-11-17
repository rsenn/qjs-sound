import { AudioContext, OfflineAudioContext } from 'labsound';
import { readFileSync, openSync, writeSync, closeSync } from 'fs';

async function generateMoodbar(decodedAudio) {
  /* 1. Set up OfflineAudioContext (runs as fast as CPU allows, no audio playback) */
  const sampleRate = 44100; // Standard rate, or read from your source

  const offlineCtx = new OfflineAudioContext(
    1, // Downmix to mono for analysis
    decodedAudio.length,
    decodedAudio.sampleRate,
  );

  /* 2. Set up Audio Graph: Source -> Analyser -> Destination */
  const source = offlineCtx.createBufferSource();
  source.buffer = decodedAudio;

  const analyser = offlineCtx.createAnalyser();
  analyser.fftSize = 2048; // Matches C++ fftwspectrum def-size
  analyser.smoothingTimeConstant = 0.0; // Disable smoothing to get raw frames

  source.connect(analyser);
  analyser.connect(offlineCtx.destination);

  /* 3. Process frames over time */
  const sampleRateOut = decodedAudio.sampleRate;
  const hopSize = 1024; // Matches C++ def-step
  const binCount = analyser.frequencyBinCount; // 1025 bins
  const freqData = new Float32Array(binCount);

  const rFrames = [];
  const gFrames = [];
  const bFrames = [];

  /* Bark band boundaries (from C++ gstmoodbar.c) */
  const barkBands = [100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500];

  /* Pre-calculate bin-to-bark mapping table */
  const barkTable = new Uint32Array(binCount);
  let currentBark = 0;
  for(let i = 0; i < binCount; i++) {
    const freq = (i * sampleRateOut) / analyser.fftSize;
    if(currentBark < 23 && freq >= barkBands[currentBark]) {
      currentBark++;
    }
    barkTable[i] = currentBark;
  }

  /* Hook into the offline rendering timeline step-by-step */
  const duration = decodedAudio.duration;
  const totalFrames = Math.floor((decodedAudio.length - analyser.fftSize) / hopSize);

  /* Start rendering */
  source.start(0);

  /* Because OfflineAudioContext processes blocks internally, we can pull data
     by rendering in chunks or scheduling script processor/worklet equivalents.
     Alternatively, using standard rendering loop with renderquantum:
     Note: Modern WebAudio standard uses AudioWorklet for precise block handling,
     but for offline batch tasks, we can inspect via frame callbacks or render chunks. */

  /* Simplified frame extraction simulation over timeline ticks:
     (In a true batch script, you iterate time by hopSize / sampleRateOut) */
  let currentTime = 0;
  while(currentTime < duration) {
    offlineCtx.suspend(currentTime).then(() => {
      analyser.getFloatFrequencyData(freqData); // dB values

      const amplitudes = new Float32Array(24);
      for(let i = 0; i < binCount; i++) {
        /* Convert dB back to linear magnitude (approx matching sqrt(real^2 + imag^2)) */
        const linearMag = Math.pow(10, freqData[i] / 20);
        amplitudes[barkTable[i]] += linearMag;
      }

      /* Sum into RGB thirds (0-7: Red, 8-15: Green, 16-23: Blue)[cite: 3] */
      let r = 0,
        g = 0,
        b = 0;
      for(let i = 0; i < 24; i++) {
        const valSq = amplitudes[i] * amplitudes[i];
        if(i < 8) r += valSq;
        else if(i < 16) g += valSq;
        else b += valSq;
      }

      rFrames.push(Math.sqrt(r));
      gFrames.push(Math.sqrt(g));
      bFrames.push(Math.sqrt(b));

      offlineCtx.resume();
    });

    currentTime += hopSize / sampleRateOut;
  }

  await offlineCtx.startRendering();

  /* 4. Normalize channels (Port of Exscalibar / gstmoodbar.c normalization)[cite: 3] */
  normalizeChannel(rFrames);
  normalizeChannel(gFrames);
  normalizeChannel(bFrames);

  /* 5. Resample/Pool down to fixed width (e.g., 1000 bars)[cite: 1, 3] */
  const targetWidth = Math.min(1000, rFrames.length);
  const moodbarRGB = new Uint8Array(targetWidth * 3);

  for(let i = 0; i < targetWidth; i++) {
    const start = Math.floor((i * rFrames.length) / targetWidth);
    const end = Math.floor(((i + 1) * rFrames.length) / targetWidth);
    const count = Math.max(1, end - start);

    let sumR = 0,
      sumG = 0,
      sumB = 0;
    for(let j = start; j < end; j++) {
      sumR += rFrames[j];
      sumG += gFrames[j];
      sumB += bFrames[j];
    }

    moodbarRGB[i * 3] = Math.min(255, Math.max(0, (sumR / count) * 255));
    moodbarRGB[i * 3 + 1] = Math.min(255, Math.max(0, (sumG / count) * 255));
    moodbarRGB[i * 3 + 2] = Math.min(255, Math.max(0, (sumB / count) * 255));
  }

  return moodbarRGB; // Raw RGB byte array of constant size width x 3
}

/* Helper: Classic GStreamer moodbar outlier-trimming normalization[cite: 3] */
function normalizeChannel(vals) {
  if(vals.length === 0) return;
  let mini = vals[0],
    maxi = vals[0];
  for(let i = 1; i < vals.length; i++) {
    if(vals[i] > maxi) maxi = vals[i];
    else if(vals[i] < mini) mini = vals[i];
  }

  let avg = 0,
    t = 0;
  for(let i = 0; i < vals.length; i++) {
    if(vals[i] !== mini && vals[i] !== maxi) {
      avg += vals[i];
      t++;
    }
  }
  avg = t > 0 ? avg / t : mini;

  let avgu = 0,
    tu = 0,
    avgb = 0,
    tb = 0;
  for(let i = 0; i < vals.length; i++) {
    if(vals[i] !== mini && vals[i] !== maxi) {
      if(vals[i] > avg) {
        avgu += vals[i];
        tu++;
      } else {
        avgb += vals[i];
        tb++;
      }
    }
  }
  avgu = tu > 0 ? avgu / tu : avg;
  avgb = tb > 0 ? avgb / tb : avg;

  const minLimit = avg + (avgb - avg) * 2.0;
  const maxLimit = avg + (avgu - avg) * 2.0;
  const delta = maxLimit - minLimit === 0 ? 1.0 : maxLimit - minLimit;

  for(let i = 0; i < vals.length; i++) {
    const val = (vals[i] - minLimit) / delta;
    vals[i] = Math.min(1.0, Math.max(0.0, val));
  }
}

function writeMoodbarPPM(rgbArray, height, outputPath) {
  /* width is the number of RGB triplets (each triplet is one vertical column) */
  const width = Math.floor(rgbArray.length / 3);

  if(width <= 0 || height <= 0) throw new RangeError('Invalid dimensions: width and height must be greater than 0.');

  const fd = openSync(outputPath, 'w');
  try {
    /* 1. write the P6 NetPBM binary header (must be ASCII text) */
    const header = `P6\n${width} ${height}\n255\n`;
    writeSync(fd, header, 'ascii');

    /* 2. create a Buffer view of the rgbArray without copying memory */
    const rowBuffer = Buffer.from(rgbArray.buffer, rgbArray.byteOffset, rgbArray.byteLength);

    /* 3. since every row is identical (due to vertical column stretching),
       write the entire row's pixel bytes 'height' times. */
    for(let y = 0; y < height; y++) writeSync(fd, rowBuffer);
  } finally {
    closeSync(fd);
  }

  console.log(`Successfully saved binary P6 PPM file (${width}x${height}) to ${outputPath}`);
}

async function main(file = 'in.wav') {
  const ac = new AudioContext();

  /* we decode the audio data first (an ArrayBuffer of WAV/MP3/FLAC/OGG) */
  const data = await ac.decodeAudioData(readFileSync(file));

  const rgb = await generateMoodbar(data);

  console.log('rgb', console.config({ compact: true, maxArrayLength: 8, numberBase: 16 }), rgb);

  writeMoodbarPPM(rgb, 32, 'moodbar.ppm');
}

main(...scriptArgs.slice(1));
