#include "PluginProcessor.h"
#include <juce_cryptography/juce_cryptography.h>
#include "PluginEditor.h"
#include "Core/Scales.h"
#include "Core/TransientDetector.h"
#include "Core/DrumNMF.h"

namespace
{
    constexpr int64_t maxSampleFloats = 200'000'000; // ~800 MB: documented memory limit for one sample
}

NeuroMPCProcessor::NeuroMPCProcessor()
    : AudioProcessor ([] {
          // Main output + one optional stereo output per drum class, for mixing each kit piece on its own DAW track.
          auto buses = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
          for (int c = 0; c < neuro::numDrumClasses; ++c)
              buses = buses.withOutput (neuro::drumClassName ((neuro::DrumClass) c), juce::AudioChannelSet::stereo(), false);
          return buses; }()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();
    gainParam    = apvts.getRawParameterValue (ParamIDs::gain);
    attackParam  = apvts.getRawParameterValue (ParamIDs::attack);
    releaseParam = apvts.getRawParameterValue (ParamIDs::release);
    modeParam    = apvts.getRawParameterValue (ParamIDs::playMode);
    transposeParam = apvts.getRawParameterValue (ParamIDs::transpose);
    fineParam      = apvts.getRawParameterValue (ParamIDs::fineTune);
    bendRangeParam = apvts.getRawParameterValue (ParamIDs::bendRange);
    lfoDepthParam  = apvts.getRawParameterValue (ParamIDs::lfoDepth);
    lfoRateParam   = apvts.getRawParameterValue (ParamIDs::lfoRate);
    lfoShapeParam  = apvts.getRawParameterValue (ParamIDs::lfoShape);
    lfoSyncParam   = apvts.getRawParameterValue (ParamIDs::lfoSync);
    lfoPhaseParam  = apvts.getRawParameterValue (ParamIDs::lfoPhase);
    keyModeParam   = apvts.getRawParameterValue (ParamIDs::keyMode);
    scaleParam     = apvts.getRawParameterValue (ParamIDs::scale);
    scaleRootParam = apvts.getRawParameterValue (ParamIDs::scaleRoot);
    arpOnParam        = apvts.getRawParameterValue (ParamIDs::arpOn);
    arpLatchParam     = apvts.getRawParameterValue (ParamIDs::arpLatch);
    arpDirectionParam = apvts.getRawParameterValue (ParamIDs::arpDirection);
    arpRateParam      = apvts.getRawParameterValue (ParamIDs::arpRate);
    arpGateParam      = apvts.getRawParameterValue (ParamIDs::arpGate);
    arpOctavesParam   = apvts.getRawParameterValue (ParamIDs::arpOctaves);
    arpSwingParam     = apvts.getRawParameterValue (ParamIDs::arpSwing);
    arpChanceParam    = apvts.getRawParameterValue (ParamIDs::arpChance);
    arpRepeatsParam   = apvts.getRawParameterValue (ParamIDs::arpRepeats);
    arpVelocityParam  = apvts.getRawParameterValue (ParamIDs::arpVelocity);
    arpRetriggerParam = apvts.getRawParameterValue (ParamIDs::arpRetrigger);
    arpSeedParam      = apvts.getRawParameterValue (ParamIDs::arpSeed);
    arpAccentParam     = apvts.getRawParameterValue (ParamIDs::arpAccent);
    arpPhraseParam     = apvts.getRawParameterValue (ParamIDs::arpPhrase);
    arpVariationParam  = apvts.getRawParameterValue (ParamIDs::arpVariation);
    arpVaryPitchParam  = apvts.getRawParameterValue (ParamIDs::arpVaryPitch);
    arpVaryVelParam    = apvts.getRawParameterValue (ParamIDs::arpVaryVel);
    arpVaryRhythmParam = apvts.getRawParameterValue (ParamIDs::arpVaryRhythm);
    arpReturnParam     = apvts.getRawParameterValue (ParamIDs::arpReturn);
    arpHumanTimeParam  = apvts.getRawParameterValue (ParamIDs::arpHumanTime);
    arpHumanVelParam   = apvts.getRawParameterValue (ParamIDs::arpHumanVel);
    rtArpProgram.publish (std::make_shared<const neuro::ArpProgram> (arpProgram));
    seqModeParam    = apvts.getRawParameterValue (ParamIDs::seqMode);
    seqPatternParam = apvts.getRawParameterValue (ParamIDs::seqPattern);
    seqRateParam    = apvts.getRawParameterValue (ParamIDs::seqRate);
    seqSwingParam   = apvts.getRawParameterValue (ParamIDs::seqSwing);
    seqFillParam           = apvts.getRawParameterValue (ParamIDs::seqFill);
    weaveAccentParam       = apvts.getRawParameterValue (ParamIDs::weaveAccent);
    weaveAccentAmountParam = apvts.getRawParameterValue (ParamIDs::weaveAccentAmount);
    weaveFillParam         = apvts.getRawParameterValue (ParamIDs::weaveFill);
    weaveFillEveryParam    = apvts.getRawParameterValue (ParamIDs::weaveFillEvery);
    weavePitchParam        = apvts.getRawParameterValue (ParamIDs::weavePitch);
    padBankParam       = apvts.getRawParameterValue (ParamIDs::padBank);
    padBankFollowParam = apvts.getRawParameterValue (ParamIDs::padBankFollow);
    padChannelParam    = apvts.getRawParameterValue (ParamIDs::padChannel);

    for (int c = 0; c < neuro::numDrumClasses; ++c)
        classLevelParams[(size_t) c] = apvts.getRawParameterValue (ParamIDs::classLevel[c]);

    chordLockParam  = apvts.getRawParameterValue (ParamIDs::chordLock);
    chordInputParam = apvts.getRawParameterValue (ParamIDs::chordInput);
    chordSplitParam = apvts.getRawParameterValue (ParamIDs::chordSplit);
    clockInParam    = apvts.getRawParameterValue (ParamIDs::clockIn);
    clockOutParam   = apvts.getRawParameterValue (ParamIDs::clockOut);
    padNoteHeld.fill (-1);

    for (int i = 0; i < neuro::padsPerBank; ++i)
        hardwarePadNotes[(size_t) i] = neuro::PadBank::baseNote + i; // generic 16-pad layout: notes 36-51

    publishHardwarePads();
    publishMidiRouting();
    rtSeq.publish (std::make_shared<const SeqBank> (seqPatterns));
    publishPads();
    startTimerHz (50); // background results, MIDI-learn and mapped UI actions
}

NeuroMPCProcessor::~NeuroMPCProcessor()
{
    stopTimer();
    cancelAi();
    modelDownload.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout NeuroMPCProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout {
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::gain, 1 }, "Gain",
                                               NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("dB")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::attack, 1 }, "Attack",
                                               NormalisableRange<float> (0.0f, 500.0f, 0.1f, 0.4f), 1.0f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::release, 1 }, "Release",
                                               NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.4f), 30.0f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::playMode, 1 }, "Play Mode",
                                                StringArray { "One-Shot", "Gate" }, 0),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::transpose, 1 }, "Transpose",
                                               NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("st")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::fineTune, 1 }, "Fine Tune",
                                               NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("c")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::bendRange, 1 }, "Bend Range",
                                               NormalisableRange<float> (0.0f, 24.0f, 1.0f), 2.0f,
                                               AudioParameterFloatAttributes().withLabel ("st")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::lfoDepth, 1 }, "Pitch LFO Depth",
                                               NormalisableRange<float> (0.0f, 12.0f, 0.01f, 0.5f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("st")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::lfoRate, 1 }, "Pitch LFO Rate",
                                               NormalisableRange<float> (0.01f, 20.0f, 0.01f, 0.4f), 5.0f,
                                               AudioParameterFloatAttributes().withLabel ("Hz")),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::lfoShape, 1 }, "Pitch LFO Shape",
                                                StringArray { "Sine", "Triangle", "Saw", "Square", "Sample & Hold" }, 0),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::lfoSync, 1 }, "Pitch LFO Sync",
                                                StringArray { "Free", "1 bar", "1/2", "1/4", "1/8", "1/16" }, 0),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::lfoPhase, 1 }, "Pitch LFO Phase",
                                               NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("deg")),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::keyMode, 1 }, "Key Mode",
                                                StringArray { "Pads", "Keys" }, 0),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scale, 1 }, "Scale", [] {
                                                    StringArray names;
                                                    for (const auto& sc : neuro::scales) names.add (sc.name);
                                                    return names; }(), 0),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scaleRoot, 1 }, "Scale Root",
                                                StringArray { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::arpOn, 1 }, "Arp On", false),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::arpLatch, 1 }, "Arp Latch", false),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::arpDirection, 1 }, "Arp Direction",
                                                StringArray { "Up", "Down", "Up-Down", "Down-Up", "As played", "Reverse played",
                                                              "Random", "Random (no repeat)", "Chord", "Octave cycle" }, 0),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::arpRate, 1 }, "Arp Rate",
                                                StringArray { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 6),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpGate, 1 }, "Arp Gate",
                                               NormalisableRange<float> (5.0f, 100.0f, 1.0f), 50.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::arpOctaves, 1 }, "Arp Octaves", 1, 4, 1),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpSwing, 1 }, "Arp Swing",
                                               NormalisableRange<float> (50.0f, 75.0f, 0.5f), 50.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpChance, 1 }, "Arp Probability",
                                               NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::arpRepeats, 1 }, "Arp Repeats", 1, 4, 1),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::arpVelocity, 1 }, "Arp Velocity", 0, 127, 0,
                                             AudioParameterIntAttributes().withStringFromValueFunction (
                                                 [] (int v, int) { return v == 0 ? String ("as played") : String (v); })),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::arpRetrigger, 1 }, "Arp Retrigger",
                                                StringArray { "Off", "First note", "Every note" }, 1),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::arpSeed, 1 }, "Arp Seed", 1, 9999, 1),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpAccent, 1 }, "Arp Accent",
                                               NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::arpPhrase, 1 }, "Arp Phrase Length", 0, 64, 0,
                                             AudioParameterIntAttributes().withStringFromValueFunction (
                                                 [] (int v, int) { return v == 0 ? String ("off") : String (v); })),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpVariation, 1 }, "Arp Variation",
                                               NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::arpVaryPitch, 1 }, "Arp Vary Pitch", true),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::arpVaryVel, 1 }, "Arp Vary Velocity", true),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::arpVaryRhythm, 1 }, "Arp Vary Rhythm", false),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::arpReturn, 1 }, "Arp Return Every", 0, 16, 0,
                                             AudioParameterIntAttributes().withStringFromValueFunction (
                                                 [] (int v, int) { return v == 0 ? String ("never") : String (v); })),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpHumanTime, 1 }, "Arp Humanize Time",
                                               NormalisableRange<float> (0.0f, 30.0f, 0.1f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arpHumanVel, 1 }, "Arp Humanize Velocity",
                                               NormalisableRange<float> (0.0f, 50.0f, 1.0f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::seqMode, 1 }, "Sequencer",
                                                StringArray { "Off", "Host transport", "Free running" }, 0),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::seqPattern, 1 }, "Sequencer Pattern", 1, numSeqPatterns, 1),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::seqRate, 1 }, "Sequencer Rate",
                                                StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::seqSwing, 1 }, "Sequencer Swing",
                                               NormalisableRange<float> (50.0f, 75.0f, 0.5f), 50.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::seqFill, 1 }, "Sequencer Fill", false),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::weaveAccent, 1 }, "Weave Accents From", 0, numSeqPatterns, 0,
                                             AudioParameterIntAttributes().withStringFromValueFunction (
                                                 [] (int v, int) { return v == 0 ? String ("none") : "pattern " + String (v); })),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::weaveAccentAmount, 1 }, "Weave Accent Amount",
                                               NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::weaveFill, 1 }, "Weave Fills From", 0, numSeqPatterns, 0,
                                             AudioParameterIntAttributes().withStringFromValueFunction (
                                                 [] (int v, int) { return v == 0 ? String ("none") : "pattern " + String (v); })),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::weaveFillEvery, 1 }, "Weave Fill Every (bars)", 1, 16, 4),
        std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::weavePitch, 1 }, "Weave Pitch From", 0, numSeqPatterns, 0,
                                             AudioParameterIntAttributes().withStringFromValueFunction (
                                                 [] (int v, int) { return v == 0 ? String ("none") : "pattern " + String (v); })),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::padBank, 1 }, "Pad Bank", StringArray { "A", "B", "C", "D" }, 0),
        std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::padBankFollow, 1 }, "Pads Follow Bank", true),
        std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::padChannel, 1 }, "Pad MIDI Channel", [] {
                                                    StringArray names { "Any" };
                                                    for (int c = 1; c <= 16; ++c) names.add (String (c));
                                                    return names; }(), 0)
    };

    for (int c = 0; c < neuro::numDrumClasses; ++c)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::classLevel[c], 1 },
                                                           String (neuro::drumClassName ((neuro::DrumClass) c)) + " Level",
                                                           NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::chordLock, 1 }, "Chord-Lock",
                                                        StringArray { "Off", "Resample", "Keep length" }, 0),
                std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::chordInput, 1 }, "Chord-Lock Input",
                                                        StringArray { "Non-pad notes", "Below split" }, 0),
                std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::chordSplit, 1 }, "Chord-Lock Split", 0, 127, 60,
                                                     AudioParameterIntAttributes().withStringFromValueFunction (
                                                         [] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })),
                // Run in unison with an MPC: follow its MIDI clock when the host transport is not playing (e.g.
                // standalone), and/or send clock (24 PPQN + Start/Stop) from NEURO's transport.
                std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::clockIn, 1 }, "Follow MIDI Clock", true),
                std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::clockOut, 1 }, "Send MIDI Clock", false));
    return layout;
}

bool NeuroMPCProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i) // class outputs: off or stereo
        if (! layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;

    return true;
}

void NeuroMPCProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    hostSampleRate = sampleRate;
    clockFollower.prepare (sampleRate);
    clockLeader = {};
    blockStartTime = 0;
    lastClockBeatEnd = -1.0e9;
    modBuffer.assign ((size_t) juce::jmax (1024, samplesPerBlock), 0.0f);
    bendSemitones = 0.0f;
    arp = {};
    arpWasOn = arpWasPlaying = false;
    freeBeatOrigin = 0.0;
    sequencer = {};
    seqWasRunning = false;
    seqFreeOrigin = 0.0;
    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
}

void NeuroMPCProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto* s  = rtSample.acquire();
    const auto* sl = rtSlices.acquire();
    const auto* bank = rtStretch.acquire(); // after slices: a bank is never older than the slices it serves
    const auto* padMap = rtPads.acquire();

    if (s != lastSample)
    {
        engine.killAll(); // voices must never outlive the buffer they read
        lastSample = s;
    }

    if (bank != lastBank)
    {
        engine.dropVoicesNotIn (bank, s != nullptr ? &s->buffer : nullptr);
        lastBank = bank;
    }

    neuro::VoiceParams vp;
    vp.attackMs = attackParam->load();
    vp.releaseMs = releaseParam->load();
    vp.oneShot = modeParam->load() < 0.5f;
    vp.keyboardMode = keyModeParam->load() > 0.5f;
    vp.keyboardSlice = keyboardSliceIndex.load();
    vp.keyboardRoot = keyboardRootNote.load();
    vp.scaleMask = neuro::scales[(size_t) juce::jlimit (0, (int) neuro::scales.size() - 1, (int) scaleParam->load())].mask;
    vp.scaleRoot = (int) scaleRootParam->load();
    vp.chordMode = (int) chordLockParam->load();

    // Host timing, shared by the pitch LFO and the arpeggiator. Never derived from the GUI.
    const auto numSamples = buffer.getNumSamples();
    auto bpm = 120.0;
    std::optional<double> hostPpq; // set while the host transport plays

    if (auto* ph = getPlayHead())
    {
        if (auto position = ph->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);

            if (position->getIsPlaying())
                if (const auto ppq = position->getPpqPosition())
                    hostPpq = *ppq;
        }
    }

    // MIDI clock in (MPC as master): all clock messages of this block first, then the position at the block start is
    // extrapolated from the latest tick, so a Start in the middle of the block lands on its exact sample.
    for (const auto meta : midi)
        if (const auto* raw = meta.data; meta.numBytes >= 1 && (raw[0] == 0xf8 || raw[0] == 0xfa || raw[0] == 0xfb || raw[0] == 0xfc || raw[0] == 0xf2))
            clockFollower.handle (blockStartTime + meta.samplePosition, raw[0], raw[0] == 0xf2 && meta.numBytes >= 3 ? raw[1] | (raw[2] << 7) : 0);

    if (! hostPpq && clockInParam->load() > 0.5f && clockFollower.isRunning (blockStartTime + numSamples))
    {
        // Each block covers [start, end) of the clock's timeline. A block that ran ahead (tempo still being measured,
        // jitter) is followed by a slightly slower one: the position slews back onto the clock, never backwards (no
        // repeated steps). A big difference is a relocation (Start, Song Position): jump.
        const auto trueStart = clockFollower.ppqAt (blockStartTime);
        const auto trueEnd = clockFollower.ppqAt (blockStartTime + numSamples);
        const auto start = std::abs (trueStart - lastClockBeatEnd) < 0.25 ? lastClockBeatEnd : trueStart;
        const auto end = juce::jmax (start, trueEnd);
        hostPpq = start;
        bpm = juce::jmax (1.0, (end - start) / numSamples * 60.0 * hostSampleRate); // effective tempo of this block
        lastClockBeatEnd = end;
    }

    const auto beatsPerSample = bpm / 60.0 / hostSampleRate;
    lastHostBpm.store (bpm);

    // Pitch LFO rate: free-running, or locked to the host tempo and beat position.
    lfoIncrement = lfoRateParam->load() / hostSampleRate;

    if (const auto sync = (int) lfoSyncParam->load(); sync > 0)
    {
        static constexpr double beatsPerCycle[] { 1.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
        const auto beats = beatsPerCycle[juce::jlimit (1, 5, sync)];

        if (hostPpq)
            lfoCyclePos = std::fmod (*hostPpq / beats, 1.0);

        lfoIncrement = bpm / 60.0 / beats / hostSampleRate;
    }

    // ---- Collect this block's events (incoming MIDI, UI, arpeggiator) ----
    size_t numEvents = 0;
    auto push = [&] (int offset, Event::Type type, int note, float value, int slice = -1)
    {
        if (numEvents < events.size())
            events[numEvents++] = { juce::jlimit (0, juce::jmax (0, numSamples - 1), offset), type, note, value, slice, 0.0f };
    };

    auto arpSink = [&] (int offset, int note, float velocity)
    {
        push (offset, velocity > 0.0f ? Event::noteOn : Event::noteOff, note, velocity);

        if (velocity > 0.0f)
            arpLastNote.store (note);
    };

    const auto arpOn = arpOnParam->load() > 0.5f;
    arp.setProgram (rtArpProgram.acquire());
    updateArpSettings (bpm);

    if (arpWasOn && ! arpOn)
        arp.flush (0, arpSink, true);          // switching off: release everything the arp started

    if (arpWasPlaying && ! hostPpq)
    {
        arp.flush (0, arpSink, false);         // transport stopped: end generated notes, keep held keys
        freeBeatOrigin = 0.0;
    }

    arpWasOn = arpOn;
    arpWasPlaying = hostPpq.has_value();

    auto beatAt = [&] (int offset) { return (hostPpq ? *hostPpq : freeBeatOrigin) + offset * beatsPerSample; };
    int arpPos = 0;

    auto noteInput = [&] (int offset, int note, float velocity)
    {
        if (! arpOn)
        {
            push (offset, velocity > 0.0f ? Event::noteOn : Event::noteOff, note, velocity);
            return;
        }

        arp.process (arpPos, offset, beatAt (arpPos), beatsPerSample, arpSink);
        arpPos = offset;

        if (velocity <= 0.0f)
            arp.noteOff (note);
        else if (arp.noteOn (note, velocity) && ! hostPpq)
            freeBeatOrigin = -offset * beatsPerSample; // free-running: the first step lands on this key press
    };

    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        auto handleUi = [&] (const UiNote& u)
        {
            if (u.slice >= 0 || (u.velocity <= 0.0f && u.note >= sliceAuditionTag))
                push (0, u.velocity > 0.0f ? Event::noteOn : Event::noteOff, u.note, u.velocity, u.slice); // direct audition
            else
                noteInput (0, u.note, u.velocity);
        };

        for (int i = 0; i < scope.blockSize1; ++i) handleUi (uiNotes[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) handleUi (uiNotes[(size_t) (scope.startIndex2 + i)]);
    }

    // Hardware pads: hardware pad n (learned note layout) plays NEURO pad n of the selected bank, by position.
    const auto padBankIndex = juce::jlimit (0, neuro::numBanks - 1, (int) padBankParam->load());
    const auto followBank = padBankFollowParam->load() > 0.5f && ! vp.keyboardMode && padMap != nullptr;
    const auto* hardwarePads = rtHardwarePads.acquire();
    const auto padChannel = (int) padChannelParam->load(); // 0 = any

    auto translatePadNote = [&] (int note, int channel, bool on)
    {
        if (! on)
        {
            const auto held = std::exchange (padNoteHeld[(size_t) note], -1);
            return held >= 0 ? held : note;
        }

        const auto pad = hardwarePads != nullptr ? hardwarePads->indexOfNote[(size_t) note] : (int8_t) -1;

        if (! followBank || pad < 0 || (padChannel != 0 && channel != padChannel))
            return note;

        const auto translated = (*padMap)[(size_t) (padBankIndex * neuro::padsPerBank + pad)].note;
        padNoteHeld[(size_t) note] = translated;
        return translated;
    };

    for (const auto meta : midi)
    {
        const auto t = juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition);
        const auto msg = meta.getMessage();

        if (msg.isMidiClock() || msg.isMidiStart() || msg.isMidiContinue() || msg.isMidiStop() || msg.isSongPositionPointer())
            continue; // transport, handled above

        if (handleMappedMidi (msg))
            continue; // MIDI Learn: learned or mapped messages do not play

        // Chord-Lock input: chord notes are consumed and latched (a new chord replaces the last one).
        if (vp.chordMode != 0 && msg.isNoteOnOrOff())
        {
            const auto note = msg.getNoteNumber();
            const auto isChordNote = (int) chordInputParam->load() == 1
                                         ? note < (int) chordSplitParam->load()
                                         : (hardwarePads == nullptr || hardwarePads->indexOfNote[(size_t) note] < 0);

            if (isChordNote)
            {
                if (msg.isNoteOn())
                {
                    if (chordKeysDown++ == 0)
                        chordMask = 0;

                    chordMask = (uint16_t) (chordMask | (1u << (note % 12)));
                }
                else
                {
                    chordKeysDown = juce::jmax (0, chordKeysDown - 1);
                }

                chordMaskAtomic.store (chordMask);
                vp.chordMask = chordMask;
                continue;
            }
        }

        if (msg.isNoteOn())
        {
            lastMidiNote.store (msg.getNoteNumber());
            midiNoteCount.fetch_add (1);
            noteInput (t, translatePadNote (msg.getNoteNumber(), msg.getChannel(), true), msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            noteInput (t, translatePadNote (msg.getNoteNumber(), msg.getChannel(), false), 0.0f);
        }
        else if (msg.isPitchWheel())
        {
            push (t, Event::control, -1, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            if (arpOn)
            {
                arp.process (arpPos, t, beatAt (arpPos), beatsPerSample, arpSink);
                arpPos = t;
            }

            arp.flush (t, arpSink, true);
            push (t, Event::control, -2, 0.0f);
        }
    }

    vp.chordMask = vp.chordMode != 0 ? chordMask : (uint16_t) 0;

    if (arpOn)
        arp.process (arpPos, numSamples, beatAt (arpPos), beatsPerSample, arpSink);

    arp.endBlock (numSamples);
    arpHeldCount.store (arp.heldCount());

    // ---- Step sequencer: Host = follows the host transport (PPQ); Free = own clock from bar 1 when switched on ----
    {
        const auto mode = (int) seqModeParam->load();
        const auto running = mode == 1 ? hostPpq.has_value() : mode == 2;
        auto seqSink = [&] (int offset, int note, float velocity, float pitch, const neuro::SeqTarget& target)
        {
            // Resolve sound targets against the current kit: exact slice, or nearest slice on the timbre map.
            auto slice = -1;

            if (velocity > 0.0f && sl != nullptr)
                slice = target.sliceId > 0 ? sl->indexOfId (target.sliceId)
                      : target.x >= 0.0f   ? sl->nearestTimbre (target.x, target.y, target.drumClass)
                                           : -1;

            push (offset, velocity > 0.0f ? Event::noteOn : Event::noteOff, note, velocity, slice);

            if (numEvents > 0)
                events[numEvents - 1].pitch = pitch;
        };

        if (seqWasRunning && ! running)
            sequencer.flush (0, seqSink);

        if (running && ! seqWasRunning && mode == 2)
            seqFreeOrigin = 0.0;

        seqWasRunning = running;
        seqPlayStep.store (-1);

        if (running)
        {
            static constexpr double stepBeats[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
            const auto* bankOfPatterns = rtSeq.acquire();
            const auto beat = mode == 1 ? *hostPpq : seqFreeOrigin;
            neuro::SeqTiming timing;
            timing.stepBeats = stepBeats[juce::jlimit (0, 5, (int) seqRateParam->load())];
            timing.swing = seqSwingParam->load() / 100.0f;
            timing.seed = (uint32_t) arpSeedParam->load();
            timing.fill = seqFillParam->load() > 0.5f;
            neuro::SeqWeave weave;
            weave.accentSource = (int) weaveAccentParam->load() - 1;
            weave.accentAmount = weaveAccentAmountParam->load() / 100.0f;
            weave.fillSource = (int) weaveFillParam->load() - 1;
            weave.fillEvery = (int) weaveFillEveryParam->load();
            weave.pitchSource = (int) weavePitchParam->load() - 1;
            sequencer.process (bankOfPatterns->data(), numSeqPatterns, (int) seqPatternParam->load() - 1, weave, timing,
                               0, numSamples, beat, beatsPerSample, seqSink);
            seqPlayStep.store (neuro::Sequencer::stepAt (beat, timing.stepBeats));

            if (mode == 2)
                seqFreeOrigin += numSamples * beatsPerSample;
        }

        sequencer.endBlock (numSamples);
    }

    if (! hostPpq)
        freeBeatOrigin += numSamples * beatsPerSample;

    // Stable insertion sort by time; at equal times note-offs and controls precede note-ons. No allocation.
    for (size_t i = 1; i < numEvents; ++i)
    {
        const auto e = events[i];
        auto j = i;

        for (; j > 0 && (events[j - 1].offset > e.offset || (events[j - 1].offset == e.offset && events[j - 1].type > e.type)); --j)
            events[j] = events[j - 1];

        events[j] = e;
    }

    // ---- Output routing: each drum class renders to its own bus when the host enabled it, else to the main bus ----
    neuro::OutputBuses outs;
    auto mainBus = getBusBuffer (buffer, false, 0);
    std::array<juce::AudioBuffer<float>, neuro::numDrumClasses> classBuses; // alias host channels; no allocation
    outs.main = &mainBus;

    for (int c = 0; c < neuro::numDrumClasses; ++c)
    {
        if (auto* bus = getBus (false, c + 1); bus != nullptr && bus->isEnabled())
        {
            classBuses[(size_t) c] = getBusBuffer (buffer, false, c + 1);
            outs.classes[(size_t) c] = &classBuses[(size_t) c];
        }

        const auto db = classLevelParams[(size_t) c]->load();
        outs.levels[(size_t) c] = db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    // ---- Render between events ----
    auto handle = [&] (int note, float velocity, int slice, float pitch)
    {
        if (velocity <= 0.0f)
            engine.noteOff (note);
        else if (s != nullptr && sl != nullptr && slice >= 0)
            engine.playSlice (slice, note, velocity, pitch, 0, *s, *sl, bank, vp);
        else if (s != nullptr && sl != nullptr)
            engine.noteOn (note, velocity, *s, *sl, bank, padMap, vp, pitch);
    };

    int pos = 0;

    for (size_t i = 0; i < numEvents; ++i)
    {
        const auto& e = events[i];
        renderWithModulation (outs, pos, e.offset);
        pos = e.offset;

        if (e.type == Event::control)
        {
            if (e.note == -1)
                bendSemitones = e.value * bendRangeParam->load();
            else
                engine.killAll();
        }
        else
        {
            handle (e.note, e.type == Event::noteOn ? e.value : 0.0f, e.slice, e.pitch);
        }
    }

    renderWithModulation (outs, pos, numSamples);

    // MIDI out: only NEURO's clock (incoming MIDI is not echoed). Follows whichever transport drives NEURO: host,
    // MIDI clock in (then it is a thru), or the sequencer's free-running clock.
    midi.clear();
    {
        const auto freeRunning = (int) seqModeParam->load() == 2;
        const auto running = clockOutParam->load() > 0.5f && (hostPpq.has_value() || freeRunning);
        const auto beat = hostPpq ? *hostPpq : seqFreeOrigin - numSamples * beatsPerSample; // free origin already advanced
        clockLeader.process (running, beat, beatsPerSample, numSamples, [&] (int offset, uint8_t status)
                             { midi.addEvent (juce::MidiMessage (status), offset); });
    }

    blockStartTime += numSamples;

    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
    gainSmoothed.applyGain (buffer, buffer.getNumSamples());
}

// Renders [from, to) in chunks, filling the per-sample global pitch offset: transpose + fine + bend + LFO.
void NeuroMPCProcessor::renderWithModulation (const neuro::OutputBuses& outs, int from, int to)
{
    const auto base = transposeParam->load() + fineParam->load() / 100.0f + bendSemitones;
    const auto depth = lfoDepthParam->load();
    const auto shape = (int) lfoShapeParam->load();
    const auto phaseOffset = lfoPhaseParam->load() / 360.0;

    while (from < to)
    {
        const auto n = juce::jmin (to - from, (int) modBuffer.size());

        for (int i = 0; i < n; ++i)
        {
            const auto p = std::fmod (lfoCyclePos + phaseOffset, 1.0);
            const auto lfo = shape == 0 ? std::sin (juce::MathConstants<double>::twoPi * p)
                           : shape == 1 ? 4.0 * std::abs (p - 0.5) - 1.0   // +1 at cycle start, -1 mid-cycle
                           : shape == 2 ? 2.0 * p - 1.0
                           : shape == 3 ? (p < 0.5 ? 1.0 : -1.0)
                                        : (double) sampleAndHold;

            modBuffer[(size_t) i] = base + depth * (float) lfo;
            lfoCyclePos += lfoIncrement;

            if (lfoCyclePos >= 1.0)
            {
                lfoCyclePos -= std::floor (lfoCyclePos);
                sampleAndHold = lfoRandom.nextFloat() * 2.0f - 1.0f;
            }
        }

        engine.render (outs, from, n, modBuffer.data());
        from += n;
    }
}

// ---- MIDI Learn ----

bool NeuroMPCProcessor::handleMappedMidi (const juce::MidiMessage& msg)
{
    using Source = neuro::MidiBinding::Source;
    const auto isCc = msg.isController();
    const auto isNote = msg.isNoteOnOrOff();

    if (! isCc && ! isNote)
        return false;

    const auto source = isCc ? Source::cc : Source::note;
    const auto number = isCc ? msg.getControllerNumber() : msg.getNoteNumber();

    if (padLayoutLearnArmed.load() && isNote)
    {
        if (msg.isNoteOn())
        {
            const auto scope = learnFifo.write (1);

            if (scope.blockSize1 > 0)
                learned[(size_t) scope.startIndex1] = { 2, msg.getChannel(), number }; // 2 = pad layout capture
        }

        return true; // consumed while learning the layout
    }

    if (learnArmed.load() && (isCc || msg.isNoteOn()))
    {
        if (learnArmed.exchange (false))
        {
            const auto scope = learnFifo.write (1);

            if (scope.blockSize1 > 0)
                learned[(size_t) scope.startIndex1] = { (int) source, msg.getChannel(), number };
        }

        return true;
    }

    const auto* routing = rtMidi.acquire();
    const auto index = routing != nullptr ? routing->table.find (source, msg.getChannel(), number) : -1;

    if (index < 0 || index >= (int) routing->targets.size())
        return false;

    const auto& target = routing->targets[(size_t) index];

    if (isNote && ! msg.isNoteOn())
        return true; // note-offs of mapped notes are swallowed too

    if (target.kind == MidiRouting::Target::parameter && target.param != nullptr)
    {
        auto* p = target.param;
        const auto steps = p->getNumSteps();
        const auto discrete = p->isDiscrete() || p->isBoolean();
        float value = p->getValue();

        if (target.step >= 0)
        {
            if (isCc && msg.getControllerValue() < 64)
                return true;

            value = steps > 1 ? (float) target.step / (float) (steps - 1) : 0.0f;
        }
        else if (isCc)
        {
            value = (float) msg.getControllerValue() / 127.0f;
        }
        else if (p->isBoolean())
        {
            value = value >= 0.5f ? 0.0f : 1.0f;                                           // note toggles
        }
        else if (discrete && steps > 1)
        {
            const auto current = juce::roundToInt (value * (float) (steps - 1));
            value = (float) ((current + 1) % steps) / (float) (steps - 1);                // note steps through choices
        }
        else
        {
            value = msg.getFloatVelocity();                                                // note sets by velocity
        }

        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
    }
    else if (target.kind == MidiRouting::Target::control)
    {
        const auto scope = actionFifo.write (1);

        if (scope.blockSize1 > 0)
            uiActions[(size_t) scope.startIndex1] = { target.ui, isCc ? (float) msg.getControllerValue() / 127.0f : -1.0f };
        else if (scope.blockSize2 > 0)
            uiActions[(size_t) scope.startIndex2] = { target.ui, isCc ? (float) msg.getControllerValue() / 127.0f : -1.0f };
    }

    return true;
}

void NeuroMPCProcessor::publishMidiRouting()
{
    auto routing = std::make_shared<MidiRouting>();
    routing->table = neuro::MidiMapTable (midiMap);

    for (const auto& b : midiMap.getBindings())
    {
        MidiRouting::Target t;
        const juce::String target (b.target);

        if (target.startsWith ("param:"))
        {
            const auto spec = target.fromFirstOccurrenceOf ("param:", false, false);
            t.param = apvts.getParameter (spec.upToFirstOccurrenceOf ("@", false, false));
            t.step = spec.contains ("@") ? spec.fromFirstOccurrenceOf ("@", false, false).getIntValue() : -1;
            t.kind = t.param != nullptr ? MidiRouting::Target::parameter : MidiRouting::Target::none;
        }
        else if (target.startsWith ("ui:"))
        {
            const auto it = std::find (uiTargets.begin(), uiTargets.end(), b.target);
            t.ui = it != uiTargets.end() ? (int) (it - uiTargets.begin()) : -1;
            t.kind = t.ui >= 0 ? MidiRouting::Target::control : MidiRouting::Target::none; // still consumed while the editor is closed
        }

        routing->targets.push_back (t);
    }

    rtMidi.publish (routing);
}

void NeuroMPCProcessor::editMidiMap (const std::function<void (neuro::MidiMap&)>& edit)
{
    const std::scoped_lock lock (modelLock);
    edit (midiMap);
    publishMidiRouting();
}

void NeuroMPCProcessor::startMidiLearn (const juce::String& target)
{
    learnTarget = target;
    learnArmed.store (true);
}

void NeuroMPCProcessor::stopMidiLearn()
{
    learnArmed.store (false);
    learnTarget.clear();
}

juce::String NeuroMPCProcessor::describeMidiBindings (const juce::String& target) const
{
    juce::StringArray parts;

    for (const auto* b : midiMap.bindingsFor (target.toStdString()))
        parts.add (neuro::MidiMap::describe (*b));

    return parts.joinIntoString ("; ");
}

void NeuroMPCProcessor::setUiTargets (std::vector<std::string> ids, std::function<void (int, float)> handler)
{
    uiTargets = std::move (ids);
    uiHandler = std::move (handler);
    publishMidiRouting();
}

void NeuroMPCProcessor::clearUiTargets()
{
    uiTargets.clear();
    uiHandler = nullptr;
    publishMidiRouting();
}

void NeuroMPCProcessor::processMidiQueues()
{
    for (auto ready = learnFifo.getNumReady(); ready > 0; --ready)
    {
        LearnedMessage m {};

        {
            const auto scope = learnFifo.read (1);

            if (scope.blockSize1 > 0)
                m = learned[(size_t) scope.startIndex1];
        }

        if (m.source == 2)
        {
            if (padLayoutLearnArmed.load() && std::find (padLayoutCaptured.begin(), padLayoutCaptured.end(), m.number) == padLayoutCaptured.end())
                padLayoutCaptured.push_back (m.number);

            if ((int) padLayoutCaptured.size() == neuro::padsPerBank)
                stopPadLayoutLearn();

            continue;
        }

        if (learnTarget.isNotEmpty())
            editMidiMap ([&] (auto& map) { map.learn (learnTarget.toStdString(), (neuro::MidiBinding::Source) m.source, m.channel, m.number); });

        learnTarget.clear();
    }

    const auto scope = actionFifo.read (actionFifo.getNumReady());
    auto run = [this] (const UiAction& a) { if (uiHandler) uiHandler (a.index, a.value); };

    for (int i = 0; i < scope.blockSize1; ++i) run (uiActions[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) run (uiActions[(size_t) (scope.startIndex2 + i)]);
}

// Akai MPK mini mk2, measured on 2026-10-29 from a connected unit (CoreMIDI capture): knobs K1-K8 send CC 1-8 on
// channel 1 (CC 1 is shared with the joystick's mod axis); pads send on channel 1: bank A notes 36,32,37,33,38,34,39,35,
// bank B notes 48,44,49,45,50,46,51,47 (in the order they were hit); joystick X = pitch bend. If the hit order did not
// follow NEURO's pad order, re-run "Learn hardware pad layout".
void NeuroMPCProcessor::applyMpkMiniMk2Template()
{
    static constexpr std::pair<int, const char*> knobs[] {
        { 1, ParamIDs::gain }, { 2, ParamIDs::attack }, { 3, ParamIDs::release }, { 4, ParamIDs::transpose },
        { 5, ParamIDs::lfoDepth }, { 6, ParamIDs::lfoRate }, { 7, ParamIDs::arpGate }, { 8, ParamIDs::seqSwing } };

    editMidiMap ([] (auto& map)
    {
        for (auto [cc, id] : knobs)
            map.learn (std::string ("param:") + id, neuro::MidiBinding::Source::cc, 1, cc);
    });

    setHardwarePadNotes ({ 36, 32, 37, 33, 38, 34, 39, 35, 48, 44, 49, 45, 50, 46, 51, 47 });

    for (auto [id, value] : { std::pair { ParamIDs::padChannel, 0.0f }, { ParamIDs::padBankFollow, 1.0f } })
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

void NeuroMPCProcessor::setHardwarePadNotes (const std::array<int, neuro::padsPerBank>& notes)
{
    hardwarePadNotes = notes;
    publishHardwarePads();
}

void NeuroMPCProcessor::publishHardwarePads()
{
    auto layout = std::make_shared<HardwarePads>();
    layout->indexOfNote.fill (-1);

    for (int i = 0; i < neuro::padsPerBank; ++i)
        if (juce::isPositiveAndBelow (hardwarePadNotes[(size_t) i], 128))
            layout->indexOfNote[(size_t) hardwarePadNotes[(size_t) i]] = (int8_t) i;

    rtHardwarePads.publish (layout);
}

void NeuroMPCProcessor::startPadLayoutLearn()
{
    stopMidiLearn();
    padLayoutCaptured.clear();
    padLayoutLearnArmed.store (true);
}

// Ends layout learning; a complete capture (16 pads) replaces the layout, a partial one replaces only those pads.
void NeuroMPCProcessor::stopPadLayoutLearn()
{
    if (! padLayoutLearnArmed.exchange (false))
        return;

    auto notes = hardwarePadNotes;

    for (size_t i = 0; i < padLayoutCaptured.size() && i < notes.size(); ++i)
        notes[i] = padLayoutCaptured[i];

    if (! padLayoutCaptured.empty())
        setHardwarePadNotes (notes);

    padLayoutCaptured.clear();
}

void NeuroMPCProcessor::updateArpSettings (double bpm)
{
    static constexpr double stepBeats[] { 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    auto a = arp.getSettings();
    a.direction = (neuro::ArpDirection) juce::jlimit (0, 9, (int) arpDirectionParam->load());
    a.stepBeats = stepBeats[juce::jlimit (0, 8, (int) arpRateParam->load())];
    a.gate = arpGateParam->load() / 100.0f;
    a.octaves = (int) arpOctavesParam->load();
    a.swing = arpSwingParam->load() / 100.0f;
    a.probability = arpChanceParam->load() / 100.0f;
    a.repeats = (int) arpRepeatsParam->load();
    a.fixedVelocity = (int) arpVelocityParam->load();
    a.retrigger = (neuro::ArpRetrigger) juce::jlimit (0, 2, (int) arpRetriggerParam->load());
    a.seed = (uint32_t) arpSeedParam->load();
    a.accentAmount = arpAccentParam->load() / 100.0f;
    a.phraseLength = (int) arpPhraseParam->load();
    a.variation = arpVariationParam->load() / 100.0f;
    a.varyPitch = arpVaryPitchParam->load() > 0.5f;
    a.varyVelocity = arpVaryVelParam->load() > 0.5f;
    a.varyRhythm = arpVaryRhythmParam->load() > 0.5f;
    a.returnEvery = (int) arpReturnParam->load();
    a.humanizeBeats = arpHumanTimeParam->load() / 1000.0 * bpm / 60.0;
    a.humanizeVelocity = arpHumanVelParam->load() / 100.0f;
    a.scaleMask = neuro::scales[(size_t) juce::jlimit (0, (int) neuro::scales.size() - 1, (int) scaleParam->load())].mask;
    a.scaleRoot = (int) scaleRootParam->load();
    arp.setSettings (a);                        // keeps the current latch state...
    arp.setLatch (arpLatchParam->load() > 0.5f); // ...so un-latching can release latched notes
}

bool NeuroMPCProcessor::loadSample (const juce::File& file, juce::String& error)
{
    const std::scoped_lock lock (modelLock);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported or unreadable file: " + file.getFileName();
        return false;
    }

    const auto length = (int64_t) reader->lengthInSamples;
    const auto channels = (int) juce::jmin (2u, reader->numChannels);

    if (length <= 0 || channels <= 0 || reader->sampleRate <= 0.0)
    {
        error = "File contains no audio: " + file.getFileName();
        return false;
    }

    if (length * channels > maxSampleFloats || length > std::numeric_limits<int>::max())
    {
        error = "File exceeds the per-sample memory limit: " + file.getFileName();
        return false;
    }

    auto data = std::make_shared<neuro::SampleData>();
    data->buffer.setSize (channels, (int) length);
    data->sampleRate = reader->sampleRate;
    data->file = file;

    for (const auto& key : reader->metadataValues.getAllKeys())
        if (key.containsIgnoreCase ("tempo") || key.containsIgnoreCase ("bpm"))
            if (const auto tempo = reader->metadataValues[key].getDoubleValue(); tempo >= 40.0 && tempo <= 300.0)
                data->fileTempo = tempo;

    if (! reader->read (&data->buffer, 0, (int) length, 0, true, channels > 1))
    {
        error = "Failed to decode: " + file.getFileName();
        return false;
    }

    sample = data;
    ++sampleVersion; // before any work is requested for the new sample
    rtSample.publish (sample);
    stretchCache.clear();
    stretchRequested.clear();
    chordRequested.clear();
    analyses.clear();
    analysisRequested.clear();
    slices.reset (length);
    publishSlices();
    missingSamplePath.clear();
    pendingSliceTree = {};
    return true;
}

void NeuroMPCProcessor::editSlices (const std::function<void (neuro::SliceSet&)>& edit)
{
    const std::scoped_lock lock (modelLock);
    edit (slices);
    publishSlices();
}

void NeuroMPCProcessor::publishSlices()
{
    rtSlices.publish (std::make_shared<const neuro::SliceSet> (slices));

    if (pads.followSlices)
    {
        pads.autoAssign (slices);
        publishPads();
    }

    refreshStretch();
    refreshAnalysis();
    updateKeyboardMap();
}

void NeuroMPCProcessor::setKeyboardSlice (int sliceIndex)
{
    const std::scoped_lock lock (modelLock);
    keyboardSlice = juce::jmax (0, sliceIndex);
    updateKeyboardMap();
}

void NeuroMPCProcessor::updateKeyboardMap()
{
    const auto index = juce::jlimit (0, juce::jmax (0, slices.size() - 1), keyboardSlice);
    auto root = 60.0f;

    if (index < slices.size())
    {
        const auto& s = slices.getSlices()[(size_t) index];
        const auto* a = getSliceAnalysis (index);

        if (s.rootNote >= 0.0f)
            root = s.rootNote;
        else if (a != nullptr && a->pitch.status == neuro::PitchEstimate::Status::pitched)
            root = (float) a->pitch.midi;
    }

    keyboardSliceIndex.store (index);
    keyboardRootNote.store (root);
}

int NeuroMPCProcessor::padNote (int padIndex) const
{
    // Keys mode: pads play the keyboard slice chromatically upwards from its root (per bank of 16).
    return isKeyboardMode() ? juce::jlimit (0, 127, juce::roundToInt (keyboardRootNote.load()) + padIndex % neuro::padsPerBank)
                            : pads[juce::jlimit (0, neuro::numPads - 1, padIndex)].note;
}

int NeuroMPCProcessor::startSliceAudition (int sliceIndex, float velocity)
{
    if (isKeyboardMode())
    {
        setKeyboardSlice (sliceIndex);
        const auto note = juce::jlimit (0, 127, juce::roundToInt (keyboardRootNote.load()));
        pushUiNote (note, velocity, -1);
        return note;
    }

    pushUiNote (sliceAuditionTag + sliceIndex, velocity, sliceIndex);
    return sliceAuditionTag + sliceIndex;
}

void NeuroMPCProcessor::stopAudition (int tag)
{
    pushUiNote (tag, 0.0f, -1);
}

void NeuroMPCProcessor::editSeqPattern (int index, const std::function<void (neuro::SeqPattern&)>& edit, Undo undo)
{
    const std::scoped_lock lock (modelLock);
    const auto i = (size_t) juce::jlimit (0, numSeqPatterns - 1, index);
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (undo == Undo::always || (undo == Undo::record && now - seqLastUndoTime[i] > 400.0))
    {
        seqUndo[i].push_back (seqPatterns[i]);

        if (seqUndo[i].size() > 64)
            seqUndo[i].erase (seqUndo[i].begin());

        seqRedo[i].clear();
    }

    if (undo != Undo::none)
        seqLastUndoTime[i] = now;

    edit (seqPatterns[i]);
    rtSeq.publish (std::make_shared<const SeqBank> (seqPatterns));
}

NeuroMPCProcessor::DeconstructSummary NeuroMPCProcessor::deconstructLoop (int patternIndex, double bpm, float sensitivity)
{
    const std::scoped_lock lock (modelLock);
    DeconstructSummary summary;

    if (sample == nullptr || sample->length() < 64)
        return summary;

    const auto& buffer = sample->buffer;
    const auto length = sample->length();
    const auto sr = sample->sampleRate;
    const auto* const* all = buffer.getArrayOfReadPointers();

    if (bpm <= 0.0) // auto: the file's tempo tag, else estimated from the audio
        bpm = sample->fileTempo > 0.0 ? sample->fileTempo : neuro::estimateTempo (all, buffer.getNumChannels(), length, sr);

    summary.bpm = bpm;
    slices.setStarts (neuro::detectTransients (all, buffer.getNumChannels(), length, sr, sensitivity));

    // Classify every hit in context (band rises at its onset); stored as overrides so the one-shot analysis of
    // isolated slices does not replace them.
    std::vector<const float*> channels ((size_t) buffer.getNumChannels());
    std::vector<float> peaks;
    float loudest = 1.0e-6f;

    for (int i = 0; i < slices.size(); ++i)
    {
        const auto s = slices.getSlices()[(size_t) i];

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            channels[(size_t) c] = buffer.getReadPointer (c, (int) s.startSample);

        const auto features = neuro::analyseTimbre (channels.data(), (int) channels.size(), s.length(), sr);
        const auto rise = neuro::analyseHitRise (all, buffer.getNumChannels(), length, s.startSample, sr);
        const auto cls = neuro::classifyHit (rise, features);
        slices.setAnalysis (s.id, s.startSample, s.endSample, (int) cls, features.mapX(), features.mapY(), -1.0f);
        slices.setDrumClass (i, (int) cls);
        peaks.push_back (buffer.getMagnitude ((int) s.startSample, (int) s.length()));
        loudest = std::max (loudest, peaks.back());
    }

    publishSlices();

    // The loop's own 1/16 grid at its tempo; whole bars, at most 4 (64 steps). Later hits are left out.
    const auto samplesPerStep = sr * 60.0 / bpm / 4.0;
    const auto totalSteps = (double) length / samplesPerStep;
    const auto steps = juce::jlimit (1, neuro::SeqLane::maxSteps, totalSteps < 12.0 ? (int) std::lround (totalSteps)       // under a bar: as is
                                                                                  : (int) std::lround (totalSteps / 16.0) * 16); // else whole bars

    neuro::SeqPattern pattern;
    pattern.name = "Deconstructed";
    pattern.numLanes = neuro::numDrumClasses;

    for (int c = 0; c < neuro::numDrumClasses; ++c)
    {
        auto& lane = pattern.lanes[(size_t) c];
        lane.note = neuro::PadBank::baseNote + c;
        lane.drumClass = c;
        lane.name = neuro::drumClassName ((neuro::DrumClass) c);
        lane.length = steps;
    }

    for (size_t i = 0; i < slices.getSlices().size(); ++i)
    {
        const auto& s = slices.getSlices()[i];
        const auto position = (double) s.startSample / samplesPerStep;
        const auto step = (int) std::lround (position);

        if (step >= steps)
            continue;

        const auto cls = std::clamp (s.effectiveClass(), 0, neuro::numDrumClasses - 1);
        const auto micro = (float) juce::jlimit (-0.5, 0.5, position - step);
        const auto velocity = juce::jlimit (0.1f, 1.0f, peaks[i] / loudest);
        auto& st = pattern.lanes[(size_t) cls].steps[(size_t) step];

        if (! st.on || st.velocity < velocity) // two hits of one class on one step: keep the louder
        {
            st = { true, velocity, 1.0f, 1.0f, 1, micro, 0.0f };
            st.sliceId = s.id;
        }

        ++summary.hits;
        ++summary.perClass[(size_t) cls];
    }

    summary.steps = steps;
    editSeqPattern (patternIndex, [&] (auto& p) { p = pattern; }, Undo::always);

    auto* rate = apvts.getParameter (ParamIDs::seqRate); // the pattern is written in 1/16 steps
    rate->setValueNotifyingHost (rate->convertTo0to1 (3.0f));
    return summary;
}

static juce::File separatedDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NEURO MPC/Separated");
}

static bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (buffer.getNumChannels()).withBitsPerSample (24);
    auto writer = wav.createWriterFor (out, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

// ---- Extract drums (AI) ----
namespace
{
    // htdemucs fine-tuned drums model, converted for demucs.cpp (MIT, Meta weights). Pinned by SHA-256.
    const char* const drumModelUrl = "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/8f58ac0491bbea657275bcd3e38af1ab3a27bfc9/ggml-model-htdemucs_ft_drums-4s-f16.bin";
    const char* const drumModelSha256 = "cda268755cc80deb5f4616a99f9d09f26bc5ce3882aa35e0bc504708a12604bb";

    // Planar resampling with a windowed-sinc interpolator (input zero-padded so the tail is complete).
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, int numIn, double fromRate, double toRate, int numOut)
    {
        juce::AudioBuffer<float> out (in.getNumChannels(), numOut);

        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            if (std::abs (fromRate - toRate) < 0.5)
            {
                out.clear (c, 0, numOut);
                out.copyFrom (c, 0, in, c, 0, juce::jmin (numIn, numOut));
                continue;
            }

            std::vector<float> padded ((size_t) numIn + 256, 0.0f);
            std::copy (in.getReadPointer (c), in.getReadPointer (c) + numIn, padded.begin());
            juce::WindowedSincInterpolator interpolator;
            interpolator.process (fromRate / toRate, padded.data(), out.getWritePointer (c), numOut);
        }

        return out;
    }
}

juce::File NeuroMPCProcessor::getDrumModelFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/NEURO MPC/Models/ggml-model-htdemucs_ft_drums-4s-f16.bin");
}

void NeuroMPCProcessor::setAiState (AiState state, const juce::String& message)
{
    {
        const std::scoped_lock lock (aiMutex);
        aiMessage = message;
    }

    aiState = (int) state;
}

juce::String NeuroMPCProcessor::getAiMessage() const
{
    const std::scoped_lock lock (aiMutex);
    return aiMessage;
}

void NeuroMPCProcessor::downloadDrumModel()
{
    if (getAiState() == AiState::downloading || getAiState() == AiState::extracting)
        return;

    const auto part = getDrumModelFile().withFileExtension ("part");
    part.getParentDirectory().createDirectory();
    part.deleteFile();
    aiProgress = 0.0f;
    setAiState (AiState::downloading, "Downloading the AI drum model (84 MB)...");
    modelDownload = juce::URL (drumModelUrl).downloadToFile (part, juce::URL::DownloadTaskOptions().withListener (&modelListener));

    if (modelDownload == nullptr)
        setAiState (AiState::failed, "Could not start the model download");
}

void NeuroMPCProcessor::ModelDownloadListener::progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total)
{
    owner.aiProgress = total > 0 ? (float) done / (float) total : 0.0f;
}

void NeuroMPCProcessor::ModelDownloadListener::finished (juce::URL::DownloadTask* task, bool success)
{
    const auto part = getDrumModelFile().withFileExtension ("part");

    // Only a byte-exact model is installed: anything else (error page, truncated file) is discarded.
    if (success && task->statusCode() == 200 && juce::SHA256 (part).toHexString() == drumModelSha256 && part.moveFileTo (getDrumModelFile()))
    {
        owner.setAiState (AiState::idle, "AI drum model installed");
        return;
    }

    part.deleteFile();
    owner.setAiState (AiState::failed, "Model download failed (network or checksum). Try again.");
}

bool NeuroMPCProcessor::computeDrums (const neuro::SampleData& source, juce::AudioBuffer<float>& out, juce::String& error)
{
    const auto n = (int) source.length();
    const auto sr = source.sampleRate;
    juce::AudioBuffer<float> stereo (2, n);
    stereo.copyFrom (0, 0, source.buffer, 0, 0, n);
    stereo.copyFrom (1, 0, source.buffer, juce::jmin (1, source.buffer.getNumChannels() - 1), 0, n);

    const auto modelLength = (int) std::ceil ((double) n * neuro::demucs::sampleRate / sr);
    const auto in = resample (stereo, n, sr, neuro::demucs::sampleRate, modelLength);
    std::vector<float> left, right;
    std::string message;

    if (! neuro::demucs::extractDrums (getDrumModelFile().getFullPathName().toStdString(), in.getReadPointer (0), in.getReadPointer (1),
                                       modelLength, left, right, aiJob, message))
    {
        error = message;
        return false;
    }

    juce::AudioBuffer<float> drums (2, modelLength);
    drums.copyFrom (0, 0, left.data(), modelLength);
    drums.copyFrom (1, 0, right.data(), modelLength);
    out = resample (drums, modelLength, neuro::demucs::sampleRate, sr, n);
    return true;
}

bool NeuroMPCProcessor::startExtractDrums()
{
    const auto state = getAiState();

    if (state == AiState::downloading || state == AiState::extracting || ! isDrumModelInstalled())
        return false;

    std::shared_ptr<const neuro::SampleData> source;
    {
        const std::scoped_lock lock (modelLock);
        source = sample;
    }

    if (source == nullptr || source->length() < 4096)
        return false;

    if (aiThread.joinable())
        aiThread.join();

    aiJob.cancel = false;
    aiJob.progress = 0.0f;
    aiProgress = 0.0f;
    setAiState (AiState::extracting, "Extracting drums (AI)...");

    aiThread = std::thread ([this, source]
    {
        juce::AudioBuffer<float> drums;
        juce::String error;
        const auto ok = computeDrums (*source, drums, error);

        {
            const std::scoped_lock lock (aiMutex);
            aiResult = std::move (drums);
            aiSource = source;
        }

        setAiState (ok ? AiState::ready : AiState::failed, ok ? juce::String ("Drums extracted") : error);
    });

    return true;
}

juce::File NeuroMPCProcessor::finishExtractDrums()
{
    if (getAiState() != AiState::ready)
        return {};

    juce::AudioBuffer<float> drums;
    std::shared_ptr<const neuro::SampleData> source;
    {
        const std::scoped_lock lock (aiMutex);
        drums = std::move (aiResult);
        source = std::move (aiSource);
    }

    const auto file = separatedDirectory().getChildFile (source->file.getFileNameWithoutExtension() + " (drums).wav");
    juce::String error;

    if (! writeWav24 (file, drums, source->sampleRate) || ! loadSample (file, error))
    {
        setAiState (AiState::failed, "Could not write or load " + file.getFileName());
        return {};
    }

    setAiState (AiState::idle, "Drums extracted: " + file.getFileName());
    return file;
}

juce::File NeuroMPCProcessor::extractDrumsBlocking (juce::String& error)
{
    if (! startExtractDrums())
    {
        error = isDrumModelInstalled() ? "No sample loaded, or busy" : "AI drum model not installed";
        return {};
    }

    aiThread.join();

    if (getAiState() != AiState::ready)
    {
        error = getAiMessage();
        return {};
    }

    return finishExtractDrums();
}

void NeuroMPCProcessor::cancelAi()
{
    aiJob.cancel = true;

    if (aiThread.joinable())
        aiThread.join();
}

int NeuroMPCProcessor::teachDrumRange (int component, int64_t start, int64_t end)
{
    const std::scoped_lock lock (modelLock);

    if (sample == nullptr || component < 0 || component >= neuro::nmf::numDrumComponents || start < 0 || end <= start)
        return 0;

    if (taughtFile != sample->file)
    {
        taughtDrums.clear();
        taughtFile = sample->file;
    }

    taughtDrums.push_back ({ component, start, std::min (end, sample->length()) });
    return getTaughtCounts()[(size_t) component];
}

int NeuroMPCProcessor::teachDrum (int component, int sliceIndex)
{
    const std::scoped_lock lock (modelLock);

    if (sample == nullptr || sliceIndex < 0 || sliceIndex >= slices.size())
        return 0;

    const auto& s = slices.getSlices()[(size_t) sliceIndex];
    return teachDrumRange (component, s.startSample, std::min (s.endSample, s.startSample + (int64_t) (0.12 * sample->sampleRate)));
}

void NeuroMPCProcessor::clearTaughtDrums()
{
    const std::scoped_lock lock (modelLock);
    taughtDrums.clear();
}

std::array<int, neuro::nmf::numDrumComponents> NeuroMPCProcessor::getTaughtCounts() const
{
    const std::scoped_lock lock (modelLock);
    std::array<int, neuro::nmf::numDrumComponents> counts {};

    if (sample != nullptr && taughtFile == sample->file)
        for (const auto& e : taughtDrums) ++counts[(size_t) e.component];

    return counts;
}

NeuroMPCProcessor::DeconstructSummary NeuroMPCProcessor::separateDrums (int patternIndex, double bpm)
{
    const std::scoped_lock lock (modelLock);
    DeconstructSummary summary;

    if (sample == nullptr || sample->length() < 4096)
        return summary;

    const auto original = sample; // keep it alive while we replace the loaded sample
    const auto& buffer = original->buffer;
    const auto length = original->length();
    const auto sr = original->sampleRate;
    const auto numChannels = buffer.getNumChannels();

    if (bpm <= 0.0)
        bpm = original->fileTempo > 0.0 ? original->fileTempo : neuro::estimateTempo (buffer.getArrayOfReadPointers(), numChannels, length, sr);

    const auto result = neuro::nmf::separate (buffer.getArrayOfReadPointers(), numChannels, length, sr, 60,
                                              taughtFile == original->file ? taughtDrums : std::vector<neuro::nmf::Example> {});

    // Stems file: [kick | snare | cymbals | residual], each the loop's length.
    juce::AudioBuffer<float> stems (numChannels, (int) (length * neuro::nmf::numStems));

    for (int s = 0; s < neuro::nmf::numStems; ++s)
        for (int c = 0; c < numChannels; ++c)
            stems.copyFrom (c, (int) (s * length), result.stems[(size_t) s][(size_t) c].data(), (int) length);

    const auto file = separatedDirectory().getChildFile (original->file.getFileNameWithoutExtension() + " (drum stems).wav");

    if (! writeWav24 (file, stems, sr))
        return summary;

    juce::String error;

    if (! loadSample (file, error))
        return summary;

    summary.stems = file;
    summary.bpm = bpm;
    summary.taught = taughtFile == original->file && ! taughtDrums.empty();

    // Pattern grid: the loop's own 1/16 grid, whole bars (max 4); under a bar: as is.
    const auto samplesPerStep = sr * 60.0 / bpm / 4.0;
    const auto totalSteps = (double) length / samplesPerStep;
    const auto steps = juce::jlimit (1, neuro::SeqLane::maxSteps, totalSteps < 12.0 ? (int) std::lround (totalSteps)
                                                                                     : (int) std::lround (totalSteps / 16.0) * 16);
    const auto barSamples = (int64_t) std::llround (samplesPerStep * 16.0);
    const auto residualStart = length * (neuro::nmf::numStems - 1);

    auto stemOf = [] (neuro::DrumClass c)
    {
        using D = neuro::DrumClass;
        return c == D::kick ? neuro::nmf::kick : c == D::snare ? neuro::nmf::snare : neuro::nmf::cymbals;
    };

    std::vector<int64_t> starts;
    for (int s = 1; s < neuro::nmf::numStems; ++s) starts.push_back (s * length);
    for (const auto& e : result.events) starts.push_back ((int64_t) stemOf (e.drumClass) * length + e.sample);
    for (int64_t b = barSamples; b < std::min<int64_t> (length, (int64_t) (steps * samplesPerStep)); b += barSamples) starts.push_back (residualStart + b);
    slices.setStarts (starts);

    // Classes: each stem region is its drum; hits carry their detected class; the residual is Perc.
    for (int i = 0; i < slices.size(); ++i)
    {
        const auto start = slices.getSlices()[(size_t) i].startSample;
        const auto stem = (int) (start / length);
        auto cls = stem == neuro::nmf::kick ? neuro::DrumClass::kick : stem == neuro::nmf::snare ? neuro::DrumClass::snare
                 : stem == neuro::nmf::cymbals ? neuro::DrumClass::closedHat : neuro::DrumClass::perc;
        for (const auto& e : result.events)
            if ((int64_t) stemOf (e.drumClass) * length + e.sample == start)
                cls = e.drumClass;

        slices.setDrumClass (i, (int) cls);
    }

    publishSlices();

    neuro::SeqPattern pattern;
    pattern.name = "Separated";
    pattern.numLanes = neuro::numDrumClasses;

    for (int c = 0; c < neuro::numDrumClasses; ++c)
    {
        auto& lane = pattern.lanes[(size_t) c];
        lane.note = neuro::PadBank::baseNote + c;
        lane.drumClass = c;
        lane.name = neuro::drumClassName ((neuro::DrumClass) c);
        lane.length = steps;
    }

    for (const auto& e : result.events)
    {
        const auto position = (double) e.sample / samplesPerStep;
        const auto step = (int) std::lround (position);
        const auto index = slices.indexAt ((int64_t) stemOf (e.drumClass) * length + e.sample);

        if (step >= steps || index < 0)
            continue;

        auto& st = pattern.lanes[(size_t) e.drumClass].steps[(size_t) step];

        if (! st.on || st.velocity < e.strength)
        {
            st = { true, juce::jlimit (0.1f, 1.0f, e.strength), 1.0f, 1.0f, 1, (float) juce::jlimit (-0.5, 0.5, position - step), 0.0f };
            st.sliceId = slices.getSlices()[(size_t) index].id;
        }

        ++summary.hits;
        ++summary.perClass[(size_t) e.drumClass];
    }

    // Residual bed: one slice per bar on the Perc lane, so the separated lanes plus the bed reproduce the loop.
    for (int bar = 0; bar * 16 < steps; ++bar)
        if (const auto index = slices.indexAt (residualStart + bar * barSamples); index >= 0)
        {
            auto& st = pattern.lanes[(size_t) neuro::DrumClass::perc].steps[(size_t) (bar * 16)];
            st = { true, 1.0f, 1.0f, 1.0f, 1, 0.0f, 0.0f };
            st.sliceId = slices.getSlices()[(size_t) index].id;
        }

    summary.steps = steps;
    editSeqPattern (patternIndex, [&] (auto& p) { p = pattern; }, Undo::always);

    auto* rate = apvts.getParameter (ParamIDs::seqRate);
    rate->setValueNotifyingHost (rate->convertTo0to1 (3.0f));
    return summary;
}

int NeuroMPCProcessor::retargetPatternToTimbre (int patternIndex)
{
    int converted = 0;

    editSeqPattern (patternIndex, [&] (auto& pattern)
    {
        for (auto& lane : pattern.lanes)
            for (auto& st : lane.steps)
                if (st.sliceId > 0)
                    if (const auto i = slices.indexOfId (st.sliceId); i >= 0 && slices.getSlices()[(size_t) i].timbreX >= 0.0f)
                    {
                        st.timbreX = slices.getSlices()[(size_t) i].timbreX;
                        st.timbreY = slices.getSlices()[(size_t) i].timbreY;
                        st.sliceId = 0;
                        ++converted;
                    }
    }, Undo::always);

    return converted;
}

bool NeuroMPCProcessor::undoSeqPattern (int index)
{
    const std::scoped_lock lock (modelLock);
    const auto i = (size_t) juce::jlimit (0, numSeqPatterns - 1, index);

    if (seqUndo[i].empty())
        return false;

    seqRedo[i].push_back (seqPatterns[i]);
    seqPatterns[i] = seqUndo[i].back();
    seqUndo[i].pop_back();
    seqLastUndoTime[i] = 0.0;
    rtSeq.publish (std::make_shared<const SeqBank> (seqPatterns));
    return true;
}

bool NeuroMPCProcessor::redoSeqPattern (int index)
{
    const std::scoped_lock lock (modelLock);
    const auto i = (size_t) juce::jlimit (0, numSeqPatterns - 1, index);

    if (seqRedo[i].empty())
        return false;

    seqUndo[i].push_back (seqPatterns[i]);
    seqPatterns[i] = seqRedo[i].back();
    seqRedo[i].pop_back();
    seqLastUndoTime[i] = 0.0;
    rtSeq.publish (std::make_shared<const SeqBank> (seqPatterns));
    return true;
}

void NeuroMPCProcessor::editArpProgram (const std::function<void (neuro::ArpProgram&)>& edit)
{
    const std::scoped_lock lock (modelLock);
    edit (arpProgram);
    rtArpProgram.publish (std::make_shared<const neuro::ArpProgram> (arpProgram));
}

void NeuroMPCProcessor::editPads (const std::function<void (neuro::PadBank&)>& edit)
{
    const std::scoped_lock lock (modelLock);
    edit (pads);
    publishPads();
}

void NeuroMPCProcessor::publishPads()
{
    rtPads.publish (std::make_shared<const neuro::PadMap> (pads.playMap()));
}

void NeuroMPCProcessor::refreshAnalysis()
{
    if (sample == nullptr)
        return;

    auto want = [this] (int id, int64_t start, int64_t end)
    {
        if (auto it = analyses.find (id); it != analyses.end() && it->second.covers (start, end))
            return;

        if (auto it = analysisRequested.find (id); it != analysisRequested.end() && it->second == std::pair (start, end))
            return;

        analysisRequested[id] = { start, end };
        analyser.request (id, { sample, sampleVersion, id, start, end });
    };

    want (0, 0, sample->length());

    for (const auto& s : slices.getSlices())
        want (s.id, s.startSample, s.endSample);

    std::erase_if (analyses, [this] (const auto& kv)
    {
        return kv.first != 0 && std::none_of (slices.getSlices().begin(), slices.getSlices().end(),
                                              [&] (const auto& s) { return s.id == kv.first; });
    });
}

const neuro::AnalysisResult* NeuroMPCProcessor::getSliceAnalysis (int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= slices.size())
        return nullptr;

    const auto& s = slices.getSlices()[(size_t) sliceIndex];
    const auto it = analyses.find (s.id);
    return it != analyses.end() && it->second.covers (s.startSample, s.endSample) ? &it->second : nullptr;
}

const neuro::AnalysisResult* NeuroMPCProcessor::getSampleAnalysis() const
{
    const auto it = analyses.find (0);
    return sample != nullptr && it != analyses.end() && it->second.covers (0, sample->length()) ? &it->second : nullptr;
}

// Builds the bank for the current slices from the cache, requests missing renders, evicts LRU entries.
void NeuroMPCProcessor::refreshStretch()
{
    auto bank = std::make_shared<neuro::StretchBank>();

    for (const auto& s : slices.getSlices())
    {
        if (! s.needsRender())
            continue;

        const auto key = neuro::StretchKey::of (s);
        auto cached = std::find_if (stretchCache.begin(), stretchCache.end(), [&] (const auto& c) { return c.entry.key == key; });

        if (cached != stretchCache.end())
        {
            cached->lastUse = ++cacheClock;
            bank->entries.push_back (cached->entry);
        }
        else if (sample != nullptr && std::find (stretchRequested.begin(), stretchRequested.end(), key) == stretchRequested.end())
        {
            // The renderer replaces any pending job for this slice, so forget older requests for it.
            std::erase_if (stretchRequested, [&] (const auto& k) { return k.sliceId == key.sliceId; });
            stretchRequested.push_back (key);
            stretchRenderer.request (s.id, { sample, sampleVersion, s });
        }
    }

    // Keep-length Chord-Lock: variants -6..+5 semitones for every eligible (tonal) slice.
    if ((int) chordLockParam->load() == 2 && sample != nullptr)
    {
        for (const auto& s : slices.getSlices())
        {
            if (! neuro::chordLockEligible (s))
                continue;

            for (int k = -6; k <= 5; ++k)
            {
                if (k == 0)
                    continue;

                auto variant = s;
                variant.pitchMode = neuro::PitchMode::keepLength;
                variant.pitchSemitones = s.renderedPitch() + (float) k;
                const auto key = neuro::StretchKey::of (variant);
                auto cached = std::find_if (stretchCache.begin(), stretchCache.end(), [&] (const auto& c) { return c.entry.key == key; });

                if (cached != stretchCache.end())
                {
                    cached->lastUse = ++cacheClock;
                    bank->entries.push_back (cached->entry);
                }
                else if (std::find (chordRequested.begin(), chordRequested.end(), key) == chordRequested.end())
                {
                    chordRequested.push_back (key);
                    stretchRenderer.request (1000000 + s.id * 16 + k + 6, { sample, sampleVersion, variant });
                }
            }
        }
    }

    size_t bytes = 0;

    for (const auto& c : stretchCache)
        bytes += (size_t) c.entry.audio->getNumChannels() * (size_t) c.entry.audio->getNumSamples() * sizeof (float);

    while (bytes > stretchCacheBudgetBytes)
    {
        auto victim = stretchCache.end();

        for (auto it = stretchCache.begin(); it != stretchCache.end(); ++it)
            if (! bank->contains (it->entry.audio.get()) && (victim == stretchCache.end() || it->lastUse < victim->lastUse))
                victim = it;

        if (victim == stretchCache.end())
            break;

        bytes -= (size_t) victim->entry.audio->getNumChannels() * (size_t) victim->entry.audio->getNumSamples() * sizeof (float);
        stretchCache.erase (victim);
    }

    const auto same = stretchBank != nullptr && stretchBank->entries.size() == bank->entries.size()
                      && std::equal (bank->entries.begin(), bank->entries.end(), stretchBank->entries.begin(),
                                     [] (const auto& a, const auto& b) { return a.audio == b.audio; });

    if (! same)
    {
        stretchBank = bank;
        rtStretch.publish (stretchBank);
    }
}

void NeuroMPCProcessor::processBackgroundResults()
{
    processMidiQueues();
    const std::scoped_lock lock (modelLock);
    bool slicesAnalysed = false;

    for (auto& r : analyser.takeCompleted())
    {
        if (auto it = analysisRequested.find (r.sliceId); it != analysisRequested.end() && it->second == std::pair (r.start, r.end))
            analysisRequested.erase (it);

        if (r.sampleVersion == sampleVersion)
        {
            // Slices carry their class / timbre position / pitch so the audio thread can route and address them.
            if (r.sliceId > 0)
            {
                const auto pitched = r.pitch.status == neuro::PitchEstimate::Status::pitched;
                slicesAnalysed |= slices.setAnalysis (r.sliceId, r.start, r.end, (int) r.drumClass, r.timbre.mapX(), r.timbre.mapY(),
                                                      pitched ? (float) r.pitch.midi : -1.0f);
            }

            analyses[r.sliceId] = std::move (r);
        }
    }

    if (slicesAnalysed)
        rtSlices.publish (std::make_shared<const neuro::SliceSet> (slices)); // analysis data only: no re-analysis

    // Chord-Lock variant renders depend on analysis (which slices are tonal) and on the mode.
    if (const auto chordMode = (int) chordLockParam->load(); slicesAnalysed || chordMode != lastChordMode)
    {
        lastChordMode = chordMode;
        refreshStretch();
    }

    auto results = stretchRenderer.takeCompleted();

    for (auto& r : results)
    {
        std::erase (stretchRequested, r.entry.key);
        std::erase (chordRequested, r.entry.key);

        if (r.sampleVersion == sampleVersion && r.entry.audio != nullptr)
            stretchCache.push_back ({ std::move (r.entry), ++cacheClock });
    }

    if (! results.empty())
        refreshStretch();

    updateKeyboardMap(); // the detected root may have just arrived
}

bool NeuroMPCProcessor::waitForBackgroundWork (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        processBackgroundResults();

        if (stretchRequested.empty() && chordRequested.empty() && stretchRenderer.isIdle() && analysisRequested.empty() && analyser.isIdle())
            return true;

        juce::Thread::sleep (2);
    }

    return false;
}

NeuroMPCProcessor::StretchStatus NeuroMPCProcessor::getStretchStatus (int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= slices.size() || ! slices.getSlices()[(size_t) sliceIndex].needsRender())
        return StretchStatus::original;

    return stretchBank != nullptr && stretchBank->find (slices.getSlices()[(size_t) sliceIndex]) != nullptr
               ? StretchStatus::ready : StretchStatus::rendering;
}

void NeuroMPCProcessor::auditionNote (int note, float velocity)
{
    pushUiNote (note, velocity, -1);
}

void NeuroMPCProcessor::pushUiNote (int note, float velocity, int slice)
{
    const auto scope = uiFifo.write (1);

    if (scope.blockSize1 > 0)
        uiNotes[(size_t) scope.startIndex1] = { note, velocity, slice };
    else if (scope.blockSize2 > 0)
        uiNotes[(size_t) scope.startIndex2] = { note, velocity, slice };
}

void NeuroMPCProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, dest);
}

juce::ValueTree NeuroMPCProcessor::createStateTree() const
{
    const std::scoped_lock lock (modelLock);
    juce::ValueTree root ("NEURO_MPC");
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty ("keyboardSlice", keyboardSlice, nullptr);
    root.appendChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);

    juce::ValueTree sampleTree ("SAMPLE");
    sampleTree.setProperty ("path", sample != nullptr ? sample->file.getFullPathName() : missingSamplePath, nullptr);
    root.appendChild (sampleTree, nullptr);

    juce::ValueTree sliceTree ("SLICES");

    if (sample == nullptr && pendingSliceTree.isValid())
        sliceTree = pendingSliceTree.createCopy(); // keep the layout of a missing sample

    for (const auto& s : sample != nullptr ? slices.getSlices() : std::vector<neuro::SliceDefinition> {})
    {
        juce::ValueTree t ("SLICE");
        t.setProperty ("id", s.id, nullptr);
        t.setProperty ("start", (juce::int64) s.startSample, nullptr);
        t.setProperty ("end", (juce::int64) s.endSample, nullptr);
        t.setProperty ("gain", s.gain, nullptr);
        t.setProperty ("pitch", s.pitchSemitones, nullptr);
        t.setProperty ("pitchMode", (int) s.pitchMode, nullptr);
        t.setProperty ("stretch", s.timeStretchRatio, nullptr);
        t.setProperty ("preserveTransients", s.preserveTransients, nullptr);
        t.setProperty ("rootNote", s.rootNote, nullptr);
        t.setProperty ("drumClass", s.drumClass, nullptr);
        t.setProperty ("reverse", s.reverse, nullptr);
        t.setProperty ("enabled", s.enabled, nullptr);
        sliceTree.appendChild (t, nullptr);
    }

    root.appendChild (sliceTree, nullptr);

    juce::ValueTree padTree ("PADS");
    padTree.setProperty ("followSlices", pads.followSlices, nullptr);

    for (int i = 0; i < neuro::numPads; ++i)
    {
        const auto& p = pads[i];
        juce::ValueTree t ("PAD");
        t.setProperty ("slice", p.sliceId, nullptr);
        t.setProperty ("note", p.note, nullptr);
        t.setProperty ("gain", p.gain, nullptr);
        t.setProperty ("pitch", p.pitch, nullptr);
        t.setProperty ("choke", p.chokeGroup, nullptr);
        t.setProperty ("velSens", p.velocitySensitivity, nullptr);
        t.setProperty ("name", juce::String (p.name), nullptr);
        t.setProperty ("colour", juce::String::toHexString ((juce::int64) p.colour), nullptr);
        padTree.appendChild (t, nullptr);
    }

    root.appendChild (padTree, nullptr);

    juce::ValueTree programTree ("ARP_PROGRAM");
    programTree.setProperty ("length", arpProgram.length, nullptr);

    for (const auto& step : arpProgram.steps)
    {
        juce::ValueTree t ("STEP");
        t.setProperty ("on", step.on, nullptr);
        t.setProperty ("accent", step.accent, nullptr);
        t.setProperty ("ratchet", step.ratchet, nullptr);
        programTree.appendChild (t, nullptr);
    }

    for (int i = 0; i < arpProgram.numRules; ++i)
    {
        const auto& r = arpProgram.rules[(size_t) i];
        juce::ValueTree t ("RULE");
        t.setProperty ("when", (int) r.when, nullptr);
        t.setProperty ("n", r.n, nullptr);
        t.setProperty ("offset", r.offset, nullptr);
        t.setProperty ("action", (int) r.action, nullptr);
        t.setProperty ("amount", r.amount, nullptr);
        t.setProperty ("enabled", r.enabled, nullptr);
        programTree.appendChild (t, nullptr);
    }

    root.appendChild (programTree, nullptr);

    // Sequencer: only non-empty patterns, and only steps that are on.
    juce::ValueTree seqTree ("SEQUENCER");

    for (int pi = 0; pi < numSeqPatterns; ++pi)
    {
        const auto& pat = seqPatterns[(size_t) pi];

        if (pat.isEmpty() && pat.name.empty())
            continue;

        juce::ValueTree pt ("PATTERN");
        pt.setProperty ("index", pi, nullptr);
        pt.setProperty ("name", juce::String (pat.name), nullptr);
        pt.setProperty ("lanes", pat.numLanes, nullptr);

        for (int li = 0; li < neuro::SeqPattern::maxLanes; ++li)
        {
            const auto& lane = pat.lanes[(size_t) li];
            juce::ValueTree lt ("LANE");
            lt.setProperty ("index", li, nullptr);
            lt.setProperty ("note", lane.note, nullptr);
            lt.setProperty ("length", lane.length, nullptr);
            lt.setProperty ("mute", lane.mute, nullptr);
            lt.setProperty ("name", juce::String (lane.name), nullptr);
            lt.setProperty ("class", lane.drumClass, nullptr);

            for (int si = 0; si < neuro::SeqLane::maxSteps; ++si)
            {
                const auto& st = lane.steps[(size_t) si];

                if (! st.on)
                    continue;

                juce::ValueTree t ("STEP");
                t.setProperty ("i", si, nullptr);
                t.setProperty ("vel", st.velocity, nullptr);
                t.setProperty ("gate", st.gate, nullptr);
                t.setProperty ("prob", st.probability, nullptr);
                t.setProperty ("ratchet", st.ratchet, nullptr);
                t.setProperty ("micro", st.micro, nullptr);
                t.setProperty ("pitch", st.pitch, nullptr);

                if (st.sliceId > 0)       t.setProperty ("slice", st.sliceId, nullptr);
                if (st.timbreX >= 0.0f) { t.setProperty ("tx", st.timbreX, nullptr); t.setProperty ("ty", st.timbreY, nullptr); }

                if (st.condition != neuro::SeqCondition::always)
                {
                    t.setProperty ("cond", (int) st.condition, nullptr);
                    t.setProperty ("condA", st.condA, nullptr);
                    t.setProperty ("condB", st.condB, nullptr);
                }

                lt.appendChild (t, nullptr);
            }

            pt.appendChild (lt, nullptr);
        }

        seqTree.appendChild (pt, nullptr);
    }

    root.appendChild (seqTree, nullptr);

    juce::ValueTree midiTree ("MIDI_MAP");
    juce::StringArray padNotes;

    for (auto n : hardwarePadNotes)
        padNotes.add (juce::String (n));

    midiTree.setProperty ("hardwarePads", padNotes.joinIntoString (","), nullptr);

    for (const auto& b : midiMap.getBindings())
    {
        juce::ValueTree t ("BINDING");
        t.setProperty ("source", b.source == neuro::MidiBinding::Source::cc ? "cc" : "note", nullptr);
        t.setProperty ("channel", b.channel, nullptr);
        t.setProperty ("number", b.number, nullptr);
        t.setProperty ("target", juce::String (b.target), nullptr);
        midiTree.appendChild (t, nullptr);
    }

    root.appendChild (midiTree, nullptr);
    return root;
}

void NeuroMPCProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;

    if (auto xml = getXmlFromBinary (data, size))
        applyStateTree (juce::ValueTree::fromXml (*xml));
}

// Upgrades older state trees in place. Version 1 (Milestone 1) had no pads, arp, sequencer or pitch fields; every
// later field is optional with a default, so v1 loads as-is. Newer versions load best-effort (unknown data ignored).
void NeuroMPCProcessor::migrate (juce::ValueTree& root)
{
    const auto version = (int) root.getProperty ("version", 1);

    if (version < 2)
        root.setProperty ("version", 2, nullptr);
}

bool NeuroMPCProcessor::applyStateTree (juce::ValueTree root)
{
    if (! root.hasType ("NEURO_MPC"))
        return false;

    const std::scoped_lock lock (modelLock);
    loadedNewerVersion = (int) root.getProperty ("version", 1) > stateVersion;
    migrate (root);
    pendingSliceTree = {};

    if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    keyboardSlice = root.getProperty ("keyboardSlice", 0);

    if (auto midiTree = root.getChildWithName ("MIDI_MAP"); midiTree.isValid())
    {
        midiMap.clear();
        juce::StringArray padNotes;
        padNotes.addTokens (midiTree.getProperty ("hardwarePads", "").toString(), ",", "");

        if (padNotes.size() == neuro::padsPerBank)
        {
            std::array<int, neuro::padsPerBank> notes {};

            for (int i = 0; i < neuro::padsPerBank; ++i)
                notes[(size_t) i] = juce::jlimit (-1, 127, padNotes[i].getIntValue());

            setHardwarePadNotes (notes);
        }

        for (const auto& t : midiTree)
            midiMap.learn (t.getProperty ("target", "").toString().toStdString(),
                           t.getProperty ("source", "cc").toString() == "note" ? neuro::MidiBinding::Source::note : neuro::MidiBinding::Source::cc,
                           (int) t.getProperty ("channel", 0), (int) t.getProperty ("number", 0));

        publishMidiRouting();
    }

    if (auto seqTree = root.getChildWithName ("SEQUENCER"); seqTree.isValid())
    {
        for (auto& pattern : seqPatterns)
            pattern = {}; // in place: a SeqBank temporary would be ~0.5 MB of stack

        for (const auto& pt : seqTree)
        {
            auto& pat = seqPatterns[(size_t) juce::jlimit (0, numSeqPatterns - 1, (int) pt.getProperty ("index", 0))];
            pat.name = pt.getProperty ("name", "").toString().toStdString();
            pat.numLanes = juce::jlimit (1, neuro::SeqPattern::maxLanes, (int) pt.getProperty ("lanes", 8));

            for (const auto& lt : pt)
            {
                auto& lane = pat.lanes[(size_t) juce::jlimit (0, neuro::SeqPattern::maxLanes - 1, (int) lt.getProperty ("index", 0))];
                lane.note = juce::jlimit (0, 127, (int) lt.getProperty ("note", 36));
                lane.length = juce::jlimit (1, neuro::SeqLane::maxSteps, (int) lt.getProperty ("length", 16));
                lane.mute = lt.getProperty ("mute", false);
                lane.name = lt.getProperty ("name", "").toString().toStdString();
                lane.drumClass = juce::jlimit (-1, neuro::numDrumClasses - 1, (int) lt.getProperty ("class", -1));

                for (const auto& t : lt)
                {
                    auto& st = lane.steps[(size_t) juce::jlimit (0, neuro::SeqLane::maxSteps - 1, (int) t.getProperty ("i", 0))];
                    st.on = true;
                    st.velocity = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("vel", 0.8f));
                    st.gate = juce::jlimit (0.05f, 1.0f, (float) t.getProperty ("gate", 0.5f));
                    st.probability = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("prob", 1.0f));
                    st.ratchet = juce::jlimit (1, 4, (int) t.getProperty ("ratchet", 1));
                    st.micro = juce::jlimit (-0.5f, 0.5f, (float) t.getProperty ("micro", 0.0f));
                    st.pitch = juce::jlimit (-24.0f, 24.0f, (float) t.getProperty ("pitch", 0.0f));
                    st.condition = (neuro::SeqCondition) juce::jlimit (0, 7, (int) t.getProperty ("cond", 0));
                    st.condA = juce::jlimit (1, 16, (int) t.getProperty ("condA", 1));
                    st.condB = juce::jlimit (1, 16, (int) t.getProperty ("condB", 2));
                    st.sliceId = juce::jmax (0, (int) t.getProperty ("slice", 0));
                    st.timbreX = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("tx", -1.0f));
                    st.timbreY = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("ty", -1.0f));
                }
            }
        }

        rtSeq.publish (std::make_shared<const SeqBank> (seqPatterns));
    }

    if (auto programTree = root.getChildWithName ("ARP_PROGRAM"); programTree.isValid())
    {
        neuro::ArpProgram program;
        program.length = juce::jlimit (1, neuro::ArpProgram::maxSteps, (int) programTree.getProperty ("length", 16));
        int step = 0;

        for (const auto& t : programTree)
        {
            if (t.hasType ("STEP") && step < neuro::ArpProgram::maxSteps)
                program.steps[(size_t) step++] = { (bool) t.getProperty ("on", true), (bool) t.getProperty ("accent", false),
                                                   juce::jlimit (1, 4, (int) t.getProperty ("ratchet", 1)) };

            if (t.hasType ("RULE"))
                program.addRule ({ (neuro::ArpRule::When) juce::jlimit (0, 3, (int) t.getProperty ("when", 0)),
                                   juce::jlimit (1, 100, (int) t.getProperty ("n", 4)), juce::jmax (0, (int) t.getProperty ("offset", 0)),
                                   (neuro::ArpRule::Action) juce::jlimit (0, 4, (int) t.getProperty ("action", 0)),
                                   juce::jlimit (-24, 24, (int) t.getProperty ("amount", 12)), (bool) t.getProperty ("enabled", true) });
        }

        editArpProgram ([&] (auto& p) { p = program; });
    }

    // Pads are restored before slices are published, so followSlices sees the saved choice.
    if (auto padTree = root.getChildWithName ("PADS"); padTree.isValid())
    {
        pads = {};
        pads.followSlices = padTree.getProperty ("followSlices", true);

        for (int i = 0; i < juce::jmin (neuro::numPads, padTree.getNumChildren()); ++i)
        {
            const auto t = padTree.getChild (i);
            auto& p = pads[i];
            p.sliceId = t.getProperty ("slice", 0);
            p.note = juce::jlimit (0, 127, (int) t.getProperty ("note", neuro::PadBank::baseNote + i));
            p.gain = juce::jlimit (0.0f, 4.0f, (float) t.getProperty ("gain", 1.0f));
            p.pitch = juce::jlimit (-24.0f, 24.0f, (float) t.getProperty ("pitch", 0.0f));
            p.chokeGroup = juce::jlimit (0, 16, (int) t.getProperty ("choke", 0));
            p.velocitySensitivity = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("velSens", 1.0f));
            p.name = t.getProperty ("name", "").toString().toStdString();
            p.colour = (uint32_t) t.getProperty ("colour", "ff2a2f3a").toString().getHexValue64();
        }

        publishPads();
    }

    const auto path = root.getChildWithName ("SAMPLE")["path"].toString();

    if (path.isEmpty())
        return true;

    juce::String error;

    // A missing file is reported, never silently substituted; its slices wait for a relink.
    if (! juce::File::isAbsolutePath (path) || ! loadSample (juce::File (path), error))
    {
        missingSamplePath = path;
        pendingSliceTree = root.getChildWithName ("SLICES").createCopy();
        return true;
    }

    restoreSlicesFrom (root.getChildWithName ("SLICES"));
    return true;
}

void NeuroMPCProcessor::restoreSlicesFrom (const juce::ValueTree& sliceTree)
{
    if (sample == nullptr)
        return;

    std::vector<neuro::SliceDefinition> restored;

    for (const auto& t : sliceTree)
    {
        neuro::SliceDefinition s;
        s.id = t["id"];
        s.startSample = (juce::int64) t["start"];
        s.endSample = (juce::int64) t["end"];
        s.gain = t.getProperty ("gain", 1.0f);
        s.pitchSemitones = t.getProperty ("pitch", 0.0f);
        s.pitchMode = (int) t.getProperty ("pitchMode", 0) == 1 ? neuro::PitchMode::keepLength : neuro::PitchMode::resample;
        s.timeStretchRatio = t.getProperty ("stretch", 1.0f);
        s.preserveTransients = t.getProperty ("preserveTransients", true);
        s.rootNote = t.getProperty ("rootNote", -1.0f);
        s.drumClass = juce::jlimit (-1, neuro::numDrumClasses - 1, (int) t.getProperty ("drumClass", -1));
        s.reverse = t.getProperty ("reverse", false);
        s.enabled = t.getProperty ("enabled", true);
        restored.push_back (s);
    }

    slices.restore (sample->length(), std::move (restored)); // invalid layouts fall back to one slice
    publishSlices();
}

bool NeuroMPCProcessor::relinkSample (const juce::File& file, juce::String& error)
{
    const std::scoped_lock lock (modelLock);
    const auto pending = pendingSliceTree;

    if (! loadSample (file, error))
        return false;

    if (pending.isValid())
        restoreSlicesFrom (pending); // only applied if it matches the new file's length

    pendingSliceTree = {};
    return true;
}

juce::File NeuroMPCProcessor::findMissingSampleIn (const juce::File& folder) const
{
    const auto name = juce::File (missingSamplePath).getFileName();

    if (name.isEmpty() || ! folder.isDirectory())
        return {};

    juce::RangedDirectoryIterator it (folder, true, name, juce::File::findFiles);
    return it != juce::RangedDirectoryIterator() ? it->getFile() : juce::File();
}

juce::File NeuroMPCProcessor::getPresetDirectory() const
{
    return presetDirectory != juce::File() ? presetDirectory
                                           : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                                 .getChildFile ("Library/Audio/Presets/NEURO MPC/NEURO MPC");
}

juce::Array<juce::File> NeuroMPCProcessor::getPresets() const
{
    auto files = getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.neurompc");
    files.sort();
    return files;
}

bool NeuroMPCProcessor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());

    if (clean.isEmpty() || ! getPresetDirectory().createDirectory())
        return false;

    auto xml = createStateTree().createXml();

    if (xml == nullptr || ! xml->writeTo (getPresetDirectory().getChildFile (clean + ".neurompc")))
        return false;

    currentPresetName = clean;
    return true;
}

bool NeuroMPCProcessor::loadPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! applyStateTree (juce::ValueTree::fromXml (*xml)))
        return false;

    currentPresetName = file.getFileNameWithoutExtension();
    return true;
}

juce::AudioProcessorEditor* NeuroMPCProcessor::createEditor()
{
    return new NeuroMPCEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeuroMPCProcessor();
}
