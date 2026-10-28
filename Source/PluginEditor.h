#pragma once

#include "PluginProcessor.h"
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
    void load (const juce::File&);
    void updateInfo();
    void updateSlicePanel();
    void setSelectedStretch (float ratio, bool allSlices = false);
    void setSelectedPitch (float semitones);
    float selectedRoot() const; // reference for the 12-note selector
    void selectPad (int padIndex);
    void showPadPanel (bool pad);
    void updatePadPanel();
    void editSelectedPad (const std::function<void (neuro::PadDefinition&)>&);
    std::vector<juce::Component*> slicePanelComponents();
    std::vector<juce::Component*> padPanelComponents();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    NeuroMPCProcessor& proc;
    WaveformView waveform { proc };
    juce::TextButton loadButton { "Load" }, autoButton { "Auto-Slice" }, gridButton { "Grid" }, clearButton { "Clear" };
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
    juce::TextButton sliceTab { "SLICE" }, padTab { "PAD" };
    int currentBank = 0, selectedPad = 0, copiedPad = -1, learnBaseline = -1, shownSliceCount = -1;
    bool showingPad = false;

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
