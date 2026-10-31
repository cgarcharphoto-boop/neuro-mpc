#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuro
{

// Energy-flux onset detector: flags frames whose dB rise exceeds the local mean rise by a sensitivity-dependent margin.
// Suggestions only: results are meant to be reviewed and corrected in the waveform editor.
inline std::vector<int64_t> detectTransients (const float* const* channels, int numChannels, int64_t numSamples,
                                              double sampleRate, float sensitivity)
{
    constexpr int hop = 256;
    constexpr int window = 8;
    constexpr float floorDb = -60.0f;

    std::vector<int64_t> onsets;

    if (channels == nullptr || numChannels <= 0 || numSamples < hop * 4 || sampleRate <= 0.0)
        return onsets;

    const auto numFrames = (size_t) (numSamples / hop);
    std::vector<float> db (numFrames), flux (numFrames, 0.0f);

    for (size_t f = 0; f < numFrames; ++f)
    {
        double sum = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
            for (size_t i = 0; i < (size_t) hop; ++i)
            {
                auto x = (double) channels[ch][f * hop + i];
                sum += x * x;
            }

        db[f] = (float) (10.0 * std::log10 (sum / (hop * numChannels) + 1.0e-12));

        if (f > 0)
            flux[f] = std::max (0.0f, db[f] - db[f - 1]);
    }

    const auto margin = 14.0f - 11.0f * std::clamp (sensitivity, 0.0f, 1.0f); // dB
    const auto minGap = (int64_t) (0.05 * sampleRate);
    int64_t last = -minGap;

    for (size_t f = 1; f + 1 < numFrames; ++f)
    {
        if (db[f] < floorDb || flux[f] < flux[f - 1] || flux[f] <= flux[f + 1])
            continue;

        auto lo = f > window ? f - window : 0;
        auto hi = std::min (numFrames - 1, f + window);
        float mean = 0.0f;

        for (auto k = lo; k <= hi; ++k)
            mean += flux[k];

        mean /= (float) (hi - lo + 1);

        if (flux[f] > mean + margin)
        {
            auto pos = (int64_t) (f * hop);

            if (pos - last >= minGap)
            {
                onsets.push_back (pos);
                last = pos;
            }
        }
    }

    return onsets;
}

// Tempo of a loop from its onset-strength envelope: autocorrelation over 60-180 BPM with a mild preference for
// ~110 BPM (resolves half/double-tempo ambiguity towards common tempos).
inline double estimateTempo (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate)
{
    constexpr int hop = 256;
    const auto frames = (size_t) (numSamples / hop);

    if (frames < 64 || sampleRate <= 0.0)
        return 120.0;

    std::vector<float> db (frames), flux (frames, 0.0f);

    for (size_t f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            for (size_t i = 0; i < (size_t) hop; ++i)
                sum += (double) channels[ch][f * hop + i] * channels[ch][f * hop + i];
        db[f] = (float) (10.0 * std::log10 (sum / (hop * numChannels) + 1.0e-12));
        if (f > 0) flux[f] = std::max (0.0f, db[f] - db[f - 1]);
    }

    const auto framesPerSecond = sampleRate / hop;
    auto bestBpm = 120.0, bestScore = -1.0;

    for (double bpm = 60.0; bpm <= 180.0; bpm += 0.5)
    {
        const auto lag = framesPerSecond * 60.0 / bpm;
        double score = 0.0;

        for (size_t f = 0; f + (size_t) (lag * 4) + 1 < frames; ++f)
            for (int m = 1; m <= 4; ++m)
            {
                const auto pos = (double) f + lag * m;
                const auto i = (size_t) pos;
                const auto frac = (float) (pos - (double) i);
                score += flux[f] * (flux[i] * (1.0f - frac) + flux[i + 1] * frac) / m;
            }

        const auto prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 110.0) / 0.6, 2.0));
        score *= prior;

        if (score > bestScore)
        {
            bestScore = score;
            bestBpm = bpm;
        }
    }

    // Metric ambiguity (x2, x1/2, 4:3, 3:4, 3:2, 2:3): prefer the related tempo at which the loop is a whole number
    // of 4/4 bars; ties keep the autocorrelation winner.
    const auto seconds = (double) numSamples / sampleRate;
    auto barError = [seconds] (double bpm) { const auto bars = seconds * bpm / 240.0; return std::abs (bars - std::round (bars)); };
    auto chosen = bestBpm;

    for (double ratio : { 0.5, 2.0, 0.75, 4.0 / 3.0, 1.5, 2.0 / 3.0 })
    {
        const auto candidate = bestBpm * ratio;

        if (candidate >= 60.0 && candidate <= 180.0 && barError (candidate) + 0.05 < barError (chosen))
            chosen = candidate;
    }

    return chosen;
}

} // namespace neuro
