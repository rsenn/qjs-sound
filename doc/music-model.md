# A self-made music model: pretrained GRU, Tonnetz, style transfer

Mostly plan. Built so far: the Morph instrument (`doc/morph-instrument.md`),
the aubio report (`examples/audio-report.js`) and a first spiking-network
player (`examples/snn-player.js`, section 11). It collects the ideas of the Morph
instrument session: a small recurrent network that continues, accompanies
and fills in music, guided by Tonnetz harmony, fine-tuned on paired data
for style transfer (blues to ragtime), running live in QuickJS.

## 1. What the model does

One model, one first token selects the mode:

| Mode         | Given                                 | Writes                         |
|--------------|---------------------------------------|--------------------------------|
| `CONTINUE`   | a cue: a chord, a few notes           | the music that follows         |
| `ACCOMPANY`  | the melody so far (live, step by step)| bass / chords / left hand      |
| `FILL`       | the start and the end of a gap        | the missing middle (inpainting)|
| `STYLE`      | a song's content + a style token      | the same song in that style    |

The mode tokens are trained together from the same MIDI files; see 5.

## 2. Why a GRU

- Proven for symbolic music (Performance RNN, MelodyRNN, chorale models).
- Small: 2 layers of 128 units is below 300,000 weights; it trains in
  minutes to about an hour and answers in well under a millisecond per
  step, so playing live feels immediate.
- Inference is a handful of matrix products per step: about 60 lines of
  JavaScript, weights loaded from a JSON file. No ONNX, no runtime.
- A spiking network has no quality advantage on a normal CPU and little
  tooling for symbolic music; it stays a later experiment (a generative
  modulator for CC lanes, not the composer).

## 3. Representation

Event tokens, MIDI-like (after Performance RNN, 388 tokens):

- `NOTE_ON p` (128), `NOTE_OFF p` (128)
- `TIME_SHIFT t` (100 steps of 10 ms; longer gaps are chained)
- `VELOCITY v` (32 bins)
- `CC n value` for controller curves (the mod wheel lane): a controller
  number token, then a value token in 32 bins
- control tokens: the mode (`CONTINUE`, `ACCOMPANY`, `FILL`, `STYLE`), a
  style (`BLUES`, `RAGTIME`, ...), a chord per bar (`Cm`, `G7`, ...), the
  scale (`PENTA_MIN`, ...), `BAR`

The chord tokens come from the Tonnetz code (section 4), so every training
file is labeled automatically.

## 4. Tonnetz as the harmonic layer (tonnetz/lib)

The library already has triad geometry (`triNodes`, `edgeSel`), the
voice-leading cost (`vlCost`, `circ`), chord naming (`cid`, `cname`) and
`generateSong`.

- Composition from scratch: the Tonnetz walks a path through triads (the
  neighbours are the P, L and R moves, the cheap voice leadings); a
  parameter sets how adventurous the walk is (neighbours only, or a
  two-step jump). The model writes notes over the current and the next
  chord.
- Safety net: every proposed note is scored against the chord and its
  Tonnetz neighbourhood; out-of-harmony notes only as passing tones off the
  strong beats, others snapped to the nearest chord or scale tone.
- Labels: the chord of each bar in the training files is the nearest triad
  by the same code, so no hand labeling.
- Link to the instrument: the lattice position or chord distance can drive
  the mod wheel (tension and space together).

First step, before any model: a script that walks a Tonnetz path, plays
chords and a pentatonic melody on the Morph port, with the adventurousness
as a CC.

## 5. Training

Framework: PyTorch, offline. Hardware: GeForce RTX 2070 SUPER (Turing,
8 GB VRAM, driver 595.99), 8 CPU threads, 31 GB RAM. PyTorch, pretty_midi
and mido are not installed yet (nothing under python3 has them).

What 8 GB allows:

- 2 x 128 GRU: trains on a CPU too; on the GPU in minutes.
- Up to a 3 x 512 GRU (about 4 million weights) or a small transformer of
  20 to 50 million parameters with mixed precision (fp16 works on Turing,
  bf16 does not): hours at most on a few hundred hours of music.
- A language model such as qwen3:4b can be fine-tuned only with a 4-bit
  LoRA (QLoRA), tightly; not needed for this plan.
- The GRU stays small for the live reason: inference runs in QuickJS on the
  CPU, so the GPU only matters for training.

- Loss: cross entropy on the next token, teacher forcing.
- Optimizer: AdamW; dropout 0.2 to 0.3 between layers; gradient clipping.
- Sequences: windows of 256 to 512 tokens, truncated backpropagation with
  the hidden state carried between windows of one piece.
- Augmentation: transposition (plus or minus 5 semitones), small tempo
  scaling, velocity jitter.
- Evaluation: held-out pieces; the share of notes in the scale; pitch
  class histogram against the data; repetition rate; the fraction of
  phrases that fit the bar; and listening.

### Pretraining, then fine-tuning

1. Pretrain on a large general piano corpus: the model learns what music
   looks like (phrases, rhythm, voice leading).
2. Fine-tune on the style data (ragtime, your own material) with a small
   learning rate. A few dozen pieces are enough when the model is
   pretrained; far more are needed from scratch.

Candidate data, licenses to be checked before use:

| Set                 | Content                                | Notes                         |
|---------------------|----------------------------------------|-------------------------------|
| MAESTRO             | piano performances, MIDI with velocity | large; non-commercial license |
| Lakh MIDI           | very many MIDI files, mixed quality    | needs filtering               |
| Nottingham          | folk tunes with chords                 | small, clean, good first test |
| JSB chorales        | four-part harmony                      | small, clean                  |
| Mutopia, IMSLP-derived | public-domain scores, ragtime (Joplin) | check each file            |
| your own            | glassloom `.mid` exports, Ardour, takes| the most personal             |

### Training the three modes from one corpus

- `CONTINUE`: prefix of a piece, then the rest.
- `ACCOMPANY`: melody and accompaniment as interleaved streams (piano
  pieces split by register, multi-track files, glassloom tracks).
- `FILL`: each example rearranged as `[start] [end] FILL [missing
  middle]`, with the span cut at random. Costs no extra data.

## 6. Style transfer (blues to ragtime), paired data

Content stays (melody skeleton, chords, form), style changes. Steps:

1. Rules first, no model: swing to straight timing, a stride left hand
   (bass on 1 and 3, chord on 2 and 4) generated from the chord track,
   quantization. Already recognizable.
2. A style token: the model regenerates the right hand and the decoration
   from the extracted skeleton, conditioned on `RAGTIME`.
3. `FILL` to rewrite only parts (keep the main melody notes, fill the
   syncopated notes between).

### The paired data idea

A person on Instagram plays ragtime versions of well-known songs. Paired
audio is possible; the notes must be transcribed.

- Rights first: her arrangements and the songs are copyrighted. Ask her; a
  collaboration (she sends clean audio, sees what the tool does with her
  style) is better than collecting videos. Bulk downloading may breach the
  platform terms. Private experiments differ from publishing a model or
  its output.
- Transcription: use a dedicated piano transcription model (Onsets and
  Frames, a high-resolution piano transcription model, or Basic Pitch as a
  lighter option); ragtime is solo piano, their best case. A general
  audio-language model such as Gemini is not a reliable note transcriber:
  use it for what it is good at (naming the song, sections, key, style
  labels, a plausibility check).
- Quality gate: render the transcription back and compare pitch and
  onsets with the audio using aubio (`examples/audio-report.js`); drop bad
  transcriptions.
- The original side: only its skeleton is needed (melody and chords), from
  a lead sheet or a simple transcription of the original melody.
- Alignment by bars: dynamic time warping on the transcribed pitch.
- Amount: 30 to 100 pairs to fine-tune a pretrained model.

Expectation: not an exact re-arrangement, a plausible piece in the target
style built on the song's bones; the hook and the chord changes survive
best.

## 7. Running it (QuickJS)

- Export: weights as JSON (one array per matrix), plus the token table.
- Inference: the GRU step (update gate, reset gate, candidate state) in
  plain JavaScript on Float32Array; sample with a temperature, top-k.
- Live: notes in from the MIDI port (or glassloom), tokens out as MIDI
  events on the Morph port, CC tokens as controller messages.
- A CC (for example the mod wheel) can set the temperature: calm and
  repetitive at the bottom, wild at the top.
- Glassloom side: "copy as text" and "paste from text" buttons on a track
  or lane, and the `.mid` import and export it already has, for the round
  trip through Ardour or this chat.

## 8. Order of work

1. Ask for permission and the audio files (the paired data); start a
   first transcription batch of 5 songs.
2. Text to `.mid` and `.mid` to text converter (command line); the text
   format for pasting patterns into a chat.
3. Tonnetz skeleton script (no model) on the Morph port.
4. Training script (PyTorch) with the tokenizer, pretrain on a small
   clean set (Nottingham or JSB), check the three modes.
5. JavaScript inference and live playback.
6. Pretrain on a large piano set, fine-tune on ragtime and own data,
   style transfer.
7. Optional: a spiking-network modulator for CC lanes.

## 9. The language-model branch: fine-tuning qwen3:4b

A text model reads and writes music as text, so it follows instructions in
words, which the GRU cannot ("turn this into ragtime in C minor, keep the
hook"; "write an 8-bar answer ending on the dominant"; "less busy").

- Text encoding: ABC notation (the base model already knows some) or the
  line-per-event text format used to paste patterns between glassloom and a
  chat (bar.beat note velocity length; CC lines for lanes).
- Method: QLoRA (4-bit base, small adapter layers). On 8 GB it is tight
  but possible for a 4B model with sequences of about 1000 to 2000 tokens
  and gradient checkpointing; hours of training for a few thousand
  examples, not days. Not measured here.
- Precedent: published work such as ChatMusician (a 7B model tuned on ABC
  notation) shows that language models learn symbolic music this way and
  keep following instructions.
- Data: first plain music in text (folk tunes, chorales) so the model gets
  musical sense; then instruction pairs, such as "make it ragtime" with
  the original skeleton as input and the transcribed ragtime version as the
  answer (the paired data of section 6). 30 to 100 pairs is a start for the
  adapter when the base model already understands ABC.
- Test before any training: ten phrases from the plain `qwen3:4b-instruct`
  in a fixed format, scored for the share of notes in the scale and for
  bar length; code snaps and quantizes what the model writes.

### Cost against the GRU

| Aspect        | GRU                          | qwen3:4b (QLoRA)                  |
|---------------|------------------------------|-----------------------------------|
| Latency       | well under 1 ms per step     | seconds per phrase (tens of tokens per second) |
| Live playing  | yes, note by note            | no, composes ahead                |
| Instructions  | tokens and modes only        | free text                         |
| Form          | local phrases                | holds AABA, 12 bar blues better   |
| Training      | minutes to an hour           | hours                             |
| Runs in       | QuickJS on the CPU           | the GPU (4-bit, ollama or similar)|

### Combined design

- The language model is the composer or director: it takes instructions,
  plans form and chords, writes whole phrases ahead of time.
- The GRU is the live player: it accompanies and improvises note by note.
- The Tonnetz code and quantization keep everything in key and in time.
- One text format and one converter (`.mid` and text) feed both.

Order: the GRU path first (a working live instrument fastest); the
language-model branch after the converter and the paired data exist,
since the same data serves both.

## 10. The architecture: no model makes sound, an LLM conducts

Rule: no model synthesizes audio. Sound comes only from the physical-model
and spectral instruments (STK, SndObj, glassloom, the Morph instrument).
Generative networks (GRU, later SNN) output MIDI only. A language model
does not play: it conducts and listens.

```
          instructions in words
                  |
                  v
     +-----------------------------+
     |  LLM conductor              |<----------------------------+
     |  (qwen3:4b, or Claude       |   text reports: aubio       |
     |   while developing)         |   (level, pitch, onsets),   |
     +-----------------------------+   brightness, key, form     |
        |        |         |                                     |
        | mode,  | form,   | CC curves,                          |
        | style, | chords  | modulation                          |
        | temp.  | (Tonnetz)|                                    |
        v        v         v                                     |
   +---------+  +---------+  +---------+                         |
   |  GRU    |  | Tonnetz |  |  SNN    |   MIDI notes + CC       |
   | player  |  | harmony |  | modulat.|-----------+             |
   +---------+  +---------+  +---------+           |             |
        |                                          v             |
        +--------------------------> Morph instrument (MIDI in)  |
                                    STK / SndObj / glassloom     |
                                          |                      |
                                          v                      |
                                       audio ---> analysis ------+
                                    (recorded wav: aubio, FFT)
```

### Roles

| Part            | Does                                                   | Never does          |
|-----------------|--------------------------------------------------------|---------------------|
| LLM conductor   | reads instructions and analysis, sets parameters, plans form, decides what to change | write audio, play notes in real time |
| GRU player      | notes and CC tokens, live, low latency                 | follow words        |
| Tonnetz code    | chord path, in-key constraint, voice-leading cost      | taste               |
| SNN modulator   | rhythmic bursts, CC curves (wheel, morph, smear) from spike dynamics | notes, harmony |
| Instrument      | all sound, driven only by MIDI (notes, CC, program change) | decide anything |
| Analysis tools  | aubio, FFT brightness, level; report as text           | judge taste         |

### Interfaces (all text or MIDI)

- Conductor to players: a small control vocabulary, for example `mode`,
  `style`, `scale`, `temperature`, `density`, `chord path adventurousness`,
  plus CC lane targets; sent as a JSON line or a MIDI CC the players read.
- Players to instrument: standard MIDI on the Morph port (notes, CC,
  program change for the two voices). Nothing else.
- Instrument to conductor: a recording analysed by `audio-report.js` (and
  more columns later: brightness, key, form), returned as text.

### The loop

1. Conductor reads the request ("a spooky ragtime in C minor").
2. Sets players: style, scale, chord path, temperature; picks the voices
   (program change) and the wheel curves.
3. The players play; the instrument sounds; a recording is analysed.
4. The report comes back as text ("level rises 13 dB at 4 min, no
   confident pitch after 3 min, mostly bass"); the conductor compares it
   with the request and adjusts (lower the temperature, change the chord
   path, pull the wheel gain down).
5. Repeat until it fits, or until the user steers by ear.

### What exists today, what is missing

| Exists                                                         | Missing                         |
|----------------------------------------------------------------|---------------------------------|
| Morph instrument with CC, program change, MIDI port           | GRU player, SNN modulator       |
| aubio bindings, audio-report.js, recording and replay of MIDI logs | brightness / key / form columns |
| Tonnetz library (chords, voice leading)                        | Tonnetz path generator script   |
| glassloom as playground, lanes, `.mid` import and export       | text format and converter, control vocabulary |
| Claude (this session) can already act as conductor: send MIDI, read reports | a local LLM conductor (qwen3:4b) with the same tools |

Development shortcut: the conductor role can be tested now with this
session's model (a script that plays a MIDI pattern, records, and returns
the report), then the same tools are handed to a local qwen3:4b.

### Why it is distinct

- The sound is always a physically or spectrally modelled instrument:
  no neural audio, so it keeps the character, the control (every parameter
  a CC) and the CPU cost of the existing instrument.
- The networks work in the symbolic domain, where small models do well
  and where the output can be corrected by rules and by theory (Tonnetz).
- The language model is used for what it is good at (intent, planning,
  explaining) and gets measured feedback, not feelings, about what came
  out.
- A spiking network as a modulator is a natural place for it: control
  signals with timing and bursts, instead of notes.

## 11. Spiking network as a direct MIDI player (feasibility)

Idea: a real-time spiking network (SNN) whose spikes are MIDI events: a
spike is a note-on, the neuron's state at the spike gives the velocity.
It drums or improvises melodies, with no tokens and no sequence model.

### Mapping

| Network side                               | MIDI side                          |
|--------------------------------------------|------------------------------------|
| one neuron (or group) per drum voice or scale degree | note number                |
| spike                                      | note-on                            |
| membrane potential at the spike, or spikes in a burst | velocity                 |
| spike rate over a window                   | density; a CC (for the wheel)      |
| refractory period                          | minimum gap, note length           |
| a silent interval after a burst            | note-off                           |

### Cost: no obstacle

A leaky integrate-and-fire (LIF) or Izhikevich neuron is a few
multiply-adds per step. A network of 100 to 10,000 neurons stepped every
millisecond is far below one CPU core in QuickJS or C. The instrument
already renders in 5.8 ms blocks; the network can be stepped in 1 ms
sub-steps per block, which is finer than musical timing needs. No
neuromorphic hardware and no GPU are required to run it.

### What is easy, medium, hard

| Task                                         | Feasibility | How |
|----------------------------------------------|-------------|-----|
| Drum patterns, polyrhythms, rolls            | high        | coupled oscillators / central pattern generators; bursting neurons for rolls; one neuron per drum voice with mutual inhibition so hits interleave |
| Steady groove locked to a tempo              | high        | an oscillator population entrained to MIDI clock (or to the player's notes) |
| Humanization, ghost notes, accents           | high        | noise and membrane potential give variation and velocity for free |
| Melodic improvisation that sounds coherent   | medium      | winner-take-all over the scale degrees (Tonnetz-constrained set), lateral coupling that favors small intervals, a slow chord-tone rhythm |
| Responding to what you play                  | medium      | your notes inject current into matching neurons; the network answers |
| Learning a style from data                   | hard        | surrogate-gradient training (snnTorch) on MIDI, or STDP plasticity; research-grade results are mixed |
| Long-range form (a theme returns)            | hard        | needs the LLM conductor or the GRU on top; the SNN stays the local player |

### Where it fits the architecture

The SNN is a MIDI player beside the GRU (section 10), not a replacement:
the GRU plays learned phrases; the SNN supplies motion, grooves and
reactive improvisation. The LLM conductor steers it through a few real
parameters: coupling strength, noise, thresholds, the pitch set, the
oscillator frequency. Each is naturally a CC, so the same lanes in glassloom
can steer it by hand.

### Plan, cheapest evidence first

1. Prototype without training: a LIF drum network in QuickJS (kick, snare,
   hat, mutual inhibition, an entrained tempo oscillator), MIDI out on a
   port. Judge by ear and with the analysis tools: regularity of the
   inter-onset intervals, density, accent profile.
2. A melodic version: scale-degree neurons with winner-take-all and
   interval coupling, constrained by the Tonnetz chord path; input from
   incoming MIDI notes.
3. Parameters as CCs; glassloom lanes drive coupling and noise.
4. Only then: plasticity (STDP) or surrogate-gradient training on drum and
   melody MIDI; weights exported as JSON like the GRU's.

### Risks

- Musical coherence is the real question, not speed: a free network is
  chaotic; tempo locking and a pitch constraint are needed from the start.
- Tuning by hand is slow; the loop of section 10 (play, analyse, adjust)
  and an evolutionary search over a handful of parameters can do it.
- Results published for trained SNNs on music are less mature than for
  GRUs; expect an experiment, not a guarantee.

### Prototype status: examples/snn-player.js

Built: leaky integrate-and-fire neurons stepped every millisecond; each
neuron has a note and a 16-step weight template; the drive is active in the
second half of a 16th, so the spike falls just before the grid line and the
note is queued for that line (quantized by construction); one note per
neuron per step; spike inhibits the others (strongly in melody mode,
weakly in drums); refractory time of 0.9 step; self-adaptation so a note
does not repeat; velocity from the potential at the spike plus the step
weight. Drums: kick 36, snare 38, closed hat 42, open hat 46. Melody:
scale degrees over two octaves from `--root`, from `--scale` (minor-pent,
major-pent, minor, major, dorian, blues), stepwise coupling to the last
note. `--dry BARS` prints statistics without MIDI; otherwise events go to
the MIDI port matching `--port` (default Morph).

First dry runs (16 bars, seed 1), not yet heard, not yet run live:

| Run                      | Events/bar | Result |
|--------------------------|-----------:|--------|
| drums, 120 bpm           | 5.6        | fires almost only on steps 0, 4, 8, 12; hats almost never on the off-beats |
| melody, minor-pent       | 15.1       | almost only even steps; 76 percent stepwise, 20 percent repeats |
| melody, blues, density 0.9 | 23.4     | same pattern; notes stay in the scale |

Findings: scale restriction and the grid work (every note is in the scale,
every event on a 16th). Too sparse and too regular: steps with a low weight
(the off-beat hats, odd 16ths) never reach the threshold; the strongest
steps win every time. To tune: threshold against the drive, a lower
inhibition between hats and kick, more noise, a leaky bias that lets weak
steps fire sometimes. Success test, with the analysis tools: events per
bar, a histogram of the 16 steps, the spread of velocities, the fraction
of stepwise motion, then listening on the vibes and marimba voices.

## 12. Local models for the conductor (ollama)

Installed (`ollama list`): devstral-small-2:24b (15 GB), gpt-oss:20b
(13 GB), gemma4:e4b (6.6 GB), qwen3.5:9b (6.6 GB), qwen3:4b-instruct
(2.5 GB), qwen3-coder:30b (18 GB).

The GPU has 8 GB (RTX 2070 SUPER); the machine has 31 GB RAM. Sizes that
fit the GPU with some room for the context: qwen3:4b-instruct, and,
tightly, gemma4:e4b and qwen3.5:9b. devstral-small-2:24b, gpt-oss:20b and
qwen3-coder:30b exceed the VRAM and would run with a part of the layers in
RAM, slower.

The conductor needs: a strict, parseable output (a JSON line of
parameters), following a small control vocabulary, reading a text report
and adjusting, and a latency of a few seconds, not more. Coding models
are not the best fit by their training. Nothing has been measured yet.

Test to choose (also the "plain qwen3:4b-instruct on ten phrases" test of
section 9), same prompts for every model:

1. Ten pentatonic phrases in a fixed text format: valid format, share of
   notes in the scale, bar length, variety.
2. Ten control tasks: from a request ("spookier, less busy") and an
   analysis report to a JSON of parameters in the allowed ranges: valid
   JSON, values in range, direction of the change correct.
3. Seconds per answer, and whether it fits the GPU alone.

Run all that fit the GPU first (qwen3:4b-instruct, gemma4:e4b, qwen3.5:9b),
then gpt-oss:20b with offload for comparison. The `ollama-repl` skill in
`~/Sources/rsenn/skills` is the way to drive them from scripts.
