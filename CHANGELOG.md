# Changelog

## 1.0.0 (2026-10-31): Public MVP
- **Extract drums (AI):** Demucs (htdemucs fine-tuned drums, MIT) runs in-process via demucs.cpp, with no Python needed. It pulls the drum stem out of a full song, then runs Separate drums on it.
  - **Setup:** the model (84 MB) downloads on first use and is SHA-256 verified.
  - **Behaviour:** the extraction is multi-threaded, shows progress and can be cancelled.
  - **Test** (real drums + bass + chords): drums SDR goes from -14.8 dB (the mix) to **20.3 dB** (the stem).
- **Run in unison with an MPC:** MIDI clock **in** (Start / Continue / Stop / Song Position / tempo, sample-accurate, slewing so steps never repeat) and **out** (24 PPQN + Start/Stop). It works inside MPC 3 software through host sync and next to MPC hardware through the Standalone app.
  - **Tests:** 0 samples of error at 100 BPM, and under 1.5 ms with ±1 ms of jitter.
  - **Controls:** SEQ ▸ Clock in / Clock out.
- **Release:** tagged builds publish an ad-hoc signed zip with licences.
  - **Docs:** install steps in the README, an MPC guide and checklist in `MPC.md`, and the pre-release red team in `RELEASE_REDTEAM.md` (P2s fixed).
- **Limits and fixes:** Separate drums analyses at most the first 60 s, and the AI result is discarded if the sample changed during extraction.

## 0.5.0 (2026-10-31): Teach the kit
- **KIT ▸ Teach kick / snare / hat/cym, Clear taught** (all MIDI mappable): example hits from the loop become Separate drums' drum sounds. The learning step explains away overlapping drums and room sound, and each drum is limited to its own frequency range. The examples are tied to the loaded file.
- **Unusual synthetic kit:** kick F1 0.80 → 0.89. **Real loops with blindly picked examples:** Duncan snare 31% → 100%, Kyle kick 47% → 42%, the rest unchanged (details in `KIT.md`).

## 0.4.0 (2026-10-30): Separate drums (NMF)
- **Separate drums:** semi-adaptive NMF (after Wu & Lerch 2015) splits mixed drum loops into Kick / Snare / Cymbals / Residual stems that sum back to the original. It detects every drum's hits, including simultaneous ones, with sample-accurate timing and kick↔snare crosstalk rejection. A pattern plays the isolated drums on their own outputs.
- **Synthetic mixed loop:** kick and snare 100% / 100%, hats 100% / 88% (precision / recall). Real-loop results and limitations are in `KIT.md`.

## 0.3.1 (2026-10-30): field-tested
- **Loop Deconstructor:** estimates the loop's tempo (file tempo tag, else audio autocorrelation with whole-bar disambiguation; 6/6 on real Apple Drummer loops) and uses its true 1/16 grid (up to 4 bars) instead of squeezing the loop.
- **Hits classified in context** (band rises at each onset, stored as class overrides). Better than isolated analysis on full-kit recordings; still imperfect (see `KIT.md`).
- **Timbre map:** the noisiness axis is log-scaled, so real recordings spread out.
- **Tests:** the field-test tool verifies per-class outputs through the installed VST3 and AU binaries via JUCE plugin hosting.

## 0.3.0 (2026-10-30): kit intelligence
- **Sound fingerprints and drum classes** per slice (11 classes), with a timbre map and per-slice class override.
- **Timbre-Addressed Sequencing:** steps target a slice or a timbre point resolved on the current kit; Retarget to timbre; a timbre gene for Mutation.
- **Loop Deconstructor:** loop → pattern with one lane per kit piece, keeping velocity and groove.
- **12 stereo outputs** (Main plus one per class), with automatable per-class levels.
- **Chord-Lock:** tonal slices retune to the held chord (Resample or Keep length with pre-rendered variants).
- **KIT tab;** 14 new parameters (63 total); 205 MIDI-mappable controls.

## 0.2.0 (2026-10-29): MIDI mapping
- **MIDI Learn for every control:** 182 buttons, sliders and selectors, enforced by a test. Mapped messages are consumed. Parameter mappings run on the audio thread (and work with the editor closed); other controls run on the message thread.
- **Hardware pad banks:** "pads follow bank" plays NEURO pad *n* of the selected bank A–D, positionally. The bank selector is a mappable parameter (`padBank`), there is a learned hardware pad layout, and a pad MIDI channel filter (`padBankFollow`, `padChannel`).
- **Akai MPK mini mk2 template**, measured from a connected unit (knobs CC 1–8, pads 32–39 / 44–51 on ch 1).
- **Fixed:** closing the editor could crash the host (parameter attachments outlived their controls).
- **Fixed:** editing the arpeggiator pattern reset the learned hardware pad layout and wrote audio-thread state from the message thread. A Phase 12 edit had duplicated constructor code into `editArpProgram`.
- **Fixed:** the processor no longer stores ~0.5 MB of sequencer patterns inline.
- **Fixed:** MIDI acting on controls in hidden tabs now uses current values.

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
