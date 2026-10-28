#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuro
{

inline double frequencyToMidi (double hz) noexcept { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double midiToFrequency (double midi) noexcept { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

struct PitchEstimate
{
    enum class Status { pitched, uncertain, unpitched, tooShort };

    Status status = Status::unpitched;
    double frequency = 0.0;     // Hz; best guess, also reported when uncertain
    double midi = 0.0;          // fractional MIDI note
    float confidence = 0.0f;    // 0..1: voiced fraction x stability x periodicity
    float voicedFraction = 0.0f;
    bool ambiguous = false;     // periodic, but the spectrum looks like several notes (chord/cluster)

    int nearestNote() const noexcept   { return (int) std::lround (midi); }
    double cents() const noexcept      { return (midi - std::round (midi)) * 100.0; }
};

// Hann-windowed Goertzel power of x[0, length) at hz.
inline double goertzelPower (const float* x, int length, double hz, double sampleRate)
{
    const auto w = 6.283185307179586 * hz / sampleRate;
    const auto coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const auto hann = 0.5 - 0.5 * std::cos (6.283185307179586 * i / (length - 1));
        const auto s0 = x[i] * hann + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return std::max (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

// Frame-wise YIN (de Cheveigne & Kawahara 2002) over a mono mixdown, aggregated across frames.
// Monophonic/tonal material only: polyphonic, percussive or noisy input yields uncertain/unpitched.
inline PitchEstimate detectPitch (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate,
                                  double minHz = 40.0, double maxHz = 2000.0, double maxSeconds = 8.0)
{
    constexpr double threshold = 0.15;
    constexpr double silenceRms = 1.0e-3; // -60 dBFS
    PitchEstimate result;

    if (channels == nullptr || numChannels <= 0 || sampleRate <= 0.0)
        return result;

    const auto window = (int) std::lround (0.025 * sampleRate);
    const auto maxLag = (int) std::ceil (sampleRate / minHz);
    const auto minLag = std::max (2, (int) std::floor (sampleRate / maxHz));
    const auto n = (int64_t) std::min<double> ((double) numSamples, maxSeconds * sampleRate);

    if (n < window + maxLag + 2)
    {
        result.status = PitchEstimate::Status::tooShort;
        return result;
    }

    std::vector<float> x ((size_t) n);

    for (int64_t i = 0; i < n; ++i)
    {
        float sum = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            sum += channels[ch][i];

        x[(size_t) i] = sum / (float) numChannels;
    }

    struct Frame { double midi, aperiodicity; };
    std::vector<Frame> voiced;
    std::vector<double> d ((size_t) maxLag + 2), cmnd ((size_t) maxLag + 2);
    int nonSilent = 0;

    for (int64_t start = 0; start + window + maxLag + 1 < n; start += window)
    {
        const auto* f = x.data() + start;
        double energy = 0.0;

        for (int j = 0; j < window; ++j)
            energy += (double) f[j] * f[j];

        if (std::sqrt (energy / window) < silenceRms)
            continue;

        ++nonSilent;
        cmnd[0] = 1.0;
        double running = 0.0;

        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            double sum = 0.0;

            for (int j = 0; j < window; ++j)
            {
                const double diff = (double) f[j] - f[j + tau];
                sum += diff * diff;
            }

            d[(size_t) tau] = sum;
            running += sum;
            cmnd[(size_t) tau] = running > 0.0 ? sum * tau / running : 1.0;
        }

        int tau = -1;

        for (int t = minLag; t <= maxLag; ++t)
        {
            if (cmnd[(size_t) t] < threshold)
            {
                while (t + 1 <= maxLag && cmnd[(size_t) t + 1] < cmnd[(size_t) t])
                    ++t;

                tau = t;
                break;
            }
        }

        if (tau < 0)
            continue;

        // Parabolic interpolation around the minimum for sub-sample lag precision.
        const auto a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
        const auto denom = a - 2.0 * b + c;
        const auto refined = tau + (std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0);

        voiced.push_back ({ frequencyToMidi (sampleRate / refined), b });
    }

    if (nonSilent == 0 || voiced.empty())
        return result;

    std::vector<double> midis;

    for (const auto& v : voiced)
        midis.push_back (v.midi);

    std::nth_element (midis.begin(), midis.begin() + (long) midis.size() / 2, midis.end());
    const auto median = midis[midis.size() / 2];

    double sum = 0.0, aperiodicity = 0.0;
    int stable = 0;

    for (const auto& v : voiced)
    {
        if (std::abs (v.midi - median) <= 0.5)
        {
            sum += v.midi;
            aperiodicity += v.aperiodicity;
            ++stable;
        }
    }

    result.midi = sum / stable;
    result.frequency = midiToFrequency (result.midi);
    result.voicedFraction = (float) voiced.size() / (float) nonSilent;
    const auto stability = (double) stable / (double) voiced.size();
    const auto clarity = 1.0 - aperiodicity / stable;
    result.confidence = (float) (result.voicedFraction * stability * clarity);
    // Polyphony check. YIN finds the common period of a chord (A-C#-E is periodic at A1) and averages close clusters.
    // A single tone has energy at f0 or 2*f0 (low harmonics present) and its partials peak exactly at h*f0.
    {
        const auto length = (int) std::min<int64_t> (n, (int64_t) sampleRate);
        const auto offset = [&] (double hz) { return std::max (hz * 0.0293, 3.0 * sampleRate / length); }; // >= 50 cents
        double low = 0.0, all = 0.0, onPeak = 0.0, nearPeak = 0.0;

        for (int h = 1; h <= 8 && h * result.frequency < 0.45 * sampleRate; ++h)
        {
            const auto f = h * result.frequency;
            const auto e = goertzelPower (x.data(), length, f, sampleRate);
            all += e;
            low += h <= 2 ? e : 0.0;

            if (h <= 4)
            {
                onPeak += e;
                nearPeak += std::max (goertzelPower (x.data(), length, f - offset (f), sampleRate),
                                      goertzelPower (x.data(), length, f + offset (f), sampleRate));
            }
        }

        result.ambiguous = all <= 0.0 || low / all < 0.1 || onPeak < 2.0 * nearPeak;

        if (result.ambiguous)
            result.confidence = std::min (result.confidence, 0.4f);
    }

    result.status = result.confidence >= 0.6f ? PitchEstimate::Status::pitched
                    : result.voicedFraction >= 0.2f ? PitchEstimate::Status::uncertain
                                                    : PitchEstimate::Status::unpitched;
    return result;
}

} // namespace neuro
