#include "WaveformView.h"

namespace
{
    constexpr float markerHitPx = 6.0f;
    const juce::Colour accent { 0xffff8a3d };
}

WaveformView::WaveformView (NeuroMPCProcessor& p) : proc (p)
{
    sampleChanged();
}

void WaveformView::sampleChanged()
{
    thumb.clear();
    selected = -1;

    if (auto s = proc.getSample())
    {
        // Built once per sample; AudioThumbnail keeps a multi-resolution min/max peak cache.
        thumb.reset (s->buffer.getNumChannels(), s->sampleRate, s->length());
        thumb.addBlock (0, s->buffer, 0, s->buffer.getNumSamples());
        viewStart = 0;
        viewEnd = s->length();
    }

    repaint();
}

float WaveformView::sampleToX (int64_t s) const noexcept
{
    return (float) ((double) (s - viewStart) / (double) juce::jmax<int64_t> (1, viewEnd - viewStart) * getWidth());
}

int64_t WaveformView::xToSample (float x) const noexcept
{
    auto s = viewStart + (int64_t) ((double) x / juce::jmax (1, getWidth()) * (double) (viewEnd - viewStart));
    return juce::jlimit<int64_t> (0, proc.getSlices().getTotalLength(), s);
}

int WaveformView::markerNear (float x) const noexcept
{
    const auto& slices = proc.getSlices().getSlices();

    for (size_t i = 1; i < slices.size(); ++i)
        if (std::abs (sampleToX (slices[i].startSample) - x) <= markerHitPx)
            return (int) i;

    return -1;
}

void WaveformView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll (juce::Colour (0xff15171c));

    auto sample = proc.getSample();

    if (sample == nullptr)
    {
        g.setColour (juce::Colours::grey);
        auto missing = proc.getMissingSamplePath();
        g.drawFittedText (missing.isEmpty() ? "Drop an audio file here or click Load"
                                            : "Missing sample: " + missing + "\nLoad or drop the file to relink",
                          bounds, juce::Justification::centred, 2);
        return;
    }

    const auto& slices = proc.getSlices().getSlices();
    const auto h = (float) getHeight();

    for (size_t i = 0; i < slices.size(); ++i)
    {
        auto x0 = sampleToX (slices[i].startSample), x1 = sampleToX (slices[i].endSample);

        if (x1 < 0.0f || x0 > (float) getWidth())
            continue;

        g.setColour ((int) i == selected ? accent.withAlpha (0.18f)
                                         : juce::Colours::white.withAlpha (i % 2 == 0 ? 0.03f : 0.0f));
        g.fillRect (x0, 0.0f, x1 - x0, h);
    }

    g.setColour (juce::Colour (0xff7fd4ff));
    thumb.drawChannels (g, bounds.reduced (0, 14), (double) viewStart / sample->sampleRate,
                        (double) viewEnd / sample->sampleRate, 1.0f);

    g.setFont (11.0f);

    for (size_t i = 0; i < slices.size(); ++i)
    {
        auto x = sampleToX (slices[i].startSample);

        if (x < -40.0f || x > (float) getWidth())
            continue;

        g.setColour (accent);

        if (i > 0)
            g.drawVerticalLine ((int) x, 0.0f, h);

        g.drawText (juce::String ((int) i + 1), (int) x + 3, 0, 30, 14, juce::Justification::left);

        if (slices[i].isStretched())
            g.drawText ("x" + juce::String (slices[i].timeStretchRatio, 2), (int) x + 3, getHeight() - 14, 50, 14,
                        juce::Justification::left);
    }

    if (auto ph = proc.getPlayhead(); ph >= 0)
    {
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) sampleToX (ph), 0.0f, h);
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (markerNear ((float) e.x) > 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                  : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (proc.getSample() == nullptr)
        return;

    auto marker = markerNear ((float) e.x);

    if (e.mods.isPopupMenu())
    {
        if (marker > 0)
            proc.editSlices ([marker] (auto& s) { s.mergeWithPrevious (marker); });

        repaint();
        return;
    }

    if (marker > 0)
    {
        dragging = marker;
        return;
    }

    selected = proc.getSlices().indexAt (xToSample ((float) e.x));

    if (selected >= 0)
    {
        auditioning = neuro::VoiceEngine::baseNote + selected;
        proc.auditionNote (auditioning, 0.9f);
    }

    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging > 0)
    {
        auto pos = xToSample ((float) e.x);
        proc.editSlices ([this, pos] (auto& s) { s.moveStart (dragging, pos); });
        repaint();
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;

    if (auditioning >= 0)
        proc.auditionNote (auditioning, 0.0f);

    auditioning = -1;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (proc.getSample() == nullptr || markerNear ((float) e.x) > 0)
        return;

    auto pos = xToSample ((float) e.x);
    proc.editSlices ([pos] (auto& s) { s.split (pos); });
    repaint();
}

void WaveformView::zoomAround (float x, double factor)
{
    const auto total = proc.getSlices().getTotalLength();

    if (total <= 0)
        return;

    const auto anchor = xToSample (x);
    const auto len = (double) (viewEnd - viewStart);
    const auto newLen = juce::jlimit (64.0, (double) total, len * factor);
    auto start = (int64_t) ((double) anchor - (double) (anchor - viewStart) * newLen / len);

    viewStart = juce::jlimit<int64_t> (0, total - (int64_t) newLen, start);
    viewEnd = viewStart + (int64_t) newLen;
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto total = proc.getSlices().getTotalLength();

    if (total <= 0)
        return;

    if (std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const auto len = viewEnd - viewStart;
        viewStart = juce::jlimit<int64_t> (0, total - len, viewStart - (int64_t) (w.deltaX * (float) len * 0.5f));
        viewEnd = viewStart + len;
        repaint();
    }
    else
    {
        zoomAround ((float) e.x, std::pow (2.0, -w.deltaY * 2.0));
    }
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoomAround ((float) e.x, 1.0 / juce::jmax (0.01f, scale));
}
