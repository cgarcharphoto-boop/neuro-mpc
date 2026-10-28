# Pitch & Key Analysis (Phase 7)

Pitch detection and key detection are separate tasks with separate results. Both report uncertainty instead of guessing.

## Fundamental pitch: `Source/Core/PitchDetector.h`
- **YIN** (de Cheveigné & Kawahara, 2002) on a mono mixdown: 25 ms frames, 40–2000 Hz, threshold 0.15, parabolic lag interpolation, first 8 s of the region.
- **Aggregation:** the median over voiced frames, refined by averaging frames within ±50 cents.
- **Confidence:** voiced fraction × stability (share of frames near the median) × periodicity (1 − aperiodicity).
- **Polyphony check.** YIN reports a chord's common period: A–C♯–E is periodic at A1. The candidate f0 is therefore checked in the spectrum (1 s Hann-windowed Goertzel):
  - f0 + 2·f0 must hold at least 10% of the harmonic energy (h = 1..8);
  - partials h = 1..4 must peak *at* h·f0, not ±50 cents beside it.

  If either check fails, the result is marked `ambiguous` and capped at confidence 0.4.
- **Status:**
  - `pitched`: confidence ≥ 0.6;
  - `uncertain`: some voicing, low confidence or ambiguous;
  - `unpitched`: noise or silence;
  - `tooShort`: shorter than one analysis frame.
- **Output:** frequency, fractional MIDI note, nearest note and cents. MIDI note m = 69 + 12·log2(f/440).

## Key: `Source/Core/KeyDetector.h`
- **Chroma:** a Goertzel bank over C2–B7 on Hann-windowed ~170 ms frames (8192 samples at 48 kHz) with 50% overlap, using the first 60 s.
- **Profiles:** Pearson correlation with the 24 Krumhansl–Kessler key profiles.
- **`key`** only when best r ≥ 0.6, the margin over the runner-up is ≥ 0.05, and at least 4 pitch classes each hold > 5% of the energy. Otherwise **`uncertain`**, still reporting the top candidates. Silence gives **`unavailable`**.
- Runs on the whole sample. Slices get pitch only.

## Integration
- Analysis runs on a `BackgroundWorker` thread, the same coalescing worker as stretch rendering. It covers the whole sample and every slice, and is requested again when a slice's region changes. A stale result is never shown: the UI says "analysing…" until the result for the current region arrives.
- **Manual correction:** each slice has a `rootNote` (the Root dropdown, which defaults to the detected pitch) that persists with the project. Detected results are recomputed after a restore; the algorithms are deterministic.

## Measured results (`neuro_core_tests`, `neuro_engine_tests`; 48 kHz synthetic signals)
| Input | Result |
|---|---|
| Sines A1–A6 (55–1760 Hz) | pitched, error ≤ 1 cent (A6: +1.0 c), confidence 1.00 |
| A4 + 30 cents | 69.301 (+30.1 c) |
| G3, 8 harmonics | G3, < 5 c |
| A2 with a −20 dB fundamental, strong 2nd/3rd harmonics | pitched, A2 |
| White noise / silence | unpitched |
| A–C♯–E triad / A–B♭ cluster / C–F♯ tritone | uncertain, ambiguous (without the polyphony check: falsely pitched at A1 / 69.5 / 32.3) |
| I–IV–V–I in C major, F♯ major | correct key, r 0.93, margin 0.12 |
| i–iv–V–i in A minor | A minor, r 0.91, margin 0.30 |
| Single note A4 | uncertain (A major vs A minor, margin 0.02) |
| Chromatic scale | uncertain (r 0.39) |
| 2 s stereo sample + 2 slices, end to end | 141 ms |

## Known limitations
- **Inputs:** the tests use synthetic signals. Accuracy on real recordings (vibrato, reverb, mixed sources) has not been measured yet.
- **Polyphony check:** it is heuristic. A sparse chord whose lowest note carries strong low harmonics may still read as that note.
- **Key profiles:** Krumhansl–Kessler profiles suit Western tonal music. Relative major/minor pairs are often close, so expect `uncertain` on short or modal material.
- **Coverage:** only the first 8 s (pitch) and 60 s (key) of a region are analysed.
