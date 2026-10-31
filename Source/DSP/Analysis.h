#pragma once

#include "../Audio/SampleData.h"
#include "../Core/KeyDetector.h"
#include "../Core/PitchDetector.h"
#include "../Core/Timbre.h"

namespace neuro
{

// Analysis of one region: a slice (sliceId > 0) or the whole sample (sliceId 0, also gets key detection).
struct AnalysisJob
{
    std::shared_ptr<const SampleData> sample;
    int sampleVersion = 0;
    int sliceId = 0;
    int64_t start = 0, end = 0;
};

struct AnalysisResult
{
    int sampleVersion = 0;
    int sliceId = 0;
    int64_t start = 0, end = 0;
    PitchEstimate pitch;
    KeyEstimate key; // whole sample only
    TimbreFeatures timbre;
    DrumClass drumClass = DrumClass::perc;

    bool covers (int64_t s, int64_t e) const noexcept { return start == s && end == e; }
};

inline AnalysisResult analyseRegion (const AnalysisJob& job)
{
    AnalysisResult r { job.sampleVersion, job.sliceId, job.start, job.end, {}, {}, {}, DrumClass::perc };
    const auto& buffer = job.sample->buffer;
    const auto start = juce::jlimit<int64_t> (0, buffer.getNumSamples(), job.start);
    const auto end = juce::jlimit<int64_t> (start, buffer.getNumSamples(), job.end);
    std::vector<const float*> channels;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        channels.push_back (buffer.getReadPointer (ch, (int) start));

    r.pitch = detectPitch (channels.data(), (int) channels.size(), end - start, job.sample->sampleRate);

    r.timbre = analyseTimbre (channels.data(), (int) channels.size(), end - start, job.sample->sampleRate);
    r.drumClass = classify (r.timbre, r.pitch.status == PitchEstimate::Status::pitched);

    if (job.sliceId == 0)
        r.key = detectKey (channels.data(), (int) channels.size(), end - start, job.sample->sampleRate);

    return r;
}

} // namespace neuro
