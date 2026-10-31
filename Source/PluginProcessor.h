#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Audio/VoiceEngine.h"
#include "AI/DrumExtractor.h"
#include "Core/DrumNMF.h"
#include "Core/Arpeggiator.h"
#include "Core/MidiMap.h"
#include "Core/RtShared.h"
#include "Core/PatternTools.h"
#include "Core/Sequencer.h"
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
    inline constexpr auto arpOn        = "arpOn";
    inline constexpr auto arpLatch     = "arpLatch";
    inline constexpr auto arpDirection = "arpDirection";
    inline constexpr auto arpRate      = "arpRate";
    inline constexpr auto arpGate      = "arpGate";
    inline constexpr auto arpOctaves   = "arpOctaves";
    inline constexpr auto arpSwing     = "arpSwing";
    inline constexpr auto arpChance    = "arpProbability";
    inline constexpr auto arpRepeats   = "arpRepeats";
    inline constexpr auto arpVelocity  = "arpVelocity";
    inline constexpr auto arpRetrigger = "arpRetrigger";
    inline constexpr auto arpSeed      = "arpSeed";
    inline constexpr auto arpAccent    = "arpAccent";
    inline constexpr auto arpPhrase    = "arpPhrase";
    inline constexpr auto arpVariation = "arpVariation";
    inline constexpr auto arpVaryPitch = "arpVaryPitch";
    inline constexpr auto arpVaryVel   = "arpVaryVelocity";
    inline constexpr auto arpVaryRhythm = "arpVaryRhythm";
    inline constexpr auto arpReturn    = "arpReturn";
    inline constexpr auto arpHumanTime = "arpHumanTime";
    inline constexpr auto arpHumanVel  = "arpHumanVelocity";
    inline constexpr auto seqMode      = "seqMode";
    inline constexpr auto seqPattern   = "seqPattern";
    inline constexpr auto seqRate      = "seqRate";
    inline constexpr auto seqSwing     = "seqSwing";
    inline constexpr auto seqFill      = "seqFill";
    inline constexpr auto weaveAccent  = "weaveAccent";
    inline constexpr auto weaveAccentAmount = "weaveAccentAmount";
    inline constexpr auto weaveFill    = "weaveFill";
    inline constexpr auto weaveFillEvery = "weaveFillEvery";
    inline constexpr auto weavePitch   = "weavePitch";
    inline constexpr auto padBank      = "padBank";
    inline constexpr auto padBankFollow = "padBankFollow";
    inline constexpr auto padChannel   = "padChannel";
    inline constexpr auto chordLock    = "chordLock";
    inline constexpr auto chordInput   = "chordInput";
    inline constexpr auto chordSplit   = "chordSplit";
    // Per drum-class level, in DrumClass order.
    inline constexpr const char* classLevel[] { "levelKick", "levelSnare", "levelClosedHat", "levelOpenHat", "levelTom1", "levelTom2",
                                                "levelFloorTom", "levelCrash", "levelRide", "levelPerc", "levelTonal" };
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

    // State may be requested from a non-message thread by some hosts: the editable model is guarded by modelLock.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static constexpr int stateVersion = 2;
    juce::ValueTree createStateTree() const;
    bool applyStateTree (juce::ValueTree);         // false if the tree is not a NEURO MPC state
    bool wasLoadedFromNewerVersion() const noexcept { return loadedNewerVersion; }

    // Missing samples: the saved slice layout is kept until the file is relinked.
    bool relinkSample (const juce::File&, juce::String& error);
    juce::File findMissingSampleIn (const juce::File& folder) const; // recursive search by file name

    // Presets: state files (*.neurompc) in the user preset folder.
    juce::File getPresetDirectory() const;
    void setPresetDirectory (const juce::File& dir)  { presetDirectory = dir; }
    juce::Array<juce::File> getPresets() const;
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File&);
    juce::String getCurrentPresetName() const        { return currentPresetName; }

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

    // Arpeggiator rhythm pattern + grammar rules (message thread; published to the audio thread on edit).
    const neuro::ArpProgram& getArpProgram() const noexcept     { return arpProgram; }
    void editArpProgram (const std::function<void (neuro::ArpProgram&)>&);

    // Step sequencer patterns (message thread; all 16 are published to the audio thread on edit).
    static constexpr int numSeqPatterns = 16;
    using SeqBank = std::array<neuro::SeqPattern, numSeqPatterns>;
    const neuro::SeqPattern& getSeqPattern (int index) const    { return seqPatterns[(size_t) juce::jlimit (0, numSeqPatterns - 1, index)]; }
    // Undo: `record` snapshots before the edit (coalescing edits less than 0.4 s apart, e.g. a drag), `always` forces a
    // separate step (transformations), `none` skips history (A/B compare swaps).
    enum class Undo { none, record, always };
    void editSeqPattern (int index, const std::function<void (neuro::SeqPattern&)>&, Undo = Undo::record);
    bool undoSeqPattern (int index);
    bool redoSeqPattern (int index);
    bool canUndoSeqPattern (int index) const { return ! seqUndo[(size_t) juce::jlimit (0, numSeqPatterns - 1, index)].empty(); }
    // Loop Deconstructor: slices the loaded loop at its hits, classifies each hit (kick, snare, hats, toms, cymbals...),
    // and writes a pattern with one lane per kit piece (exact slice, velocity and microtiming per hit).
    struct DeconstructSummary { int hits = 0, steps = 0; double bpm = 0.0; std::array<int, neuro::numDrumClasses> perClass {}; juce::File stems; bool taught = false; };
    // bpm <= 0: estimate the loop's tempo from the audio. sensitivity 0.7 catches hits on decaying tails.
    DeconstructSummary deconstructLoop (int patternIndex, double bpm = 0.0, float sensitivity = 0.7f);
    // Separate drums (NMF): splits a mixed drum loop into Kick / Snare / Cymbals / Residual stems (they sum back to
    // the original), detects every drum's hits (simultaneous hits count for each), writes the stems to a WAV and loads it,
    // and builds a pattern whose lanes play the isolated drums on their own outputs. The residual plays bar by bar.
    DeconstructSummary separateDrums (int patternIndex, double bpm = 0.0);
    // Teach the kit: example hits (kick / snare / hats-cymbals = nmf::Component) of the loaded loop become Separate drums'
    // templates. Examples belong to the loaded file; teaching on another file starts over. Returns that drum's count.
    int teachDrum (int component, int sliceIndex);
    int teachDrumRange (int component, int64_t start, int64_t end);
    void clearTaughtDrums();
    std::array<int, neuro::nmf::numDrumComponents> getTaughtCounts() const;

    // Extract drums (AI, Demucs): pulls the drum stem out of a full song on a worker thread, then (message thread)
    // writes "<name> (drums).wav" and loads it, ready for Separate drums. The model (84 MB, MIT) is downloaded on request.
    enum class AiState { idle, downloading, extracting, ready, failed };
    static juce::File getDrumModelFile();
    static bool isDrumModelInstalled() { return getDrumModelFile().existsAsFile(); }
    void downloadDrumModel();                   // async; progress via getAiState/getAiProgress
    bool startExtractDrums();                   // async; false if busy, no sample or no model
    juce::File finishExtractDrums();            // message thread, after AiState::ready: writes + loads the stem
    juce::File extractDrumsBlocking (juce::String& error); // tests and offline use
    void cancelAi();
    AiState getAiState() const noexcept { return (AiState) aiState.load(); }
    float getAiProgress() const noexcept { return aiProgress.load(); }
    juce::String getAiMessage() const;

    // Converts exact-slice steps into timbre-map points, so the pattern re-orchestrates itself on any kit.
    int retargetPatternToTimbre (int patternIndex);
    double getLastHostBpm() const noexcept { return lastHostBpm.load(); }
    uint16_t getHeldChord() const noexcept { return chordMaskAtomic.load(); } // Chord-Lock pitch classes (bit 0 = C)
    bool canRedoSeqPattern (int index) const { return ! seqRedo[(size_t) juce::jlimit (0, numSeqPatterns - 1, index)].empty(); }
    int getSelectedSeqPattern() const noexcept                  { return (int) seqPatternParam->load() - 1; }
    int64_t getSeqPlayStep() const noexcept                     { return seqPlayStep.load(); } // -1 when stopped

    // ---- MIDI Learn ----
    // Targets: "param:<id>" (CC = value, note = toggle / next step / velocity), "param:<id>@<step>" (sets that step),
    // or "ui:<id>" (an editor control; runs on the message thread while the editor is open). Mapped messages are consumed.
    const neuro::MidiMap& getMidiMap() const noexcept           { return midiMap; }
    void editMidiMap (const std::function<void (neuro::MidiMap&)>&);
    void startMidiLearn (const juce::String& target);
    void stopMidiLearn();
    bool isMidiLearning() const noexcept                        { return learnArmed.load(); }
    juce::String getMidiLearnTarget() const                     { return learnTarget; }
    juce::String describeMidiBindings (const juce::String& target) const;
    void applyMpkMiniMk2Template(); // measured from a connected MPK mini mk2 (see .cpp)

    // Hardware pads: the controller notes of hardware pads 1..16. With "pads follow bank" on, hardware pad n plays
    // NEURO pad n of the selected bank (by position, like MPC pads). Learn by hitting the 16 pads in NEURO pad order.
    std::array<int, neuro::padsPerBank> getHardwarePadNotes() const noexcept { return hardwarePadNotes; }
    void setHardwarePadNotes (const std::array<int, neuro::padsPerBank>&);
    void startPadLayoutLearn();
    void stopPadLayoutLearn();
    int getPadLayoutLearnProgress() const noexcept { return padLayoutLearnArmed.load() ? (int) padLayoutCaptured.size() : -1; }
    // The editor registers its "ui:" controls and a handler (value 0..1 for CC, -1 for a note trigger).
    void setUiTargets (std::vector<std::string> ids, std::function<void (int, float)> handler);
    void clearUiTargets();
    void processMidiQueues(); // message thread (also run by the internal timer)

    // Arpeggiator live activity for the UI.
    int getArpLastNote() const noexcept                         { return arpLastNote.load(); }
    int getArpHeldCount() const noexcept                        { return arpHeldCount.load(); }
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
    void restoreSlicesFrom (const juce::ValueTree& sliceTree);
    static void migrate (juce::ValueTree& root);
    void publishPads();
    void pushUiNote (int note, float velocity, int slice);
    void refreshStretch();
    void refreshAnalysis();
    void updateKeyboardMap();
    void renderWithModulation (const neuro::OutputBuses&, int from, int to);
    void updateArpSettings (double bpm);
    void publishMidiRouting();
    bool handleMappedMidi (const juce::MidiMessage&); // audio thread; true = consumed
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

    // Audio-thread arpeggiator and the block's sorted event list (fixed capacity: no allocation).
    struct Event
    {
        enum Type { noteOff, control, noteOn };
        int offset; Type type; int note; float value; int slice; float pitch;
    };
    neuro::Arpeggiator arp;
    neuro::Sequencer sequencer;
    std::unique_ptr<SeqBank> seqPatternStore = std::make_unique<SeqBank>(); // ~0.5 MB: heap, not inline
    SeqBank& seqPatterns = *seqPatternStore;
    neuro::RtShared<SeqBank> rtSeq;
    double seqFreeOrigin = 0.0;
    bool seqWasRunning = false;
    std::atomic<int64_t> seqPlayStep { -1 };
    std::atomic<double> lastHostBpm { 120.0 };
    std::atomic<uint16_t> chordMaskAtomic { 0 };
    uint16_t chordMask = 0;   // audio thread: latched chord
    int lastChordMode = 0;    // message thread: re-plans variant renders when the mode changes
    int chordKeysDown = 0;
    std::vector<neuro::StretchKey> chordRequested;
    std::atomic<float> *chordLockParam = nullptr, *chordInputParam = nullptr, *chordSplitParam = nullptr;
    std::atomic<float> *seqModeParam = nullptr, *seqPatternParam = nullptr, *seqRateParam = nullptr, *seqSwingParam = nullptr,
                       *seqFillParam = nullptr, *weaveAccentParam = nullptr, *weaveAccentAmountParam = nullptr,
                       *weaveFillParam = nullptr, *weaveFillEveryParam = nullptr, *weavePitchParam = nullptr;
    std::array<std::vector<neuro::SeqPattern>, numSeqPatterns> seqUndo, seqRedo;
    std::array<double, numSeqPatterns> seqLastUndoTime {};

    // MIDI Learn
    struct MidiRouting
    {
        struct Target { enum Kind { none, parameter, control } kind = none; juce::RangedAudioParameter* param = nullptr; int step = -1; int ui = -1; };
        neuro::MidiMapTable table;
        std::vector<Target> targets; // by binding index
    };
    neuro::MidiMap midiMap;
    neuro::RtShared<MidiRouting> rtMidi;
    std::vector<std::string> uiTargets;
    std::function<void (int, float)> uiHandler;
    juce::String learnTarget;
    std::atomic<bool> learnArmed { false };
    struct LearnedMessage { int source, channel, number; };
    struct UiAction { int index; float value; };
    juce::AbstractFifo learnFifo { 16 }, actionFifo { 256 };
    std::array<LearnedMessage, 16> learned {};
    std::array<UiAction, 256> uiActions {};
    std::array<int, 128> padNoteHeld {}; // incoming note -> pad note it was translated to (for the note-off)
    struct HardwarePads { std::array<int8_t, 128> indexOfNote; };
    std::array<int, neuro::padsPerBank> hardwarePadNotes {};
    neuro::RtShared<HardwarePads> rtHardwarePads;
    std::atomic<bool> padLayoutLearnArmed { false };
    std::vector<int> padLayoutCaptured;
    void publishHardwarePads();
    std::atomic<float> *padBankParam = nullptr, *padBankFollowParam = nullptr, *padChannelParam = nullptr;
    std::array<std::atomic<float>*, neuro::numDrumClasses> classLevelParams {};
    neuro::ArpProgram arpProgram;
    neuro::RtShared<neuro::ArpProgram> rtArpProgram;
    std::array<Event, 1024> events {};
    double freeBeatOrigin = 0.0;
    bool arpWasOn = false, arpWasPlaying = false;
    std::atomic<int> arpLastNote { -1 }, arpHeldCount { 0 };
    std::atomic<float> *arpOnParam = nullptr, *arpLatchParam = nullptr, *arpDirectionParam = nullptr, *arpRateParam = nullptr,
                       *arpGateParam = nullptr, *arpOctavesParam = nullptr, *arpSwingParam = nullptr, *arpChanceParam = nullptr,
                       *arpRepeatsParam = nullptr, *arpVelocityParam = nullptr, *arpRetriggerParam = nullptr, *arpSeedParam = nullptr,
                       *arpAccentParam = nullptr, *arpPhraseParam = nullptr, *arpVariationParam = nullptr, *arpVaryPitchParam = nullptr,
                       *arpVaryVelParam = nullptr, *arpVaryRhythmParam = nullptr, *arpReturnParam = nullptr,
                       *arpHumanTimeParam = nullptr, *arpHumanVelParam = nullptr;

    neuro::PadBank pads;
    neuro::RtShared<neuro::PadMap> rtPads;
    std::atomic<int> lastMidiNote { -1 }, midiNoteCount { 0 };
    static constexpr int sliceAuditionTag = 1000; // note tags above the MIDI range

    mutable std::recursive_mutex modelLock;

    struct ModelDownloadListener : juce::URL::DownloadTaskListener
    {
        explicit ModelDownloadListener (NeuroMPCProcessor& p) : owner (p) {}
        void finished (juce::URL::DownloadTask*, bool success) override;
        void progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total) override;
        NeuroMPCProcessor& owner;
    };

    std::atomic<int> aiState { 0 };
    std::atomic<float> aiProgress { 0.0f };
    mutable std::mutex aiMutex;                 // aiMessage, aiResult, aiSource
    juce::String aiMessage;
    juce::AudioBuffer<float> aiResult;
    std::shared_ptr<const neuro::SampleData> aiSource;
    neuro::demucs::Job aiJob;
    std::thread aiThread;
    ModelDownloadListener modelListener { *this };
    std::unique_ptr<juce::URL::DownloadTask> modelDownload;
    void setAiState (AiState, const juce::String& message = {});
    bool computeDrums (const neuro::SampleData&, juce::AudioBuffer<float>& out, juce::String& error);
    std::vector<neuro::nmf::Example> taughtDrums; // guarded by modelLock
    juce::File taughtFile;
    juce::ValueTree pendingSliceTree; // slices of a missing sample, applied on relink
    bool loadedNewerVersion = false;
    juce::File presetDirectory;
    juce::String currentPresetName;

    int keyboardSlice = 0; // message thread
    std::atomic<int> keyboardSliceIndex { 0 };
    std::atomic<float> keyboardRootNote { 60.0f };
    juce::SmoothedValue<float> gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroMPCProcessor)
};
