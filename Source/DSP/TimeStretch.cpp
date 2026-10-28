#include "TimeStretch.h"
#include "../Core/TransientDetector.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace neuro
{

namespace
{
    // Stretches source[start, start + length) into dest[destStart, destStart + destLength).
    void renderRegion (const juce::AudioBuffer<float>& source, int start, int length, double sampleRate,
                       juce::AudioBuffer<float>& dest, int destStart, int destLength)
    {
        const auto channels = source.getNumChannels();
        const auto ratio = (double) destLength / (double) length;

        // Silent margins of ~one analysis block (presetDefault uses 120 ms) on both sides absorb the priming/flush
        // edge effects and the pre-echo of the region's opening transient; both are cropped away.
        const auto marginOut = (int) std::ceil (0.15 * sampleRate);
        const auto margin = (int) std::ceil (marginOut / ratio);
        const auto inLength = margin + length + margin;
        const auto lead = (int) std::lround (margin * ratio);
        const auto outLength = lead + destLength + (int) std::lround (margin * ratio);

        juce::AudioBuffer<float> input (channels, inLength), output (channels, outLength);
        input.clear();
        output.clear();

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = source.getReadPointer (ch, start);
            auto* in = input.getWritePointer (ch);
            std::copy (src, src + length, in + margin);
        }

        signalsmith::stretch::SignalsmithStretch<float> stretch;
        stretch.presetDefault (channels, (float) sampleRate);

        if (stretch.exact (input.getArrayOfReadPointers(), inLength, output.getArrayOfWritePointers(), outLength))
            for (int ch = 0; ch < channels; ++ch)
                dest.copyFrom (ch, destStart, output, ch, lead, destLength);
    }
}

std::shared_ptr<juce::AudioBuffer<float>> renderStretch (const juce::AudioBuffer<float>& source, int64_t start, int64_t end,
                                                         double sampleRate, float ratio, bool preserveTransients)
{
    const auto channels = source.getNumChannels();
    start = juce::jlimit<int64_t> (0, source.getNumSamples(), start);
    end = juce::jlimit<int64_t> (start, source.getNumSamples(), end);
    ratio = std::clamp (ratio, SliceSet::minStretch, SliceSet::maxStretch);

    const auto length = (int) (end - start);
    const auto outLength = juce::jmax (1, (int) std::lround ((double) length * ratio));
    auto output = std::make_shared<juce::AudioBuffer<float>> (juce::jmax (1, channels), outLength);
    output->clear();

    if (channels == 0 || length < 2)
        return output;

    std::vector<int> bounds { 0 };

    if (preserveTransients)
    {
        std::vector<const float*> ptrs;

        for (int ch = 0; ch < channels; ++ch)
            ptrs.push_back (source.getReadPointer (ch, (int) start));

        for (auto t : detectTransients (ptrs.data(), channels, length, sampleRate, 0.5f))
            if (t > bounds.back())
                bounds.push_back ((int) t);
    }

    bounds.push_back (length);

    for (size_t k = 0; k + 1 < bounds.size(); ++k)
    {
        // Output boundaries come from the cumulative input positions, so the total length is exact.
        const auto o0 = (int) std::lround (bounds[k] * (double) ratio);
        const auto o1 = k + 2 == bounds.size() ? outLength : (int) std::lround (bounds[k + 1] * (double) ratio);

        if (o1 > o0)
            renderRegion (source, (int) start + bounds[k], bounds[k + 1] - bounds[k], sampleRate, *output, o0, o1 - o0);
    }

    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::clip (output->getWritePointer (ch), output->getReadPointer (ch), -4.0f, 4.0f, outLength);

    return output;
}

StretchRenderer::StretchRenderer() : juce::Thread ("NEURO stretch renderer")
{
    startThread (juce::Thread::Priority::low);
}

StretchRenderer::~StretchRenderer()
{
    signalThreadShouldExit();
    notify();
    stopThread (10000);
}

void StretchRenderer::request (Job job)
{
    {
        std::scoped_lock lock (mutex);
        pending[job.slice.id] = std::move (job);
    }

    notify();
}

std::vector<StretchRenderer::Done> StretchRenderer::takeCompleted()
{
    std::scoped_lock lock (mutex);
    return std::exchange (done, {});
}

bool StretchRenderer::isIdle() const
{
    std::scoped_lock lock (mutex);
    return pending.empty() && ! working;
}

void StretchRenderer::run()
{
    while (! threadShouldExit())
    {
        std::optional<Job> job;

        {
            std::scoped_lock lock (mutex);

            if (! pending.empty())
            {
                job = std::move (pending.begin()->second);
                pending.erase (pending.begin());
                working = true;
            }
        }

        if (! job)
        {
            wait (-1);
            continue;
        }

        const auto& s = job->slice;
        auto audio = renderStretch (job->sample->buffer, s.startSample, s.endSample, job->sample->sampleRate,
                                    s.timeStretchRatio, s.preserveTransients);

        std::scoped_lock lock (mutex);
        done.push_back ({ job->sampleVersion, { StretchKey::of (s), std::move (audio) } });
        working = false;
    }
}

} // namespace neuro
