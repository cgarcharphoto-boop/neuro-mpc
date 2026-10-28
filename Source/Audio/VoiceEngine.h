#pragma once

#include "SampleData.h"
#include "../Core/PadBank.h"
#include "../Core/Scales.h"
#include "../Core/SliceSet.h"
#include "../DSP/TimeStretch.h"
#include <array>
#include <atomic>

namespace neuro
{

struct VoiceParams
{
    float attackMs = 1.0f;
    float releaseMs = 30.0f;
    bool oneShot = true;           // one-shot ignores note-off; gate releases on note-off

    // Keyboard mode plays one slice chromatically: note - keyboardRoot semitones (after scale snapping).
    bool keyboardMode = false;
    int keyboardSlice = 0;
    float keyboardRoot = 60.0f;    // fractional, so a detuned sample plays in tune
    uint16_t scaleMask = 0xFFF;
    int scaleRoot = 0;
};

// Fixed-size polyphonic slice player. All methods except getPlayhead() are audio-thread only.
class VoiceEngine
{
public:
    static constexpr int maxVoices = 32;
    static constexpr int baseNote = 36;     // pad mode without a pad map: C1 -> slice 1
    static constexpr float chokeMs = 5.0f;  // fade applied to choked voices

    void prepare (double hostSampleRate) noexcept;
    // Routes a note: keyboard mode -> the keyboard slice; pad mode -> every pad on that note (layers), or 36+n without pads.
    // pitchOffset (semitones) is added on top (e.g. a sequencer step's pitch).
    void noteOn (int note, float velocity, const SampleData&, const SliceSet&, const StretchBank*, const PadMap*,
                 const VoiceParams&, float pitchOffset = 0.0f) noexcept;

    // Starts one slice. noteTag is matched by noteOff (gate mode). Uses the slice's render from the bank when it needs
    // one and it is ready, otherwise the original region.
    void playSlice (int sliceIndex, int noteTag, float level, float pitchOffset, int chokeGroup, const SampleData&,
                    const SliceSet&, const StretchBank*, const VoiceParams&) noexcept;
    void noteOff (int note) noexcept;
    void killAll() noexcept;
    // Stops voices reading renders that are no longer in the published bank (they may be freed).
    void dropVoicesNotIn (const StretchBank*, const juce::AudioBuffer<float>* sampleBuffer) noexcept;
    // modSemitones: per-sample global pitch offset (transpose, bend, LFO) applied to every voice by resampling.
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, const float* modSemitones) noexcept;

    // Any thread: original-sample position of the most recently started active voice, or -1.
    int64_t getPlayhead() const noexcept { return playhead.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int note = -1;
        const juce::AudioBuffer<float>* src = nullptr;
        int64_t displayOffset = 0; // maps pos back to original-sample coordinates for the playhead
        double displayScale = 1.0;
        int64_t start = 0, end = 0;
        double pos = 0.0, baseRate = 1.0; // baseRate is negative for reverse playback
        float gain = 0.0f, env = 0.0f, attackStep = 1.0f, releaseStep = 1.0f;
        int chokeGroup = 0;
        uint64_t order = 0;
    };

    std::array<Voice, maxVoices> voices;
    double hostRate = 44100.0;
    uint64_t counter = 0;
    std::atomic<int64_t> playhead { -1 };
};

} // namespace neuro
