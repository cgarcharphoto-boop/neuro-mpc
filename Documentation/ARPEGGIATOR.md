# Arpeggiator (Phase 10)

## Engine: `Source/Core/Arpeggiator.h` (JUCE-free, allocation-free, unit-tested)
- **Input:** held MIDI notes and on-screen pads. In Pads mode it arpeggiates pads; in Keys mode, pitches. Direct slice auditions bypass it.
- **Directions:** Up, Down, Up-Down and Down-Up (turning notes not repeated), As played, Reverse played, Random, Random without repeats, Chord (all notes per step, octave per step), and Octave cycle (each note through its octaves).
- **Rhythm:**
  - Rate 1/1 … 1/32 including 1/4T, 1/8T and 1/16T.
  - Gate 5–100% of the step.
  - Swing 50–75%, MPC-style: the 2nd step of each pair lands at swing × the pair.
  - Repeats 1–4.
  - Probability 0–100%. A skipped step still advances the pattern.
- **Velocity:** as played, or fixed at 1–127. Octaves 1–4.
- **Latch:** keeps playing after release. The next chord replaces the latched notes, and switching latch off with no keys down stops the arp.
- **Retrigger:**
  - Off;
  - First note (default): the pattern restarts when a phrase starts;
  - Every note.
- **Seed:** random modes and probability restart from the seed on every phrase, so a phrase is reproducible.

## Timing
- **Host playing:** each step's beat position comes from the host PPQ, so steps lock to the grid, follow tempo changes, and survive loops and seeks.
- **Host stopped:** the arp free-runs at the host tempo (120 BPM fallback). The first step lands on the sample of the key press that starts the phrase.
- **Note-offs** are scheduled in samples, not beats, so a loop jump can never strand a note.
- **Flushing:** arp off, transport stop and all-notes-off release every generated note. The GUI timer is never used as a clock.
- **Event list:** in `processBlock`, MIDI, UI and arp events go into a fixed-capacity event list. It is sorted with an allocation-free insertion sort (note-offs before note-ons at the same sample) and rendered sample-accurately.
- **Gate mode:** use Gate play mode to hear gate lengths. In One-Shot mode, slices ignore note-offs.

## Tests
`neuro_core_tests` `arpTests`:
- the order of every direction, plus octaves, octave cycle and chord;
- step and gate timing to the sample (6000/3000 at 1/16, 120 BPM, 48 kHz), swing at 66% (step 2 at 7920) and 1/8T;
- seeded random and probability (reproducible, and different for a different seed), no-repeat random, repeats and fixed velocity;
- retrigger and latch;
- every note-on followed by exactly one note-off, including across a host loop; identical output for block sizes of 37, 512 and 4096.

`neuro_engine_tests`, end to end: held pads play pad 1 → 2 → 3 → 1 at the expected sample positions and levels, with silence after each gate. Switching off leaves nothing sounding, and the settings persist.

## Not yet (Phase 11)
Arpeggio Grammar, Phrase Memory, controlled variation/mutation, Gesture-to-Arpeggio, ratchets, humanisation, accents and rests, and MIDI output of generated notes.
