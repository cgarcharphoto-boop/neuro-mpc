# Step Sequencer (Phase 12)

## Model: `Source/Core/Sequencer.h` (JUCE-free, allocation-free, unit-tested)
- **16 patterns** (names, duplicate, clear). Each has **up to 16 lanes**; lane *n* plays MIDI note 36+*n* (pad *n*) by default, and the note can be changed per lane.
- **Lanes:** each has its own **length (1–64 steps)**, so lanes of different lengths run as polymeters, and **mute**.
- **Per step:**
  - on;
  - velocity;
  - gate (5–100% of the step, divided by ratchets);
  - **probability**;
  - **ratchet** ×1–4;
  - **microtiming** (±50% of a step, kept unquantised);
  - **pitch** (±24 st, added to the pad's tuning and resampled).
- **Global (host parameters):** Mode, Pattern (automatable pattern switching, applied at the next block), Rate (1/4 – 1/32 incl. triplets) and Swing (50–75%, MPC-style). Probability uses the shared Seed.

## Timing
Timing is **stateless**. A hit's time (grid + swing + microtiming + ratchet) and its probability outcome are pure functions of beat position, lane, step and seed. The results:
- **Deterministic:** probability repeats exactly for a given seed and position, and differs between cycles.
- **Block-size independent.**
- **Robust to host loops and seeks.**

Note-offs use the shared `NoteTracker` (sample-scheduled). Each call's hits are emitted in time order, because microtiming can move a step before the previous step's late ratchet.

- **Host transport** mode follows the host PPQ: it plays only while the host plays.
- **Free running** mode starts from bar 1 when switched on and uses the host tempo, or 120 BPM.
- Stopping, or switching to Off, ends every generated note.

## UI (SEQ tab)
- **Grid:** 8 lanes × 16 steps per page, with pages for steps 1–64 and lanes 1–16.
- **Grid editing:** click toggles a step and drag paints; right/ctrl-click selects a step without toggling; the lane header selects the lane and "M" mutes it.
- **Grid display:** brightness shows velocity, dots show ratchets, a corner mark shows probability, a tick shows microtiming, and an outline shows the playhead.
- **Editor rows:** Vel / Gate / Chance / Ratchet / Micro / Pitch for the selected step; Note / Length for the selected lane.

## Tests
`neuro_core_tests` `sequencerTests`:
- **Timing:** four-on-the-floor and an off-beat lane to the sample; gate, velocity and pitch; ratchet ×3 at 12000/14000/16000; microtiming +25% (+1500) and −50% (−3000); swing 66% (7920).
- **Polymeter:** 3 against 4, realigning every 12 steps.
- **Probability and mute:** probability is about 50%, reproducible per seed, and different for another seed; mute silences its lane.
- **Robustness:** with every feature on, output is identical at block sizes 53 and 4096, and on/off stays balanced through a host loop.

`neuro_engine_tests`, through the audio output:
- the hits land at their sample positions with their velocities, including a microtimed hit and a ratchet;
- a +12 st step halves a one-shot's duration (6000 of 12000 samples);
- Off and an empty pattern are silent;
- patterns persist (only non-empty patterns are stored).

## Not yet (Phase 13)
Conditional steps (every Nth, first, fill, previous-step), pattern transformations with undo, Pattern Genome, Pattern Mutation, Phrase Weaving, pattern chaining, and parameter locks.
