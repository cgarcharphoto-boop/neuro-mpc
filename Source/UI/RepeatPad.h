#pragma once

#include "../PluginProcessor.h"

// Repeat XY pad: hold it and Note Repeat is on; left .. right = rate (1 bar .. 1/128), bottom .. top = gate (1 .. 100 %).
// Slide while playing pads (MIDI controller or the other hand) for trap rolls that speed up, and gate sweeps that turn
// any sample into a glitch. Releasing it turns Note Repeat off (unless Lock is on).
class RepeatPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RepeatPad (NeuroMPCProcessor& p) : proc (p) {}

    std::function<void (bool)> onHold;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff15171c));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        const auto n = (int) neuro::repeatRateBeats.size();
        for (int i = 1; i < n; ++i)
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / n), r.getY() + 2.0f, r.getBottom() - 2.0f);

        const auto rate = (int) proc.apvts.getRawParameterValue (ParamIDs::repeatRate)->load();
        const auto gate = proc.apvts.getRawParameterValue (ParamIDs::repeatGate)->load();
        const auto on = proc.apvts.getRawParameterValue (ParamIDs::noteRepeat)->load() > 0.5f;
        const auto x = r.getX() + r.getWidth() * ((float) rate + 0.5f) / (float) n;
        const auto y = r.getBottom() - r.getHeight() * gate / 100.0f;
        g.setColour (juce::Colour (0xffff8a3d).withAlpha (on ? 1.0f : 0.5f));
        g.fillEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (11.0f);
        g.drawText (juce::String (neuro::repeatRateNames[juce::jlimit (0, n - 1, rate)]) + "  gate " + juce::String (juce::roundToInt (gate)) + "%",
                    r.reduced (6.0f), juce::Justification::topLeft);
        g.drawText ("hold: repeat", r.reduced (6.0f), juce::Justification::bottomRight);
    }

    void mouseDown (const juce::MouseEvent& e) override { if (onHold) onHold (true); apply (e); }
    void mouseDrag (const juce::MouseEvent& e) override { apply (e); }
    void mouseUp (const juce::MouseEvent&) override     { if (onHold) onHold (false); repaint(); }

private:
    void set (const char* id, float value)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void apply (const juce::MouseEvent& e)
    {
        const auto n = (int) neuro::repeatRateBeats.size();
        set (ParamIDs::repeatRate, (float) juce::jlimit (0, n - 1, (int) ((float) e.x / (float) juce::jmax (1, getWidth()) * (float) n)));
        set (ParamIDs::repeatGate, juce::jlimit (1.0f, 100.0f, 100.0f * (1.0f - (float) e.y / (float) juce::jmax (1, getHeight()))));
        repaint();
    }

    NeuroMPCProcessor& proc;
};
