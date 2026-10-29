# Release Acceptance Matrix (spec §27), recorded results

Status at v0.1.0, 2026-10-29. ✅ = implemented **and** verified by the method shown. ⚠️ = implemented, verification incomplete. ❌ = not done.

| Requirement | Acceptance condition | Status | Evidence |
|---|---|---|---|
| VST3 | Builds and is validated | ⚠️ | Builds and loads in the headless harness; **pluginval not run** (it was blocked in the dev environment), and not yet tested in Live |
| AU | Builds and is validated | ✅ / ⚠️ | `auval` passes (46 params); **not yet tested in Logic** |
| Sample import | Tested supported formats work | ⚠️ | WAV tested end to end; AIFF, FLAC, OGG and MP3 go through JUCE + CoreAudio but were not individually tested |
| Sample playback | MIDI-triggered playback works | ✅ | Engine tests: levels and lengths per slice, velocity, gate, one-shot, sample-rate conversion |
| Slicing | Slices can be edited and triggered | ✅ | Core and engine tests (split, move, merge, grid, transients, stable ids) |
| Independent stretching | Each slice has its own stretch setting | ✅ | Engine tests: ×2, ×0.5 and ×3 per slice, others unaffected |
| Time-stretch quality | Evaluated and documented | ✅ / ⚠️ | `TIME_STRETCHING.md` (length, frequency, steady/edge level, L/R error, transients, render time); **no listening tests** |
| Pitch detection | Tested on suitable reference material | ⚠️ | Synthetic signals (≤1 cent, polyphony check); **real recordings not measured** |
| Key detection | Uncertainty is handled honestly | ✅ | Uncertain on single notes, chromatic material and silence; correct on progressions |
| Chromatic pitch | All 12 pitch classes are accessible | ✅ | 12-note selector; Keys mode tested A2–A5 within 0.01 st |
| Pitch modulation | Produces actual audible modulation | ✅ | LFO measured sweeping 56.00–58.00; bend 57 → 59 → 57 |
| Arpeggiator | Produces correctly timed notes | ✅ | Sample-exact core tests; audio output tests |
| Sequencer | Plays editable patterns reliably | ✅ | Sample-exact core and audio tests; host PPQ/tempo/loop/stop with a mock host |
| Waveform editor | Supports zoom, selection and editing | ⚠️ | Implemented; **UI not visually or interactively tested** |
| MIDI | Note input and note-off behaviour are reliable | ✅ | Gate/one-shot, all-notes-off, balanced on/off for generated notes |
| Automation | Tested in target DAWs | ⚠️ | 46 frozen IDs, round trip tested; **not tested in Live or Logic** |
| State | Project settings restore correctly | ✅ | Every model and parameter round-trips; v1 migration; fuzzing; relink |
| Open source | Licence and dependency terms documented | ✅ | AGPL-3.0 `LICENSE`, `LICENSES.md` |
| Build instructions | Reproducible from a clean checkout | ✅ | Clean clone → `fetch-juce.sh` → build → ctest passes (see below) |
| Testing | Relevant tests run and results are recorded | ✅ | `TESTING.md`; CI workflow |
| Real-time safety | No known unresolved critical failures | ✅ / ⚠️ | No allocation, locks or I/O on the audio thread by design and review; lock-free hand-off stress-tested; **no instrumented allocation/lock detection run** |

## Performance (spec §28.3), measured on this development Mac (Apple Silicon)
| Metric | Result |
|---|---|
| Worst-case DSP load: 32 voices + per-sample pitch LFO + 16-lane 1/32 sequencer with ×4 ratchets + arp | 10 s of audio in 38 ms (~0.4% of one core) |
| MIDI response | Sample-accurate (events are applied at their sample offset; no added latency) |
| Sequencer and arp timing | Exact to the sample in tests (any block size) |
| Time-stretch render | 2.7–20 ms per second of stereo audio (×0.25 … ×4) |
| Analysis | 2 s stereo sample + slices analysed in ~140 ms (background) |

## Clean-checkout verification
Recorded on 2026-10-29: a fresh `git clone` of the repository, then `Scripts/fetch-juce.sh`, `cmake -S . -B build -G Xcode`, `cmake --build build --config Release` and `ctest`, all completed successfully with zero failures.

## Known limitations
- **Platform:** arm64 macOS only.
- **Generated notes:** no MIDI output of arp/sequencer notes.
- **Playback quality:** linear interpolation (large resampled pitch shifts can alias). Keep-length pitch degrades beyond about ±12 st (no formant preservation).
- **Analysis coverage:** first 8 s (pitch) and 60 s (key) of a region.
- **Not yet built:** pattern chaining/song mode, per-step parameter locks, user-defined scales in the UI, sample collection into the project folder, factory presets.
- **Untested here:** the UI (layout at sizes other than the default) and DAW behaviour, until manually tested.
