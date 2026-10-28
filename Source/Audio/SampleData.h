#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace neuro
{

// Immutable once published. The original audio is never modified; all edits are metadata.
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::File file;

    int64_t length() const noexcept { return buffer.getNumSamples(); }
};

} // namespace neuro
