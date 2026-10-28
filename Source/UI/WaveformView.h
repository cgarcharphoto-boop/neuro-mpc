#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"

// Waveform + slice editor. Double-click: split. Drag marker: move. Right-click marker: remove.
// Click a slice: select and audition. Wheel/pinch: zoom around cursor. Horizontal scroll: pan.
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (NeuroMPCProcessor&);

    void sampleChanged();
    int getSelectedSlice() const noexcept { return selected; }
    void setSelectedSlice (int index)     { selected = index; repaint(); }
    std::function<void()> onSliceClicked;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

private:
    float sampleToX (int64_t s) const noexcept;
    int64_t xToSample (float x) const noexcept;
    int markerNear (float x) const noexcept; // slice index whose start marker is near x, or -1
    void zoomAround (float x, double factor);

    NeuroMPCProcessor& proc;
    juce::AudioThumbnailCache cache { 2 };
    juce::AudioThumbnail thumb { 256, proc.formats, cache };
    int64_t viewStart = 0, viewEnd = 0;
    int selected = -1, dragging = -1, auditioning = -1;
};
