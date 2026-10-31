# Kit Intelligence: Timbre-Addressed Sequencing, Loop Deconstructor, per-class outputs, Chord-Lock

## Sound fingerprints and drum classes (`Source/Core/Timbre.h`)
Every slice is analysed in the background. The fingerprint has two parts:
- **Spectral** (first 93 ms): brightness, noisiness (flatness), roll-off, energy below 150 Hz / 150 Hz–2 kHz / above 5 kHz, and the dominant 35–400 Hz peak.
- **Envelope** (up to 2 s): attack, decay to −20 dB, and whether the sound is sustained.

This gives:
- **Timbre map position:** x = dark → bright, y = tonal → noisy (the KIT tab map).
- **Drum class:** Kick, Snare, Closed Hat, Open Hat, Tom 1, Tom 2, Floor Tom, Crash, Ride, Perc or Tonal. It is a rule-based classifier, not a trained model. It classifies all 11 synthetic test drums correctly; real kits vary, so every slice has a **class override** (KIT ▸ Class).

## Per-class outputs (mixing)
The plugin has **12 stereo outputs**: Main plus one per class. Each class plays on its own output when the host enables it (Logic: *Multi Output* instrument; Live: route other tracks' input to NEURO's outputs), and otherwise on Main. There is an automatable **level per class** (−60 dB = mute).

## 1. Timbre-Addressed Sequencing
A sequencer step can target:
- the **lane's pad** (default);
- an **exact slice**;
- a **timbre point**, resolved live to the nearest slice of the current kit, and only within the lane's class if one is set.

**KIT ▸ Step → slice / timbre / pad** sets the selected SEQ step; **Lane class** sets the lane's class. **Retarget to timbre** converts every exact-slice step into a timbre point, so the pattern **re-orchestrates itself on any kit**. The Genome gains a **timbre gene**: Mutation moves steps through sound space, and Take genes copies sound targets.

## 2. Loop Deconstructor (KIT ▸ Deconstruct loop)
1. Slices the loop at its hits (sensitivity 0.7 by default; 0.5 missed a snare on a decaying kick tail in testing).
2. Classifies every hit.
3. Writes the selected pattern with **one lane per kit piece**, in class order, named and class-filtered.
4. Each step keeps its exact slice, its velocity relative to the loudest hit, and its microtiming, so the groove is kept. The loop length is the nearest power-of-two number of beats at the host tempo, in 1/16 steps (max 64).

Combined with the per-class outputs, each kit piece arrives on its own mixer track.

## 3. Chord-Lock (KIT ▸ Chord-Lock)
- **What it does:** a held chord retunes **tonal slices** (known root, not a drum) to the nearest chord tone, including fine detune. Drums are never retuned.
- **Chord input:** *non-pad notes* (anything that isn't a hardware pad) or *below split*. Chord notes are consumed (silent) and **latched**: a new chord replaces the old one when all keys were up.
- **Resample mode:** instant; pitch changes length.
- **Keep length mode:** variants −6…+5 st are pre-rendered in the background per tonal slice and played at the original length; the shift is resampled while a variant is pending.

## Measured (tests)
- **Synthetic kit:** 11 of 11 classes correct. A kick/snare/hat/kick file reads as **Kick / Snare / Closed Hat / Kick**, with hats brighter than kicks on the map.
- **Routing:** a kick plays only on the Kick output (main silent); level −60 dB mutes; a disabled output falls back to Main.
- **Deconstructor:** 4 hits → 8-step pattern with Kick (steps 1, 7), Snare (3) and Closed Hat (5). Each plays on its own output, and hit timing matches the source to the sample.
- **Re-orchestration:** after *Retarget to timbre* and loading a different kit (hat/snare/kick/hat), the kick, snare and hat steps play the matching sounds of the new kit on the right outputs.
- **Chord-Lock** (A3 tone, chord C–E–G): Resample → G3 (55.00) with length ×2^(2/12); Keep length → G3 (55.03) at the original 96000 samples. A chord containing A leaves it unchanged; a slice classed as a drum is never retuned.

## Limitations
- The classifier is heuristic and tuned on synthetic drums; expect overrides on real kits, e.g. claps/rims vs snares, or toms vs tonal bass hits.
- Loop length assumes a power-of-two number of beats at the host tempo.
- Chord-Lock keep-length renders cost CPU and memory (11 per tonal slice).
