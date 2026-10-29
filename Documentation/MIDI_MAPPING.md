# MIDI Mapping (MIDI Learn) and Hardware Pads

## Every control is mappable
All **182** buttons, sliders and selectors in the editor can be mapped to a MIDI note or CC. A test walks the whole editor and fails if any control lacks a mapping ID. The exceptions are the MIDI Learn and MIDI menu buttons, plus the custom surfaces (waveform, step grid, gesture pad); the on-screen pads are played by notes anyway.

**How to map:**
1. Click **MIDI Learn**. Every mappable control is outlined; mapped ones are green and show their source.
2. Click a control (it turns orange), then move a knob or hit a pad/key on your controller.
3. **Right-click** a control in learn mode to remove its mapping. Click **MIDI Learn** again to finish.

**Behaviour** (mapped messages are *consumed*: a pad mapped to a button does not also play a sound):

| Control | Note | CC |
|---|---|---|
| Host parameter (knob/slider) | sets the value from the velocity | sets the value (0–127 across the range) |
| On/off parameter | toggles | ≥ 64 = on |
| Choice parameter | steps to the next choice | selects by position |
| Pad bank button A–D | selects that bank | ≥ 64 selects it |
| Action button (Reverse, Mutate, Undo, tabs…) | fires | fires when rising past 64 |
| Other slider (pattern length, stretch, step velocity…) | n/a | sets the value (respects the slider's skew) |
| Other selector | steps to the next item | selects by position |

**Where mappings run:**
- **Parameter mappings** run on the audio thread: sample-accurate, working with the editor closed, and recordable as automation.
- **Other controls** run on the message thread (≤ 20 ms) while the editor is open. When it's closed, their notes and CCs are still consumed but do nothing.
- **Saving:** mappings are saved with the project, with the learned learn-channel (or any channel).

## Hardware pads and pad banks
- **"Hardware pads follow the selected bank"** (MIDI menu, on by default): hardware pad *n* plays NEURO pad *n* of the selected bank (A–D), by position, like MPC pads. Map the **A/B/C/D bank buttons** to spare pads or buttons to switch banks from the controller.
- **Pad layout:** NEURO needs to know which notes your pads send.
  - **MIDI ▸ Learn hardware pad layout:** hit your 16 pads in NEURO pad order (bottom-left = 1, left to right, bottom row first). Switch the controller's own pad bank for 9–16. The layout is saved with the project.
  - The default layout is notes 36–51.
- **Pad MIDI channel** (MIDI menu): restrict pad handling to one channel, so keys on another channel never trigger pads.

## Akai MPK mini mk2 (measured from a connected unit, 2026-10-29)
MIDI ▸ **Akai MPK mini mk2 template**:

| Control | Sends (measured) | Mapped to |
|---|---|---|
| Knobs K1–K8 | CC 1–8, channel 1 | Gain, Attack, Release, Transpose, LFO Depth, LFO Rate, Arp Gate, Seq Swing |
| Pads, bank A | notes 36, 32, 37, 33, 38, 34, 39, 35 (ch 1) | hardware pads 1–8 |
| Pads, bank B | notes 48, 44, 49, 45, 50, 46, 51, 47 (ch 1) | hardware pads 9–16 |
| Joystick X / Y+ | pitch bend / CC 1 | pitch bend / (CC 1 = K1) |

The pad order is the order they were hit during the capture. If NEURO pad 1 isn't the pad you expect, run *Learn hardware pad layout* once.

**Note:** this unit's pads and keys both send on **channel 1**, and pad bank B (44–51) overlaps the keyboard's default octave. Either set the pads to channel 10 in Akai's MPK mini editor and choose *Pad MIDI channel ▸ 10*, or shift the keyboard octave.

## Tests
- `neuro_core_tests` `midiMapTests`: binding replacement, multiple sources per target, channel-specific vs any-channel lookup, and the audio-thread table.
- `neuro_engine_tests`, MIDI mapping block:
  - learn by CC (consumed, channel-specific);
  - CC → parameter range; note → toggle / next choice / fixed bank;
  - mapped notes are silent;
  - bank-follow plays the right bank, and the note-off reaches the right pad after a bank change;
  - pad channel filtering; a learned MPK mk2 layout (32–39 / 44–51);
  - the mk2 template knobs;
  - editor coverage (182 mappable, 0 unmappable, unique IDs);
  - a note runs a UI action (Reverse) and a CC drives a non-parameter slider (pattern length 1 ↔ 16);
  - ui-mapped notes are consumed with the editor closed;
  - mappings and pad layout persist.
