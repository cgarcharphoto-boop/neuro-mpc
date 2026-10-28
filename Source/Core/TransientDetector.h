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

} // namespace neuro
