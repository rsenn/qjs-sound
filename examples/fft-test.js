import { AudioContext, OfflineAudioContext } from "labsound";
import { readFileSync, openSync, writeSync, closeSync } from "fs";
import { basename, extname } from "path";

/* Port of the moodbar 0.2 pipeline (gstfastspectrum + moodbarbuilder):
 * audioconvert to mono, 254-point unwindowed FFTs (bands = 128) in
 * consecutive blocks within 0.1 s intervals, power averaged per interval,
 * Bark bands, RGB thirds, normalize, pool to 1000 columns. */
const BANDS = 128; /* fastspectrum bands */
const NFFT = 2 * BANDS - 2;
const INTERVAL_NS = 1e8; /* fastspectrum interval, 0.1 s */
const WIDTH = 1000; /* MoodbarPipeline::Finish(1000) */

/* Bark band boundaries (from moodbarbuilder.cpp) */
const barkBands = [100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500];

/* Power spectrum of NFFT real samples, bins 0..BANDS-1, as the direct DFT
 * (254 is 2 * 127, no radix-2 shortcut); folds x[n] and x[N-n] so the cos
 * and sin sums run over half the samples. */
function makeSpectrum() {
  const half = NFFT / 2 - 1; /* 126 */
  const cos = new Float64Array(BANDS * half),
    sin = new Float64Array(BANDS * half),
    a = new Float64Array(half),
    b = new Float64Array(half);

  for(let k = 0; k < BANDS; k++)
    for(let n = 1; n <= half; n++) {
      cos[k * half + n - 1] = Math.cos((2 * Math.PI * ((k * n) % NFFT)) / NFFT);
      sin[k * half + n - 1] = Math.sin((2 * Math.PI * ((k * n) % NFFT)) / NFFT);
    }

  return (x, off, power) => {
    const x0 = x[off],
      xm = x[off + NFFT / 2];

    for(let n = 1; n <= half; n++) {
      a[n - 1] = x[off + n] + x[off + NFFT - n];
      b[n - 1] = x[off + n] - x[off + NFFT - n];
    }

    for(let k = 0; k < BANDS; k++) {
      let re = x0 + (k & 1 ? -xm : xm),
        im = 0;
      const o = k * half;
      for(let n = 0; n < half; n++) {
        re += a[n] * cos[o + n];
        im -= b[n] * sin[o + n];
      }
      power[k] += (re * re + im * im) / (NFFT * NFFT);
    }
  };
}

function generateMoodbar(decodedAudio) {
  const rate = decodedAudio.sampleRate,
    length = decodedAudio.length,
    channels = decodedAudio.numberOfChannels;

  /* downmix to mono like audioconvert: average of all channels */
  const mono = new Float32Array(length);
  for(let c = 0; c < channels; c++) {
    const data = decodedAudio.getChannelData(c);
    for(let i = 0; i < length; i++) mono[i] += data[i] / channels;
  }

  /* MoodbarBuilder::Init */
  const barkTable = new Uint32Array(BANDS + 1);
  for(let i = 0, bark = 0; i <= BANDS; i++) {
    const freq = Math.floor((Math.floor(rate / 2) * i + Math.floor(rate / 4)) / BANDS);
    if(bark < 23 && freq >= barkBands[bark]) bark++;
    barkTable[i] = bark;
  }

  const spectrum = makeSpectrum(),
    power = new Float64Array(BANDS),
    amplitudes = new Float64Array(24),
    rFrames = [],
    gFrames = [],
    bFrames = [];

  /* the interval is a whole number of samples plus a rounding error that
   * is carried over, in ns, as fastspectrum does */
  const framesPerInterval = Math.floor((INTERVAL_NS * rate) / 1e9),
    errorPerInterval = (INTERVAL_NS * rate) % 1e9;
  let framesTodo = framesPerInterval,
    accumulatedError = 0;

  for(let pos = 0; pos + framesTodo <= length; ) {
    const nfft = Math.max(1, Math.floor(framesTodo / NFFT));

    power.fill(0);
    for(let i = 0; i < nfft; i++) spectrum(mono, pos + i * NFFT, power);

    /* MoodbarBuilder::AddFrame */
    amplitudes.fill(0);
    for(let i = 0; i < BANDS; i++) amplitudes[barkTable[i]] += power[i] / nfft;

    const rgb = [0, 0, 0];
    for(let i = 0; i < 24; i++) rgb[Math.floor((i * 3) / 24)] += amplitudes[i] * amplitudes[i];

    rFrames.push(Math.sqrt(rgb[0]));
    gFrames.push(Math.sqrt(rgb[1]));
    bFrames.push(Math.sqrt(rgb[2]));

    pos += framesTodo;
    framesTodo = framesPerInterval;
    if(accumulatedError >= 1e9) {
      accumulatedError -= 1e9;
      framesTodo++;
    }
    accumulatedError += errorPerInterval;
  }

  normalizeChannel(rFrames);
  normalizeChannel(gFrames);
  normalizeChannel(bFrames);

  /* MoodbarBuilder::Finish */
  const numFrames = rFrames.length,
    moodbarRGB = new Uint8Array(WIDTH * 3);

  for(let i = 0; i < WIDTH && numFrames; i++) {
    const start = Math.floor((i * numFrames) / WIDTH);
    let end = Math.floor(((i + 1) * numFrames) / WIDTH);
    if(end == start) end = start + 1;

    let sumR = 0,
      sumG = 0,
      sumB = 0;
    for(let j = start; j < end; j++) {
      sumR += rFrames[j] * 255;
      sumG += gFrames[j] * 255;
      sumB += bFrames[j] * 255;
    }

    const count = end - start;
    moodbarRGB[i * 3] = sumR / count;
    moodbarRGB[i * 3 + 1] = sumG / count;
    moodbarRGB[i * 3 + 2] = sumB / count;
  }

  return moodbarRGB;
}

/* Port of normalize() in gstmoodbar.c */
function normalizeChannel(vals) {
  const n = vals.length;
  if(!n) return;

  let mini = vals[0],
    maxi = vals[0];
  for(let i = 1; i < n; i++) {
    if(vals[i] > maxi) maxi = vals[i];
    else if(vals[i] < mini) mini = vals[i];
  }

  let avg = 0;
  for(let i = 0; i < n; i++) if(vals[i] !== mini && vals[i] !== maxi) avg += vals[i] / n;

  let avgu = 0,
    avgb = 0,
    tu = 0,
    tb = 0;
  for(let i = 0; i < n; i++)
    if(vals[i] !== mini && vals[i] !== maxi) {
      if(vals[i] > avg) {
        avgu += vals[i];
        tu++;
      } else {
        avgb += vals[i];
        tb++;
      }
    }
  avgu /= tu;
  avgb /= tb;

  let avguu = 0,
    avgbb = 0;
  tu = tb = 0;
  for(let i = 0; i < n; i++)
    if(vals[i] !== mini && vals[i] !== maxi) {
      if(vals[i] > avgu) {
        avguu += vals[i];
        tu++;
      } else if(vals[i] < avgb) {
        avgbb += vals[i];
        tb++;
      }
    }
  avguu /= tu;
  avgbb /= tb;

  mini = Math.max(avg + (avgb - avg) * 2, avgbb);
  maxi = Math.min(avg + (avgu - avg) * 2, avguu);
  const delta = maxi - mini == 0 ? 1 : maxi - mini;

  for(let i = 0; i < n; i++) {
    const v = (vals[i] - mini) / delta;
    vals[i] = Number.isFinite(v) ? Math.min(1, Math.max(0, v)) : 0;
  }
}

function writeMoodbarPPM(rgbArray, height, outputPath) {
  /* width is the number of RGB triplets (each triplet is one vertical column) */
  const width = Math.floor(rgbArray.length / 3);

  if(width <= 0 || height <= 0) throw new RangeError("Invalid dimensions: width and height must be greater than 0.");

  const fd = openSync(outputPath, "w");
  try {
    /* 1. write the P6 NetPBM binary header (must be ASCII text) */
    const header = `P6\n${width} ${height}\n255\n`;
    writeSync(fd, header, "ascii");

    /* 2. create a Buffer view of the rgbArray without copying memory */
    const rowBuffer = Buffer.from(rgbArray.buffer, rgbArray.byteOffset, rgbArray.byteLength);

    /* 3. since every row is identical (due to vertical column stretching),
       write the entire row's pixel bytes 'height' times. */
    for(let y = 0; y < height; y++) writeSync(fd, rowBuffer);
  } finally {
    closeSync(fd);
  }

  console.log(`Successfully saved binary P6 PPM file (${width}x${height}) to '${outputPath}'`);
}

async function main(file = "in.wav") {
  let rgb;

  if(file.endsWith(".mood")) {
    console.log(`Reading mood file '${file}'....`);
    const moodBuffer = readFileSync(file);
    rgb = new Uint8Array(moodBuffer);
  } else {
    const ac = new AudioContext();
    console.log(`Analyzing audio file '${file}'....`);
    const data = await ac.decodeAudioData(readFileSync(file));

    rgb = generateMoodbar(data);

    /* Save raw 3000-byte mood file using sync file I/O */
    const moodPath = basename(file, extname(file)) + ".mood";
    const fd = openSync(moodPath, "w");
    try {
      writeSync(fd, Buffer.from(rgb.buffer, rgb.byteOffset, rgb.byteLength));
    } finally {
      closeSync(fd);
    }
    console.log(`Successfully saved mood file to '${moodPath}' (${rgb.length} bytes)`);
  }

  console.log("rgb", rgb.length);

  writeMoodbarPPM(rgb, 32, basename(file, extname(file)) + ".ppm");
}

main(...scriptArgs.slice(1));
