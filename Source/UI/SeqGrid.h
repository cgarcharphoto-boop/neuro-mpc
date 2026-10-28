#pragma once

#include "../PluginProcessor.h"

// Step grid: 8 lanes x 16 steps per page. Click toggles a step (drag paints), right/ctrl-click selects without
// toggling, the lane header selects the lane, its "M" toggles mute. Brightness = velocity, dots = ratchets,
// a dimmed corner = probability < 100 %, a tick = microtiming; outlined column = playhead.
class SeqGrid : public juce::Component
{
public:
    static constexpr int rows = 8, columns = 16, header = 78;

    explicit SeqGrid (NeuroMPCProcessor& p) : proc (p) {}

    int lanePage = 0, stepPage = 0, selectedLane = 0, selectedStep = 0;
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics& g) override
    {
        const auto& pattern = proc.getSeqPattern (proc.getSelectedSeqPattern());
        const auto playStep = proc.getSeqPlayStep();
        const auto accent = juce::Colour (0xffff8a3d);
        const auto rowH = (float) getHeight() / rows;
        const auto cellW = (float) (getWidth() - header) / columns;
        g.setFont (11.0f);

        for (int r = 0; r < rows; ++r)
        {
            const auto laneIndex = lanePage * rows + r;
            const auto& lane = pattern.lanes[(size_t) laneIndex];
            const auto active = laneIndex < pattern.numLanes;
            const auto y = (float) r * rowH;

            g.setColour (laneIndex == selectedLane ? accent.withAlpha (0.25f) : juce::Colour (0xff1c1f26));
            g.fillRect (0.0f, y + 1.0f, (float) header - 4.0f, rowH - 2.0f);
            g.setColour (juce::Colours::white.withAlpha (active ? 0.85f : 0.35f));
            const auto label = lane.name.empty() ? juce::MidiMessage::getMidiNoteName (lane.note, true, true, 3) : juce::String (lane.name);
            g.drawText (juce::String (laneIndex + 1) + " " + label, 4, (int) y, header - 26, (int) rowH, juce::Justification::centredLeft);
            g.setColour (lane.mute ? juce::Colours::red.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.25f));
            g.drawText ("M", header - 22, (int) y, 16, (int) rowH, juce::Justification::centred);

            for (int c = 0; c < columns; ++c)
            {
                const auto stepIndex = stepPage * columns + c;
                auto cell = juce::Rectangle<float> ((float) header + (float) c * cellW, y, cellW, rowH).reduced (1.5f);

                if (stepIndex >= lane.length)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.4f));
                    g.fillRect (cell);
                    continue;
                }

                const auto& st = lane.steps[(size_t) stepIndex];
                const auto beatGroup = (stepIndex / 4) % 2 == 0;
                g.setColour (st.on ? accent.withAlpha ((lane.mute ? 0.15f : 0.3f) + 0.65f * st.velocity)
                                   : juce::Colour (beatGroup ? 0xff262a33 : 0xff20232b));
                g.fillRoundedRectangle (cell, 3.0f);

                if (st.on)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.7f));

                    for (int d = 0; d < st.ratchet && st.ratchet > 1; ++d)
                        g.fillEllipse (cell.getX() + 3.0f + (float) d * 5.0f, cell.getBottom() - 6.0f, 3.0f, 3.0f);

                    if (st.probability < 1.0f)
                        g.fillRect (cell.getRight() - 6.0f, cell.getY() + 1.0f, 5.0f, 5.0f * (1.0f - st.probability) + 1.0f);

                    if (st.micro != 0.0f)
                        g.drawVerticalLine ((int) (cell.getCentreX() + st.micro * cell.getWidth()), cell.getY() + 2.0f, cell.getY() + 7.0f);
                }

                if (playStep >= 0 && (int) (playStep % lane.length) == stepIndex && active)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.8f));
                    g.drawRoundedRectangle (cell, 3.0f, 1.5f);
                }

                if (laneIndex == selectedLane && stepIndex == selectedStep)
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (cell.reduced (1.0f), 3.0f, 2.0f);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto [lane, step] = cellAt (e.position);

        if (e.x < header)
        {
            if (e.x >= header - 24)
                edit (lane, [] (auto& l) { l.mute = ! l.mute; });

            selectedLane = lane;
            changed();
            return;
        }

        selectedLane = lane;
        selectedStep = step;

        if (! (e.mods.isPopupMenu() || e.mods.isCommandDown()) && step < laneOf (lane).length)
        {
            painting = ! laneOf (lane).steps[(size_t) step].on;
            setStep (lane, step, painting);
            dragging = true;
        }

        changed();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || e.x < header)
            return;

        const auto [lane, step] = cellAt (e.position);

        if (step < laneOf (lane).length && laneOf (lane).steps[(size_t) step].on != painting)
        {
            setStep (lane, step, painting);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    std::pair<int, int> cellAt (juce::Point<float> p) const
    {
        const auto r = juce::jlimit (0, rows - 1, (int) (p.y / ((float) getHeight() / rows)));
        const auto c = juce::jlimit (0, columns - 1, (int) ((p.x - header) / ((float) (getWidth() - header) / columns)));
        return { lanePage * rows + r, stepPage * columns + c };
    }

    const neuro::SeqLane& laneOf (int lane) const { return proc.getSeqPattern (proc.getSelectedSeqPattern()).lanes[(size_t) lane]; }

    void edit (int lane, const std::function<void (neuro::SeqLane&)>& fn)
    {
        proc.editSeqPattern (proc.getSelectedSeqPattern(), [&] (auto& pattern)
        {
            pattern.numLanes = juce::jmax (pattern.numLanes, lane + 1); // using a lane activates it
            fn (pattern.lanes[(size_t) lane]);
        });
    }

    void setStep (int lane, int step, bool on)
    {
        edit (lane, [step, on] (auto& l) { l.steps[(size_t) step].on = on; });
    }

    void changed()
    {
        repaint();

        if (onSelectionChanged)
            onSelectionChanged();
    }

    NeuroMPCProcessor& proc;
    bool dragging = false, painting = true;
};
