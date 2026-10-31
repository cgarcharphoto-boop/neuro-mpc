#pragma once

#include "PluginProcessor.h"
#include "UI/GesturePad.h"
#include "UI/SeqGrid.h"
#include "UI/TimbreMap.h"
#include "UI/WaveformView.h"

class NeuroMPCEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit NeuroMPCEditor (NeuroMPCProcessor&);
    ~NeuroMPCEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

    // MIDI mapping: every button, slider and selector carries a component ID "param:<id>" or "ui:<id>".
    std::vector<juce::Component*> getMappableControls() const;
    int countUnmappableControls() const;          // buttons/sliders/selectors without a mapping ID (should be 0)
    void runMappedAction (int uiIndex, float value); // value 0..1 = CC, -1 = note trigger
    void showTab (int index) { showPanel ((Panel) juce::jlimit (0, (int) Panel::kit, index)); } // 0 slice .. 6 kit (tests, tooling)

private:
    // MPC-style pad (one of 16 visible; the bank selects which 16 of the 64). Velocity from the vertical click position
    // (top = loudest); lit while its slice plays; click selects it for editing.
    struct Pad : public juce::Component
    {
        Pad (NeuroMPCEditor& e, int i) : editor (e), index (i) {}
        int padIndex() const noexcept { return editor.currentBank * neuro::padsPerBank + index; }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        NeuroMPCEditor& editor;
        int index;
        int heldNote = -1;
        bool lit = false;
    };

    void timerCallback() override;
    void attachSlider (juce::Slider&, const char* paramId);
    void attachCombo (juce::ComboBox&, const char* paramId);
    void attachButton (juce::Button&, const char* paramId);
    void registerMappables();
    void setMidiLearnMode (bool);
    void showMidiMenu();

    // Shown in MIDI Learn mode: outlines mappable controls; click one, then move/press a control on the MIDI device.
    struct LearnOverlay : public juce::Component
    {
        explicit LearnOverlay (NeuroMPCEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::Component* controlAt (juce::Point<int>) const;
        NeuroMPCEditor& editor;
    };
    void load (const juce::File&);
    void updateInfo();
    void updateSlicePanel();
    void setSelectedStretch (float ratio, bool allSlices = false);
    void setSelectedPitch (float semitones);
    float selectedRoot() const; // reference for the 12-note selector
    void selectPad (int padIndex);
    enum class Panel { slice, pad, arp, gen, seq, evolve, kit };
    void showPanel (Panel);
    void updateArpPanel();
    void updateGenPanel();
    void updateSeqPanel();
    void updateEvolvePanel();
    void updateKitPanel();
    std::vector<juce::Component*> kitPanelComponents();
    void forEachScopedLane (const std::function<void (neuro::SeqLane&, int)>&, NeuroMPCProcessor::Undo);
    std::vector<juce::Component*> evolvePanelComponents();
    void editSelectedSeqStep (const std::function<void (neuro::SeqStep&)>&);
    std::vector<juce::Component*> seqPanelComponents();
    std::vector<juce::Component*> arpPanelComponents();
    std::vector<juce::Component*> genPanelComponents();
    void updatePadPanel();
    void editSelectedPad (const std::function<void (neuro::PadDefinition&)>&);
    std::vector<juce::Component*> slicePanelComponents();
    std::vector<juce::Component*> padPanelComponents();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    NeuroMPCProcessor& proc;
    WaveformView waveform { proc };
    juce::TextButton loadButton { "Load" }, autoButton { "Auto-Slice" }, gridButton { "Grid" }, clearButton { "Clear" },
                     savePresetButton { "Save" }, relinkButton { "Relink..." }, midiLearnButton { "MIDI Learn" }, midiMenuButton { "MIDI" };
    std::vector<juce::Component*> uiControls; // "ui:" controls, in the order registered with the processor
    std::vector<float> lastMappedValue;
    juce::ComboBox presetBox;
    juce::Rectangle<int> titleArea;
    juce::Array<juce::File> presetFiles;
    void refreshPresets();
    juce::Slider sensitivity { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ComboBox gridDivisions, playMode;
    juce::Slider gain, attack, release;
    juce::Label info;

    // Selected-slice panel
    juce::Label sliceTitle, durationInfo, targetSeconds;
    juce::Slider stretch { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton halfButton { "x0.5" }, doubleButton { "x2" }, resetButton { "1:1" }, allButton { "Apply to all" };
    juce::ToggleButton preserveTransients { "Preserve transients" };
    juce::Label pitchInfo;
    juce::ComboBox rootNote;

    // Selected-slice pitch: 12-note selector, octave, fine, mode
    std::array<juce::TextButton, 12> noteButtons;
    juce::TextButton octaveDown { "Oct-" }, octaveUp { "Oct+" };
    juce::Label semitoneInfo;
    juce::Slider fineCents { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox pitchMode;

    // Global pitch
    juce::Slider transpose, fineTune, bendRange, lfoDepth, lfoRate, lfoPhase;
    juce::ComboBox keyMode, scale, scaleRoot, lfoShape, lfoSync;
    std::vector<std::unique_ptr<juce::Label>> knobLabels;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<Pad>> pads;
    std::array<juce::TextButton, neuro::numBanks> bankButtons;
    juce::TextButton sliceTab { "SLICE" }, padTab { "PAD" }, arpTab { "ARP" }, genTab { "GEN" }, seqTab { "SEQ" }, evolveTab { "EVOLVE" }, kitTab { "KIT" };
    int currentBank = 0, selectedPad = 0, copiedPad = -1, learnBaseline = -1, shownSliceCount = -1;
    Panel activePanel = Panel::slice;

    // Arpeggiator panel (all controls are host parameters)
    juce::Label arpTitle, arpActivity;
    juce::ToggleButton arpOn { "On" }, arpLatch { "Latch" };
    juce::ComboBox arpDirection, arpRate, arpRetrigger;
    juce::Slider arpGate { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpSwing { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpChance { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpOctaves { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpRepeats { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpVelocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpSeed { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    GesturePad gesturePad { proc.apvts, ParamIDs::arpChance, ParamIDs::arpOctaves, ParamIDs::arpVariation };

    // Generative panel: rhythm pattern, Euclid, Phrase Memory, humanise, Arpeggio Grammar
    juce::Label genTitle, rulesList;
    std::array<juce::TextButton, neuro::ArpProgram::maxSteps> stepButtons;
    juce::Slider patternLength { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 euclidPulses { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 euclidRotate { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpPhrase { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpVariation { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpReturn { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpAccent { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpHumanTime { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 arpHumanVel { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 ruleN { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 ruleOffset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 ruleAmount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton varyPitch { "Pitch" }, varyVelocity { "Velocity" }, varyRhythm { "Rhythm" };
    juce::TextButton euclidButton { "Euclid" }, allOnButton { "All on" }, addRule { "Add" }, removeRule { "Remove last" },
                     clearRules { "Clear rules" };
    juce::ComboBox ruleWhen, ruleAction, grammarPresets;

    // Step sequencer panel
    SeqGrid seqGrid { proc };
    juce::Label seqTitle, seqName, seqInfo;
    juce::ComboBox seqPatternBox, seqModeBox, seqRateBox, laneNote;
    juce::Slider seqSwing { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stepVelocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stepGate { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stepChance { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stepRatchet { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stepMicro { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stepPitch { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 laneLength { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton seqDuplicate { "Duplicate" }, seqClear { "Clear" }, lanePageButton { "Lanes 1-8" }, seqUndo { "Undo" }, seqRedo { "Redo" };
    juce::ToggleButton seqFill { "Fill" }, clockIn { "Clock in" }, clockOut { "Clock out" };
    juce::ComboBox stepCondition;

    // Evolve panel: transformations, mutation (genome), recombination, phrase weaving
    juce::Label evolveTitle, mutateTitle, weaveTitle, genomeInfo;
    juce::ComboBox evolveScope, velocityCurve, donorPattern, weaveAccent, weaveFill, weavePitch;
    juce::TextButton xReverse { "Reverse" }, xLeft { "< Shift" }, xRight { "Shift >" }, xDouble { "x2" }, xHalve { "/2" },
                     xExpand { "Expand" }, xContract { "Contract" }, applyCurve { "Apply" }, applyProfile { "Apply" },
                     mutateButton { "Mutate" }, compareButton { "A/B" }, combineButton { "Take genes" }, evolveUndo { "Undo" },
                     evolveRedo { "Redo" };
    std::array<juce::ToggleButton, 7> geneToggles;
    juce::Slider curveDepth { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 offbeatChance { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutIntensity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutPreserve { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutDensity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutPitch { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutVelocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutTiming { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 mutSeed { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 weaveAmount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 weaveEvery { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::optional<neuro::SeqPattern> compareSnapshot; // A/B: the pattern before the last mutation
    int compareIndex = -1;
    // Kit panel: timbre map, classes, Loop Deconstructor, timbre targets, per-class levels, Chord-Lock
    TimbreMap timbreMap { proc };
    juce::Label kitTitle, kitInfo, chordInfo;
    juce::ComboBox sliceClass, laneClass, chordMode, chordInput;
    juce::Slider hitSensitivity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 chordSplit { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::array<juce::Slider, neuro::numDrumClasses> classLevels;
    juce::TextButton separateButton { "Separate drums" }, deconstructButton { "Deconstruct loop" }, retargetButton { "Retarget to timbre" },
                     stepToSlice { "Step -> slice" }, stepToTimbre { "Step -> timbre" }, stepToPad { "Step -> pad" },
                     extractAiButton { "Extract drums (AI)" }, teachKick { "Teach kick" }, teachSnare { "Teach snare" }, teachHat { "Teach hat/cym" }, teachClear { "Clear taught" };

    int shownAiState = -1;
    void updateAiStatus();

    LearnOverlay learnOverlay { *this };   // declared last: on top of everything
    std::array<juce::TextButton, 4> stepPageButtons;

    // Selected-pad panel
    juce::Label padTitle, padName;
    juce::ComboBox padSlice, padNote, padChoke;
    juce::Slider padGain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 padPitch { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 padVelocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton padLearn { "Learn" }, padColour { "Colour" }, padCopy { "Copy" }, padPaste { "Paste" },
                     padDuplicate { "Duplicate" }, padClear { "Clear pad" }, padAutoAssign { "Auto-assign" };
    std::unique_ptr<juce::FileChooser> chooser;
    int shownSampleVersion = -1;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroMPCEditor)
};
