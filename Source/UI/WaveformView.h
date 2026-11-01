#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"

// Waveform + slice editor. Double-click: split. Drag marker: move. Right-click marker: remove.
// Click a slice: select and audition. Wheel/pinch: zoom around cursor. Horizontal scroll: pan.
// Chop precision: crosshair with time readout; slices tinted by drum class; new and dragged markers snap to the nearest
// hit (magnetic, Snap on); keys: Delete/Backspace remove the hovered (else selected) marker, Left/Right nudge the
// selected slice's start (Shift: 1 sample), Up/Down select the previous/next slice, Space auditions the selected slice.
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
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool snapToHits = true;

private:
    float sampleToX (int64_t s) const noexcept;
    int64_t xToSample (float x) const noexcept;
    int markerNear (float x) const noexcept; // slice index whose start marker is near x, or -1
    void zoomAround (float x, double factor);
    int64_t snapped (int64_t pos);           // nearest hit within 12 px when snapping
    void selectSlice (int index);

    NeuroMPCProcessor& proc;
    juce::AudioThumbnailCache cache { 2 };
    juce::AudioThumbnail thumb { 256, proc.formats, cache };
    int64_t viewStart = 0, viewEnd = 0;
    int selected = -1, dragging = -1, auditioning = -1, hoveredMarker = -1;
    float hoverX = -1.0f;
    std::vector<int64_t> hits;               // transient positions for snapping, per sample
    const void* hitsFor = nullptr;
};
