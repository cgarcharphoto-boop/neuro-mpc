# Chromatic Pitch (Phase 8)

## Resampling vs. keep-length (per slice, `pitchMode`)
| | **Resample** (default) | **Keep length** |
|---|---|---|
| How | Playback rate × 2^(st/12) | Offline Signalsmith render with `setTransposeSemitones`, played at rate 1 |
| Duration | Changes (+12 st = half as long) | Preserved |
| Character | Classic sampler: transients speed up and timbre shifts | Independent pitch and length; may add phase-vocoder artefacts |
| Cost | Free, real time | Background render; until it's ready the slice plays resampled |
| Combines with stretch | Yes (rate on top of the stretch render) | Yes (stretch and transpose happen in one render) |

Per-slice pitch = semitones + octaves + fine/100, limited to ±24 st (`SliceSet::maxPitch`).

## Real-time pitch, global, applied to every voice by resampling
`transpose` (±24 st) + `fineTune` (±100 c) + pitch bend (± `bendRange`, 0–24 st, default 2) + LFO. These are computed per sample in `renderWithModulation`, and the voice rate updates every 16 samples, so they also affect notes already playing.
- **LFO:** depth 0–12 st; rate 0.01–20 Hz; shapes sine, triangle, saw, square and sample-and-hold; phase offset 0–360°; sync Free, 1 bar, 1/2, 1/4, 1/8 or 1/16 to host tempo. While the host plays, the LFO phase locks to the PPQ position (4/4 assumed).
- **Note:** real-time modulation is always resampling-based, so it changes speed slightly, even on keep-length slices.

## Note mapping (`keyMode`)
- **Pads:** note 36 + n plays slice n (MPC convention).
- **Keys:** every note plays the *selected* slice, shifted by `note − root`. The root is the manual Root if set, else the detected pitch (fractional, so a detuned sample plays in tune), else C4. Pads play upwards chromatically from the root. Incoming notes can snap to a scale first: Chromatic, Major, Natural/Harmonic/Melodic minor, Major/Minor pentatonic, or Blues, on any root. Ties resolve downwards, and Chromatic disables snapping.

## UI (selected slice)
- **12-note selector (C … B):** highlights the pitch class the slice's root currently lands on. Clicking a note moves the root to it by the nearest shift (±6 st) and keeps the octave offset and fine part.
- **Shifting controls:** Oct−/Oct+, a semitone readout, a fine slider in cents, and Resample / Keep length.

## Measured (`neuro_engine_tests`; 220 Hz harmonic tone, 2 s, 48 kHz; pitch measured from the plugin output with the YIN detector)
| Case | Result |
|---|---|
| Resample +7 st | MIDI 64.00; 64072 samples (exp. 64072) |
| Keep length +7 st | MIDI 64.00; 95998 samples (exp. 96000) |
| Fine +50 c | 57.50 |
| Keys mode, notes 45/57/64/69/81 | 45.00 / 57.00 / 64.00 / 69.00 / 81.00 |
| C-major snap: C#4, F#4 | C4, F4 |
| Manual root C4, note 60 | plays the sample unshifted (57.00) |
| Transpose +12, fine −50 | 68.50 |
| Bend: full up with ±2 range, then centred | 57.00 → 59.00 → 57.00 |
| LFO sine ±1 st, 2 Hz | sweeps 56.00 … 58.00 |

## Known limitations
- **Linear interpolation:** upward resampling (and downward by large amounts) aliases. A higher-quality interpolator is future work.
- **LFO:** one global LFO shared by all voices, with no per-voice phase reset. Sync assumes 4/4.
- **Scales:** user-defined scales are not exposed yet; the engine accepts any 12-bit mask.
- **Large shifts:** keep-length quality degrades beyond roughly ±12 st (formants are not preserved).
