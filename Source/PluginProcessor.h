#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Audio/VoiceEngine.h"
#include "Core/RtShared.h"
#include "DSP/Analysis.h"
#include "DSP/BackgroundWorker.h"

namespace ParamIDs
{
    inline constexpr auto gain     = "gain";
    inline constexpr auto attack   = "attack";
    inline constexpr auto release  = "release";
    inline constexpr auto playMode = "playMode";
    inline constexpr auto transpose = "transpose";
    inline constexpr auto fineTune  = "fineTune";
    inline constexpr auto bendRange = "bendRange";
    inline constexpr auto lfoDepth  = "lfoDepth";
    inline constexpr auto lfoRate   = "lfoRate";
    inline constexpr auto lfoShape  = "lfoShape";
    inline constexpr auto lfoSync   = "lfoSync";
    inline constexpr auto lfoPhase  = "lfoPhase";
    inline constexpr auto keyMode   = "keyMode";
    inline constexpr auto scale     = "scale";
    inline constexpr auto scaleRoot = "scaleRoot";
}

class NeuroMPCProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    NeuroMPCProcessor();
    ~NeuroMPCProcessor() override;

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

    // Pads (message thread). editPads publishes the result to the audio thread.
    const neuro::PadBank& getPads() const noexcept              { return pads; }
    void editPads (const std::function<void (neuro::PadBank&)>&);
    int getLastMidiNote() const noexcept                        { return lastMidiNote.load(); } // for MIDI learn
    int getMidiNoteCount() const noexcept                       { return midiNoteCount.load(); }
    int64_t getPlayhead() const noexcept                        { return engine.getPlayhead(); }
    const juce::String& getMissingSamplePath() const noexcept   { return missingSamplePath; }

    enum class StretchStatus { original, rendering, ready };
    StretchStatus getStretchStatus (int sliceIndex) const;
    // Pitch/key analysis, run in the background whenever the sample or a slice region changes.
    // Returns nullptr while the analysis for the current region is pending.
    const neuro::AnalysisResult* getSliceAnalysis (int sliceIndex) const;
    const neuro::AnalysisResult* getSampleAnalysis() const;

    // Keyboard mode plays one slice chromatically from its root (manual Root, else detected pitch, else C4).
    void setKeyboardSlice (int sliceIndex);
    int getKeyboardSlice() const noexcept                       { return keyboardSlice; }
    float getKeyboardRoot() const noexcept                      { return keyboardRootNote.load(); }
    bool isKeyboardMode() const noexcept                        { return keyModeParam->load() > 0.5f; }
    int padNote (int padIndex) const;                           // pad -> MIDI note for the current mode
    // Plays a slice directly (assigned to a pad or not) at its original pitch; returns a tag for stopAudition.
    int startSliceAudition (int sliceIndex, float velocity = 0.9f);
    void stopAudition (int tag);

    void processBackgroundResults();            // also driven by the internal timer, so it works with the editor closed
    bool waitForBackgroundWork (int timeoutMs); // for tests and offline use

    juce::AudioFormatManager formats;
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishSlices();
    void publishPads();
    void pushUiNote (int note, float velocity, int slice);
    void refreshStretch();
    void refreshAnalysis();
    void updateKeyboardMap();
    void renderWithModulation (juce::AudioBuffer<float>&, int from, int to);
    void timerCallback() override { processBackgroundResults(); }

    std::shared_ptr<const neuro::SampleData> sample;
    neuro::SliceSet slices;
    int sampleVersion = 0;
    juce::String missingSamplePath;

    neuro::RtShared<neuro::SampleData> rtSample;
    neuro::RtShared<neuro::SliceSet> rtSlices;
    const neuro::SampleData* lastSample = nullptr;
    const neuro::StretchBank* lastBank = nullptr;

    static constexpr size_t stretchCacheBudgetBytes = 512u << 20;
    struct CacheEntry { neuro::StretchEntry entry; uint64_t lastUse = 0; };
    neuro::BackgroundWorker<neuro::StretchJob, neuro::StretchDone> stretchRenderer { "NEURO stretch", neuro::renderStretchJob };
    neuro::RtShared<neuro::StretchBank> rtStretch;
    std::shared_ptr<const neuro::StretchBank> stretchBank;
    std::vector<CacheEntry> stretchCache;
    std::vector<neuro::StretchKey> stretchRequested;
    uint64_t cacheClock = 0;

    neuro::BackgroundWorker<neuro::AnalysisJob, neuro::AnalysisResult> analyser { "NEURO analysis", neuro::analyseRegion };
    std::map<int, neuro::AnalysisResult> analyses;                      // by slice id; 0 = whole sample
    std::map<int, std::pair<int64_t, int64_t>> analysisRequested;       // id -> requested region
    neuro::VoiceEngine engine;

    struct UiNote { int note; float velocity; int slice; }; // slice >= 0: direct slice audition tagged `note`
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiNote, 256> uiNotes {};

    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float> *transposeParam = nullptr, *fineParam = nullptr, *bendRangeParam = nullptr, *lfoDepthParam = nullptr,
                       *lfoRateParam = nullptr, *lfoShapeParam = nullptr, *lfoSyncParam = nullptr, *lfoPhaseParam = nullptr,
                       *keyModeParam = nullptr, *scaleParam = nullptr, *scaleRootParam = nullptr;

    // Audio-thread modulation state
    double hostSampleRate = 44100.0;
    std::vector<float> modBuffer = std::vector<float> (1024);
    float bendSemitones = 0.0f, sampleAndHold = 0.0f;
    double lfoCyclePos = 0.0, lfoIncrement = 0.0;
    juce::Random lfoRandom;

    neuro::PadBank pads;
    neuro::RtShared<neuro::PadMap> rtPads;
    std::atomic<int> lastMidiNote { -1 }, midiNoteCount { 0 };
    static constexpr int sliceAuditionTag = 1000; // note tags above the MIDI range

    int keyboardSlice = 0; // message thread
    std::atomic<int> keyboardSliceIndex { 0 };
    std::atomic<float> keyboardRootNote { 60.0f };
    juce::SmoothedValue<float> gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroMPCProcessor)
};
