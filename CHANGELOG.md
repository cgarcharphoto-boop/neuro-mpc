# Changelog

## 0.1.0 (2026-10-29): first complete build of the specification (Phases 1–15)
- **Sampler:** WAV/AIFF/FLAC/OGG/MP3 import; waveform with zoom; manual, transient and grid slicing with stable ids; 32-voice engine with one-shot/gate, envelopes, velocity and reverse support.
- **Per-slice time-stretch:** ×0.25–×4 with pitch preserved (Signalsmith Stretch), transient preservation, background rendering, deterministic renders.
- **Analysis:** YIN pitch with a polyphony check; chroma/Krumhansl–Kessler key with honest uncertainty; manual root correction.
- **Chromatic pitch:** ±24 st per slice with a 12-note selector, Resample or Keep-length modes; transpose, fine tune, bend and a pitch LFO with host sync; Keys mode with scale snapping.
- **Pads:** 64 pads in 4 banks, slice-by-id assignment, MIDI learn, layering, choke groups, level/tune/velocity, names and colours.
- **Arpeggiator:**
  - core: 10 directions, triplet rates, swing, probability, latch, retrigger, seeded randomness;
  - generative layer: rhythm pattern with accents and ratchets, Euclid, Arpeggio Grammar rules, Phrase Memory, humanise and a gesture pad.
- **Step sequencer:**
  - core: 16 patterns × 16 lanes, polymeter, per-step velocity/gate/probability/ratchet/microtiming/pitch, host-transport or free-running;
  - advanced: trig conditions, transformations with undo/redo, Pattern Genome, Mutation with A/B compare, and Phrase Weaving.
- **State:** 46 frozen automation parameters, versioned state with migration, fuzz-safe loading, missing-sample relink, presets.
- **Project:** CI workflow, contribution guide, architecture/build/testing docs, acceptance matrix with recorded results.
