# Running NEURO MPC with an MPC

NEURO MPC runs in unison with Akai MPC 3 (desktop software, as a plugin) and with MPC hardware (over MIDI clock).

## 1. Inside MPC 3 software (plugin)
1. Install the AU/VST3 (see the README), open MPC 3, and let it rescan plugins (Preferences ▸ Plugins).
2. Create a **Plugin** program and pick **NEURO MPC** (AU or VST3).
3. NEURO follows the MPC's tempo, transport and song position through the host: set **SEQ ▸ Seq: host transport** (or Arp / LFO sync) and press Play on the MPC.

## 2. With MPC hardware or another app (MIDI clock)
Use the **Standalone** app (no host transport) and connect the MPC by USB or DIN MIDI.

| Who leads | MPC setting (Preferences/Settings ▸ Sync / MIDI) | NEURO setting |
|---|---|---|
| **MPC leads** | Send: **MIDI Clock** (+ Song Position) to the port NEURO receives | Options ▸ Audio/MIDI Settings: enable the MPC as MIDI input; **SEQ ▸ Clock in** on (default), **Seq: host transport** |
| **NEURO leads** | Receive: **MIDI Clock** from the port NEURO sends | Audio/MIDI Settings: MIDI output = the MPC; **SEQ ▸ Clock out** on; **Seq: free running** (switching it on sends Start, off sends Stop) |

**What is synchronised:**
- Start / Continue / Stop and the Song Position Pointer.
- Tempo, measured from the clock ticks.
- The position between ticks, which is extrapolated so steps land on the sample rather than snapping to 1/24 beat.

**Tested** (automated tests):
- A Start in the middle of an audio block lands on its exact sample.
- Notes have 0 samples of error at 100 BPM.
- With ±1 ms of tick jitter, as on real USB MIDI, notes stay within 1.5 ms of the leader's beat and steps never repeat.
- When the leader disappears, NEURO stops after 0.5 s.

**Not supported:**
- MPC standalone hardware (MPC OS) does not load third-party plugins, so NEURO cannot run *on* the MPC, only next to it.
- MIDI clock carries no tempo map: NEURO follows tempo changes as the ticks change, with the usual MIDI-clock lag of a few ticks.

## Manual test checklist (MPC 3)
- [ ] NEURO MPC appears in the MPC 3 plugin list (AU and VST3) and opens its editor.
- [ ] Play in MPC 3: SEQ (host transport) starts on the MPC's bar 1 and stays in time for 5 minutes at 90 and 140 BPM.
- [ ] Loop a 2-bar region in MPC 3: NEURO jumps back with it and plays no stray notes.
- [ ] Tempo change in MPC 3 while playing: NEURO follows.
- [ ] Stop: no hanging notes.
- [ ] MPC hardware leads: Standalone with Clock in follows Start / Stop / tempo; Continue from the middle of a song resumes at the right bar.
- [ ] NEURO leads: the MPC follows Start, Stop and tempo from Clock out.
- [ ] Pads (MPK mini or MPC pads as a controller) play NEURO pads; MIDI Learn works on MPC knobs.
