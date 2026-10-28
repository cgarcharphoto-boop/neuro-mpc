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
    // MPC-style pad: velocity from vertical click position (top = loudest), lit while its slice plays.
    struct Pad : public juce::Component
    {
        Pad (NeuroMPCProcessor& p, int i) : proc (p), index (i) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        NeuroMPCProcessor& proc;
        int index;
        bool lit = false;
    };

    void timerCallback() override;
    void load (const juce::File&);
    void updateInfo();
    void updateSlicePanel();
    void setSelectedStretch (float ratio, bool allSlices = false);

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
    std::vector<std::unique_ptr<Pad>> pads;
    std::unique_ptr<SliderAttachment> gainAttachment, attackAttachment, releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    int shownSampleVersion = -1;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuroMPCEditor)
};
