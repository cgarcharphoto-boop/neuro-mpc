#include "VoiceEngine.h"

namespace neuro
{

void VoiceEngine::prepare (double hostSampleRate) noexcept
{
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    killAll();
}

void VoiceEngine::noteOn (int note, float velocity, const SampleData& sample, const SliceSet& slices,
                          const StretchBank* bank, const VoiceParams& params) noexcept
{
    auto index = note - baseNote;

    if (index < 0 || index >= slices.size())
        return;

    const auto& slice = slices.getSlices()[(size_t) index];
    // Clamp to the buffer: the slice set may briefly belong to a previous sample during a swap.
    auto start = juce::jlimit<int64_t> (0, sample.length(), slice.startSample);
    auto end   = juce::jlimit<int64_t> (0, sample.length(), slice.endSample);

    if (! slice.enabled || end - start < 2)
        return;

    const auto* src = &sample.buffer;
    const auto displayOffset = start;
    auto displayScale = 1.0;

    if (bank != nullptr && slice.isStretched())
    {
        if (const auto* entry = bank->find (slice); entry != nullptr && entry->audio->getNumSamples() >= 2)
        {
            src = entry->audio.get();
            start = 0;
            end = src->getNumSamples();
            displayScale = 1.0 / slice.timeStretchRatio;
        }
    }

    auto* v = &voices[0];

    for (auto& candidate : voices)
    {
        if (! candidate.active) { v = &candidate; break; }
        if (candidate.order < v->order) v = &candidate; // steal the oldest
    }

    // Pitch is resampling-based (changes duration); pitch-preserving shift arrives in Phase 8.
    auto rate = sample.sampleRate / hostRate * std::pow (2.0, (double) slice.pitchSemitones / 12.0);

    v->active = true;
    v->releasing = false;
    v->note = note;
    v->src = src;
    v->displayOffset = displayOffset;
    v->displayScale = displayScale;
    v->start = start;
    v->end = end;
    v->rate = slice.reverse ? -rate : rate;
    v->pos = slice.reverse ? (double) (end - 1) : (double) start;
    v->gain = juce::jlimit (0.0f, 1.0f, velocity) * slice.gain;
    v->attackStep = 1.0f / (params.attackMs * 0.001f * (float) hostRate + 1.0f);
    v->releaseStep = 1.0f / (params.releaseMs * 0.001f * (float) hostRate + 1.0f);
    v->env = params.attackMs > 0.0f ? 0.0f : 1.0f;
    v->order = ++counter;

    if (params.oneShot)
        v->note = -1; // one-shot voices ignore note-off
}

void VoiceEngine::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && v.note == note)
            v.releasing = true;
}

void VoiceEngine::killAll() noexcept
{
    for (auto& v : voices)
        v.active = false;

    playhead.store (-1, std::memory_order_relaxed);
}

void VoiceEngine::dropVoicesNotIn (const StretchBank* bank, const juce::AudioBuffer<float>* sampleBuffer) noexcept
{
    for (auto& v : voices)
        if (v.active && v.src != sampleBuffer && (bank == nullptr || ! bank->contains (v.src)))
            v.active = false;
}

void VoiceEngine::render (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept
{
    constexpr double declick = 64.0; // samples of fade before a slice's end
    const auto outChannels = out.getNumChannels();
    const Voice* newest = nullptr;

    for (auto& v : voices)
    {
        const auto srcChannels = v.active ? v.src->getNumChannels() : 0;

        for (int i = 0; i < numSamples && v.active; ++i)
        {
            auto idx = (int64_t) std::floor (v.pos);

            if (idx < v.start || idx >= v.end)
            {
                v.active = false;
                break;
            }

            if (v.releasing)
            {
                v.env -= v.releaseStep;

                if (v.env <= 0.0f)
                {
                    v.active = false;
                    break;
                }
            }
            else if (v.env < 1.0f)
            {
                v.env = std::min (1.0f, v.env + v.attackStep);
            }

            auto remaining = v.rate > 0.0 ? ((double) v.end - v.pos) / v.rate : (v.pos - (double) v.start) / -v.rate;
            auto g = v.gain * v.env * (float) std::min (1.0, remaining / declick);
            auto frac = (float) (v.pos - (double) idx);
            auto next = idx + 1 < v.end ? idx + 1 : idx;

            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto* src = v.src->getReadPointer (std::min (ch, srcChannels - 1));
                auto a = src[idx];
                out.addSample (ch, startSample + i, (a + frac * (src[next] - a)) * g);
            }

            v.pos += v.rate;
        }

        if (v.active && (newest == nullptr || v.order > newest->order))
            newest = &v;
    }

    playhead.store (newest != nullptr ? newest->displayOffset + (int64_t) ((newest->pos - (double) newest->start) * newest->displayScale) : -1,
                    std::memory_order_relaxed);
}

} // namespace neuro
