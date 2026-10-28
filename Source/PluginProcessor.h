#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Audio/VoiceEngine.h"
#include "Core/RtShared.h"

namespace ParamIDs
{
    inline constexpr auto gain     = "gain";
    inline constexpr auto attack   = "attack";
    inline constexpr auto release  = "release";
    inline constexpr auto playMode = "playMode";
}

class NeuroMPCProcessor : public juce::AudioProcessor
{
public:
    NeuroMPCProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Message-thread API ----
    bool loadSample (const juce::File&, juce::String& error);
    std::shared_ptr<const neuro::SampleData> getSample() const  { return sample; }
    int getSampleVersion() const noexcept                       { return sampleVersion; }
    const neuro::SliceSet& getSlices() const noexcept           { return slices; }
    void editSlices (const std::function<void (neuro::SliceSet&)>&);
    void auditionNote (int note, float velocity); // velocity 0 = note-off; lock-free into the audio thread
    int64_t getPlayhead() const noexcept                        { return engine.getPlayhead(); }
    const juce::String& getMissingSamplePath() const noexcept   { return missingSamplePath; }

    juce::AudioFormatManager formats;
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishSlices();

    std::shared_ptr<const neuro::SampleData> sample;
    neuro::SliceSet slices;
    int sampleVersion = 0;
    juce::String missingSamplePath;

    neuro::RtShared<neuro::SampleData> rtSample;
    neuro::RtShared<neuro::SliceSet> rtSlices;
    const neuro::SampleData* lastSample = nullptr;
    neuro::VoiceEngine engine;

    struct UiNote { int note; float velocity; };
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiNote, 256> uiNotes {};

    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    juce::SmoothedValue<float> gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroMPCProcessor)
};
