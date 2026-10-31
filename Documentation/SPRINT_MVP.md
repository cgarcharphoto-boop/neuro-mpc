# Sprint: public MVP (v1.0.0)

Goal: NEURO MPC is ready to share publicly (AGPL-3.0, open source): AI drum extraction for full songs, tight sync with
MPC 3 (as a plugin) and MPC hardware (over MIDI clock), and release-grade packaging.

| # | Story | Acceptance criteria |
|---|---|---|
| S1 | **Extract drums (Demucs)**: AI drum stem from a full song, then Separate drums | demucs.cpp (MIT) runs in-process, no Python; model (htdemucs_ft drums, MIT, 84 MB) downloaded on request with SHA-256 check, never bundled in git; runs off the message and audio threads with progress and cancel; drums stem saved and loaded; tests on real audio |
| S2 | **Run in unison with MPC**: host sync + MIDI clock in/out | In a host (MPC 3, Logic, Live) the sequencer, arp and LFO follow host tempo/position (existing, re-verified). With no playing host transport, incoming MIDI clock (Start/Continue/Stop/SPP/ticks) drives them sample-accurately; optional MIDI clock out (24 PPQN + Start/Stop) lets an MPC follow NEURO; engine tests on rendered timing |
| S3 | **Release packaging** | Third-party notices, signed builds, install instructions, CI release artifacts (zip on tag), MPC 3 setup guide + manual test checklist |
| S4 | **Pre-release red team** (mandatory) | Deep pre-release checklist: threading, file I/O, network download, state, licensing; P1s fixed or the release is halted |

Out of scope for the MVP: running on MPC standalone hardware (MPC OS does not load third-party plugins), Windows/Linux builds, notarization (needs a Developer ID).
