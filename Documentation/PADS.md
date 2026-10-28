# Pads (Phase 9)

## Model: `Source/Core/PadBank.h`
- **Banks:** 64 pads in banks A–D of 16. Default notes are 36–99 (pad A1 = C1, the MPC convention).
- **Per pad:**
  - the slice reference, by **stable slice id**, so renaming or editing other slices never re-points a pad;
  - a MIDI note;
  - a level and a tuning (±24 st, added to the slice's own pitch and resampled);
  - velocity sensitivity, where the level is `gain × (1 − s + s × velocity)`;
  - a choke group (0–16);
  - a name and a colour.
- **Follow slices** (the default): pad n plays slice n, and this is re-applied on every slice edit. Assigning, copying, pasting, duplicating or clearing a pad switches to a **custom map**. *Auto-assign* returns to following. Level, tuning, notes, names and choke groups are kept either way.
- **Layering:** several pads may share a note; every pad on that note plays.
- **Choke:** starting a pad fades out (5 ms) every voice in the same choke group. This includes another pad layered on the same note, if it is in the same group.
- **Removed slices:** a pad whose slice was removed (merge or clear) is silent until it is reassigned. Copy and paste copy everything except the target's note.
- **MIDI learn:** *Learn* assigns the next incoming note-on to the selected pad.

## Keys mode
Pads play the keyboard slice chromatically, upwards from its root (pad n = root + n within the bank).

## UI
Bank buttons A–D sit above the 4×4 grid. Clicking a pad auditions it, selects it, selects its slice in the waveform, and opens the **PAD** panel. Clicking the waveform returns to the **SLICE** panel. Pads show bank/number, name (or slice), note and colour, and light up while their slice plays.

## Tests (`neuro_core_tests` padTests; `neuro_engine_tests` Phase 9 block)
- **Mapping:** follow and auto-assign, assignment by id surviving a split, copy/duplicate/clear, and the velocity curve.
- **Layering and level:** layering on one note (0.1 + 0.2 = 0.3), pad level 0.5, fixed velocity, and +12 st tuning (half length).
- **Choke:** pad 1 is cut to pad 2's level after the 5 ms fade; without a group both sound.
- **Assignment and persistence:** a custom assignment survives slice edits, a removed slice silences its pad, an unassigned slice can still be auditioned, every pad property persists, and the MIDI learn note capture works.
