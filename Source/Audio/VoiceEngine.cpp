#include "VoiceEngine.h"

namespace neuro
{

void VoiceEngine::prepare (double hostSampleRate) noexcept
{
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    killAll();
}

void VoiceEngine::noteOn (int note, float velocity, const SampleData& sample, const SliceSet& slices,
                          const StretchBank* bank, const PadMap* pads, const VoiceParams& params) noexcept
{
    velocity = juce::jlimit (0.0f, 1.0f, velocity);

    if (params.keyboardMode)
    {
        const auto offset = (float) (snapToScale (note, params.scaleMask, params.scaleRoot) - (double) params.keyboardRoot);
        playSlice (params.keyboardSlice, note, velocity, offset, 0, sample, slices, bank, params);
    }
    else if (pads == nullptr)
    {
        playSlice (note - baseNote, note, velocity, 0.0f, 0, sample, slices, bank, params);
    }
    else
    {
        for (const auto& pad : *pads)
            if (pad.note == note && pad.sliceId > 0)
                playSlice (slices.indexOfId (pad.sliceId), note, pad.levelFor (velocity), pad.pitch, pad.chokeGroup,
                           sample, slices, bank, params);
    }
}

void VoiceEngine::playSlice (int index, int noteTag, float level, float pitchOffset, int chokeGroup, const SampleData& sample,
                             const SliceSet& slices, const StretchBank* bank, const VoiceParams& params) noexcept
{
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
    auto pitch = (double) slice.pitchSemitones + pitchOffset; // slice pitch is resampled unless a keep-length render is used

    if (bank != nullptr && slice.needsRender())
    {
        if (const auto* entry = bank->find (slice); entry != nullptr && entry->audio->getNumSamples() >= 2)
        {
            src = entry->audio.get();
            start = 0;
            end = src->getNumSamples();
            displayScale = 1.0 / slice.timeStretchRatio;
            pitch -= slice.renderedPitch(); // already in the render
        }
    }

    if (chokeGroup > 0)
        for (auto& other : voices)
            if (other.active && other.chokeGroup == chokeGroup)
            {
                other.releasing = true;
                other.releaseStep = std::max (other.releaseStep, 1.0f / (chokeMs * 0.001f * (float) hostRate));
            }

    auto* v = &voices[0];

    for (auto& candidate : voices)
    {
        if (! candidate.active) { v = &candidate; break; }
        if (candidate.order < v->order) v = &candidate; // steal the oldest
    }

    const auto rate = sample.sampleRate / hostRate * std::exp2 (pitch / 12.0);

    v->active = true;
    v->releasing = false;
    v->note = params.oneShot ? -1 : noteTag; // one-shot voices ignore note-off
    v->chokeGroup = chokeGroup;
    v->src = src;
    v->displayOffset = displayOffset;
    v->displayScale = displayScale;
    v->start = start;
    v->end = end;
    v->baseRate = slice.reverse ? -rate : rate;
    v->pos = slice.reverse ? (double) (end - 1) : (double) start;
    v->gain = std::max (0.0f, level) * slice.gain;
    v->attackStep = 1.0f / (params.attackMs * 0.001f * (float) hostRate + 1.0f);
    v->releaseStep = 1.0f / (params.releaseMs * 0.001f * (float) hostRate + 1.0f);
    v->env = params.attackMs > 0.0f ? 0.0f : 1.0f;
    v->order = ++counter;
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

void VoiceEngine::render (juce::AudioBuffer<float>& out, int startSample, int numSamples, const float* modSemitones) noexcept
{
    constexpr double declick = 64.0; // samples of fade before a slice's end
    constexpr int modInterval = 16;  // pitch modulation update rate (samples)
    const auto outChannels = out.getNumChannels();
    const Voice* newest = nullptr;

    for (auto& v : voices)
    {
        const auto srcChannels = v.active ? v.src->getNumChannels() : 0;
        double rate = v.baseRate;

        for (int i = 0; i < numSamples && v.active; ++i)
        {
            if (i % modInterval == 0)
                rate = v.baseRate * std::exp2 ((double) modSemitones[i] / 12.0);

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

            auto remaining = rate > 0.0 ? ((double) v.end - v.pos) / rate : (v.pos - (double) v.start) / -rate;
            auto g = v.gain * v.env * (float) std::min (1.0, remaining / declick);
            auto frac = (float) (v.pos - (double) idx);
            auto next = idx + 1 < v.end ? idx + 1 : idx;

            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto* src = v.src->getReadPointer (std::min (ch, srcChannels - 1));
                auto a = src[idx];
                out.addSample (ch, startSample + i, (a + frac * (src[next] - a)) * g);
            }

            v.pos += rate;
        }

        if (v.active && (newest == nullptr || v.order > newest->order))
            newest = &v;
    }

    playhead.store (newest != nullptr ? newest->displayOffset + (int64_t) ((newest->pos - (double) newest->start) * newest->displayScale) : -1,
                    std::memory_order_relaxed);
}

} // namespace neuro
