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

# Advanced sequencing (Phase 13)

## Trig conditions (per step; stateless, evaluated from the grid position)
- **Always.**
- **A:B:** plays on lane cycle A of every B (1:2 … 4:4).
- **First / not first:** the first lane cycle since the start.
- **Fill / not fill:** follows the **Fill** switch, an automatable parameter, and is also true on woven fill bars.
- **If previous / if not previous:** the outcome of the lane's previous on-step, including its own condition and probability, evaluated recursively with a depth limit of 16.

## Transformations (EVOLVE tab; selected lane or all lanes; each is one undo step)
- **Reverse:** microtiming is mirrored too.
- **Shift ◀ / ▶:** wraps within the lane.
- **×2:** repeat the lane. **÷2:** keep the first half.
- **Expand:** step i → 2i. **Contract:** the inverse of Expand.
- **Velocity curve:** ramp up, ramp down, downbeats, offbeats or flat, with depth.
- **Probability profile:** downbeats 100%, everything else the set %.

## Pattern Genome (`Source/Core/PatternTools.h`)
- **Genes:** a lane becomes explicit genes. **Rhythm** is the event positions plus length. **Velocity, pitch, groove (microtiming), chance, ratchet and gate** are *contours in event order*.
- **Expression:** expressing a genome onto a rhythm with a different event count cycles the contours, so for example an accent contour keeps its order when density changes.
- **Readout:** the EVOLVE tab shows the selected lane's genome (rhythm string, event count, velocity and pitch contours).
- **Take genes from pattern N:** recombines lane by lane. The ticked genes come from the donor; everything else is kept exactly. For example, keep the groove and change the pitch, or keep the pitches and take another pattern's rhythm.

## Pattern Mutation
Ticked genes mutate and unticked genes are preserved exactly. The settings:
- **Intensity:** the chance per step, per gene.
- **Preserve %:** steps that are never touched, chosen by seed.
- **Density bias:** −100 thins out, +100 fills in.
- **Pitch ± / Velocity ± / Time ±:** the ranges.
- **Seed.**

Mutation is deterministic for a given seed. Each click advances the seed, so you can regenerate variations without losing anything. **A/B** swaps between the pattern before and after the last mutation, and Undo/Redo also covers it.

## Phrase Weaving (WEAVE row; all are host parameters)
On top of the playing pattern, lane n of a source affects lane n:
- **Accents from:** a pattern whose on-steps add *Amount* to the matching base hit's velocity.
- **Fills from:** a pattern that replaces the base on the last bar of every *Every* bars (4/4).
- **Pitch from:** a pattern whose on-steps add their pitch to the matching base hit.

## Undo / Redo
Per pattern, 64 levels. Edits less than 0.4 s apart, such as a drag-paint or a slider drag, coalesce into one step. Transformations, mutation and recombination are always separate steps.

## Tests
`neuro_core_tests` `advancedSequencingTests`:
- **Conditions:** 1:2 / 2:2 / first / not first on exact cycles; fill on and off; previous and not previous exactly follow a 50% step over 100 cycles, with no orphans.
- **Weaving:** fill bars, the accent amount and pitch addition.
- **Transformations:** every one, including expand∘contract = identity.
- **Genome:** a lossless round trip, and recombination keeping contour order.
- **Mutation:** only ticked genes change, the same seed reproduces, another seed differs, preserve = 100% leaves the lane untouched, and the density bias works in both directions.

`neuro_engine_tests`, through the audio output:
- woven fill bars sound on bars 2 and 4;
- a 1:2 step sounds on bars 1 and 3 only;
- undo and redo work around a transformation;
- conditions and weave settings persist.

## Fixed in this phase
Signalsmith Stretch seeded its phase randomisation (used above ×2) from `std::random_device`, so renders were not reproducible and a quality test was flaky. Renders are now seeded from their region and verified bit-identical.

## Not yet
- pattern chaining / song mode;
- parameter locks per step;
- per-step slice selection, independent of the lane note;
- user-defined A:B values beyond the 1:2–4:4 menu (the engine accepts any).
