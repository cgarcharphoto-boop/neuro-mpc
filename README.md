# NEURO MPC

An open-source, MPC-style slice sampler for macOS: VST3 (Ableton Live), AU (Logic Pro), and Standalone.
C++20 · JUCE 9.0.4 (pinned) · CMake. The master spec lives in `../gmni sampler/`.

## Status: Milestones 1–2 (Phases 1–8: sampler, slicing, time-stretch, pitch analysis, chromatic pitch)

| Works and is tested | |
|---|---|
| Sample import | WAV/AIFF/FLAC/OGG/MP3 via JUCE + CoreAudio; drag-and-drop or Load; mono and stereo (first 2 ch); ≤200M floats |
| Waveform | Multiresolution peaks (AudioThumbnail), zoom (wheel/pinch around the cursor), horizontal pan, live playhead |
| Slicing | Double-click to split, drag a marker to move, right-click a marker to remove; Auto-Slice (transients, sensitivity slider); Grid 4–64; Clear |
| Playback | 32-voice polyphony with oldest-voice stealing; slice *n* = MIDI note 36+*n*−1 (C1 = pad 1); velocity; One-Shot / Gate; attack/release; smoothed gain; sample-rate conversion |
| Pads | 4×4 bank (pad 1 at bottom-left), velocity set by click height, lit while its slice plays |
| State | Parameters, sample path, slices (stable ids) saved in the DAW project; a missing file is reported, never substituted |
| Time-stretch | Per-slice ratio ×0.25–×4 with pitch preserved (Signalsmith Stretch), target duration, ×0.5/×2/1:1, Apply to all, per-slice Preserve transients, background rendering + LRU cache. See [Documentation/TIME_STRETCHING.md](Documentation/TIME_STRETCHING.md) |
| Pitch & key | YIN pitch per slice and per sample with confidence, a polyphony check, and pitched/uncertain/unpitched status; chroma + Krumhansl–Kessler key that reports uncertainty honestly; per-slice manual Root correction. See [Documentation/PITCH_ANALYSIS.md](Documentation/PITCH_ANALYSIS.md) |
| Chromatic pitch | Per-slice ±24 st (12-note selector, octave, fine), Resample or Keep-length mode; global transpose, fine tune, pitch bend (range 0–24), pitch LFO (5 shapes, phase, host-tempo sync); Keys mode plays a slice chromatically from its detected or manual root, with scale snapping. See [Documentation/PITCH_SHIFTING.md](Documentation/PITCH_SHIFTING.md) |
| Real-time safety | No locks, file I/O or allocation on the audio thread. Samples and slices reach the audio thread through `RtShared` (hazard-pointer handoff); UI note input goes through a lock-free FIFO |

**Validation:** `auval` passes. Unit and integration tests pass (`ctest`).
**Not yet done:** VST3 pluginval run, and manual testing inside Live and Logic.

**Known limitations:** linear interpolation (resampling can alias); stretched slices play the original until their render is ready; files load on the message thread with no progress bar; no undo yet; arm64 only.

## Build

```sh
Scripts/build.sh            # fetch JUCE, build, install plugins, run tests, run auval
# or, using an existing JUCE checkout:
cmake -S . -B build -G Xcode -DJUCE_PATH=/path/to/JUCE && cmake --build build --config Release
```

Builds are installed to `~/Library/Audio/Plug-Ins/{VST3,Components}`. The Standalone app is at `build/NEURO_MPC_artefacts/Release/Standalone/`.

## Layout

```
Source/Core/    JUCE-free model + DSP: SliceSet, TransientDetector, PitchDetector, KeyDetector, RtShared (unit-tested)
Source/Audio/   SampleData, VoiceEngine
Source/DSP/     TimeStretch, Analysis, BackgroundWorker (coalescing job thread)
ThirdParty/     vendored MIT header libraries (Documentation/LICENSES.md)
Source/UI/      WaveformView
Source/Plugin*  processor (params, MIDI, state) and editor
Tests/          CoreTests (pure C++), EngineTests (headless processor integration)
```

## License

AGPL-3.0. JUCE 9's open-source licence is AGPLv3, so any distributed build that uses it must also be AGPLv3 unless it is built under a commercial JUCE licence. See `LICENSE`.
