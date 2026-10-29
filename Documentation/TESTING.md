# Testing

```sh
ctest --test-dir build -C Release --output-on-failure
```

## Suites
**`neuro_core_tests`** (`Tests/CoreTests.cpp`, ~175 checks): pure C++ with no JUCE, running in under a second. It covers:
- slices, transients and the lock-free hand-off (including a thread stress test);
- pitch and key analysis, scales and pads;
- the arpeggiator: timing, every direction, grammar, phrase memory, humanise, robustness;
- the sequencer: timing, polymeter, probability, conditions, weaving, robustness;
- pattern tools: transforms, genome, mutation.

**`neuro_engine_tests`** (`Tests/EngineTests.cpp`, ~165 checks): drives the real `NeuroMPCProcessor` headless and asserts on **rendered audio**. It covers:
- loading, slicing, voices and state;
- time-stretch quality measurements and determinism;
- analysis on a real file; pitch from plugin output (measured with the YIN detector);
- pads and choke; arp, pattern and sequencer output at exact sample positions;
- conditions, weaving and undo;
- frozen parameter IDs; state versioning, fuzzing and relink; presets;
- host sync with a mock playhead;
- CPU load.

## Principles
- **Audible behaviour is tested from audio output.** Test samples use a constant level per slice, so the output level identifies what is playing.
- **Robustness properties:**
  - every generated note-on has exactly one note-off, including across host loops;
  - output is identical across block sizes (37 … 4096);
  - corrupt state never crashes.
- **Determinism:** seeded features are checked for exact repetition, and for different output with another seed.
- **Measurements** (stretch quality, analysis accuracy, CPU) are printed by the tests and recorded in the docs.

## Manual (cannot be automated here)
- **Ableton Live (VST3) and Logic Pro (AU):** discovery, playback, automation record/playback, project reopen, transport, bypass, long sessions.
- **pluginval** for the VST3 (`pluginval --strictness-level 5 --validate "NEURO MPC.vst3"`).
- **Listening tests:** stretch artefacts on real material, and UI usability.
