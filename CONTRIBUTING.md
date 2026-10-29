# Contributing to NEURO MPC

Thanks for helping. Two principles shape this project: **audio code must be real-time safe**, and **nothing is claimed that isn't tested**.

## Getting started
```sh
git clone <this repo> && cd NEURO-MPC
Scripts/build.sh          # fetches pinned JUCE, builds, runs tests, validates the AU
```
Read `Documentation/ARCHITECTURE.md` first.

## Rules for changes
1. **Tests with every behaviour change.**
   - Logic belongs in `Source/Core` (JUCE-free) with tests in `Tests/CoreTests.cpp`.
   - Anything audible gets an end-to-end check in `Tests/EngineTests.cpp`: drive `NeuroMPCProcessor` and assert on the rendered audio.
   - `ctest --test-dir build -C Release` must pass and `auval -v aumu Nm26 Nrmp` must succeed.
2. **The audio thread** (`processBlock` and everything it calls) must not allocate, lock, do file or network I/O, or wait. Hand data over with `RtShared<T>` (immutable snapshots) or the lock-free FIFO. Keep fixed capacities and bounded loops.
3. **Determinism.** Randomness is seeded, and timing is computed from host/grid position, never from the GUI timer or wall-clock time.
4. **Parameter IDs are frozen.** Never rename or remove an ID; append new ones (and add them to the frozen list in `EngineTests.cpp`). State changes need a migration step in `NeuroMPCProcessor::migrate` and a bump of `stateVersion`.
5. **Zero warnings.** Builds use JUCE's recommended warning flags (including `-Wshadow`, `-Wfloat-equal` and `-Wsign-conversion`); keep them clean.
6. **Style:** match the surrounding JUCE style (Allman braces, a space before parentheses, `auto`, `noexcept` on audio paths). Comments explain *why*.
7. **Dependencies:** licence compatibility with AGPL-3.0 first, then pin the version and record it in `Documentation/LICENSES.md`.
8. **Claims:** documentation states measured results and known limitations. No "best", "unprecedented" or "studio quality" without comparative evidence.

## Pull requests
- One topic per PR, with a description of the user-visible change and how it was verified.
- CI (`.github/workflows/ci.yml`) must be green.
- Note anything verified only in a DAW (Live/Logic), with the host version.

By contributing you agree that your contribution is licensed under AGPL-3.0-or-later (see `LICENSE`).
