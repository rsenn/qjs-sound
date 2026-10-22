import { AudioContext } from "labsound";
import { readFileSync } from "fs";

async function main(file = "in.wav") {
  const ac = new AudioContext();

  const data = await ac.decodeAudioData(readFileSync(file));

  console.log("data", data);
}

main(...scriptArgs.slice(1));
