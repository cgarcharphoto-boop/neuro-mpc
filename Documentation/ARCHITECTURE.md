# Architecture

```
            message thread (UI, host state, timer)                      audio thread (processBlock)
  ┌──────────────────────────────────────────────────┐        ┌──────────────────────────────────────────────┐
  │ SampleData (decoded file)        ─ RtShared ───────────────▶ sample                                        │
  │ SliceSet (ids, regions, stretch, pitch, root)  ─ RtShared ─▶ slices                                         │
  │ StretchBank  ◀─ BackgroundWorker (Signalsmith renders)     ─▶ stretch/keep-length renders                  │
  │ Analysis     ◀─ BackgroundWorker (YIN pitch, chroma key)  │ │                                              │
  │ PadBank (64 pads)              ─ RtShared ──────────────────▶ PadMap                                       │
  │ ArpProgram (pattern + grammar) ─ RtShared ──────────────────▶ Arpeggiator (held notes, decisions, NoteTracker)│
  │ SeqBank (16 patterns) + undo   ─ RtShared ──────────────────▶ Sequencer (stateless timing, NoteTracker)     │
  │ UI notes                       ─ lock-free FIFO ────────────▶ event list (MIDI + UI + arp + seq, sorted)     │
  │ 46 host parameters (APVTS) ─ atomics ───────────────────────▶ VoiceEngine (32 voices, pads/keys, choke,      │
  └──────────────────────────────────────────────────┘        │   per-sample pitch mod) → gain → output         │
                                                               └──────────────────────────────────────────────┘
```

## Threads and ownership
- **Message thread** owns every editable model object (`SliceSet`, `PadBank`, `ArpProgram`, sequencer patterns, caches). Edits are made there, guarded by `modelLock` (hosts may call `getStateInformation` from elsewhere). Each edit **publishes an immutable copy** through `RtShared<T>`.
- **`RtShared<T>`** (`Core/RtShared.h`) is a lock-free hand-off with a single hazard pointer. The audio thread `acquire()`s once per block; the message thread frees only snapshots that are neither current nor in use. The audio thread never allocates, frees, locks or blocks.
- **Background workers** (`DSP/BackgroundWorker.h`) render time-stretch / keep-length pitch shifts and run pitch/key analysis. Requests coalesce per id, and results are collected by the processor's timer, so this works with the editor closed.
- **Voice safety:** when the sample or a render a voice reads is replaced, that voice is stopped at the block boundary before the old buffer can be freed.

## Block processing (`NeuroMPCProcessor::processBlock`)
1. Acquire snapshots (sample → slices → stretch bank → pads → arp program → sequencer bank).
2. Read host timing (BPM, PPQ, playing). Nothing uses the GUI timer.
3. Collect events into a fixed-capacity list:
   - UI notes (FIFO);
   - incoming MIDI, routed through the arpeggiator when it's on;
   - arpeggiator output;
   - sequencer output;
   - pitch bend and all-notes-off.
4. Insertion-sort events by sample offset (offs before ons), with no allocation.
5. Render between events (`renderWithModulation`: transpose + fine + bend + LFO per sample) and apply each event to the `VoiceEngine`.
6. Apply smoothed output gain.

## Core modules (JUCE-free, unit-tested: `Source/Core`)
| Module | Role |
|---|---|
| `SliceSet` | contiguous slices with stable ids; split/move/merge/grid/restore |
| `TransientDetector` | energy-flux onsets |
| `PitchDetector` / `KeyDetector` | YIN + polyphony check / chroma + Krumhansl–Kessler |
| `Scales` | scale masks, snapping, the 12-note selector maths |
| `PadBank` | 64 pads, follow-slices, layering data |
| `NoteTracker` | sample-scheduled note-offs shared by arp and sequencer |
| `Arpeggiator` | directions, rhythm, grammar, phrase memory, humanise |
| `Sequencer` | lanes, conditions, weaving, stateless timing |
| `PatternTools` | transformations, Pattern Genome, Mutation |
| `RtShared` | lock-free snapshot hand-off |

`Source/DSP` holds `TimeStretch` (Signalsmith renders with silent margins and transient segmentation), `Analysis` and `BackgroundWorker`. `Source/Audio` holds `SampleData` and `VoiceEngine`. `Source/UI` holds the waveform, sequencer grid and gesture pad. The editor (`PluginEditor`) has six tabs: SLICE, PAD, ARP, GEN, SEQ and EVOLVE.

## Determinism
Every random choice is seeded:
- arp random modes, probability, phrase variation and humanise;
- sequencer probability;
- mutation;
- Signalsmith phase randomisation (seeded per region).

Every timing decision is a function of host/grid position, so identical input gives identical output at any block size. This is tested.
