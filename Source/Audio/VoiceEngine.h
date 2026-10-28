#pragma once

#include "SampleData.h"
#include "../Core/SliceSet.h"
#include <array>
#include <atomic>

namespace neuro
{

struct VoiceParams
{
    float attackMs = 1.0f;
    float releaseMs = 30.0f;
    bool oneShot = true; // one-shot ignores note-off; gate releases on note-off
};

// Fixed-size polyphonic slice player. All methods except getPlayhead() are audio-thread only.
class VoiceEngine
{
public:
    static constexpr int maxVoices = 32;
    static constexpr int baseNote = 36; // C1 -> slice 1, MPC pad convention

    void prepare (double hostSampleRate) noexcept;
    void noteOn (int note, float velocity, const SampleData&, const SliceSet&, const VoiceParams&) noexcept;
    void noteOff (int note) noexcept;
    void killAll() noexcept;
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, const SampleData&) noexcept;

    // Any thread: source position of the most recently started active voice, or -1.
    int64_t getPlayhead() const noexcept { return playhead.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int note = -1;
        int64_t start = 0, end = 0;
        double pos = 0.0, rate = 1.0;
        float gain = 0.0f, env = 0.0f, attackStep = 1.0f, releaseStep = 1.0f;
        uint64_t order = 0;
    };

    std::array<Voice, maxVoices> voices;
    double hostRate = 44100.0;
    uint64_t counter = 0;
    std::atomic<int64_t> playhead { -1 };
};

} // namespace neuro
