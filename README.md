# NEURO MPC

An open-source, MPC-style slice sampler for macOS: VST3 (Ableton Live), AU (Logic Pro), and Standalone.
C++20 · JUCE 9.0.4 (pinned) · CMake · AGPL-3.0.

## Install (macOS, Apple Silicon)
1. Download `NEURO-MPC-<version>-macOS-arm64.zip` from [Releases](../../releases) and unzip it.
2. Copy `NEURO MPC.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `NEURO MPC.component` to `~/Library/Audio/Plug-Ins/Components/`. The Standalone `NEURO MPC.app` can go anywhere.
3. The builds are not notarized yet, so macOS blocks them on first use. Run this once in Terminal:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"NEURO MPC.vst3" ~/Library/Audio/Plug-Ins/Components/"NEURO MPC.component" /path/to/"NEURO MPC.app"`
4. Rescan plugins in your host: Logic, Live or MPC 3. To run alongside an MPC, see [Documentation/MPC.md](Documentation/MPC.md).

## Status: v1.1.0, public MVP + GW-1 / KAOSS NEXUS link

The acceptance status of each requirement, with evidence, is in [Documentation/ACCEPTANCE.md](Documentation/ACCEPTANCE.md). **Still open:** hands-on sessions in MPC 3, Ableton Live and Logic Pro (checklist in [MPC.md](Documentation/MPC.md)), a pluginval run on the VST3, notarization. See [RELEASE_REDTEAM.md](Documentation/RELEASE_REDTEAM.md).

| Feature | |
|---|---|
| Sample import | WAV/AIFF/FLAC/OGG/MP3 via JUCE + CoreAudio; drag-and-drop or Load; mono and stereo (first 2 ch); ≤200M floats |
| Waveform | Multiresolution peaks (AudioThumbnail), zoom (wheel/pinch around the cursor), horizontal pan, live playhead |
| Slicing | Double-click to split, drag a marker to move, right-click a marker to remove; Auto-Slice (transients, sensitivity slider); Grid 4–64; Clear |
| Playback | 32-voice polyphony with oldest-voice stealing; slice *n* = MIDI note 36+*n*−1 (C1 = pad 1); velocity; One-Shot / Gate; attack/release; smoothed gain; sample-rate conversion |
| Pads | 64 pads in banks A–D; slice-by-id assignment (follow slices or custom), MIDI note + learn, layering, choke groups, level/tune/velocity sensitivity, names and colours, copy/paste/duplicate. See [Documentation/PADS.md](Documentation/PADS.md) |
| State & presets | 66 frozen automation IDs; versioned state (v1 migrates; corrupt data is rejected safely); missing samples keep their slices and can be relinked by file or folder search; presets in ~/Library/Audio/Presets. See [Documentation/STATE_AND_AUTOMATION.md](Documentation/STATE_AND_AUTOMATION.md) |
| Time-stretch | Per-slice ratio ×0.25–×4 with pitch preserved (Signalsmith Stretch), target duration, ×0.5/×2/1:1, Apply to all, per-slice Preserve transients, background rendering + LRU cache. See [Documentation/TIME_STRETCHING.md](Documentation/TIME_STRETCHING.md) |
| Pitch & key | YIN pitch per slice and per sample with confidence, a polyphony check, and pitched/uncertain/unpitched status; chroma + Krumhansl–Kessler key that reports uncertainty honestly; per-slice manual Root correction. See [Documentation/PITCH_ANALYSIS.md](Documentation/PITCH_ANALYSIS.md) |
| Chromatic pitch | Per-slice ±24 st (12-note selector, octave, fine), Resample or Keep-length mode; global transpose, fine tune, pitch bend (range 0–24), pitch LFO (5 shapes, phase, host-tempo sync); Keys mode plays a slice chromatically from its detected or manual root, with scale snapping. See [Documentation/PITCH_SHIFTING.md](Documentation/PITCH_SHIFTING.md) |
| Arpeggiator | 10 directions, 1/1–1/32 with triplets, gate, octaves, MPC swing, probability, repeats, latch, retrigger, seeded randomness; locked to host PPQ or free-running from the key press; note-offs in samples (no stuck notes on loops). Generative: 16-step rhythm pattern with accents/ratchets and Euclid, inspectable Arpeggio Grammar rules with presets, Phrase Memory with seeded variation, humanise, gesture XY pad. See [Documentation/ARPEGGIATOR.md](Documentation/ARPEGGIATOR.md) |
| Step sequencer | 16 patterns × 16 lanes, per-lane length (polymeter) and mute, per-step velocity/gate/probability/ratchet/microtiming/pitch, swing, host-transport or free-running; stateless deterministic timing. Advanced: trig conditions (A:B, first, fill, previous), transformations with undo/redo, Pattern Genome recombination, seeded Mutation with A/B compare, Phrase Weaving. See [Documentation/SEQUENCER.md](Documentation/SEQUENCER.md) |
| MIDI mapping | MIDI Learn for every control (215, enforced by test); parameter mappings work with the editor closed; hardware pads follow pad banks A–D via a learned pad layout; measured Akai MPK mini mk2 template. See [Documentation/MIDI_MAPPING.md](Documentation/MIDI_MAPPING.md) |
| Kit intelligence | Sound fingerprints + drum classes (kick, snare, hats, toms, crash, ride…) with a timbre map; **Timbre-Addressed Sequencing** (patterns re-orchestrate on any kit); **Loop Deconstructor** (loop → one lane per kit piece, groove kept); **Extract drums (AI)** (Demucs, in-process: full song → drum stem); **Separate drums** (NMF: mixed loop → isolated Kick/Snare/Cymbals stems + lanes; **teach the kit** with example hits); **12 outputs** (one per class) for mixing; **Chord-Lock** (tonal slices follow your chord). See [Documentation/KIT.md](Documentation/KIT.md) |
| Rig link | Runs in unison with **GW-1** (synth) and **KAOSS NEXUS** (XY effect): sequencer lanes play GW-1 (**Out: MIDI ch N**), virtual MIDI port **NEURO MPC** (lanes + clock), shared MIDI clock. See [Documentation/GMNI_LINK.md](Documentation/GMNI_LINK.md) |
| MPC sync | Runs inside MPC 3 software (host sync), and next to MPC hardware over **MIDI clock**: follows Start/Continue/Stop/Song Position/tempo sample-accurately, or sends clock. See [Documentation/MPC.md](Documentation/MPC.md) |
| Real-time safety | No locks, file I/O or allocation on the audio thread. Samples and slices reach the audio thread through `RtShared` (hazard-pointer handoff); UI note input goes through a lock-free FIFO |

**Validation:**
- `auval` passes.
- About 340 automated checks pass (`ctest`). Most assert on rendered audio, with exact sample timing, levels, pitch, determinism and robustness.
- A clean checkout builds and passes.
- CI: `.github/workflows/ci.yml`.

**Performance:** the worst case measured, 32 voices plus pitch LFO, a 16-lane 1/32 sequencer with ratchets, and the arpeggiator, uses about 0.4% of one Apple Silicon core.

## Documentation
| | |
|---|---|
| [ARCHITECTURE](Documentation/ARCHITECTURE.md) | threads, ownership, block processing, modules |
| [BUILD](Documentation/BUILD.md) · [TESTING](Documentation/TESTING.md) · [ACCEPTANCE](Documentation/ACCEPTANCE.md) | build, test and release status |
| [TIME_STRETCHING](Documentation/TIME_STRETCHING.md) · [PITCH_ANALYSIS](Documentation/PITCH_ANALYSIS.md) · [PITCH_SHIFTING](Documentation/PITCH_SHIFTING.md) | DSP, with measured results |
| [PADS](Documentation/PADS.md) · [ARPEGGIATOR](Documentation/ARPEGGIATOR.md) · [SEQUENCER](Documentation/SEQUENCER.md) · [KIT](Documentation/KIT.md) | performance features |
| [MPC](Documentation/MPC.md) · [RELEASE_REDTEAM](Documentation/RELEASE_REDTEAM.md) · [SPRINT_MVP](Documentation/SPRINT_MVP.md) | MPC sync, release review, MVP sprint |
| [STATE_AND_AUTOMATION](Documentation/STATE_AND_AUTOMATION.md) · [MIDI_MAPPING](Documentation/MIDI_MAPPING.md) · [LICENSES](Documentation/LICENSES.md) | parameters, state, presets, MIDI learn, licences |
| [CONTRIBUTING](CONTRIBUTING.md) · [CHANGELOG](CHANGELOG.md) · [SECURITY](SECURITY.md) | project |

## Build

```sh
Scripts/build.sh            # fetch JUCE, build, install plugins, run tests, run auval
# or, using an existing JUCE checkout:
cmake -S . -B build -G Xcode -DJUCE_PATH=/path/to/JUCE && cmake --build build --config Release
```

Builds are installed to `~/Library/Audio/Plug-Ins/{VST3,Components}`. The Standalone app is at `build/NEURO_MPC_artefacts/Release/Standalone/`.

## Layout

```
Source/Core/    JUCE-free model + logic: slices, analysis, scales, pads, arpeggiator, sequencer, pattern tools, RtShared (unit-tested)
Source/Audio/   SampleData, VoiceEngine
Source/DSP/     TimeStretch, Analysis, BackgroundWorker (coalescing job thread)
ThirdParty/     vendored MIT header libraries (Documentation/LICENSES.md)
Source/UI/      WaveformView, SeqGrid, GesturePad
Source/Plugin*  processor (params, MIDI, state) and editor
Tests/          CoreTests (pure C++), EngineTests (headless processor integration)
```

## License

AGPL-3.0. JUCE 9's open-source licence is AGPLv3, so any distributed build that uses it must also be AGPLv3 unless it is built under a commercial JUCE licence. See `LICENSE`.
