#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int stateVersion = 1;
    constexpr int64_t maxSampleFloats = 200'000'000; // ~800 MB: documented memory limit for one sample
}

NeuroMPCProcessor::NeuroMPCProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();
    gainParam    = apvts.getRawParameterValue (ParamIDs::gain);
    attackParam  = apvts.getRawParameterValue (ParamIDs::attack);
    releaseParam = apvts.getRawParameterValue (ParamIDs::release);
    modeParam    = apvts.getRawParameterValue (ParamIDs::playMode);
}

juce::AudioProcessorValueTreeState::ParameterLayout NeuroMPCProcessor::createLayout()
{
    using namespace juce;
    return {
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
                                                StringArray { "One-Shot", "Gate" }, 0)
    };
}

bool NeuroMPCProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NeuroMPCProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
}

void NeuroMPCProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto* s  = rtSample.acquire();
    const auto* sl = rtSlices.acquire();

    if (s != lastSample)
    {
        engine.killAll(); // voices must never outlive the buffer they read
        lastSample = s;
    }

    const neuro::VoiceParams vp { attackParam->load(), releaseParam->load(), modeParam->load() < 0.5f };

    auto handle = [&] (int note, float velocity)
    {
        if (velocity > 0.0f)
        {
            if (s != nullptr && sl != nullptr)
                engine.noteOn (note, velocity, *s, *sl, vp);
        }
        else
        {
            engine.noteOff (note);
        }
    };

    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i) handle (uiNotes[(size_t) (scope.startIndex1 + i)].note, uiNotes[(size_t) (scope.startIndex1 + i)].velocity);
        for (int i = 0; i < scope.blockSize2; ++i) handle (uiNotes[(size_t) (scope.startIndex2 + i)].note, uiNotes[(size_t) (scope.startIndex2 + i)].velocity);
    }

    auto render = [&] (int from, int to)
    {
        if (s != nullptr && to > from)
            engine.render (buffer, from, to - from, *s);
    };

    int pos = 0;

    for (const auto meta : midi)
    {
        const auto t = juce::jlimit (pos, buffer.getNumSamples(), meta.samplePosition);
        render (pos, t);
        pos = t;

        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
            handle (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            handle (msg.getNoteNumber(), 0.0f);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine.killAll();
    }

    render (pos, buffer.getNumSamples());

    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
    gainSmoothed.applyGain (buffer, buffer.getNumSamples());
}

bool NeuroMPCProcessor::loadSample (const juce::File& file, juce::String& error)
{
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

    if (! reader->read (&data->buffer, 0, (int) length, 0, true, channels > 1))
    {
        error = "Failed to decode: " + file.getFileName();
        return false;
    }

    sample = data;
    rtSample.publish (sample);
    slices.reset (length);
    publishSlices();
    missingSamplePath.clear();
    ++sampleVersion;
    return true;
}

void NeuroMPCProcessor::editSlices (const std::function<void (neuro::SliceSet&)>& edit)
{
    edit (slices);
    publishSlices();
}

void NeuroMPCProcessor::publishSlices()
{
    rtSlices.publish (std::make_shared<const neuro::SliceSet> (slices));
}

void NeuroMPCProcessor::auditionNote (int note, float velocity)
{
    const auto scope = uiFifo.write (1);

    if (scope.blockSize1 > 0)
        uiNotes[(size_t) scope.startIndex1] = { note, velocity };
    else if (scope.blockSize2 > 0)
        uiNotes[(size_t) scope.startIndex2] = { note, velocity };
}

void NeuroMPCProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("NEURO_MPC");
    root.setProperty ("version", stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree sampleTree ("SAMPLE");
    sampleTree.setProperty ("path", sample != nullptr ? sample->file.getFullPathName() : missingSamplePath, nullptr);
    root.appendChild (sampleTree, nullptr);

    juce::ValueTree sliceTree ("SLICES");

    for (const auto& s : slices.getSlices())
    {
        juce::ValueTree t ("SLICE");
        t.setProperty ("id", s.id, nullptr);
        t.setProperty ("start", (juce::int64) s.startSample, nullptr);
        t.setProperty ("end", (juce::int64) s.endSample, nullptr);
        t.setProperty ("gain", s.gain, nullptr);
        t.setProperty ("pitch", s.pitchSemitones, nullptr);
        t.setProperty ("stretch", s.timeStretchRatio, nullptr);
        t.setProperty ("reverse", s.reverse, nullptr);
        t.setProperty ("enabled", s.enabled, nullptr);
        sliceTree.appendChild (t, nullptr);
    }

    root.appendChild (sliceTree, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void NeuroMPCProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);

    if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    const auto path = root.getChildWithName ("SAMPLE")["path"].toString();

    if (path.isEmpty())
        return;

    juce::String error;

    // A missing file is reported, never silently substituted.
    if (! loadSample (juce::File (path), error))
    {
        missingSamplePath = path;
        return;
    }

    std::vector<neuro::SliceDefinition> restored;

    for (const auto& t : root.getChildWithName ("SLICES"))
    {
        neuro::SliceDefinition s;
        s.id = t["id"];
        s.startSample = (juce::int64) t["start"];
        s.endSample = (juce::int64) t["end"];
        s.gain = t.getProperty ("gain", 1.0f);
        s.pitchSemitones = t.getProperty ("pitch", 0.0f);
        s.timeStretchRatio = t.getProperty ("stretch", 1.0f);
        s.reverse = t.getProperty ("reverse", false);
        s.enabled = t.getProperty ("enabled", true);
        restored.push_back (s);
    }

    slices.restore (sample->length(), std::move (restored));
    publishSlices();
}

juce::AudioProcessorEditor* NeuroMPCProcessor::createEditor()
{
    return new NeuroMPCEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeuroMPCProcessor();
}
