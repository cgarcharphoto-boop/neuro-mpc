# Kit Intelligence: Timbre-Addressed Sequencing, Loop Deconstructor, per-class outputs, Chord-Lock

## Sound fingerprints and drum classes (`Source/Core/Timbre.h`)
Every slice is analysed in the background. The fingerprint has two parts:
- **Spectral** (first 93 ms): brightness, noisiness (flatness), roll-off, energy below 150 Hz / 150 Hz–2 kHz / above 5 kHz, and the dominant 35–400 Hz peak.
- **Envelope** (up to 2 s): attack, decay to −20 dB, and whether the sound is sustained.

This gives:
- **Timbre map position:** x = dark → bright, y = tonal → noisy (the KIT tab map).
- **Drum class:** Kick, Snare, Closed Hat, Open Hat, Tom 1, Tom 2, Floor Tom, Crash, Ride, Perc or Tonal. It is a rule-based classifier, not a trained model. It classifies all 11 synthetic test drums correctly; real kits vary, so every slice has a **class override** (KIT ▸ Class).

## Per-class outputs (mixing)
The plugin has **12 stereo outputs**: Main plus one per class. Each class plays on its own output when the host enables it (Logic: *Multi Output* instrument; Live: route other tracks' input to NEURO's outputs), and otherwise on Main. There is an automatable **level per class** (−60 dB = mute).

## 1. Timbre-Addressed Sequencing
A sequencer step can target:
- the **lane's pad** (default);
- an **exact slice**;
- a **timbre point**, resolved live to the nearest slice of the current kit, and only within the lane's class if one is set.

**KIT ▸ Step → slice / timbre / pad** sets the selected SEQ step; **Lane class** sets the lane's class. **Retarget to timbre** converts every exact-slice step into a timbre point, so the pattern **re-orchestrates itself on any kit**. The Genome gains a **timbre gene**: Mutation moves steps through sound space, and Take genes copies sound targets.

## 2. Loop Deconstructor (KIT ▸ Deconstruct loop)
1. Slices the loop at its hits (sensitivity 0.7 by default; 0.5 missed a snare on a decaying kick tail in testing).
2. Classifies every hit.
3. Writes the selected pattern with **one lane per kit piece**, in class order, named and class-filtered.
4. Each step keeps its exact slice, its velocity relative to the loudest hit, and its microtiming, so the groove is kept. The loop length is the nearest power-of-two number of beats at the host tempo, in 1/16 steps (max 64).

Combined with the per-class outputs, each kit piece arrives on its own mixer track.

## 3. Chord-Lock (KIT ▸ Chord-Lock)
- **What it does:** a held chord retunes **tonal slices** (known root, not a drum) to the nearest chord tone, including fine detune. Drums are never retuned.
- **Chord input:** *non-pad notes* (anything that isn't a hardware pad) or *below split*. Chord notes are consumed (silent) and **latched**: a new chord replaces the old one when all keys were up.
- **Resample mode:** instant; pitch changes length.
- **Keep length mode:** variants −6…+5 st are pre-rendered in the background per tonal slice and played at the original length; the shift is resampled while a variant is pending.

## Measured (tests)
- **Synthetic kit:** 11 of 11 classes correct. A kick/snare/hat/kick file reads as **Kick / Snare / Closed Hat / Kick**, with hats brighter than kicks on the map.
- **Routing:** a kick plays only on the Kick output (main silent); level −60 dB mutes; a disabled output falls back to Main.
- **Deconstructor:** 4 hits → 8-step pattern with Kick (steps 1, 7), Snare (3) and Closed Hat (5). Each plays on its own output, and hit timing matches the source to the sample.
- **Re-orchestration:** after *Retarget to timbre* and loading a different kit (hat/snare/kick/hat), the kick, snare and hat steps play the matching sounds of the new kit on the right outputs.
- **Chord-Lock** (A3 tone, chord C–E–G): Resample → G3 (55.00) with length ×2^(2/12); Keep length → G3 (55.03) at the original 96000 samples. A chord containing A leaves it unchanged; a slice classed as a drum is never retuned.

## Field test (`Tests/FieldTest.cpp`, run manually; 2026-10-30)
**Separate outputs through the real plugin APIs.** The *installed* VST3 and AU were loaded with JUCE's hosting (the APIs Live and Logic use). Both expose 12 named outputs, accept all 24 channels, and take state in their host's format (VST3 `IComponent`, AU ClassInfo). Each kit piece arrived **only** on its own output: Kick 0.665, Snare 0.676, Closed Hat 0.685; Main and the others were silent. *Live and Logic themselves were not driven, so their routing UI is still untested.*

**Real recordings: six Apple Logic "Drummer" loops** (acoustic, full kit, 8 bars):
- **Tempo** (audio autocorrelation, file tempo tag when present, whole-bar disambiguation): **6/6 correct** (75, 85, 75, 120, 100, 110 BPM).
- **Grid:** the loop's own 1/16 grid, first 4 bars. Four-on-the-floor kicks and offbeat hats come out on the right steps.
- **Per-hit classes in a full mix** (band-energy rise at each onset):
  - *mixed* results. Snares on beats 2/4: Rose 83%, Tyrell 50%, Kyle 50%.
  - On Mason, Duncan and Gavin, most backbeat snares were read as kicks: the hats already ringing in the 1–5 kHz band mask the snare crack.
  - Separating drums *inside* a mixed recording is a known hard problem (automatic drum transcription; state of the art uses trained neural networks). Use the per-slice class override, or isolated one-shots, which classify far better.

## Separate drums (NMF), for full-kit recordings (`Source/Core/DrumNMF.h`)
- **Method:** semi-adaptive NMF after Wu & Lerch (ISMIR 2015). The magnitude spectrogram (96 log bands, 2048/512 STFT) is factorised with **kick, snare and hi-hat/cymbal** templates, adapted to the loop but kept ≥ 70% anchored to their initial shapes, plus 2 free components.
- **Hits:** onsets come from the log-rise of each activation, so a hit on top of another drum's tail still registers. Hits are refined to the sample on the attack. Simultaneous hits count for **each** drum. A co-occurrence rule removes kick↔snare crosstalk, and the cymbal ring length separates closed hat / open hat / ride / crash.
- **Stems:** Wiener-style soft masks give **Kick / Snare / Cymbals / Residual** stems that **sum back to the original** (109 dB SNR in tests; 108 dB through the 24-bit stems file in `~/Music/NEURO MPC/Separated/`).
- **Pattern:** KIT ▸ **Separate drums** loads the stems file, slices each stem at its own hits, and builds a pattern whose lanes play **isolated** drums on their own outputs. The residual (room, toms, anything unmodelled) plays bar by bar on the Perc lane.
- **No tom component:** in testing it captured the attacks of kicks and snares, so toms fall into the nearest drum or the residual.

**Synthetic mixed loop** (kick/snare + hats playing together): kick 100/100%, snare 100/100%, hats 100% precision / 88% recall (precision / recall); exact timing.

**Real Apple Drummer loops** (no hand labels; *groove check* = % of kicks on beats 1/3 and % of snares on 2/4):

| Loop | Kick 1/3 | Snare 2/4 | Notes |
|---|---|---|---|
| Rose | 67% | **100%** | clean 8th-note hats |
| Duncan | 50% | 31% | kick is **four-on-the-floor** (every beat: correct, but the metric only counts 1/3); 8th hats |
| Tyrell | 40% | 44% | |
| Kyle | 47% | 31% | |
| Mason | 40% | 17% | busy ghost notes → snare crosstalk |
| Gavin | 0% | 5% | snares fall on beat 3 (likely a **half-time** groove, where the metric doesn't apply) |

### Teach the kit
Generic drum sounds can't fit every kit. **Teach kick / Teach snare / Teach hat/cym** use the selected slice (one clear hit of that drum) as an example. Several examples are averaged, and **Clear taught** forgets them. The examples belong to the loaded loop: they survive reloading it, and are ignored on any other file. All four buttons are MIDI mappable.
- Each example's sound is learned while the other drums (generic, plus any already taught) and a broadband "room" template explain everything else sounding at that moment. So a kick example with a crash on top, or a snare example with a kick under it, still learns just that drum. Each drum is also limited to a soft frequency range (kick lows, snare mids, cymbal highs), and taught sounds stay ≥ 50% anchored to the example during separation.
- **Unusual synthetic kit** (a 220 Hz tom-like kick the generic kick sound misreads): kick F1 goes from 0.80 to 0.89, with snare and hats unchanged at 1.00.
- **Real loops, with examples picked blindly** (bar 1: beat 1 = kick, beat 2 = snare, the "and" of 1 = hat, crash/kick overlaps included): Duncan's snare goes from 31% to **100%** on 2/4, and Kyle's kick drops slightly (47% → 42%). Rose, Mason, Tyrell and Gavin are unchanged. Hand-picked clean hits should do at least as well.

The groove check is only a proxy. A real accuracy figure needs a hand-labelled set, e.g. IDMT-SMT-Drums or ENST-Drums, used for evaluation only. For one-shot kits, the per-slice classifier remains more reliable.

## Limitations
- The classifier is heuristic and tuned on synthetic drums; expect overrides on real kits, e.g. claps/rims vs snares, or toms vs tonal bass hits.
- Loop length assumes a power-of-two number of beats at the host tempo.
- Chord-Lock keep-length renders cost CPU and memory (11 per tonal slice).
