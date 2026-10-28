#include "PluginEditor.h"
#include "Core/TransientDetector.h"

namespace
{
    constexpr int numPads = 16;
    const juce::Colour background { 0xff0e1014 }, panel { 0xff1c1f26 }, accent { 0xffff8a3d };

    juce::String describePitch (const neuro::PitchEstimate& p)
    {
        using Status = neuro::PitchEstimate::Status;

        if (p.status == Status::tooShort)  return "too short to analyse";
        if (p.status == Status::unpitched) return "unpitched";

        const auto cents = juce::roundToInt (p.cents());
        auto text = juce::MidiMessage::getMidiNoteName (p.nearestNote(), true, true, 3) + (cents >= 0 ? " +" : " ")
                    + juce::String (cents) + " c (" + juce::String (p.frequency, 1) + " Hz)  "
                    + juce::String (juce::roundToInt (p.confidence * 100.0f)) + "%";

        return p.status == Status::uncertain ? "uncertain: " + text + (p.ambiguous ? ", likely polyphonic" : "") : text;
    }

    juce::String describeKey (const neuro::KeyEstimate& k)
    {
        auto name = [] (const neuro::KeyEstimate::Candidate& c)
        {
            return juce::String (neuro::pitchClassName (c.tonic)) + (c.minor ? " minor" : " major");
        };

        if (k.status == neuro::KeyEstimate::Status::unavailable)
            return "unavailable";

        const auto best = name (k.candidates[0]) + " (r " + juce::String (k.candidates[0].correlation, 2) + ")";
        return k.status == neuro::KeyEstimate::Status::key ? best + ", alt " + name (k.candidates[1])
                                                         : "uncertain: " + best + " / " + name (k.candidates[1]);
    }

    std::unique_ptr<juce::Label> setupKnob (juce::Slider& s, juce::Component& parent, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        parent.addAndMakeVisible (s);

        auto label = std::make_unique<juce::Label> (name, name);
        label->setFont (juce::FontOptions (11.0f));
        label->setJustificationType (juce::Justification::centred);
        label->attachToComponent (&s, false);
        return label;
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
                          + juce::MidiMessage::getMidiNoteName (proc.padNote (index), true, true, 3),
                      r.toNearestInt(), juce::Justification::centred, 2);
}

void NeuroMPCEditor::Pad::mouseDown (const juce::MouseEvent& e)
{
    auto velocity = 1.0f - 0.7f * (float) e.y / (float) juce::jmax (1, getHeight());
    proc.auditionNote (proc.padNote (index), velocity);
}

void NeuroMPCEditor::Pad::mouseUp (const juce::MouseEvent&)
{
    proc.auditionNote (proc.padNote (index), 0.0f);
}

NeuroMPCEditor::NeuroMPCEditor (NeuroMPCProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &waveform, &loadButton, &autoButton, &gridButton,
                                                             &clearButton, &sensitivity, &gridDivisions, &playMode, &info,
                                                             &sliceTitle, &durationInfo, &targetSeconds, &stretch,
                                                             &halfButton, &doubleButton, &resetButton, &allButton,
                                                             &preserveTransients, &pitchInfo, &rootNote,
                                                             &octaveDown, &octaveUp, &semitoneInfo, &fineCents, &pitchMode,
                                                             &keyMode, &scale, &scaleRoot, &lfoShape, &lfoSync })
        addAndMakeVisible (c);

    for (int pc = 0; pc < 12; ++pc)
    {
        auto& b = noteButtons[(size_t) pc];
        b.setButtonText (neuro::pitchClassName (pc));
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        b.setTooltip ("Shift the slice so its root lands on " + juce::String (neuro::pitchClassName (pc)));
        b.onClick = [this, pc]
        {
            const auto sel = waveform.getSelectedSlice();

            if (sel >= 0 && sel < proc.getSlices().size())
                setSelectedPitch (neuro::shiftForPitchClass (selectedRoot(), proc.getSlices().getSlices()[(size_t) sel].pitchSemitones, pc));
        };
        addAndMakeVisible (b);
    }

    auto currentPitch = [this]
    {
        const auto sel = waveform.getSelectedSlice();
        return sel >= 0 && sel < proc.getSlices().size() ? proc.getSlices().getSlices()[(size_t) sel].pitchSemitones : 0.0f;
    };

    octaveDown.onClick = [this, currentPitch] { setSelectedPitch (currentPitch() - 12.0f); };
    octaveUp.onClick   = [this, currentPitch] { setSelectedPitch (currentPitch() + 12.0f); };
    fineCents.setRange (-100.0, 100.0, 1.0);
    fineCents.setTextValueSuffix (" c");
    fineCents.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    fineCents.onValueChange = [this, currentPitch]
    {
        setSelectedPitch (std::round (currentPitch()) + (float) fineCents.getValue() / 100.0f);
    };
    pitchMode.addItemList ({ "Resample", "Keep length" }, 1);
    pitchMode.setTooltip ("Resample: classic sampler (pitch changes duration). Keep length: rendered pitch-shift");
    pitchMode.onChange = [this]
    {
        const auto sel = waveform.getSelectedSlice();

        if (sel >= 0 && sel < proc.getSlices().size())
            proc.editSlices ([sel, mode = pitchMode.getSelectedId() == 2 ? neuro::PitchMode::keepLength : neuro::PitchMode::resample] (auto& set)
                             { set.setPitchMode (sel, mode); });
    };

    for (int i = 0; i < numPads; ++i)
        addAndMakeVisible (*pads.emplace_back (std::make_unique<Pad> (proc, i)));

    sensitivity.setRange (0.0, 1.0);
    sensitivity.setValue (0.6, juce::dontSendNotification);
    sensitivity.setTooltip ("Transient sensitivity");

    for (int n : { 4, 8, 16, 32, 64 })
        gridDivisions.addItem (juce::String (n) + " slices", n);

    gridDivisions.setSelectedId (16, juce::dontSendNotification);
    playMode.addItemList ({ "One-Shot", "Gate" }, 1);

    for (auto [knob, id, name] : std::initializer_list<std::tuple<juce::Slider*, const char*, const char*>> {
             { &gain, ParamIDs::gain, "Gain" }, { &attack, ParamIDs::attack, "Attack" }, { &release, ParamIDs::release, "Release" },
             { &transpose, ParamIDs::transpose, "Transpose" }, { &fineTune, ParamIDs::fineTune, "Fine" },
             { &bendRange, ParamIDs::bendRange, "Bend" }, { &lfoDepth, ParamIDs::lfoDepth, "LFO Depth" },
             { &lfoRate, ParamIDs::lfoRate, "LFO Rate" }, { &lfoPhase, ParamIDs::lfoPhase, "LFO Phase" } })
    {
        knobLabels.push_back (setupKnob (*knob, *this, name));
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, id, *knob));
    }

    keyMode.addItemList ({ "Pads", "Keys" }, 1);
    keyMode.setTooltip ("Pads: note 36+n plays slice n. Keys: the selected slice plays chromatically from its root");

    for (const auto& sc : neuro::scales)
        scale.addItem (sc.name, scale.getNumItems() + 1);

    for (int pc = 0; pc < 12; ++pc)
        scaleRoot.addItem (neuro::pitchClassName (pc), pc + 1);

    scale.setTooltip ("Keys mode: snap incoming notes to this scale");
    lfoShape.addItemList ({ "Sine", "Triangle", "Saw", "Square", "S&H" }, 1);
    lfoSync.addItemList ({ "Free", "1 bar", "1/2", "1/4", "1/8", "1/16" }, 1);

    for (auto [box, id] : std::initializer_list<std::pair<juce::ComboBox*, const char*>> {
             { &playMode, ParamIDs::playMode }, { &keyMode, ParamIDs::keyMode }, { &scale, ParamIDs::scale },
             { &scaleRoot, ParamIDs::scaleRoot }, { &lfoShape, ParamIDs::lfoShape }, { &lfoSync, ParamIDs::lfoSync } })
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *box));

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

    rootNote.addItem ("Root: detected", 1);

    for (int note = 0; note < 128; ++note)
        rootNote.addItem ("Root: " + juce::MidiMessage::getMidiNoteName (note, true, true, 3), note + 2);

    rootNote.setTooltip ("Manual pitch correction: overrides the detected pitch for this slice");
    rootNote.onChange = [this]
    {
        const auto sel = waveform.getSelectedSlice();

        if (sel >= 0 && sel < proc.getSlices().size())
            proc.editSlices ([sel, id = rootNote.getSelectedId()] (auto& set) { set.setRootNote (sel, id <= 1 ? -1.0f : (float) (id - 2)); });
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
    setResizeLimits (1000, 700, 2400, 1600);
    setSize (1120, 760);
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

    if (const auto* a = proc.getSampleAnalysis())
        text << "\nSample pitch: " << describePitch (a->pitch) << "\nKey: " << describeKey (a->key);
    else if (proc.getSample() != nullptr)
        text << "\nAnalysing...";
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

float NeuroMPCEditor::selectedRoot() const
{
    const auto sel = waveform.getSelectedSlice();

    if (sel < 0 || sel >= proc.getSlices().size())
        return 60.0f;

    const auto& s = proc.getSlices().getSlices()[(size_t) sel];
    const auto* a = proc.getSliceAnalysis (sel);

    if (s.rootNote >= 0.0f)
        return s.rootNote;

    return a != nullptr && a->pitch.status == neuro::PitchEstimate::Status::pitched ? (float) a->pitch.midi : 60.0f;
}

void NeuroMPCEditor::setSelectedPitch (float semitones)
{
    const auto sel = waveform.getSelectedSlice();

    if (sel >= 0 && sel < proc.getSlices().size())
        proc.editSlices ([sel, semitones] (auto& set) { set.setPitch (sel, semitones); });

    updateSlicePanel();
}

void NeuroMPCEditor::updateSlicePanel()
{
    const auto sel = waveform.getSelectedSlice();
    auto sample = proc.getSample();
    const auto valid = sample != nullptr && sel >= 0 && sel < proc.getSlices().size();

    for (auto* c : std::initializer_list<juce::Component*> { &stretch, &halfButton, &doubleButton, &resetButton, &allButton,
                                                             &targetSeconds, &preserveTransients, &rootNote, &octaveDown,
                                                             &octaveUp, &fineCents, &pitchMode })
        c->setEnabled (valid);

    for (auto& b : noteButtons)
        b.setEnabled (valid);

    if (! valid)
    {
        sliceTitle.setText ("Select a slice", juce::dontSendNotification);
        durationInfo.setText ({}, juce::dontSendNotification);
        pitchInfo.setText ({}, juce::dontSendNotification);
        semitoneInfo.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto& slice = proc.getSlices().getSlices()[(size_t) sel];
    const auto original = (double) slice.length() / sample->sampleRate;
    const auto status = proc.getStretchStatus (sel);

    sliceTitle.setText ("SLICE " + juce::String (sel + 1) + "  (pad " + juce::String (sel + 1) + ")", juce::dontSendNotification);
    stretch.setValue (slice.timeStretchRatio, juce::dontSendNotification);
    preserveTransients.setToggleState (slice.preserveTransients, juce::dontSendNotification);
    rootNote.setSelectedId (slice.rootNote < 0.0f ? 1 : juce::roundToInt (slice.rootNote) + 2, juce::dontSendNotification);

    // 12-note selector shows the pitch class the slice's root is shifted to.
    const auto target = ((juce::roundToInt (selectedRoot() + slice.pitchSemitones) % 12) + 12) % 12;

    for (int pc = 0; pc < 12; ++pc)
        noteButtons[(size_t) pc].setToggleState (pc == target, juce::dontSendNotification);

    const auto semis = juce::roundToInt (slice.pitchSemitones);
    semitoneInfo.setText ((semis >= 0 ? "+" : "") + juce::String (semis) + " st", juce::dontSendNotification);

    if (! fineCents.isMouseButtonDown())
        fineCents.setValue (juce::roundToInt ((slice.pitchSemitones - (float) semis) * 100.0f), juce::dontSendNotification);

    pitchMode.setSelectedId (slice.pitchMode == neuro::PitchMode::keepLength ? 2 : 1, juce::dontSendNotification);

    const auto* analysis = proc.getSliceAnalysis (sel);
    pitchInfo.setText ("Pitch: " + (analysis != nullptr ? describePitch (analysis->pitch) : juce::String ("analysing...")),
                       juce::dontSendNotification);
    durationInfo.setText (juce::String (original, 3) + " s  ->  " + juce::String (original * slice.timeStretchRatio, 3) + " s   "
                              + (status == NeuroMPCProcessor::StretchStatus::rendering ? "rendering..."
                                 : status == NeuroMPCProcessor::StretchStatus::ready ? "rendered" : "original"),
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
    top.removeFromLeft (6);
    keyMode.setBounds (top.removeFromLeft (80));

    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (juce::jmin (400, area.getHeight() * 3 / 5));
    waveform.setBounds (area.withTrimmedBottom (8));

    // Pads, bottom row = pad 1 as on MPC hardware.
    auto padArea = bottom.removeFromLeft (juce::jmin (300, bottom.getHeight())).removeFromBottom (300).toFloat();
    const auto cell = padArea.getWidth() / 4.0f;

    for (int i = 0; i < numPads; ++i)
        pads[(size_t) i]->setBounds (juce::Rectangle<float> (padArea.getX() + (float) (i % 4) * cell,
                                                             padArea.getBottom() - (float) (i / 4 + 1) * cell, cell, cell)
                                         .toNearestInt());

    bottom.removeFromLeft (16);

    // Global knobs (labels sit above), then global pitch/LFO selectors.
    bottom.removeFromTop (16);
    auto knobs = bottom.removeFromTop (76);

    for (auto* k : { &gain, &attack, &release, &transpose, &fineTune, &bendRange, &lfoDepth, &lfoRate, &lfoPhase })
        k->setBounds (knobs.removeFromLeft (juce::jmin (84, bottom.getWidth() / 9)));

    bottom.removeFromTop (6);
    auto selectors = bottom.removeFromTop (24);

    for (auto [box, width] : { std::pair { &scale, 140 }, { &scaleRoot, 60 }, { &lfoShape, 100 }, { &lfoSync, 80 } })
    {
        box->setBounds (selectors.removeFromLeft (width));
        selectors.removeFromLeft (6);
    }

    // Selected slice
    bottom.removeFromTop (10);
    sliceTitle.setBounds (bottom.removeFromTop (22));
    auto row = bottom.removeFromTop (26);
    stretch.setBounds (row.removeFromLeft (juce::jmin (320, row.getWidth() - 220)));

    for (auto* b : { &halfButton, &doubleButton, &resetButton })
    {
        row.removeFromLeft (4);
        b->setBounds (row.removeFromLeft (44));
    }

    row.removeFromLeft (4);
    allButton.setBounds (row.removeFromLeft (84));

    bottom.removeFromTop (4);
    auto row2 = bottom.removeFromTop (24);
    targetSeconds.setBounds (row2.removeFromLeft (70));
    row2.removeFromLeft (8);
    preserveTransients.setBounds (row2.removeFromLeft (150));
    durationInfo.setBounds (row2);

    bottom.removeFromTop (4);
    auto row3 = bottom.removeFromTop (26);

    for (auto& b : noteButtons)
        b.setBounds (row3.removeFromLeft (30));

    row3.removeFromLeft (6);
    octaveDown.setBounds (row3.removeFromLeft (40));
    octaveUp.setBounds (row3.removeFromLeft (40));
    row3.removeFromLeft (6);
    semitoneInfo.setBounds (row3.removeFromLeft (48));
    fineCents.setBounds (row3.removeFromLeft (juce::jmax (100, row3.getWidth() - 120)));
    row3.removeFromLeft (6);
    pitchMode.setBounds (row3);

    bottom.removeFromTop (4);
    auto row4 = bottom.removeFromTop (24);
    rootNote.setBounds (row4.removeFromLeft (130));
    row4.removeFromLeft (8);
    pitchInfo.setBounds (row4);

    info.setBounds (bottom.reduced (0, 4));
}
