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

# Advanced arpeggiation (Phase 11)

Each grid step is **decided once**, cached by grid index, in this order: rhythm pattern → Phrase Memory → Grammar rules → notes. Ratchets and humanised timing therefore stay consistent across block boundaries and host loops. Every result is deterministic for a given seed.

## Rhythm pattern (GEN tab, saved with the project)
- **Steps:** up to 16, each with **on/rest**, **accent** (velocity × (1 + Accent %)) and a **ratchet** of ×1–4 (evenly spaced hits within the step, gate divided between them).
- **Rests:** a pattern rest skips time but *not* a note, so the note order continues after it. A probability skip or a rule rest uses up its note.
- **Euclid:** spreads `pulses` evenly over the pattern length, then rotates (3 over 8 = `x..x..x.`).

## Arpeggio Grammar (inspectable, editable rules; up to 8, applied in order)
- **When:**
  - step N of every M;
  - steps from N of every M;
  - the last N steps of the phrase;
  - N% chance (seeded).
- **Do:**
  - transpose ± semitones;
  - rest;
  - accent;
  - ratchet ×N;
  - **passing tone** ± scale degrees, using the Keys-mode scale (chromatic = semitones).
- **Display:** each rule is listed in plain language, e.g. "On step 2 of every 2: transpose +12 st".
- **Presets** load ordinary rules you can then edit: Octave climb, Passing tones, Phrase-end fill, Accents 3-3-2, Stutter & drop, Call & answer.

## Phrase Memory
- **Recording:** with Phrase length N > 0, the first N steps (note choice, rests, accents, ratchets) are recorded.
- **Replay:** later cycles replay the recording. Each step's **pitch / velocity / rhythm** varies with probability *Variation*, chosen per attribute and seeded per cycle, so a variation is reproducible.
- **Return every C** replays the original exactly on every Cth cycle. Pitch is stored as a pattern index, so a recorded motif follows a new chord.
- **What this allows:**
  - repeat exact pitches while varying only velocity;
  - keep the rhythm while changing note order;
  - keep a motif and vary its ending (combine with a phrase-end rule);
  - alternate between the original and a variation (Return every 2).

## Humanise
- **Timing:** each step is delayed by 0…*ms* (deterministic per step). Steps are only ever delayed, never moved earlier.
- **Velocity:** ±%.

## Gesture-to-Arpeggio (ARP tab, optional)
The XY pad sets host parameters, so gestures can be automated and recorded:
- X → density (Chance 25–100%);
- Y → octave range (1–4);
- drag speed → Variation (it decays when the drag is slow).

The arpeggiator works the same without the pad.

## Tests
`neuro_core_tests` `arpGenerativeTests`:
- **Pattern and ratchets:** rest timing without skipping a note, accent velocity, ratchet times and gate, Euclid patterns.
- **Grammar:** each rule type (octave jump, scale passing tone, phrase-end ratchet, seeded chance), plus rule text and disabling a rule.
- **Phrase Memory:** an exact repeat of a random phrase; pitch-only variation keeping the rhythm; Return every 2 restoring the original; rhythm variation changing timing.
- **Humanise:** bounded and deterministic.
- **With every feature on at once:** balanced on/off, a 2-beat host loop, and identical output at block sizes 61 and 2048.

`neuro_engine_tests`, through the audio output: the ratchet hits, a pattern rest and a grammar transpose (+1 → the next pad) are all audible at the expected samples. The program persists.

## Not yet
- MIDI output of generated notes;
- per-voice humanised pitch;
- user-saved grammar presets;
- a dedicated Pattern-defined direction (patterns currently shape rhythm, not order).
