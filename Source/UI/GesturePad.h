#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Gesture-to-Arpeggio: dragging sets host parameters (so gestures can be automated and recorded).
// X -> density (step probability 25..100 %), Y -> octave range (1..4), drag speed -> phrase variation (0..100 %).
// Optional: the arpeggiator works the same without it.
class GesturePad : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    GesturePad (juce::AudioProcessorValueTreeState& s, const char* densityId, const char* octavesId, const char* variationId)
        : density (*s.getParameter (densityId)), octaves (*s.getParameter (octavesId)), variation (*s.getParameter (variationId))
    {
        setTooltip ("Gesture: left-right = density, up-down = octave range, drag speed = variation");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff1c1f26));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));

        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());

        const auto x = r.getX() + r.getWidth() * (density.getValue() - 0.25f) / 0.75f;
        const auto y = r.getBottom() - r.getHeight() * (octaves.convertFrom0to1 (octaves.getValue()) - 0.5f) / 4.0f;
        const auto glow = 4.0f + 10.0f * variation.getValue();
        g.setColour (juce::Colour (0xffff8a3d).withAlpha (0.35f));
        g.fillEllipse (x - glow, y - glow, glow * 2.0f, glow * 2.0f);
        g.setColour (juce::Colour (0xffff8a3d));
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (10.0f);
        g.drawText ("GESTURE", r.reduced (4.0f), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto* p : { &density, &octaves, &variation })
            p->beginChangeGesture();

        last = e.position;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        apply (e, 0.0f);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto speed = e.position.getDistanceFrom (last) / (float) juce::jmax (1.0, now - lastTime); // px per ms
        last = e.position;
        lastTime = now;
        apply (e, juce::jlimit (0.0f, 1.0f, speed / 1.5f));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        for (auto* p : { &density, &octaves, &variation })
            p->endChangeGesture();
    }

private:
    void apply (const juce::MouseEvent& e, float speed)
    {
        const auto nx = juce::jlimit (0.0f, 1.0f, e.position.x / (float) juce::jmax (1, getWidth()));
        const auto ny = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / (float) juce::jmax (1, getHeight()));
        density.setValueNotifyingHost (0.25f + 0.75f * nx);              // probability param is 0..100 %
        octaves.setValueNotifyingHost (octaves.convertTo0to1 (1.0f + std::round (ny * 3.0f)));
        variation.setValueNotifyingHost (juce::jmax (variation.getValue() * 0.9f, speed)); // speed raises, stillness decays
        repaint();
    }

    juce::RangedAudioParameter &density, &octaves, &variation;
    juce::Point<float> last;
    double lastTime = 0.0;
};
