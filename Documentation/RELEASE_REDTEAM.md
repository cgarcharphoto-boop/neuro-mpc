# Pre-release red team: v1.0.0 (2026-10-31)

Deep pre-release checklist (BMad Quality Suite), adapted to a desktop audio plugin. The web-only sections (auth, DB, API, CORS) are not applicable: there is no server, no account and no user data. The attack surface is what a plugin receives:
- **Untrusted files:** samples, presets, host project state and the downloaded AI model.
- **One user-initiated network download.**
- **The host's threads.**

**Verdict: GO.** No open P1. All P2 findings are fixed and covered by tests. Open P3s are listed below.

## Findings
| # | Sev | Finding (persona) | Resolution |
|---|---|---|---|
| 1 | P2 | **Model file parsing trusts the file** (Bad actor / Edge case): demucs.cpp's loader reads tensor dimensions from the file unchecked, so a damaged or tampered model could corrupt memory in the host. | The model is installed only when its SHA-256 matches the pinned hash. It is **verified again before first use** each run; a mismatch deletes the file and asks to re-download. |
| 2 | P2 | **Separate drums on a full song** (Resource constrained): after Extract drums (AI), a 5-min song would need ~2 GB and freeze the UI on the message thread. | Separate drums analyses at most the first 60 s (~100 MB, ~1 s). The pattern uses ≤4 bars. |
| 3 | P2 | **Cancel blocked the UI ~1.6 s** (Skeptical user): it joined the worker. | Cancel only signals: it returns in <5 ms, and the job stops in ~1.7 s. Only plugin shutdown waits. Tested. |
| 4 | P3 | **Sample changed during extraction** (Edge case): the finished stem replaced the newly loaded sample. | The result is discarded with a message if the sample changed. |
| 5 | P2 | **Clock-follow drift** (Domain expert): an early tempo guess plus the no-backwards guard locked a constant ~145-sample offset. | The position slews back onto the clock (never backwards). 0 samples of error at 100 BPM, <1.5 ms with ±1 ms jitter. Tested. |
| 6 | P2 | **Integer overflow** (Implementer): the resampled length `n * 44100` overflowed for >48k samples, causing bad_alloc in the worker. | Computed in double. Covered by the AI test on 8 s of real audio. |
| 7 | P3 | **No notarization** (Skeptical user): Gatekeeper blocks ad-hoc signed builds. | README gives the `xattr` step. Notarization needs an Apple Developer ID (open). |
| 8 | P3 | **pluginval not run** on the VST3 (Implementer). | Open: run `pluginval --strictness-level 5` before announcing. auval passes. |
| 9 | P3 | **Real-host sessions** in MPC 3 / Logic / Live not yet done by a person. | Manual checklist in `MPC.md`. Automated host-sync and clock tests pass. |
| 10 | P3 | **Personal email in git history** (Bad actor, privacy): commits use the author's personal address. | Owner decision before making the repo public (keep it, or rewrite to a GitHub noreply address). |

## Attack surface review
- **Network:**
  - **Scope:** one HTTPS download, only when the user clicks.
  - **Pinning:** the URL is pinned to an immutable commit and the file to a SHA-256. Nothing is executed. No network at load or scan time.
  - **Failure:** a failed or partial download leaves no model (`.part` is deleted).
- **Files:**
  - **What is read:** samples (JUCE readers, ≤200M floats), presets and state (versioned; corrupt data is rejected, as tested in Phase 14).
  - **What is written:** only `~/Music/NEURO MPC/Separated`, `~/Library/Application Support/NEURO MPC/Models`, and presets.
- **Threads:**
  - **Audio thread:** unchanged rules: no locks, allocation or I/O.
  - **MIDI clock:** parsing is allocation-free.
  - **AI worker:** owns copies (`shared_ptr` source). Results cross under a mutex and are applied on the message thread. The destructor cancels and joins.
- **Dependencies (licences in `LICENSES.md`):**
  - **Versions:** JUCE 9.0.4, Signalsmith (MIT), demucs.cpp (MIT, pinned commit), Eigen (MPL-2.0, SHA-256 pinned), all at fixed versions.
  - **CVEs:** none known for these versions in this use (offline DSP, no parsing of network data except the hash-verified model).
- **Licensing:**
  - **AGPL-3.0:** the source must be public when binaries are distributed. Publish the repo before or with the release.
  - **Model:** the Demucs model (MIT) is downloaded by the user, not redistributed.

## Failure modes
| Failure | Behaviour |
|---|---|
| No network / download error | Message "Model download failed", button stays "Get AI model" |
| Corrupt model on disk | Detected before load, deleted, re-download offered |
| Clock master disappears | Stops following after 0.5 s; host or free transport resumes |
| Host without transport | MIDI clock (if any) drives the sequencer, otherwise Free mode |

## Release and rollback
- **Release:** tag `vX.Y.Z`. CI builds, tests and validates the AU, then publishes an ad-hoc signed zip with licences.
- **Rollback:** delete the release, or mark a previous one as latest. The plugin state format is unchanged since v0.3, so older builds open newer projects. The new parameters are ignored by old builds.
