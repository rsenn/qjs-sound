# Tonnetz Table: prompts

## Original first prompt (verbatim)

> make me an audio-visual touch-controlled instrument like reactable (but not with fidicual markers) or psychosynth (opensource) .... we'll have circular modules which send audio wave to another one ... around the circular modules you can touch and drag to modulate a parameter.... and we'll have a tonnetz to generate MIDI notes for the oscillator... (will be polyphonic)

That alone does not reproduce the current app, because the design changed over many follow-ups. The consolidated prompt below describes where it ended up.

## Consolidated prompt

```
Build a single-page, installable, offline-capable PWA (plain HTML + ES modules,
Canvas2D + WebAudio, no libraries) called "Tonnetz Table": an audio-visual
touch instrument in the spirit of the Reactable (but without fiducial markers).

LAYOUT
- Top area ("table"): circular draggable modules. Bottom area: a tonnetz.
- A thin status strip between them shows the chord name, key/mode and the
  suggested next chords.

MODULES (circles with concentric parameter rings)
- Each ring around a module is one parameter. The knob follows the finger's
  absolute angle on a 270-degree arc (gap at the bottom; never wrap max->min).
  While dragging, show a value pill above the module. Drag the centre to move,
  double-tap to remove.
- Patching is explicit: tap a module, then tap the module (or OUT) it should
  feed. Cables show the live waveform and have an "x" handle to disconnect.
  No proximity patching.
- Types: VCO (saw<->square blend, glide, level), VCF (two cascaded lowpass
  biquads + tanh shaper, cutoff, resonance, env mod, decay; envelope via a
  ConstantSource into both filters' detune), PIANO (polyphonic distorted 2-op
  FM electric piano: 1:1 sine carrier, triangle modulator, modulation index that
  decays after the strike; 12-voice cap), DIST (asymmetric tanh curve + tone
  lowpass + drive), PAD, DELAY (feedback delay with lowpassed loop), REVERB
  (convolver with generated decaying-noise IR).
- Default patch: VCO -> VCF -> DIST -> DELAY -> OUT, PIANO -> DIST.
  The VCO+VCF must sound like a 303 acid line (slides, accents, resonant
  filter envelope).
- OUT: master gain -> compressor, a radial spectrum ring, and a peak/hold level
  meter in dB.

TONNETZ (3 rows of triangles, node(i,j) = pitch class 7i+4j mod 12)
- Touch a node (1 note), the line between two nodes (2 notes) or a triangle
  (major/minor triad); slide to change. Each selection is voiced on the piano.
- Generate a scale on the fly from the selection: score all 12 major-key
  scales by how many selected notes (plus decayed history) they contain, with
  a prior on plausible roots; the chosen scale's degrees drive the arpeggio.

INVISIBLE PROGRESSION ENGINE (no visible sequencer grid)
- Releasing a touch latches the chord as "home" and restarts the walk.
- Every N bars (1/2/4, default 2) pick the next triad: score all 24 triads by
  voice-leading cost (best permutation, circular semitone distance),
  diatonic fit, preference for returning to recent chords, phrase position,
  minus a penalty for the last 3 chords. Weighted random among the top 4.
  Every 4th chord deterministically cadences home (V/IV before it).
- Mark the top 3 candidate triads on the lattice with dashed outlines (all
  on-screen copies), best one pulsing; a back-arrow marks "return" candidates.
- Per chord generate a 16-step pattern with a seeded RNG from a style pool
  (acid, up, updown, pedal, skip, rolling, octave; per-phrase-position pools
  in "auto"). Steps have rest/accent/slide flags. Play the pattern on the VCO
  (slide = glide with tied gate, accent = louder + shorter filter decay) and
  strike the chord on the piano on the bar, with a softer re-hit on step 10.
- Scheduler: setInterval 25 ms, 100 ms lookahead against ctx.currentTime.
- Controls: Pause (also Space), Style, chord length, BPM, octave.

PAD (dreamy, soft, musical)
- Per chord tone: two triangle oscillators detuned +-2..11 cents and panned
  +-0.5, plus a quiet sine an octave up (keeps every partial harmonic, no
  saws), through a lowpass (250 Hz..2.5 kHz, Q 0.3) modulated by a slow
  0.11 Hz LFO; a sine bass on the root. Slow swell attack (0.2-4 s) and slow
  release. Chord = selected notes + the diatonic 7th, placed by pitch class in
  a fixed octave window so shared notes keep their pitch across chords.

DIRECT MODE
- A "Mode" button switches to unsequenced play: no engine, exactly the touched
  notes sound while a finger is down (piano strike, pad hold, VCO gate).

ARCHITECTURE
- DOM-free ES modules in lib/: theory (pure functions), geometry (lattice
  layout and hit-testing), harmony (state, analysis, engine), synth (rack; takes
  an injected audio context and timers), sequencer, player. The page only does
  drawing and pointer input, so the modules also run under QuickJS (qjsm).

PERFORMANCE / PWA
- Must not crackle on a phone: cache the static lattice on an offscreen canvas,
  throttle drawing to ~30 fps, use latencyHint 'playback', cap voices.
- Service worker with stale-while-revalidate, manifest, icons, "new version"
  banner; touch-action:none, multitouch pointer events.
```

For the acoustic character of the FM piano, the design was informed by girlinbluemusic.com/interactive-tonnetz, but none of its GPL code was copied. Implement from the description rather than copying from it.
