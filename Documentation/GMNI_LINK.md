# NEURO MPC + GW-1 + KAOSS NEXUS: running in unison

The three plug-ins work as one rig:
- **NEURO MPC** plays the beat and can play **GW-1**'s notes from its sequencer.
- **KAOSS NEXUS** effects the audio, and its XY pad can move GW-1 and NEURO controls.
- All three share **one tempo and one transport**.

| Link | How |
|---|---|
| Tempo + transport | In a DAW: all three follow the DAW. Without a DAW: NEURO sends **MIDI clock** on its **NEURO MPC** port; GW-1 (v2.5+) and KAOSS NEXUS (v1.2+) follow it |
| NEURO → GW-1 notes | A NEURO sequencer lane set to **Out: MIDI ch N** sends its notes to the plugin's MIDI output and the **NEURO MPC** port |
| KAOSS → GW-1 / NEURO | KAOSS **MIDI Out** sends the pad as **CC20 (X) / CC21 (Y)** on its **KAOSS NEXUS** port. GW-1 macros 1 and 2 already listen to CC20/21; in NEURO, use MIDI Learn |

## A. Ableton Live (recommended: sample-accurate notes)
1. Track 1: drag in **NEURO MPC**. On its effects, drag **KAOSS NEXUS** after it.
2. Track 2 (MIDI): drag in **GW-1**. In its **MIDI From** box choose **1-NEURO MPC**, then the sub-box **NEURO MPC**. Set **Monitor** to **In**.
3. In NEURO: **SEQ** tab ▸ click a lane (for example lane 9) ▸ **Lane note** = the note to play ▸ **Out: MIDI ch 1**. Turn on some steps.
4. **SEQ ▸ Seq: host transport**. Press Play in Live: NEURO's drums and GW-1's line play together.
5. (Optional) KAOSS ▸ turn on **MIDI Out**. On GW-1's track, **MIDI From = KAOSS NEXUS** instead, or in addition through a second MIDI track. Moving the pad moves GW-1's macros 1–2.

## B. Logic Pro
Logic instruments can't pass MIDI to another track, so use the virtual port:
1. Track 1: **NEURO MPC** (instrument). Audio FX slot: **KAOSS NEXUS**.
2. Track 2: **GW-1** (instrument). Select this track (Logic sends live MIDI to the selected or record-armed track).
3. NEURO ▸ **SEQ** ▸ switch **Link port** on. Set a lane to **Out: MIDI ch 1** and turn on some steps.
4. Press Play. NEURO's lane notes arrive at GW-1 through the **NEURO MPC** port. Timing is within about one audio buffer; for sample-exact timing, use Live or record the notes onto GW-1's track.

## C. No DAW (the three standalone apps)
1. Open **NEURO MPC.app**. **SEQ** ▸ **Link port** on and **Clock out** on, **Seq: free running**.
2. Open **GW-1.app** ▸ Options ▸ Audio/MIDI Settings ▸ tick **NEURO MPC** under MIDI inputs.
3. Open **KAOSS NEXUS.app** ▸ Options ▸ Audio/MIDI Settings ▸ tick **NEURO MPC** under MIDI inputs. It processes its audio input; route NEURO into it with a loopback device if you want NEURO through it.
4. Turning NEURO's sequencer on sends **Start**. GW-1's arp, sequences and LFOs and KAOSS's synced delay, orbit and gestures run at NEURO's tempo. Turning it off sends **Stop**.

## What is tested (automated)
- **NEURO lane → MIDI:** note-ons land on the exact step sample (0 / 24000 / 48000 at 120 BPM) on the lane's channel, and MIDI-only lanes are silent in the sampler.
- **The NEURO MPC port:** received by a separate MIDI input in real time, with 3 of 3 notes and 52 clock ticks.
- **Clock follow:** GW-1 locks to 100.00 BPM with 0.0000 beats of position error and stops on Stop. KAOSS locks to 100.00 BPM. NEURO is sample-exact, and within 1.5 ms with ±1 ms of jitter.

**Still to check by hand:** the setups in sections A–C on your Mac, by ear.
