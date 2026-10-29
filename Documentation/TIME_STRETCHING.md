# Time Stretching (Phase 6)

## Convention
`timeStretchRatio` R: output duration = original duration × R. R = 1 is the original, 2 is twice as long, 0.5 is half. The range is 0.25–4.0 and values outside it are clamped. Pitch is preserved. Each slice has its own ratio, and changing one slice never affects another.

## Algorithm
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) 1.4.0 (MIT) with `presetDefault` (120 ms blocks, 30 ms interval), rendered **offline** through `exact()`. Evaluated against the spec §8.3 criteria:
- **License:** MIT, compatible with AGPL.
- **Code:** header-only, built on Signalsmith Linear 0.6.4 with Apple Accelerate FFTs.
- **Quality:** strong tonal stability.
- **Tradeoff:** a phase vocoder smears transients, which the segmentation below addresses.

`renderStretch()` (`Source/DSP/TimeStretch.cpp`) adds two measures:
1. **Silent margins.** Each region is stretched with ~150 ms (output-domain) of silence on both sides, then cropped. The algorithm's priming/flush edge effects and the pre-echo of the region's first transient land in the cropped margins.
2. **Preserve transients** (per slice, on by default). The slice is split at detected transients, and each segment is stretched on its own and placed at `round(segmentStart × R)`. No transient can then smear backwards into the audio before it. Turn it off for sustained tonal material, where segment joins could be audible.

## Real-time architecture
UI edit → `SliceSet` → `refreshStretch()`, which looks in the cache. On a miss it sends a request to the `StretchRenderer` background thread, which coalesces requests per slice so a dragged slider renders only its latest value. The processor timer collects results into an LRU cache (512 MB budget). The bank of active renders is published to the audio thread through `RtShared`.

The audio thread never renders, allocates or locks. Until a render is ready, the slice plays its original audio and the UI shows "rendering...". If a render a voice is playing gets replaced, that voice stops at the bank switch, because its buffer may be freed. The timer runs inside the processor, so renders complete with the editor closed.

## Measured results
These come from `neuro_engine_tests` on an M-series Mac, using a 1 s stereo 440 Hz tone at 48 kHz and a 3-hit noise-burst sequence.

| Ratio | Length (exp.) | Frequency | Steady peak (in 0.5) | Edge peak | L/R error | Render time |
|---|---|---|---|---|---|---|
| ×0.25 | 12000 (12000) | 440.0 Hz | 0.508 | 0.648 | 0.0002 | 2.7 ms |
| ×0.50 | 24000 (24000) | 440.0 Hz | 0.507 | 0.652 | 0.0001 | 3.7 ms |
| ×1.50 | 72000 (72000) | 440.0 Hz | 0.502 | 0.539 | 0.0002 | 7.8 ms |
| ×2.00 | 96000 (96000) | 440.0 Hz | 0.500 | 0.605 | 0.0004 | 10.0 ms |
| ×4.00 | 192000 (192000) | 440.8 Hz | 0.526 | 0.565 | 0.0002 | 20.0 ms |

Transients (hits at 0.25/0.5/0.75 s, stretched ×2, expected at 24000/48000/72000):
- With preserve off, onsets are detected at 17664, 41728, 47872, 65792 and 71936: each hit gets a pre-echo about 128 ms early.
- With preserve on, onsets are at 23552, 47616 and 71680: three hits, within 9 ms, no pre-echo.

## Known limitations
- **Edges:** a signal that switches on or off abruptly at full level rings by up to about +2.3 dB at the switch. This is inherent to STFT processing.
- **Above ×2:** the algorithm randomises phases, which gives about ±5% amplitude ripple and a slightly diffuse sound. The randomness is seeded from the region, so identical settings always render identical audio (verified by test).
- **Short slices:** renders of slices under ~120 ms are dominated by the window. Very short drum hits can sound softened; resampling playback (stretch = 1) is exact.
- **Joins:** with Preserve transients on, segment joins have no crossfade. Each join sits at a detected onset, which usually masks it.
- **Not yet done:** pitch-preserving *pitch shift* is Phase 8. Comparative listening tests against commercial stretchers have not been done, so no claim of superior quality is made.
