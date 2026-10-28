#include "PluginEditor.h"
#include "Core/TransientDetector.h"

namespace
{
    constexpr int numPads = 16;
    const juce::Colour background { 0xff0e1014 }, panel { 0xff1c1f26 }, accent { 0xffff8a3d };

    void setupKnob (juce::Slider& s, juce::Component& parent)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        parent.addAndMakeVisible (s);
    }
}

void NeuroMPCEditor::Pad::paint (juce::Graphics& g)
{
    const auto sliceCount = proc.getSlices().size();
    const auto assigned = proc.getSample() != nullptr && index < sliceCount;
    auto r = getLocalBounds().toFloat().reduced (3.0f);

    g.setColour (lit ? accent : (assigned ? panel.brighter (0.25f) : panel));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (lit ? juce::Colours::black : juce::Colours::white.withAlpha (assigned ? 0.85f : 0.3f));
    g.setFont (12.0f);
    g.drawFittedText ("PAD " + juce::String (index + 1) + "\n"
                          + juce::MidiMessage::getMidiNoteName (neuro::VoiceEngine::baseNote + index, true, true, 3),
                      r.toNearestInt(), juce::Justification::centred, 2);
}

void NeuroMPCEditor::Pad::mouseDown (const juce::MouseEvent& e)
{
    auto velocity = 1.0f - 0.7f * (float) e.y / (float) juce::jmax (1, getHeight());
    proc.auditionNote (neuro::VoiceEngine::baseNote + index, velocity);
}

void NeuroMPCEditor::Pad::mouseUp (const juce::MouseEvent&)
{
    proc.auditionNote (neuro::VoiceEngine::baseNote + index, 0.0f);
}

NeuroMPCEditor::NeuroMPCEditor (NeuroMPCProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &waveform, &loadButton, &autoButton, &gridButton,
                                                             &clearButton, &sensitivity, &gridDivisions, &playMode, &info,
                                                             &sliceTitle, &durationInfo, &targetSeconds, &stretch,
                                                             &halfButton, &doubleButton, &resetButton, &allButton,
                                                             &preserveTransients })
        addAndMakeVisible (c);

    for (int i = 0; i < numPads; ++i)
        addAndMakeVisible (*pads.emplace_back (std::make_unique<Pad> (proc, i)));

    sensitivity.setRange (0.0, 1.0);
    sensitivity.setValue (0.6, juce::dontSendNotification);
    sensitivity.setTooltip ("Transient sensitivity");

    for (int n : { 4, 8, 16, 32, 64 })
        gridDivisions.addItem (juce::String (n) + " slices", n);

    gridDivisions.setSelectedId (16, juce::dontSendNotification);
    playMode.addItemList ({ "One-Shot", "Gate" }, 1);

    setupKnob (gain, *this);
    setupKnob (attack, *this);
    setupKnob (release, *this);
    gainAttachment    = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::gain, gain);
    attackAttachment  = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::attack, attack);
    releaseAttachment = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::release, release);
    modeAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamIDs::playMode, playMode);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load sample", juce::File(), proc.formats.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      load (fc.getResult());
                              });
    };

    autoButton.onClick = [this]
    {
        if (auto s = proc.getSample())
        {
            auto onsets = neuro::detectTransients (s->buffer.getArrayOfReadPointers(), s->buffer.getNumChannels(),
                                                   s->length(), s->sampleRate, (float) sensitivity.getValue());
            proc.editSlices ([&] (auto& set) { set.setStarts (std::move (onsets)); });
        }
    };

    gridButton.onClick  = [this] { proc.editSlices ([n = gridDivisions.getSelectedId()] (auto& set) { set.grid (n); }); };
    clearButton.onClick = [this] { proc.editSlices ([] (auto& set) { set.reset (set.getTotalLength()); }); };

    stretch.setRange (neuro::SliceSet::minStretch, neuro::SliceSet::maxStretch, 0.001);
    stretch.setSkewFactorFromMidPoint (1.0);
    stretch.setTextValueSuffix (" x");
    stretch.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    stretch.setTooltip ("Time-stretch ratio: output duration = original x ratio (pitch preserved)");
    stretch.onValueChange = [this] { setSelectedStretch ((float) stretch.getValue()); };
    halfButton.onClick   = [this] { setSelectedStretch ((float) stretch.getValue() * 0.5f); };
    doubleButton.onClick = [this] { setSelectedStretch ((float) stretch.getValue() * 2.0f); };
    resetButton.onClick  = [this] { setSelectedStretch (1.0f); };
    allButton.onClick    = [this] { setSelectedStretch ((float) stretch.getValue(), true); };

    preserveTransients.setTooltip ("Stretch each transient-delimited segment separately: keeps drum attacks tight, no pre-echo");
    preserveTransients.onClick = [this]
    {
        const auto sel = waveform.getSelectedSlice();

        if (sel >= 0 && sel < proc.getSlices().size())
            proc.editSlices ([sel, on = preserveTransients.getToggleState()] (auto& set) { set.setPreserveTransients (sel, on); });
    };

    sliceTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    targetSeconds.setEditable (true);
    targetSeconds.setTooltip ("Target duration in seconds (sets the ratio)");
    targetSeconds.setColour (juce::Label::outlineColourId, panel.brighter (0.3f));
    targetSeconds.onTextChange = [this]
    {
        const auto sel = waveform.getSelectedSlice();
        auto sample = proc.getSample();

        if (sample != nullptr && sel >= 0 && sel < proc.getSlices().size())
        {
            const auto original = (double) proc.getSlices().getSlices()[(size_t) sel].length() / sample->sampleRate;
            const auto target = targetSeconds.getText().getDoubleValue();

            if (target > 0.0 && original > 0.0)
                setSelectedStretch ((float) (target / original));
        }
    };

    info.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    info.setJustificationType (juce::Justification::topLeft);

    setResizable (true, true);
    setResizeLimits (760, 520, 2400, 1600);
    setSize (980, 620);
    startTimerHz (30);
}

NeuroMPCEditor::~NeuroMPCEditor()
{
    stopTimer();
}

void NeuroMPCEditor::load (const juce::File& file)
{
    lastError.clear();

    if (! proc.loadSample (file, lastError))
        updateInfo();
}

void NeuroMPCEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        load (juce::File (files[0]));
}

void NeuroMPCEditor::updateInfo()
{
    juce::String text;

    if (auto s = proc.getSample())
        text << s->file.getFileName() << "\n"
             << juce::String ((double) s->length() / s->sampleRate, 2) << " s  |  " << s->sampleRate << " Hz  |  "
             << s->buffer.getNumChannels() << " ch\n"
             << proc.getSlices().size() << " slices  |  selected: "
             << (waveform.getSelectedSlice() >= 0 ? juce::String (waveform.getSelectedSlice() + 1) : juce::String ("-"));
    else
        text << "No sample loaded";

    if (lastError.isNotEmpty())
        text << "\n" << lastError;

    info.setText (text, juce::dontSendNotification);
}

void NeuroMPCEditor::setSelectedStretch (float ratio, bool allSlices)
{
    const auto sel = waveform.getSelectedSlice();

    if (sel < 0 || sel >= proc.getSlices().size())
        return;

    proc.editSlices ([&] (auto& set)
    {
        for (int i = 0; i < set.size(); ++i)
            if (allSlices || i == sel)
                set.setStretch (i, ratio);
    });

    updateSlicePanel();
}

void NeuroMPCEditor::updateSlicePanel()
{
    const auto sel = waveform.getSelectedSlice();
    auto sample = proc.getSample();
    const auto valid = sample != nullptr && sel >= 0 && sel < proc.getSlices().size();

    for (auto* c : std::initializer_list<juce::Component*> { &stretch, &halfButton, &doubleButton, &resetButton, &allButton,
                                                             &targetSeconds, &preserveTransients })
        c->setEnabled (valid);

    if (! valid)
    {
        sliceTitle.setText ("Select a slice", juce::dontSendNotification);
        durationInfo.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto& slice = proc.getSlices().getSlices()[(size_t) sel];
    const auto original = (double) slice.length() / sample->sampleRate;
    const auto status = proc.getStretchStatus (sel);

    sliceTitle.setText ("SLICE " + juce::String (sel + 1) + "  (pad " + juce::String (sel + 1) + ")", juce::dontSendNotification);
    stretch.setValue (slice.timeStretchRatio, juce::dontSendNotification);
    preserveTransients.setToggleState (slice.preserveTransients, juce::dontSendNotification);
    durationInfo.setText (juce::String (original, 3) + " s  ->  " + juce::String (original * slice.timeStretchRatio, 3) + " s   "
                              + (status == NeuroMPCProcessor::StretchStatus::rendering ? "rendering..."
                                 : status == NeuroMPCProcessor::StretchStatus::ready ? "stretched" : "original"),
                          juce::dontSendNotification);

    if (! targetSeconds.isBeingEdited())
        targetSeconds.setText (juce::String (original * slice.timeStretchRatio, 3), juce::dontSendNotification);
}

void NeuroMPCEditor::timerCallback()
{
    if (shownSampleVersion != proc.getSampleVersion())
    {
        shownSampleVersion = proc.getSampleVersion();
        waveform.sampleChanged();
    }

    const auto playhead = proc.getPlayhead();
    const auto playing = playhead >= 0 ? proc.getSlices().indexAt (playhead) : -1;

    for (auto& pad : pads)
    {
        if (pad->lit != (pad->index == playing))
        {
            pad->lit = pad->index == playing;
            pad->repaint();
        }
    }

    updateInfo();
    updateSlicePanel();
    waveform.repaint();
}

void NeuroMPCEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (accent);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("NEURO MPC", getLocalBounds().removeFromTop (44).reduced (12, 0), juce::Justification::centredRight);
}

void NeuroMPCEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (34);

    for (auto* b : { &loadButton, &autoButton })
    {
        b->setBounds (top.removeFromLeft (90));
        top.removeFromLeft (6);
    }

    sensitivity.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (12);
    gridDivisions.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (6);
    gridButton.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (6);
    clearButton.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (12);
    playMode.setBounds (top.removeFromLeft (100));

    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (juce::jmin (260, area.getHeight() / 2));
    waveform.setBounds (area.withTrimmedBottom (8));

    auto padArea = bottom.removeFromLeft (bottom.getHeight()).toFloat();
    const auto cell = padArea.getWidth() / 4.0f;

    // Bottom row is pad 1, as on MPC hardware.
    for (int i = 0; i < numPads; ++i)
        pads[(size_t) i]->setBounds (juce::Rectangle<float> (padArea.getX() + (float) (i % 4) * cell,
                                                             padArea.getBottom() - (float) (i / 4 + 1) * cell, cell, cell)
                                         .toNearestInt());

    bottom.removeFromLeft (16);
    auto knobs = bottom.removeFromTop (100);

    for (auto* k : { &gain, &attack, &release })
        k->setBounds (knobs.removeFromLeft (90));

    auto slicePanel = bottom.removeFromTop (88);
    sliceTitle.setBounds (slicePanel.removeFromTop (22));
    auto row = slicePanel.removeFromTop (28);
    stretch.setBounds (row.removeFromLeft (juce::jmin (320, row.getWidth() - 220)));

    for (auto* b : { &halfButton, &doubleButton, &resetButton })
    {
        row.removeFromLeft (4);
        b->setBounds (row.removeFromLeft (44));
    }

    row.removeFromLeft (4);
    allButton.setBounds (row.removeFromLeft (84));
    slicePanel.removeFromTop (6);
    auto row2 = slicePanel.removeFromTop (24);
    targetSeconds.setBounds (row2.removeFromLeft (70));
    row2.removeFromLeft (8);
    preserveTransients.setBounds (row2.removeFromLeft (150));
    durationInfo.setBounds (row2);

    info.setBounds (bottom.reduced (0, 4));
}
