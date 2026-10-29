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
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &savePresetButton, &relinkButton, &waveform, &loadButton, &autoButton, &gridButton,
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

    for (auto* tab : { &sliceTab, &padTab, &arpTab, &genTab, &seqTab, &evolveTab })
    {
        tab->setRadioGroupId (2);
        tab->setClickingTogglesState (true);
        tab->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
        addAndMakeVisible (tab);
    }

    sliceTab.onClick = [this] { showPanel (Panel::slice); };
    padTab.onClick   = [this] { showPanel (Panel::pad); };
    arpTab.onClick   = [this] { showPanel (Panel::arp); };
    genTab.onClick   = [this] { showPanel (Panel::gen); };
    seqTab.onClick   = [this] { showPanel (Panel::seq); };
    evolveTab.onClick = [this] { showPanel (Panel::evolve); };

    // ---- SEQ additions: step condition, fill, undo ----
    stepCondition.addItemList ({ "Cond: always", "Cond: 1:2", "Cond: 2:2", "Cond: 1:3", "Cond: 2:3", "Cond: 3:3", "Cond: 1:4", "Cond: 2:4",
                                 "Cond: 3:4", "Cond: 4:4", "Cond: first", "Cond: not first", "Cond: fill", "Cond: not fill",
                                 "Cond: if previous", "Cond: if not previous" }, 1);
    stepCondition.setTooltip ("Trig condition for the selected step (A:B = plays on lane cycle A of every B)");
    stepCondition.onChange = [this]
    {
        static constexpr int ratios[][2] { { 1, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };
        const auto id = stepCondition.getSelectedId();
        editSelectedSeqStep ([id] (auto& st)
        {
            using C = neuro::SeqCondition;
            st.condition = id == 1 ? C::always : id <= 10 ? C::ratio
                         : (C) ((int) C::first + (id - 11));
            if (id >= 2 && id <= 10) { st.condA = ratios[id - 2][0]; st.condB = ratios[id - 2][1]; }
        });
    };
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::seqFill, seqFill));
    seqFill.setTooltip ("Fill: steps with fill / not-fill conditions follow this switch");
    seqUndo.onClick = [this] { proc.undoSeqPattern (proc.getSelectedSeqPattern()); updateSeqPanel(); };
    seqRedo.onClick = [this] { proc.redoSeqPattern (proc.getSelectedSeqPattern()); updateSeqPanel(); };

    // ---- Evolve panel ----
    for (auto* c : evolvePanelComponents())
        addAndMakeVisible (c);

    for (auto [label, text] : { std::pair { &evolveTitle, "TRANSFORM" }, { &mutateTitle, "MUTATE" }, { &weaveTitle, "WEAVE" } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setFont (juce::FontOptions (13.0f, juce::Font::bold));
    }

    genomeInfo.setJustificationType (juce::Justification::topLeft);
    genomeInfo.setColour (juce::Label::backgroundColourId, panel);
    evolveScope.addItemList ({ "Selected lane", "All lanes" }, 1);
    evolveScope.setSelectedId (1, juce::dontSendNotification);
    velocityCurve.addItemList ({ "Ramp up", "Ramp down", "Downbeats", "Offbeats", "Flat" }, 1);
    velocityCurve.setSelectedId (3, juce::dontSendNotification);

    static constexpr auto always = NeuroMPCProcessor::Undo::always;
    xReverse.onClick  = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::reverse (l); }, always); };
    xLeft.onClick     = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::rotate (l, -1); }, always); };
    xRight.onClick    = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::rotate (l, 1); }, always); };
    xDouble.onClick   = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::doubleLength (l); }, always); };
    xHalve.onClick    = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::halve (l); }, always); };
    xExpand.onClick   = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::expand (l); }, always); };
    xContract.onClick = [this] { forEachScopedLane ([] (auto& l, int) { neuro::pattern::contract (l); }, always); };
    curveDepth.setRange (0.0, 1.0, 0.01);
    curveDepth.setValue (1.0, juce::dontSendNotification);
    applyCurve.onClick = [this]
    {
        const auto curve = (neuro::pattern::VelocityCurve) (velocityCurve.getSelectedId() - 1);
        forEachScopedLane ([curve, d = (float) curveDepth.getValue()] (auto& l, int) { neuro::pattern::applyVelocityCurve (l, curve, d); }, always);
    };
    offbeatChance.setRange (0, 100, 1);
    offbeatChance.setValue (60, juce::dontSendNotification);
    offbeatChance.setTextValueSuffix (" %");
    applyProfile.onClick = [this]
    {
        forEachScopedLane ([chance = (float) offbeatChance.getValue() / 100.0f] (auto& l, int) { neuro::pattern::applyProbabilityProfile (l, chance); }, always);
    };

    const char* geneNames[] { "Rhythm", "Velocity", "Pitch", "Groove", "Chance", "Ratchet", "Gate" };

    for (size_t g = 0; g < geneToggles.size(); ++g)
    {
        geneToggles[g].setButtonText (geneNames[g]);
        geneToggles[g].setToggleState (g < 3, juce::dontSendNotification);
        geneToggles[g].setTooltip ("Genes Mutate may change and Take genes copies; unticked genes are preserved exactly");
    }

    for (auto [slider, lo, hi, step, value] : std::initializer_list<std::tuple<juce::Slider*, double, double, double, double>> {
             { &mutIntensity, 0, 100, 1, 30 }, { &mutPreserve, 0, 100, 1, 50 }, { &mutDensity, -100, 100, 1, 0 },
             { &mutPitch, 0, 24, 1, 7 }, { &mutVelocity, 0, 100, 1, 30 }, { &mutTiming, 0, 50, 1, 15 }, { &mutSeed, 1, 9999, 1, 1 } })
    {
        slider->setRange (lo, hi, step);
        slider->setValue (value, juce::dontSendNotification);
    }

    mutateButton.setTooltip ("Mutate the selected genes (seeded); the seed then advances so the next click gives a new variation");
    mutateButton.onClick = [this]
    {
        const auto index = proc.getSelectedSeqPattern();
        compareSnapshot = proc.getSeqPattern (index);
        compareIndex = index;
        neuro::pattern::MutationSettings m;
        m.intensity = (float) mutIntensity.getValue() / 100.0f;
        m.preservation = (float) mutPreserve.getValue() / 100.0f;
        m.densityBias = (float) mutDensity.getValue() / 100.0f;
        m.pitchRange = (int) mutPitch.getValue();
        m.velocityRange = (float) mutVelocity.getValue() / 100.0f;
        m.timingRange = (float) mutTiming.getValue() / 100.0f;
        m.seed = (uint32_t) mutSeed.getValue();
        m.genes = 0;

        for (size_t g = 0; g < geneToggles.size(); ++g)
            m.genes |= geneToggles[g].getToggleState() ? (1u << g) : 0u;

        forEachScopedLane ([&m] (auto& l, int lane) { auto lm = m; lm.seed = m.seed * 131u + (uint32_t) lane; neuro::pattern::mutate (l, lm); }, always);
        mutSeed.setValue (juce::jlimit (1.0, 9999.0, mutSeed.getValue() + 1.0), juce::dontSendNotification);
        compareButton.setButtonText ("A/B: B");
    };
    compareButton.setTooltip ("Compare: swap between the pattern before the last mutation (A) and after (B)");
    compareButton.onClick = [this]
    {
        if (! compareSnapshot || compareIndex != proc.getSelectedSeqPattern())
            return;

        auto current = proc.getSeqPattern (compareIndex);
        proc.editSeqPattern (compareIndex, [this] (auto& pat) { pat = *compareSnapshot; }, NeuroMPCProcessor::Undo::none);
        compareSnapshot = current;
        compareButton.setButtonText (compareButton.getButtonText() == "A/B: B" ? "A/B: A" : "A/B: B");
    };

    for (int i = 1; i <= NeuroMPCProcessor::numSeqPatterns; ++i)
        donorPattern.addItem ("from pattern " + juce::String (i), i);

    donorPattern.setSelectedId (2, juce::dontSendNotification);
    combineButton.setTooltip ("Pattern Genome: take the ticked genes from another pattern (lane by lane), keep the rest");
    combineButton.onClick = [this]
    {
        const auto& donor = proc.getSeqPattern (donorPattern.getSelectedId() - 1);
        uint32_t genes = 0;

        for (size_t g = 0; g < geneToggles.size(); ++g)
            genes |= geneToggles[g].getToggleState() ? (1u << g) : 0u;

        forEachScopedLane ([&donor, genes] (auto& l, int lane)
        {
            neuro::pattern::express (l, neuro::pattern::combine (neuro::pattern::extract (l),
                                                                 neuro::pattern::extract (donor.lanes[(size_t) lane]), genes));
        }, always);
    };
    evolveUndo.onClick = [this] { proc.undoSeqPattern (proc.getSelectedSeqPattern()); updateEvolvePanel(); };
    evolveRedo.onClick = [this] { proc.redoSeqPattern (proc.getSelectedSeqPattern()); updateEvolvePanel(); };

    for (auto* box : { &weaveAccent, &weaveFill, &weavePitch })
    {
        box->addItem (box == &weaveAccent ? "Accents: none" : box == &weaveFill ? "Fills: none" : "Pitch: none", 1);

        for (int i = 1; i <= NeuroMPCProcessor::numSeqPatterns; ++i)
            box->addItem ((box == &weaveAccent ? "Accents from " : box == &weaveFill ? "Fills from " : "Pitch from ") + juce::String (i), i + 1);
    }

    for (auto [box, id] : std::initializer_list<std::pair<juce::ComboBox*, const char*>> {
             { &weaveAccent, ParamIDs::weaveAccent }, { &weaveFill, ParamIDs::weaveFill }, { &weavePitch, ParamIDs::weavePitch } })
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *box));

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::weaveAccentAmount, weaveAmount));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::weaveFillEvery, weaveEvery));

    for (auto [slider, name] : std::initializer_list<std::pair<juce::Slider*, const char*>> {
             { &curveDepth, "Depth" }, { &offbeatChance, "Offbeat" }, { &mutIntensity, "Intensity" }, { &mutPreserve, "Preserve" },
             { &mutDensity, "Density" }, { &mutPitch, "Pitch +/-" }, { &mutVelocity, "Vel +/-" }, { &mutTiming, "Time +/-" },
             { &mutSeed, "Seed" }, { &weaveAmount, "Amount" }, { &weaveEvery, "Every" } })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
        auto label = std::make_unique<juce::Label> (name, name);
        label->setFont (juce::FontOptions (11.0f));
        label->attachToComponent (slider, true);
        knobLabels.push_back (std::move (label));
    }

    // ---- Sequencer panel ----
    for (auto* c : seqPanelComponents())
        addAndMakeVisible (c);

    seqTitle.setText ("SEQUENCER", juce::dontSendNotification);
    seqTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    for (int i = 1; i <= NeuroMPCProcessor::numSeqPatterns; ++i)
        seqPatternBox.addItem ("Pattern " + juce::String (i), i);

    seqModeBox.addItemList ({ "Seq: off", "Seq: host transport", "Seq: free running" }, 1);
    seqRateBox.addItemList ({ "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);

    for (auto [box, id] : std::initializer_list<std::pair<juce::ComboBox*, const char*>> {
             { &seqPatternBox, ParamIDs::seqPattern }, { &seqModeBox, ParamIDs::seqMode }, { &seqRateBox, ParamIDs::seqRate } })
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *box));

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::seqSwing, seqSwing));
    seqPatternBox.onChange = [this] { updateSeqPanel(); };

    seqName.setEditable (true);
    seqName.setColour (juce::Label::outlineColourId, panel.brighter (0.3f));
    seqName.setTooltip ("Pattern name (double-click to edit)");
    seqName.onTextChange = [this]
    {
        proc.editSeqPattern (proc.getSelectedSeqPattern(), [t = seqName.getText().toStdString()] (auto& pat) { pat.name = t; });
    };

    seqDuplicate.setTooltip ("Copy this pattern to the next empty slot and select it");
    seqDuplicate.onClick = [this]
    {
        const auto current = proc.getSelectedSeqPattern();

        for (int i = 1; i < NeuroMPCProcessor::numSeqPatterns; ++i)
        {
            const auto target = (current + i) % NeuroMPCProcessor::numSeqPatterns;

            if (proc.getSeqPattern (target).isEmpty())
            {
                const auto copy = proc.getSeqPattern (current);
                proc.editSeqPattern (target, [&copy] (auto& pat) { pat = copy; });
                seqPatternBox.setSelectedId (target + 1); // updates the parameter through the attachment
                return;
            }
        }
    };
    seqClear.onClick = [this] { proc.editSeqPattern (proc.getSelectedSeqPattern(), [] (auto& pat) { pat = {}; }); updateSeqPanel(); };

    for (int i = 0; i < 4; ++i)
    {
        auto& b = stepPageButtons[(size_t) i];
        b.setButtonText (juce::String (i * 16 + 1) + "-" + juce::String (i * 16 + 16));
        b.setRadioGroupId (3);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
        b.onClick = [this, i] { seqGrid.stepPage = i; seqGrid.repaint(); };
    }

    stepPageButtons[0].setToggleState (true, juce::dontSendNotification);
    lanePageButton.onClick = [this]
    {
        seqGrid.lanePage = 1 - seqGrid.lanePage;
        lanePageButton.setButtonText (seqGrid.lanePage == 0 ? "Lanes 1-8" : "Lanes 9-16");
        seqGrid.repaint();
    };
    seqGrid.onSelectionChanged = [this] { updateSeqPanel(); };

    stepVelocity.setRange (1, 127, 1);
    stepGate.setRange (5, 100, 1);
    stepGate.setTextValueSuffix (" %");
    stepChance.setRange (0, 100, 1);
    stepChance.setTextValueSuffix (" %");
    stepRatchet.setRange (1, 4, 1);
    stepMicro.setRange (-50, 50, 1);
    stepMicro.setTextValueSuffix (" %");
    stepMicro.setTooltip ("Microtiming: shift this step early/late by up to half a step");
    stepPitch.setRange (-24, 24, 0.01);
    stepPitch.setTextValueSuffix (" st");
    stepVelocity.onValueChange = [this] { editSelectedSeqStep ([v = (float) stepVelocity.getValue() / 127.0f] (auto& st) { st.velocity = v; }); };
    stepGate.onValueChange     = [this] { editSelectedSeqStep ([v = (float) stepGate.getValue() / 100.0f] (auto& st) { st.gate = v; }); };
    stepChance.onValueChange   = [this] { editSelectedSeqStep ([v = (float) stepChance.getValue() / 100.0f] (auto& st) { st.probability = v; }); };
    stepRatchet.onValueChange  = [this] { editSelectedSeqStep ([v = (int) stepRatchet.getValue()] (auto& st) { st.ratchet = v; }); };
    stepMicro.onValueChange    = [this] { editSelectedSeqStep ([v = (float) stepMicro.getValue() / 100.0f] (auto& st) { st.micro = v; }); };
    stepPitch.onValueChange    = [this] { editSelectedSeqStep ([v = (float) stepPitch.getValue()] (auto& st) { st.pitch = v; }); };

    for (int note = 0; note < 128; ++note)
        laneNote.addItem ("Lane note: " + juce::MidiMessage::getMidiNoteName (note, true, true, 3), note + 1);

    laneNote.setTooltip ("Note this lane plays (pads mode: the pad on that note)");
    laneNote.onChange = [this]
    {
        proc.editSeqPattern (proc.getSelectedSeqPattern(), [this, n = laneNote.getSelectedId() - 1] (auto& pat)
                             { pat.lanes[(size_t) seqGrid.selectedLane].note = n; });
    };
    laneLength.setRange (1, neuro::SeqLane::maxSteps, 1);
    laneLength.setTooltip ("Steps in this lane; lanes of different lengths run as polymeters");
    laneLength.onValueChange = [this]
    {
        proc.editSeqPattern (proc.getSelectedSeqPattern(), [this, n = (int) laneLength.getValue()] (auto& pat)
                             { pat.lanes[(size_t) seqGrid.selectedLane].length = n; });
    };

    for (auto [slider, name] : std::initializer_list<std::pair<juce::Slider*, const char*>> {
             { &seqSwing, "Swing" }, { &stepVelocity, "Vel" }, { &stepGate, "Gate" }, { &stepChance, "Chance" },
             { &stepRatchet, "Ratchet" }, { &stepMicro, "Micro" }, { &stepPitch, "Pitch" }, { &laneLength, "Length" } })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        auto label = std::make_unique<juce::Label> (name, name);
        label->setFont (juce::FontOptions (11.0f));
        label->attachToComponent (slider, true);
        knobLabels.push_back (std::move (label));
    }
    waveform.onSliceClicked = [this] { if (activePanel == Panel::slice || activePanel == Panel::pad) showPanel (Panel::slice); };

    // ---- Generative panel ----
    for (auto* c : genPanelComponents())
        addAndMakeVisible (c);

    genTitle.setText ("PATTERN", juce::dontSendNotification);
    genTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    rulesList.setJustificationType (juce::Justification::topLeft);
    rulesList.setColour (juce::Label::backgroundColourId, panel);

    for (int i = 0; i < neuro::ArpProgram::maxSteps; ++i)
    {
        auto& b = stepButtons[(size_t) i];
        b.setTooltip ("Click: on/rest. Shift-click: accent. Alt-click: ratchet x1-x4");
        b.onClick = [this, i]
        {
            const auto mods = juce::ModifierKeys::getCurrentModifiers();
            proc.editArpProgram ([&] (auto& prog)
            {
                auto& step = prog.steps[(size_t) i];

                if (mods.isShiftDown())     step.accent = ! step.accent;
                else if (mods.isAltDown())  step.ratchet = step.ratchet % 4 + 1;
                else                        step.on = ! step.on;
            });
            updateGenPanel();
        };
    }

    patternLength.setRange (1, neuro::ArpProgram::maxSteps, 1);
    patternLength.onValueChange = [this] { proc.editArpProgram ([v = (int) patternLength.getValue()] (auto& prog) { prog.length = v; }); };
    euclidPulses.setRange (0, neuro::ArpProgram::maxSteps, 1);
    euclidPulses.setValue (5, juce::dontSendNotification);
    euclidRotate.setRange (0, neuro::ArpProgram::maxSteps - 1, 1);
    euclidButton.setTooltip ("Spread the pulses evenly over the pattern length (Euclidean rhythm)");
    euclidButton.onClick = [this]
    {
        proc.editArpProgram ([this] (auto& prog) { prog.euclid ((int) euclidPulses.getValue(), prog.length, (int) euclidRotate.getValue()); });
        updateGenPanel();
    };
    allOnButton.onClick = [this]
    {
        proc.editArpProgram ([] (auto& prog) { for (auto& st : prog.steps) st = {}; });
        updateGenPanel();
    };

    for (auto [slider, id] : std::initializer_list<std::pair<juce::Slider*, const char*>> {
             { &arpPhrase, ParamIDs::arpPhrase }, { &arpVariation, ParamIDs::arpVariation }, { &arpReturn, ParamIDs::arpReturn },
             { &arpAccent, ParamIDs::arpAccent }, { &arpHumanTime, ParamIDs::arpHumanTime }, { &arpHumanVel, ParamIDs::arpHumanVel } })
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, id, *slider));

    for (auto [button, id] : std::initializer_list<std::pair<juce::Button*, const char*>> {
             { &varyPitch, ParamIDs::arpVaryPitch }, { &varyVelocity, ParamIDs::arpVaryVel }, { &varyRhythm, ParamIDs::arpVaryRhythm } })
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *button));

    ruleWhen.addItemList ({ "Step N of every", "Steps from N of every", "Phrase end (last N)", "Chance (N %)" }, 1);
    ruleWhen.setSelectedId (1, juce::dontSendNotification);
    ruleAction.addItemList ({ "Transpose", "Rest", "Accent", "Ratchet", "Passing tone" }, 1);
    ruleAction.setSelectedId (1, juce::dontSendNotification);
    ruleN.setRange (1, 100, 1);
    ruleN.setValue (4, juce::dontSendNotification);
    ruleOffset.setRange (1, 16, 1);
    ruleOffset.setValue (1, juce::dontSendNotification);
    ruleAmount.setRange (-24, 24, 1);
    ruleAmount.setValue (12, juce::dontSendNotification);
    addRule.onClick = [this]
    {
        neuro::ArpRule r;
        r.when = (neuro::ArpRule::When) (ruleWhen.getSelectedId() - 1);
        r.n = (int) ruleN.getValue();
        r.offset = (int) ruleOffset.getValue() - 1; // UI counts steps from 1
        r.action = (neuro::ArpRule::Action) (ruleAction.getSelectedId() - 1);
        r.amount = (int) ruleAmount.getValue();
        proc.editArpProgram ([r] (auto& prog) { prog.addRule (r); });
        updateGenPanel();
    };
    removeRule.onClick = [this] { proc.editArpProgram ([] (auto& prog) { prog.numRules = juce::jmax (0, prog.numRules - 1); }); updateGenPanel(); };
    clearRules.onClick = [this] { proc.editArpProgram ([] (auto& prog) { prog.numRules = 0; }); updateGenPanel(); };

    // Grammar presets: ordinary, editable rules (shown in the list below).
    grammarPresets.addItemList ({ "Octave climb", "Passing tones", "Phrase-end fill", "Accents 3-3-2", "Stutter & drop", "Call & answer" }, 1);
    grammarPresets.setTextWhenNothingSelected ("Grammar preset...");
    grammarPresets.onChange = [this]
    {
        using W = neuro::ArpRule::When;
        using A = neuro::ArpRule::Action;
        std::vector<neuro::ArpRule> rules;

        switch (grammarPresets.getSelectedId())
        {
            case 1: rules = { { W::range, 8, 4, A::transpose, 12, true } }; break;
            case 2: rules = { { W::every, 2, 1, A::passing, 1, true } }; break;
            case 3: rules = { { W::phraseEnd, 2, 0, A::ratchet, 3, true }, { W::phraseEnd, 1, 0, A::transpose, 12, true } }; break;
            case 4: rules = { { W::every, 8, 0, A::accent, 0, true }, { W::every, 8, 3, A::accent, 0, true }, { W::every, 8, 6, A::accent, 0, true } }; break;
            case 5: rules = { { W::chance, 15, 0, A::ratchet, 2, true }, { W::chance, 10, 0, A::rest, 0, true } }; break;
            case 6: rules = { { W::range, 16, 8, A::transpose, -12, true }, { W::every, 16, 15, A::passing, -1, true } }; break;
            default: return;
        }

        proc.editArpProgram ([&] (auto& prog) { prog.numRules = 0; for (auto& r : rules) prog.addRule (r); });
        grammarPresets.setSelectedId (0, juce::dontSendNotification);
        updateGenPanel();
    };

    for (auto [slider, name] : std::initializer_list<std::pair<juce::Slider*, const char*>> {
             { &patternLength, "Length" }, { &euclidPulses, "Pulses" }, { &euclidRotate, "Rotate" }, { &arpPhrase, "Phrase" },
             { &arpVariation, "Variation" }, { &arpReturn, "Return" }, { &arpAccent, "Accent" }, { &arpHumanTime, "Feel ms" },
             { &arpHumanVel, "Feel vel" }, { &ruleN, "N" }, { &ruleOffset, "Step" }, { &ruleAmount, "Amount" } })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        auto label = std::make_unique<juce::Label> (name, name);
        label->setFont (juce::FontOptions (11.0f));
        label->attachToComponent (slider, true);
        knobLabels.push_back (std::move (label));
    }

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

    // ---- Presets and relinking ----
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setTooltip ("Presets are saved in ~/Library/Audio/Presets/NEURO MPC");
    presetBox.onChange = [this]
    {
        const auto index = presetBox.getSelectedId() - 1;

        if (juce::isPositiveAndBelow (index, presetFiles.size()) && ! proc.loadPreset (presetFiles[index]))
            lastError = "Could not load preset " + presetFiles[index].getFileName();
    };
    savePresetButton.setTooltip ("Save the current state (sample reference, slices, pads, arp, sequencer, parameters) as a preset");
    savePresetButton.onClick = [this]
    {
        auto* window = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", proc.getCurrentPresetName());
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            if (result == 1 && ! proc.savePreset (window->getTextEditorContents ("name")))
                lastError = "Could not save the preset";

            refreshPresets();
        }), true);
    };
    relinkButton.setTooltip ("Locate the missing sample: pick the file, or a folder to search");
    relinkButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Locate " + juce::File (proc.getMissingSamplePath()).getFileName(), juce::File(),
                                                       proc.formats.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto target = fc.getResult();

                                  if (target.isDirectory())
                                      target = proc.findMissingSampleIn (target);

                                  lastError.clear();

                                  if (! target.existsAsFile())
                                      lastError = "Not found: " + juce::File (proc.getMissingSamplePath()).getFileName();
                                  else if (! proc.relinkSample (target, lastError))
                                      lastError = "Relink failed: " + lastError;
                              });
    };
    refreshPresets();

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
    setResizeLimits (1040, 780, 2400, 1600);
    setSize (1140, 840);
    startTimerHz (30);
}

NeuroMPCEditor::~NeuroMPCEditor()
{
    stopTimer();
}

void NeuroMPCEditor::load (const juce::File& file)
{
    lastError.clear();

    // Loading the missing file (same name) is a relink: its saved slices come back.
    const auto missing = proc.getMissingSamplePath();
    const auto ok = missing.isNotEmpty() && file.getFileName() == juce::File (missing).getFileName()
                        ? proc.relinkSample (file, lastError)
                        : proc.loadSample (file, lastError);

    if (! ok)
        updateInfo();
}

void NeuroMPCEditor::refreshPresets()
{
    presetFiles = proc.getPresets();
    presetBox.clear (juce::dontSendNotification);

    for (int i = 0; i < presetFiles.size(); ++i)
        presetBox.addItem (presetFiles[i].getFileNameWithoutExtension(), i + 1);

    for (int i = 0; i < presetFiles.size(); ++i)
        if (presetFiles[i].getFileNameWithoutExtension() == proc.getCurrentPresetName())
            presetBox.setSelectedId (i + 1, juce::dontSendNotification);
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
             &arpChance, &arpOctaves, &arpRepeats, &arpVelocity, &arpSeed, &gesturePad };
}

std::vector<juce::Component*> NeuroMPCEditor::genPanelComponents()
{
    std::vector<juce::Component*> list { &genTitle, &rulesList, &patternLength, &euclidPulses, &euclidRotate, &arpPhrase,
                                         &arpVariation, &arpReturn, &arpAccent, &arpHumanTime, &arpHumanVel, &ruleN, &ruleOffset,
                                         &ruleAmount, &varyPitch, &varyVelocity, &varyRhythm, &euclidButton, &allOnButton,
                                         &addRule, &removeRule, &clearRules, &ruleWhen, &ruleAction, &grammarPresets };

    for (auto& b : stepButtons)
        list.push_back (&b);

    return list;
}

std::vector<juce::Component*> NeuroMPCEditor::seqPanelComponents()
{
    std::vector<juce::Component*> list { &seqGrid, &seqTitle, &seqName, &seqInfo, &seqPatternBox, &seqModeBox, &seqRateBox,
                                         &laneNote, &seqSwing, &stepVelocity, &stepGate, &stepChance, &stepRatchet, &stepMicro,
                                         &stepPitch, &laneLength, &seqDuplicate, &seqClear, &lanePageButton, &seqUndo, &seqRedo,
                                         &seqFill, &stepCondition };

    for (auto& b : stepPageButtons)
        list.push_back (&b);

    return list;
}

std::vector<juce::Component*> NeuroMPCEditor::evolvePanelComponents()
{
    std::vector<juce::Component*> list { &evolveTitle, &mutateTitle, &weaveTitle, &genomeInfo, &evolveScope, &velocityCurve,
                                         &donorPattern, &weaveAccent, &weaveFill, &weavePitch, &xReverse, &xLeft, &xRight, &xDouble,
                                         &xHalve, &xExpand, &xContract, &applyCurve, &applyProfile, &mutateButton, &compareButton,
                                         &combineButton, &evolveUndo, &evolveRedo, &curveDepth, &offbeatChance, &mutIntensity,
                                         &mutPreserve, &mutDensity, &mutPitch, &mutVelocity, &mutTiming, &mutSeed, &weaveAmount,
                                         &weaveEvery };

    for (auto& t : geneToggles)
        list.push_back (&t);

    return list;
}

// Applies an edit to the selected lane or to every active lane of the selected pattern, as one undo step.
void NeuroMPCEditor::forEachScopedLane (const std::function<void (neuro::SeqLane&, int)>& fn, NeuroMPCProcessor::Undo undo)
{
    const auto all = evolveScope.getSelectedId() == 2;
    proc.editSeqPattern (proc.getSelectedSeqPattern(), [&] (auto& pat)
    {
        for (int l = 0; l < neuro::SeqPattern::maxLanes; ++l)
            if (all ? l < pat.numLanes : l == seqGrid.selectedLane)
                fn (pat.lanes[(size_t) l], l);
    }, undo);

    updateEvolvePanel();
}

void NeuroMPCEditor::updateEvolvePanel()
{
    const auto index = proc.getSelectedSeqPattern();
    const auto& lane = proc.getSeqPattern (index).lanes[(size_t) seqGrid.selectedLane];
    genomeInfo.setText ("Pattern " + juce::String (index + 1) + ", lane " + juce::String (seqGrid.selectedLane + 1) + " genome:\n"
                            + juce::String (neuro::pattern::describe (neuro::pattern::extract (lane))),
                        juce::dontSendNotification);
    evolveUndo.setEnabled (proc.canUndoSeqPattern (index));
    evolveRedo.setEnabled (proc.canRedoSeqPattern (index));
    compareButton.setEnabled (compareSnapshot.has_value() && compareIndex == index);
}

void NeuroMPCEditor::editSelectedSeqStep (const std::function<void (neuro::SeqStep&)>& fn)
{
    proc.editSeqPattern (proc.getSelectedSeqPattern(), [this, &fn] (auto& pat)
                         { fn (pat.lanes[(size_t) seqGrid.selectedLane].steps[(size_t) seqGrid.selectedStep]); });
    seqGrid.repaint();
}

void NeuroMPCEditor::updateSeqPanel()
{
    const auto& pat = proc.getSeqPattern (proc.getSelectedSeqPattern());
    const auto& lane = pat.lanes[(size_t) seqGrid.selectedLane];
    const auto& st = lane.steps[(size_t) seqGrid.selectedStep];

    if (! seqName.isBeingEdited())
        seqName.setText (pat.name.empty() ? juce::String ("(unnamed)") : juce::String (pat.name), juce::dontSendNotification);

    for (auto [slider, value] : std::initializer_list<std::pair<juce::Slider*, double>> {
             { &stepVelocity, std::round (st.velocity * 127.0f) }, { &stepGate, std::round (st.gate * 100.0f) },
             { &stepChance, std::round (st.probability * 100.0f) }, { &stepRatchet, st.ratchet },
             { &stepMicro, std::round (st.micro * 100.0f) }, { &stepPitch, st.pitch }, { &laneLength, lane.length } })
        if (! slider->isMouseButtonDown())
            slider->setValue (value, juce::dontSendNotification);

    laneNote.setSelectedId (lane.note + 1, juce::dontSendNotification);

    {
        using C = neuro::SeqCondition;
        int id = 1;

        if (st.condition == C::ratio)
        {
            static constexpr int ratios[][2] { { 1, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };
            for (int r = 0; r < 9; ++r)
                if (ratios[r][0] == st.condA && ratios[r][1] == st.condB)
                    id = r + 2;
        }
        else if (st.condition != C::always)
        {
            id = 11 + ((int) st.condition - (int) C::first);
        }

        stepCondition.setSelectedId (id, juce::dontSendNotification);
    }

    seqUndo.setEnabled (proc.canUndoSeqPattern (proc.getSelectedSeqPattern()));
    seqRedo.setEnabled (proc.canRedoSeqPattern (proc.getSelectedSeqPattern()));

    const auto playStep = proc.getSeqPlayStep();
    seqInfo.setText ("Lane " + juce::String (seqGrid.selectedLane + 1) + ", step " + juce::String (seqGrid.selectedStep + 1)
                         + (st.on ? "" : " (off)") + (playStep >= 0 ? "   playing" : "   stopped"),
                     juce::dontSendNotification);
    seqGrid.repaint();
}

void NeuroMPCEditor::updateGenPanel()
{
    const auto& p = proc.getArpProgram();

    for (int i = 0; i < neuro::ArpProgram::maxSteps; ++i)
    {
        const auto& st = p.steps[(size_t) i];
        auto& b = stepButtons[(size_t) i];
        b.setVisible (activePanel == Panel::gen && i < p.length);
        b.setButtonText (! st.on ? juce::String ("-") : (st.accent ? "X" : "x") + (st.ratchet > 1 ? juce::String (st.ratchet) : juce::String()));
        b.setColour (juce::TextButton::buttonColourId, ! st.on ? panel : st.accent ? accent : accent.withAlpha (0.45f));
    }

    if (! patternLength.isMouseButtonDown())
        patternLength.setValue (p.length, juce::dontSendNotification);

    juce::String text;

    for (int i = 0; i < p.numRules; ++i)
        text << (i + 1) << ". " << neuro::describe (p.rules[(size_t) i]) << "\n";

    rulesList.setText (p.numRules == 0 ? juce::String ("Arpeggio Grammar: no rules (add one, or pick a preset)") : text,
                       juce::dontSendNotification);
}

void NeuroMPCEditor::showPanel (Panel p)
{
    activePanel = p;

    for (auto* c : slicePanelComponents()) c->setVisible (p == Panel::slice);
    for (auto* c : padPanelComponents())   c->setVisible (p == Panel::pad);
    for (auto* c : arpPanelComponents())   c->setVisible (p == Panel::arp);
    for (auto* c : genPanelComponents())   c->setVisible (p == Panel::gen);
    for (auto* c : seqPanelComponents())   c->setVisible (p == Panel::seq);
    for (auto* c : evolvePanelComponents()) c->setVisible (p == Panel::evolve);

    info.setVisible (p == Panel::slice || p == Panel::pad || p == Panel::arp); // the others use the info area too

    sliceTab.setToggleState (p == Panel::slice, juce::dontSendNotification);
    padTab.setToggleState (p == Panel::pad, juce::dontSendNotification);
    arpTab.setToggleState (p == Panel::arp, juce::dontSendNotification);
    genTab.setToggleState (p == Panel::gen, juce::dontSendNotification);
    seqTab.setToggleState (p == Panel::seq, juce::dontSendNotification);
    evolveTab.setToggleState (p == Panel::evolve, juce::dontSendNotification);

    if (p == Panel::pad) updatePadPanel();
    if (p == Panel::arp) updateArpPanel();
    if (p == Panel::gen) updateGenPanel();
    if (p == Panel::seq) updateSeqPanel();
    if (p == Panel::evolve) updateEvolvePanel();
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

    relinkButton.setVisible (proc.getMissingSamplePath().isNotEmpty());

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
    if (activePanel == Panel::arp) gesturePad.repaint();
    if (activePanel == Panel::seq) updateSeqPanel();
    if (activePanel == Panel::evolve) updateEvolvePanel();

    updateInfo();
    updateSlicePanel();
    waveform.repaint();
}

void NeuroMPCEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (accent);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("NEURO MPC", titleArea, juce::Justification::centredRight);
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
    top.removeFromLeft (12);
    presetBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (4);
    savePresetButton.setBounds (top.removeFromLeft (48));
    top.removeFromLeft (6);
    relinkButton.setBounds (top.removeFromLeft (76));

    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (juce::jmin (460, area.getHeight() * 3 / 5));
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
    genTab.setBounds (tabs.removeFromLeft (70));
    seqTab.setBounds (tabs.removeFromLeft (70));
    evolveTab.setBounds (tabs.removeFromLeft (76));
    titleArea = tabs;
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

    // Generative panel: the panel area plus the info area below it.
    {
        auto gp = panelArea.withBottom (bottom.getBottom());
        auto rowOf = [&gp] (int gap = 6) { gp.removeFromTop (gap); return gp.removeFromTop (24); };
        auto r0 = gp.removeFromTop (24);
        genTitle.setBounds (r0.removeFromLeft (76));

        for (auto& b : stepButtons)
            b.setBounds (r0.removeFromLeft (28).reduced (1, 0));

        r0.removeFromLeft (56);
        patternLength.setBounds (r0.removeFromLeft (juce::jmax (80, r0.getWidth())));

        auto r1 = rowOf();
        r1.removeFromLeft (50);
        euclidPulses.setBounds (r1.removeFromLeft (150));
        r1.removeFromLeft (50);
        euclidRotate.setBounds (r1.removeFromLeft (150));
        r1.removeFromLeft (8);
        euclidButton.setBounds (r1.removeFromLeft (64));
        r1.removeFromLeft (6);
        allOnButton.setBounds (r1.removeFromLeft (64));

        auto r2 = rowOf();
        const auto third = (r2.getWidth() - 3 * 60) / 3;

        for (auto* sld : { &arpPhrase, &arpVariation, &arpReturn })
        {
            r2.removeFromLeft (60);
            sld->setBounds (r2.removeFromLeft (third));
        }

        auto r3 = rowOf();
        r3.removeFromLeft (60);
        varyPitch.setBounds (r3.removeFromLeft (70));
        varyVelocity.setBounds (r3.removeFromLeft (84));
        varyRhythm.setBounds (r3.removeFromLeft (76));

        for (auto* sld : { &arpAccent, &arpHumanTime, &arpHumanVel })
        {
            r3.removeFromLeft (58);
            sld->setBounds (r3.removeFromLeft ((r3.getWidth() - 2 * 58) / (sld == &arpHumanVel ? 1 : (sld == &arpAccent ? 3 : 2))));
        }

        auto r4 = rowOf (10);
        ruleWhen.setBounds (r4.removeFromLeft (160));

        for (auto* sld : { &ruleN, &ruleOffset })
        {
            r4.removeFromLeft (40);
            sld->setBounds (r4.removeFromLeft (110));
        }

        r4.removeFromLeft (6);
        ruleAction.setBounds (r4.removeFromLeft (120));
        r4.removeFromLeft (54);
        ruleAmount.setBounds (r4.removeFromLeft (120));
        r4.removeFromLeft (6);
        addRule.setBounds (r4.removeFromLeft (50));

        auto r5 = rowOf();
        grammarPresets.setBounds (r5.removeFromLeft (160));
        r5.removeFromLeft (6);
        removeRule.setBounds (r5.removeFromLeft (96));
        r5.removeFromLeft (6);
        clearRules.setBounds (r5.removeFromLeft (90));

        gp.removeFromTop (6);
        rulesList.setBounds (gp);
    }

    // Sequencer panel: also uses the info area.
    {
        auto qp = panelArea.withBottom (bottom.getBottom());
        auto r0 = qp.removeFromTop (24);

        for (auto [c, w] : std::initializer_list<std::pair<juce::Component*, int>> {
                 { &seqTitle, 96 }, { &seqPatternBox, 104 }, { &seqName, 120 }, { &seqDuplicate, 80 }, { &seqClear, 56 },
                 { &seqModeBox, 150 }, { &seqRateBox, 70 } })
        {
            c->setBounds (r0.removeFromLeft (w));
            r0.removeFromLeft (6);
        }

        r0.removeFromLeft (44);
        seqSwing.setBounds (r0);

        qp.removeFromTop (6);
        auto r1 = qp.removeFromTop (22);

        for (auto& b : stepPageButtons)
            b.setBounds (r1.removeFromLeft (60).reduced (1, 0));

        r1.removeFromLeft (8);
        lanePageButton.setBounds (r1.removeFromLeft (90));
        r1.removeFromLeft (8);
        seqFill.setBounds (r1.removeFromLeft (56));
        seqUndo.setBounds (r1.removeFromLeft (52));
        r1.removeFromLeft (4);
        seqRedo.setBounds (r1.removeFromLeft (52));
        r1.removeFromLeft (12);
        seqInfo.setBounds (r1);

        auto editorRow = qp.removeFromBottom (24);
        auto laneRow = editorRow;
        qp.removeFromBottom (6);
        auto stepRow = qp.removeFromBottom (24);
        qp.removeFromBottom (6);
        qp.removeFromTop (6);
        seqGrid.setBounds (qp);

        const auto cellWidth = (stepRow.getWidth() - 6 * 52) / 6;

        for (auto* sld : { &stepVelocity, &stepGate, &stepChance, &stepRatchet, &stepMicro, &stepPitch })
        {
            stepRow.removeFromLeft (52);
            sld->setBounds (stepRow.removeFromLeft (cellWidth));
        }

        laneNote.setBounds (laneRow.removeFromLeft (190));
        laneRow.removeFromLeft (56);
        laneLength.setBounds (laneRow.removeFromLeft (200));
        laneRow.removeFromLeft (12);
        stepCondition.setBounds (laneRow.removeFromLeft (190));
    }

    // Evolve panel: also uses the info area.
    {
        auto ep = panelArea.withBottom (bottom.getBottom());
        auto rowOf = [&ep] (int gap = 6) { ep.removeFromTop (gap); return ep.removeFromTop (24); };
        auto place = [] (juce::Rectangle<int>& r, juce::Component& c, int w, int gap = 6) { c.setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); };

        auto r0 = ep.removeFromTop (24);
        place (r0, evolveTitle, 92);
        place (r0, evolveScope, 120);

        for (auto* b : { &xReverse, &xLeft, &xRight, &xDouble, &xHalve, &xExpand, &xContract })
            place (r0, *b, b == &xDouble || b == &xHalve ? 40 : 66, 4);

        r0.removeFromLeft (8);
        place (r0, evolveUndo, 52, 4);
        place (r0, evolveRedo, 52);

        auto r1 = rowOf();
        place (r1, velocityCurve, 120);
        r1.removeFromLeft (44);
        place (r1, curveDepth, 130);
        place (r1, applyCurve, 54, 24);
        r1.removeFromLeft (52);
        place (r1, offbeatChance, 150);
        place (r1, applyProfile, 54);

        auto r2 = rowOf (10);
        place (r2, mutateTitle, 92);

        for (auto& t : geneToggles)
            place (r2, t, 82, 2);

        for (auto sliders : { std::array<juce::Slider*, 4> { &mutIntensity, &mutPreserve, &mutDensity, nullptr },
                              std::array<juce::Slider*, 4> { &mutPitch, &mutVelocity, &mutTiming, &mutSeed } })
        {
            auto r = rowOf();
            const auto w = (r.getWidth() - 4 * 60) / 4;

            for (auto* sld : sliders)
            {
                r.removeFromLeft (60);
                auto slot = r.removeFromLeft (w);

                if (sld != nullptr)
                    sld->setBounds (slot);
            }
        }

        auto r5 = rowOf();
        r5.removeFromLeft (92);
        place (r5, mutateButton, 80);
        place (r5, compareButton, 70, 24);
        place (r5, donorPattern, 140);
        place (r5, combineButton, 96);

        auto r6 = rowOf (10);
        place (r6, weaveTitle, 92);
        place (r6, weaveAccent, 150);
        r6.removeFromLeft (50);
        place (r6, weaveAmount, 120, 12);
        place (r6, weaveFill, 130);
        r6.removeFromLeft (40);
        place (r6, weaveEvery, 110, 12);
        place (r6, weavePitch, 130);

        ep.removeFromTop (8);
        genomeInfo.setBounds (ep);
    }

    auto ap = panelArea;
    gesturePad.setBounds (ap.removeFromRight (juce::jmin (ap.getHeight(), 130)));
    ap.removeFromRight (10);
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
