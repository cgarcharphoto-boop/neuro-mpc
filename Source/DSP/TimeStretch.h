#pragma once

#include <juce_core/juce_core.h>
#include "../Audio/SampleData.h"
#include "../Core/SliceSet.h"

namespace neuro
{

// Identifies one rendered slice. Any change to the slice's region, ratio or rendered pitch produces a new key.
struct StretchKey
{
    int sliceId = 0;
    int64_t start = 0, end = 0;
    float ratio = 1.0f;
    bool preserveTransients = true;
    float pitch = 0.0f; // duration-preserving shift in semitones (keepLength mode only)

    static StretchKey of (const SliceDefinition& s) noexcept
    {
        return { s.id, s.startSample, s.endSample, s.timeStretchRatio, s.preserveTransients, s.renderedPitch() };
    }

    bool operator== (const StretchKey& o) const noexcept
    {
        return sliceId == o.sliceId && start == o.start && end == o.end && preserveTransients == o.preserveTransients
               && std::abs (ratio - o.ratio) < 1.0e-6f && std::abs (pitch - o.pitch) < 1.0e-6f;
    }
};

struct StretchEntry
{
    StretchKey key;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
};

// Immutable set of rendered slices available to the audio thread (published via RtShared).
struct StretchBank
{
    std::vector<StretchEntry> entries;

    // extraPitch: a Chord-Lock variant rendered `extraPitch` semitones above the slice's own rendered pitch.
    const StretchEntry* find (const SliceDefinition& s, int extraPitch = 0) const noexcept
    {
        auto key = StretchKey::of (s);
        key.pitch += (float) extraPitch;

        for (const auto& e : entries)
            if (e.key == key)
                return &e;

        return nullptr;
    }

    bool contains (const juce::AudioBuffer<float>* audio) const noexcept
    {
        for (const auto& e : entries)
            if (e.audio.get() == audio)
                return true;

        return false;
    }
};

// Offline stretch (pitch preserved, optionally transposed by pitchSemitones) of [start, end) to exactly round((end - start) * ratio) samples (Signalsmith Stretch).
// Each region is rendered between silent margins, then cropped, so edge artefacts and the
// pre-echo of a region's opening transient fall outside the result. With preserveTransients, the slice is split at
// detected transients and each segment is stretched independently, so no transient smears into the audio before it.
std::shared_ptr<juce::AudioBuffer<float>> renderStretch (const juce::AudioBuffer<float>& source, int64_t start, int64_t end,
                                                         double sampleRate, float ratio, bool preserveTransients = true,
                                                         float pitchSemitones = 0.0f);

struct StretchJob  { std::shared_ptr<const SampleData> sample; int sampleVersion = 0; SliceDefinition slice; };
struct StretchDone { int sampleVersion = 0; StretchEntry entry; };

StretchDone renderStretchJob (const StretchJob&);

} // namespace neuro
