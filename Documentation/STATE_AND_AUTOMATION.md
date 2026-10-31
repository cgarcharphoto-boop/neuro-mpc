# State, Automation and Presets (Phase 14)

## Host parameters (63, frozen)
Every automatable control is a JUCE `AudioProcessorValueTreeState` parameter with a **stable ID**, version hint 1. The full list is frozen in `neuro_engine_tests`, which fails if any ID is renamed, removed or duplicated, because that would break automation in saved projects. New parameters must be appended to that list.

| Group | IDs |
|---|---|
| Voice | gain, attack, release, playMode |
| Pitch | transpose, fineTune, bendRange, lfoDepth, lfoRate, lfoShape, lfoSync, lfoPhase, keyMode, scale, scaleRoot |
| Arpeggiator | arpOn, arpLatch, arpDirection, arpRate, arpGate, arpOctaves, arpSwing, arpProbability, arpRepeats, arpVelocity, arpRetrigger, arpSeed |
| Generative arp | arpAccent, arpPhrase, arpVariation, arpVaryPitch, arpVaryVelocity, arpVaryRhythm, arpReturn, arpHumanTime, arpHumanVelocity |
| Sequencer | seqMode, seqPattern, seqRate, seqSwing, seqFill |
| Weaving | weaveAccent, weaveAccentAmount, weaveFill, weaveFillEvery, weavePitch |
| Pads / MIDI | padBank, padBankFollow, padChannel |
| Class levels | levelKick, levelSnare, levelClosedHat, levelOpenHat, levelTom1, levelTom2, levelFloorTom, levelCrash, levelRide, levelPerc, levelTonal |
| Chord-Lock | chordLock, chordInput, chordSplit |

Model data that isn't a single value is saved with the project, not as parameters: slices, pads, the arp rhythm and grammar, sequencer patterns, the keyboard slice, the sample reference, MIDI mappings and the hardware pad layout.

## State format and versions
`NEURO_MPC` XML (`createStateTree` / `applyStateTree`), currently **version 2**.
- **Version 1** (the Milestone 1 format) loads through `migrate()`. Every later field is optional with a default.
- **Newer versions** load best-effort: unknown data is ignored and `wasLoadedFromNewerVersion()` is set.
- **Invalid input** never crashes and never changes the current state: random bytes, truncated data, null, or a foreign root. This is tested with 200 random inputs.
- **Thread safety:** some hosts save state off the message thread, so the editable model is guarded by a mutex. The audio thread never takes it; it only reads lock-free published snapshots.

## Missing samples
- A missing file is reported ("Missing sample: …") and never substituted.
- Its **slice layout is kept**, even if the project is saved again while the file is missing.
- **Relink…**, shown only when a sample is missing, accepts the file itself or a **folder to search recursively** by file name. Loading a file with the same name through Load is also treated as a relink. The saved slices are restored when they fit the file's length.

## Presets
- **Location:** `~/Library/Audio/Presets/NEURO MPC/NEURO MPC/*.neurompc`, using the same XML as project state.
- **Content:** a preset stores a *reference* to its sample, not the audio.
- **UI:** the preset menu and Save are in the top bar. Names are made filesystem-safe.

## Host sync (tested with a mock host playhead)
- **Sequencer (Host mode):** silent while the host is stopped; locks to PPQ (quarter notes at 0 / 24000 / 48000 samples at 120 BPM); follows tempo (60 BPM doubles the spacing); keeps playing through a 2-beat host loop; releases everything when the transport stops (no stuck notes).
- **Arpeggiator:** the same PPQ model, plus free-running and the sample-scheduled note-offs covered in Phase 10.
- **Pitch LFO sync:** locks to PPQ while the host plays.

## Not verified here
Behaviour in **Ableton Live and Logic Pro** themselves: automation recording, project reopen and transport. That needs manual testing in the DAWs (see `Documentation/ACCEPTANCE.md`).
