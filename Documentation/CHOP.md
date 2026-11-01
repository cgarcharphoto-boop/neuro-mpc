# CHOP screen and Note Repeat

The design takes the best workflow ideas from two MPC screens and builds them NEURO's own way:
- **MPC XL:** chopping on a big pinch-zoom waveform, and a touch strip assignable to Note Repeat.
- **MPC Key 37:** Note Repeat, 16 Levels and swing without menus, and dragging and trimming on the waveform.

None of Akai's UI, artwork or code is used.

## CHOP tab
| Control | What it does |
|---|---|
| **Chop mode** | **Transients** (a slice per hit, sensitivity), **Grid** (4–64 equal slices), **Kit** (recognise the drums and put them on pads: A1 Kick, A2 Snare, A3 Closed Hat …), **Manual** (one slice; double-click to chop) |
| **Chop** | Apply the mode |
| **Snap to hits** | *Magnetic chop:* new and dragged markers snap to the nearest hit within 12 px |
| **Slices → pads** / **Kit → pads** | Pads follow the slices (slice 1 = A1…) / one of each drum on bank A in MPC order |
| **16 Levels** | The selected slice on all 16 pads of the current bank. **Velocity:** fixed levels 1/16 … 16/16. **Tune:** −8 … +7 semitones (pad 9 = original) |
| Info line | Slice count; the selected slice's drum and length |

**On the waveform** (any tab):
- **Crosshair:** shows where a chop would land, with its time.
- **Drum colours:** slices are tinted by drum.
- **Keys:**

  | Key | Action |
  |---|---|
  | **Delete / Backspace** | remove the hovered marker (else the selected slice's marker) |
  | **← / →** | nudge the selected slice's start (**Shift**: 1 sample) |
  | **↑ / ↓** | previous / next slice |
  | **Space** | play the selected slice |

## Note Repeat
- **REPEAT** (next to the A–D bank buttons): hold it and play pads, which retrigger at the rate.
- **Lock:** keeps Note Repeat on.
- **Rate box:** **1 bar, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, 1/64T, 1/128**.
- **Gate** (CHOP tab): the length of each hit, 1–100 %.
  - Repeat hits are gated even in One-Shot mode, with a 5 ms release.
  - A short gate on any sample gives stutter and glitch chops.
- **Swing:** 50–75 %, every second hit late.
- **Latch:** pads keep repeating after you let go.
- **Repeat XY pad** (CHOP tab): hold it and you're repeating. Left → right sets the rate (1 bar → 1/128), down → up sets the gate. Slide while playing pads on your controller for trap rolls that speed up and gate sweeps.
- **Timing:**
  - A press plays at once. Later hits lock to the grid: the host's bar grid when the transport runs, otherwise the first press.
  - Note Repeat takes priority over the arpeggiator.
- **MIDI:** all of it is MIDI mappable. Learn **REPEAT** to a momentary button on your controller for a hardware Note Repeat button.

**Tested** (rendered audio):
- 1/16 at 120 BPM hits at exactly 0 / 6000 / 12000 / 18000 samples.
- A 25 % gate chops a One-Shot slice at 1741 samples (1500 + fade).
- 1/128 hits every 750 samples.
- Latch keeps going; Lock off stops cleanly.
- 16 Levels velocity and tune values are exact.
