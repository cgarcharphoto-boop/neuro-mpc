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
    auto& processor = editor.proc;
    const auto p = padIndex();
    const auto& def = processor.getPads()[p];
    const auto keys = processor.isKeyboardMode();
    const auto sliceIndex = def.sliceId > 0 ? processor.getSlices().indexOfId (def.sliceId) : -1;
    const auto assigned = processor.getSample() != nullptr && (keys || sliceIndex >= 0);
    auto r = getLocalBounds().toFloat().reduced (3.0f);

    g.setColour (lit ? accent : (assigned ? juce::Colour (def.colour).brighter (0.25f) : panel));
    g.fillRoundedRectangle (r, 6.0f);

    if (p == editor.selectedPad)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (r, 6.0f, 2.0f);
    }

    const auto label = keys ? juce::String ("KEY")
                     : ! def.name.empty() ? juce::String (def.name)
                     : sliceIndex >= 0 ? "S" + juce::String (sliceIndex + 1) : juce::String ("empty");

    g.setColour (lit ? juce::Colours::black : juce::Colours::white.withAlpha (assigned ? 0.85f : 0.3f));
    g.setFont (11.0f);
    g.drawFittedText (juce::String::charToString ((juce::juce_wchar) ('A' + p / neuro::padsPerBank)) + juce::String (p % neuro::padsPerBank + 1)
                          + "\n" + label + "\n" + juce::MidiMessage::getMidiNoteName (processor.padNote (p), true, true, 3),
                      r.toNearestInt(), juce::Justification::centred, 3);
}

void NeuroMPCEditor::Pad::mouseDown (const juce::MouseEvent& e)
{
    auto velocity = 1.0f - 0.7f * (float) e.y / (float) juce::jmax (1, getHeight());
    heldNote = editor.proc.padNote (padIndex());
    editor.proc.auditionNote (heldNote, velocity);
    editor.selectPad (padIndex());
}

void NeuroMPCEditor::Pad::mouseUp (const juce::MouseEvent&)
{
    if (heldNote >= 0)
        editor.proc.auditionNote (heldNote, 0.0f);

    heldNote = -1;
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
        addAndMakeVisible (*pads.emplace_back (std::make_unique<Pad> (*this, i)));

    for (int b = 0; b < neuro::numBanks; ++b)
    {
        auto& button = bankButtons[(size_t) b];
        button.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        button.setRadioGroupId (1);
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, accent);
        button.onClick = [this, b] { currentBank = b; selectPad (b * neuro::padsPerBank + selectedPad % neuro::padsPerBank); };
        addAndMakeVisible (button);
    }

    bankButtons[0].setToggleState (true, juce::dontSendNotification);

    for (auto* tab : { &sliceTab, &padTab, &arpTab })
    {
        tab->setRadioGroupId (2);
        tab->setClickingTogglesState (true);
        tab->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
        addAndMakeVisible (tab);
    }

    sliceTab.onClick = [this] { showPanel (Panel::slice); };
    padTab.onClick   = [this] { showPanel (Panel::pad); };
    arpTab.onClick   = [this] { showPanel (Panel::arp); };
    waveform.onSliceClicked = [this] { if (activePanel != Panel::arp) showPanel (Panel::slice); };

    // ---- Arp panel ----
    for (auto* c : arpPanelComponents())
        addAndMakeVisible (c);

    arpTitle.setText ("ARPEGGIATOR", juce::dontSendNotification);
    arpTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    arpDirection.addItemList ({ "Up", "Down", "Up-Down", "Down-Up", "As played", "Reverse played", "Random",
                                "Random (no repeat)", "Chord", "Octave cycle" }, 1);
    arpRate.addItemList ({ "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);
    arpRetrigger.addItemList ({ "Retrig: off", "Retrig: first note", "Retrig: every note" }, 1);
    arpOn.setTooltip ("Arpeggiate held notes and pads (sample-accurate; follows host tempo and transport)");
    arpLatch.setTooltip ("Keep playing after keys are released, until a new chord is played");

    for (auto [box, id] : std::initializer_list<std::pair<juce::ComboBox*, const char*>> {
             { &arpDirection, ParamIDs::arpDirection }, { &arpRate, ParamIDs::arpRate }, { &arpRetrigger, ParamIDs::arpRetrigger } })
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *box));

    for (auto [button, id] : std::initializer_list<std::pair<juce::Button*, const char*>> { { &arpOn, ParamIDs::arpOn }, { &arpLatch, ParamIDs::arpLatch } })
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *button));

    for (auto [slider, id, name] : std::initializer_list<std::tuple<juce::Slider*, const char*, const char*>> {
             { &arpGate, ParamIDs::arpGate, "Gate" }, { &arpSwing, ParamIDs::arpSwing, "Swing" }, { &arpChance, ParamIDs::arpChance, "Chance" },
             { &arpOctaves, ParamIDs::arpOctaves, "Octaves" }, { &arpRepeats, ParamIDs::arpRepeats, "Repeat" },
             { &arpVelocity, ParamIDs::arpVelocity, "Velocity" }, { &arpSeed, ParamIDs::arpSeed, "Seed" } })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 20);
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, id, *slider));
        auto label = std::make_unique<juce::Label> (name, name);
        label->setFont (juce::FontOptions (11.0f));
        label->attachToComponent (slider, true);
        knobLabels.push_back (std::move (label));
    }

    // ---- Pad panel ----
    for (auto* c : padPanelComponents())
        addAndMakeVisible (c);

    padTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    padName.setEditable (true);
    padName.setColour (juce::Label::outlineColourId, panel.brighter (0.3f));
    padName.setTooltip ("Pad name (double-click to edit)");
    padName.onTextChange = [this] { editSelectedPad ([t = padName.getText().toStdString()] (auto& d) { d.name = t; }); };

    padSlice.onChange = [this]
    {
        const auto id = padSlice.getSelectedId();
        const auto& sl = proc.getSlices().getSlices();
        const auto sliceId = id >= 2 && id - 2 < (int) sl.size() ? sl[(size_t) id - 2].id : 0;
        proc.editPads ([this, sliceId] (auto& bank) { bank.assign (selectedPad, sliceId); });
    };

    for (int note = 0; note < 128; ++note)
        padNote.addItem (juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")", note + 1);

    padNote.setTooltip ("MIDI note that triggers this pad (several pads may share a note to layer)");
    padNote.onChange = [this] { editSelectedPad ([n = padNote.getSelectedId() - 1] (auto& d) { d.note = n; }); };

    padChoke.addItem ("No choke", 1);

    for (int c = 1; c <= 16; ++c)
        padChoke.addItem ("Choke " + juce::String (c), c + 1);

    padChoke.setTooltip ("Pads in the same choke group cut each other off (e.g. open/closed hi-hat)");
    padChoke.onChange = [this] { editSelectedPad ([c = padChoke.getSelectedId() - 1] (auto& d) { d.chokeGroup = c; }); };

    padGain.setRange (-48.0, 12.0, 0.1);
    padGain.setTextValueSuffix (" dB");
    padGain.onValueChange = [this] { editSelectedPad ([v = (float) padGain.getValue()] (auto& d) { d.gain = juce::Decibels::decibelsToGain (v, -48.0f); }); };
    padPitch.setRange (-24.0, 24.0, 0.01);
    padPitch.setTextValueSuffix (" st");
    padPitch.onValueChange = [this] { editSelectedPad ([v = (float) padPitch.getValue()] (auto& d) { d.pitch = v; }); };
    padVelocity.setRange (0.0, 1.0, 0.01);
    padVelocity.setTooltip ("Velocity sensitivity: 0 = always full level, 1 = level follows velocity");
    padVelocity.onValueChange = [this] { editSelectedPad ([v = (float) padVelocity.getValue()] (auto& d) { d.velocitySensitivity = v; }); };

    for (auto [slider, name] : { std::pair { &padGain, "Level" }, { &padPitch, "Tune" }, { &padVelocity, "Vel" } })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        auto label = std::make_unique<juce::Label> (name, name);
        label->setFont (juce::FontOptions (11.0f));
        label->attachToComponent (slider, true);
        knobLabels.push_back (std::move (label));
    }

    padLearn.setClickingTogglesState (true);
    padLearn.setColour (juce::TextButton::buttonOnColourId, accent);
    padLearn.setTooltip ("Assign the next incoming MIDI note to this pad");
    padLearn.onClick = [this] { learnBaseline = padLearn.getToggleState() ? proc.getMidiNoteCount() : -1; };

    padColour.onClick = [this]
    {
        static constexpr uint32_t palette[] { 0xff2a2f3a, 0xffc0392b, 0xffd35400, 0xfff1c40f, 0xff27ae60, 0xff16a085, 0xff2980b9, 0xff8e44ad };
        const auto current = proc.getPads()[selectedPad].colour;
        auto next = palette[0];

        for (size_t i = 0; i < std::size (palette); ++i)
            if (palette[i] == current)
                next = palette[(i + 1) % std::size (palette)];

        editSelectedPad ([next] (auto& d) { d.colour = next; });
    };

    padCopy.onClick      = [this] { copiedPad = selectedPad; };
    padPaste.onClick     = [this] { if (copiedPad >= 0) proc.editPads ([this] (auto& b) { b.copy (copiedPad, selectedPad); }); };
    padDuplicate.onClick = [this]
    {
        int target = -1;
        proc.editPads ([this, &target] (auto& b) { target = b.duplicate (selectedPad); });

        if (target >= 0)
        {
            currentBank = target / neuro::padsPerBank;
            bankButtons[(size_t) currentBank].setToggleState (true, juce::dontSendNotification);
            selectPad (target);
        }
    };
    padClear.onClick      = [this] { proc.editPads ([this] (auto& b) { b.clear (selectedPad); }); };
    padAutoAssign.onClick = [this] { proc.editPads ([this] (auto& b) { b.followSlices = true; b.autoAssign (proc.getSlices()); }); };
    padAutoAssign.setTooltip ("Pad n plays slice n, and keeps following slice edits until a pad is assigned by hand");

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

    showPanel (Panel::slice);
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

std::vector<juce::Component*> NeuroMPCEditor::slicePanelComponents()
{
    std::vector<juce::Component*> list { &sliceTitle, &durationInfo, &targetSeconds, &stretch, &halfButton, &doubleButton,
                                         &resetButton, &allButton, &preserveTransients, &pitchInfo, &rootNote, &octaveDown,
                                         &octaveUp, &semitoneInfo, &fineCents, &pitchMode };

    for (auto& b : noteButtons)
        list.push_back (&b);

    return list;
}

std::vector<juce::Component*> NeuroMPCEditor::padPanelComponents()
{
    return { &padTitle, &padName, &padSlice, &padNote, &padChoke, &padGain, &padPitch, &padVelocity, &padLearn,
             &padColour, &padCopy, &padPaste, &padDuplicate, &padClear, &padAutoAssign };
}

std::vector<juce::Component*> NeuroMPCEditor::arpPanelComponents()
{
    return { &arpTitle, &arpActivity, &arpOn, &arpLatch, &arpDirection, &arpRate, &arpRetrigger, &arpGate, &arpSwing,
             &arpChance, &arpOctaves, &arpRepeats, &arpVelocity, &arpSeed };
}

void NeuroMPCEditor::showPanel (Panel p)
{
    activePanel = p;

    for (auto* c : slicePanelComponents()) c->setVisible (p == Panel::slice);
    for (auto* c : padPanelComponents())   c->setVisible (p == Panel::pad);
    for (auto* c : arpPanelComponents())   c->setVisible (p == Panel::arp);

    sliceTab.setToggleState (p == Panel::slice, juce::dontSendNotification);
    padTab.setToggleState (p == Panel::pad, juce::dontSendNotification);
    arpTab.setToggleState (p == Panel::arp, juce::dontSendNotification);

    if (p == Panel::pad) updatePadPanel();
    if (p == Panel::arp) updateArpPanel();
}

void NeuroMPCEditor::updateArpPanel()
{
    const auto note = proc.getArpLastNote();
    const auto held = proc.getArpHeldCount();
    arpActivity.setText (held > 0 ? juce::String (held) + " held   last: " + juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                                  : juce::String ("no notes held"),
                         juce::dontSendNotification);
}

void NeuroMPCEditor::selectPad (int padIndex)
{
    selectedPad = juce::jlimit (0, neuro::numPads - 1, padIndex);
    const auto sliceId = proc.getPads()[selectedPad].sliceId;

    if (const auto index = proc.getSlices().indexOfId (sliceId); index >= 0 && sliceId > 0)
        waveform.setSelectedSlice (index);

    if (activePanel != Panel::arp)
        showPanel (Panel::pad);

    for (auto& p : pads)
        p->repaint();
}

void NeuroMPCEditor::editSelectedPad (const std::function<void (neuro::PadDefinition&)>& edit)
{
    proc.editPads ([this, &edit] (auto& bank) { edit (bank[selectedPad]); });
}

void NeuroMPCEditor::updatePadPanel()
{
    const auto& bank = proc.getPads();
    const auto& def = bank[selectedPad];
    const auto& sl = proc.getSlices();

    padTitle.setText ("PAD " + juce::String::charToString ((juce::juce_wchar) ('A' + selectedPad / neuro::padsPerBank))
                          + juce::String (selectedPad % neuro::padsPerBank + 1)
                          + (bank.followSlices ? "   following slices" : "   custom map"),
                      juce::dontSendNotification);

    if (! padName.isBeingEdited())
        padName.setText (def.name, juce::dontSendNotification);

    if (shownSliceCount != sl.size())
    {
        shownSliceCount = sl.size();
        padSlice.clear (juce::dontSendNotification);
        padSlice.addItem ("Empty", 1);

        for (int i = 0; i < sl.size(); ++i)
            padSlice.addItem ("Slice " + juce::String (i + 1), i + 2);
    }

    const auto index = def.sliceId > 0 ? sl.indexOfId (def.sliceId) : -1;
    padSlice.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
    padNote.setSelectedId (def.note + 1, juce::dontSendNotification);
    padChoke.setSelectedId (def.chokeGroup + 1, juce::dontSendNotification);

    if (! padGain.isMouseButtonDown())     padGain.setValue (juce::Decibels::gainToDecibels (def.gain, -48.0f), juce::dontSendNotification);
    if (! padPitch.isMouseButtonDown())    padPitch.setValue (def.pitch, juce::dontSendNotification);
    if (! padVelocity.isMouseButtonDown()) padVelocity.setValue (def.velocitySensitivity, juce::dontSendNotification);

    padLearn.setToggleState (learnBaseline >= 0, juce::dontSendNotification);
    padPaste.setEnabled (copiedPad >= 0 && copiedPad != selectedPad);
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

    // MIDI learn: the first note-on after Learn was pressed becomes the pad's note.
    if (learnBaseline >= 0 && proc.getMidiNoteCount() != learnBaseline)
    {
        const auto note = proc.getLastMidiNote();
        learnBaseline = -1;
        editSelectedPad ([note] (auto& d) { d.note = note; });
    }

    const auto playhead = proc.getPlayhead();
    const auto playing = playhead >= 0 ? proc.getSlices().indexAt (playhead) : -1;
    const auto playingId = playing >= 0 ? proc.getSlices().getSlices()[(size_t) playing].id : 0;

    for (auto& pad : pads)
    {
        pad->lit = ! proc.isKeyboardMode() && playingId > 0 && proc.getPads()[pad->padIndex()].sliceId == playingId;
        pad->repaint();
    }

    if (activePanel == Panel::pad) updatePadPanel();
    if (activePanel == Panel::arp) updateArpPanel();

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

    // Bank selector over the pads; bottom row = pad 1 as on MPC hardware.
    auto padColumn = bottom.removeFromLeft (juce::jmin (300, bottom.getHeight() - 26));
    auto bankRow = padColumn.removeFromTop (22);

    const auto bankWidth = bankRow.getWidth() / neuro::numBanks;

    for (auto& b : bankButtons)
        b.setBounds (bankRow.removeFromLeft (bankWidth).reduced (2, 0));

    padColumn.removeFromTop (4);
    auto padArea = padColumn.removeFromTop (padColumn.getWidth()).toFloat();
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

    // Tabs: selected slice / selected pad share one panel area.
    bottom.removeFromTop (8);
    auto tabs = bottom.removeFromTop (24);
    sliceTab.setBounds (tabs.removeFromLeft (70));
    padTab.setBounds (tabs.removeFromLeft (70));
    arpTab.setBounds (tabs.removeFromLeft (70));
    bottom.removeFromTop (6);
    const auto panelArea = bottom.removeFromTop (142);

    auto pp = panelArea;
    auto padRow0 = pp.removeFromTop (22);
    padTitle.setBounds (padRow0.removeFromLeft (260));
    padName.setBounds (padRow0.removeFromLeft (160));
    pp.removeFromTop (6);
    auto padRow1 = pp.removeFromTop (26);

    for (auto [c, w] : std::initializer_list<std::pair<juce::Component*, int>> { { &padSlice, 110 }, { &padNote, 100 }, { &padLearn, 56 },
                                                                                 { &padChoke, 100 }, { &padColour, 64 } })
    {
        c->setBounds (padRow1.removeFromLeft (w));
        padRow1.removeFromLeft (6);
    }

    pp.removeFromTop (6);
    auto padRow2 = pp.removeFromTop (26);
    const auto sliderWidth = (padRow2.getWidth() - 3 * 40) / 3;

    for (auto* sld : { &padGain, &padPitch, &padVelocity })
    {
        padRow2.removeFromLeft (40); // attached label
        sld->setBounds (padRow2.removeFromLeft (sliderWidth));
    }

    pp.removeFromTop (8);
    auto padRow3 = pp.removeFromTop (26);

    for (auto* b : { &padCopy, &padPaste, &padDuplicate, &padClear, &padAutoAssign })
    {
        b->setBounds (padRow3.removeFromLeft (b == &padAutoAssign ? 96 : 76));
        padRow3.removeFromLeft (6);
    }

    auto ap = panelArea;
    auto arpRow0 = ap.removeFromTop (22);
    arpTitle.setBounds (arpRow0.removeFromLeft (130));
    arpOn.setBounds (arpRow0.removeFromLeft (60));
    arpLatch.setBounds (arpRow0.removeFromLeft (80));
    arpActivity.setBounds (arpRow0);
    ap.removeFromTop (6);
    auto arpRow1 = ap.removeFromTop (24);

    for (auto [box, w] : { std::pair { &arpDirection, 160 }, { &arpRate, 80 }, { &arpRetrigger, 160 } })
    {
        box->setBounds (arpRow1.removeFromLeft (w));
        arpRow1.removeFromLeft (6);
    }

    for (auto row : { std::array<juce::Slider*, 4> { &arpGate, &arpSwing, &arpChance, nullptr },
                      std::array<juce::Slider*, 4> { &arpOctaves, &arpRepeats, &arpVelocity, &arpSeed } })
    {
        ap.removeFromTop (8);
        auto r = ap.removeFromTop (24);
        const auto cellWidth = (r.getWidth() - 4 * 56) / 4;

        for (auto* sld : row)
        {
            r.removeFromLeft (56); // attached label
            auto slot = r.removeFromLeft (cellWidth);

            if (sld != nullptr)
                sld->setBounds (slot);
        }
    }

    auto sp = panelArea;
    auto& bottomPanel = sp; // slice panel rows below
    sliceTitle.setBounds (bottomPanel.removeFromTop (22));
    auto row = bottomPanel.removeFromTop (26);
    stretch.setBounds (row.removeFromLeft (juce::jmin (320, row.getWidth() - 220)));

    for (auto* b : { &halfButton, &doubleButton, &resetButton })
    {
        row.removeFromLeft (4);
        b->setBounds (row.removeFromLeft (44));
    }

    row.removeFromLeft (4);
    allButton.setBounds (row.removeFromLeft (84));

    bottomPanel.removeFromTop (4);
    auto row2 = bottomPanel.removeFromTop (24);
    targetSeconds.setBounds (row2.removeFromLeft (70));
    row2.removeFromLeft (8);
    preserveTransients.setBounds (row2.removeFromLeft (150));
    durationInfo.setBounds (row2);

    bottomPanel.removeFromTop (4);
    auto row3 = bottomPanel.removeFromTop (26);

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

    bottomPanel.removeFromTop (4);
    auto row4 = bottomPanel.removeFromTop (24);
    rootNote.setBounds (row4.removeFromLeft (130));
    row4.removeFromLeft (8);
    pitchInfo.setBounds (row4);

    info.setBounds (bottom.reduced (0, 4));
}
