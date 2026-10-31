#pragma once

#include "../PluginProcessor.h"

// 2D timbre map of the current slices: x = dark .. bright, y = tonal .. noisy, colour = drum class.
// Click a dot to select (and audition) that slice.
class TimbreMap : public juce::Component
{
public:
    explicit TimbreMap (NeuroMPCProcessor& p) : proc (p) {}

    int selected = -1;
    std::function<void (int)> onSliceClicked;

    static juce::Colour colourOf (int cls)
    {
        static const juce::Colour colours[] { juce::Colour (0xffe74c3c), juce::Colour (0xfff39c12), juce::Colour (0xfff1c40f), juce::Colour (0xffd4ac0d),
                                              juce::Colour (0xff2ecc71), juce::Colour (0xff27ae60), juce::Colour (0xff1e8449), juce::Colour (0xff3498db),
                                              juce::Colour (0xff5dade2), juce::Colour (0xff9b59b6), juce::Colour (0xffecf0f1) };
        return cls >= 0 && cls < neuro::numDrumClasses ? colours[cls] : juce::Colours::grey;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff15171c));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (10.0f);
        g.drawText ("dark", r.reduced (4.0f), juce::Justification::bottomLeft);
        g.drawText ("bright", r.reduced (4.0f), juce::Justification::bottomRight);
        g.drawText ("noisy", r.reduced (4.0f), juce::Justification::topLeft);

        const auto& slices = proc.getSlices().getSlices();

        for (size_t i = 0; i < slices.size(); ++i)
        {
            const auto& s = slices[i];

            if (s.timbreX < 0.0f)
                continue;

            const auto p = toScreen (s.timbreX, s.timbreY);
            const auto radius = (int) i == selected ? 7.0f : 5.0f;
            g.setColour (colourOf (s.effectiveClass()));
            g.fillEllipse (p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f);

            if ((int) i == selected)
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (p.x - radius - 2.0f, p.y - radius - 2.0f, radius * 2.0f + 4.0f, radius * 2.0f + 4.0f, 1.5f);
            }

            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText (juce::String ((int) i + 1), (int) p.x + 6, (int) p.y - 6, 30, 12, juce::Justification::left);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& slices = proc.getSlices().getSlices();
        auto best = -1;
        auto bestDistance = 400.0f; // within 20 px

        for (size_t i = 0; i < slices.size(); ++i)
            if (slices[i].timbreX >= 0.0f)
                if (const auto d = toScreen (slices[i].timbreX, slices[i].timbreY).getDistanceSquaredFrom (e.position); d < bestDistance)
                {
                    bestDistance = d;
                    best = (int) i;
                }

        if (best >= 0 && onSliceClicked)
            onSliceClicked (best);
    }

private:
    juce::Point<float> toScreen (float x, float y) const
    {
        const auto r = getLocalBounds().toFloat().reduced (14.0f);
        return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
    }

    NeuroMPCProcessor& proc;
};
