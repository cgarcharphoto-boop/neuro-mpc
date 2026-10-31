#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace neuro
{

// Drum / sound classes. Order = Loop Deconstructor lane order and the plugin's per-class output bus order.
enum class DrumClass { kick, snare, closedHat, openHat, tom1, tom2, floorTom, crash, ride, perc, tonal, count };

inline constexpr int numDrumClasses = (int) DrumClass::count;

inline const char* drumClassName (DrumClass c) noexcept
{
    static constexpr const char* names[] { "Kick", "Snare", "Closed Hat", "Open Hat", "Tom 1", "Tom 2", "Floor Tom",
                                           "Crash", "Ride", "Perc", "Tonal" };
    return (int) c >= 0 && c < DrumClass::count ? names[(int) c] : "?";
}

// Sound fingerprint of one slice. Spectral features describe the first ~93 ms (the attack and body);
// the envelope features describe up to 2 s.
struct TimbreFeatures
{
    float centroidHz = 0.0f;  // brightness
    float flatness = 0.0f;    // 0 = tonal (peaky spectrum) .. ~0.6 = white noise
    float rolloffHz = 0.0f;   // 85 % of the spectral energy lies below
    float low = 0.0f, mid = 0.0f, high = 0.0f; // energy fractions: < 150 Hz, 150 Hz - 2 kHz, > 5 kHz
    float lowPeakHz = 0.0f;   // strongest spectral peak 35 - 400 Hz (drum fundamental)
    float attackMs = 0.0f;    // start -> envelope peak
    float decayMs = 0.0f;     // envelope peak -> -20 dB (capped at the analysed length)
    float peakDb = -120.0f;
    float lengthMs = 0.0f;    // analysed length
    bool silent = true;

    bool sustained() const noexcept { return decayMs >= lengthMs - 10.0f || decayMs > 1500.0f; } // never fell 20 dB

    // 2D timbre map: x = dark (0) .. bright (1), y = tonal (0) .. noisy (1).
    float mapX() const noexcept { return std::clamp ((std::log2 (std::max (centroidHz, 50.0f)) - std::log2 (100.0f)) / (std::log2 (12000.0f) - std::log2 (100.0f)), 0.0f, 1.0f); }
    // Log scale: real recordings sit around flatness 1e-3 .. 1e-1, white noise ~0.5.
    float mapY() const noexcept { return std::clamp ((std::log10 (std::max (flatness, 1.0e-5f)) + 4.0f) / 3.7f, 0.0f, 1.0f); }
};

namespace detail
{
    inline void fft (std::vector<std::complex<float>>& a)
    {
        const auto n = a.size();

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const auto w = std::polar (1.0f, -6.2831853f / (float) len);

            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> wn (1.0f, 0.0f);

                for (size_t k = 0; k < len / 2; ++k, wn *= w)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                }
            }
        }
    }
}

inline TimbreFeatures analyseTimbre (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate)
{
    TimbreFeatures f;

    if (channels == nullptr || numChannels <= 0 || numSamples <= 0 || sampleRate <= 0.0)
        return f;

    const auto envLength = (size_t) std::min<double> ((double) numSamples, 2.0 * sampleRate);
    std::vector<float> mono (envLength);

    for (size_t i = 0; i < envLength; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < numChannels; ++c) s += channels[c][i];
        mono[i] = s / (float) numChannels;
    }

    // Envelope: 5 ms RMS frames.
    const auto hop = std::max<size_t> (1, (size_t) (0.005 * sampleRate));
    std::vector<float> env;

    for (size_t start = 0; start < envLength; start += hop)
    {
        double e = 0.0;
        const auto end = std::min (envLength, start + hop);
        for (auto i = start; i < end; ++i) e += (double) mono[i] * mono[i];
        env.push_back ((float) std::sqrt (e / (double) (end - start)));
    }

    const auto peakIt = std::max_element (env.begin(), env.end());
    const auto peak = *peakIt;
    f.peakDb = 20.0f * std::log10 (peak + 1.0e-9f);

    if (f.peakDb < -60.0f)
        return f;

    f.silent = false;
    f.lengthMs = (float) ((double) envLength / sampleRate * 1000.0);
    const auto peakFrame = (size_t) (peakIt - env.begin());
    f.attackMs = (float) ((double) peakFrame * (double) hop / sampleRate * 1000.0);
    auto decayFrame = env.size();

    for (auto i = peakFrame; i < env.size(); ++i)
        if (env[i] < peak * 0.1f) { decayFrame = i; break; }

    f.decayMs = (float) ((double) (decayFrame - peakFrame) * (double) hop / sampleRate * 1000.0);

    // Spectrum of the first 4096 samples (zero-padded), Hann-windowed.
    constexpr size_t n = 4096;
    std::vector<std::complex<float>> spec (n);

    for (size_t i = 0; i < n && i < envLength; ++i)
        spec[i] = mono[i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (n - 1)));

    detail::fft (spec);
    const auto binHz = (float) sampleRate / (float) n;
    double total = 0.0, weighted = 0.0, logSum = 0.0, low = 0.0, mid = 0.0, high = 0.0;
    std::vector<double> power (n / 2);
    float lowPeak = 0.0f;

    for (size_t k = 1; k < n / 2; ++k)
    {
        const auto p = (double) std::norm (spec[k]) + 1.0e-12;
        const auto hz = (float) k * binHz;
        power[k] = p;
        total += p;
        weighted += p * hz;
        logSum += std::log (p);
        if (hz < 150.0f)        low += p;
        else if (hz < 2000.0f)  mid += p;
        else if (hz > 5000.0f)  high += p;

        if (hz >= 35.0f && hz <= 400.0f && (float) p > lowPeak)
        {
            lowPeak = (float) p;
            f.lowPeakHz = hz;
        }
    }

    const auto bins = (double) (n / 2 - 1);
    f.centroidHz = (float) (weighted / total);
    f.flatness = (float) (std::exp (logSum / bins) / (total / bins));
    f.low = (float) (low / total);
    f.mid = (float) (mid / total);
    f.high = (float) (high / total);

    double cumulative = 0.0;

    for (size_t k = 1; k < n / 2; ++k)
        if ((cumulative += power[k]) >= 0.85 * total) { f.rolloffHz = (float) k * binHz; break; }

    return f;
}

// Band-energy rise (dB) at an onset inside a mixed loop: post-onset window vs the window just before it.
// In a full kit the absolute spectrum is mostly bleed (kick room, hats over everything); what changes at the hit
// tells which drum was struck.
struct HitRise
{
    float low = 0.0f;     // 35-120 Hz: kick, floor tom
    float body = 0.0f;    // 150-600 Hz: snare/tom body
    float snap = 0.0f;    // 1-5 kHz: snare wires, stick
    float high = 0.0f;    // 6-16 kHz: hats, cymbals
    float highDecayMs = 0.0f;
};

inline HitRise analyseHitRise (const float* const* channels, int numChannels, int64_t numSamples, int64_t onset, double sampleRate)
{
    constexpr size_t n = 2048; // ~43 ms windows
    HitRise r;

    auto bandEnergies = [&] (int64_t start, std::array<double, 4>& bands)
    {
        std::vector<std::complex<float>> spec (n);

        for (size_t i = 0; i < n; ++i)
        {
            const auto idx = start + (int64_t) i;
            float s = 0.0f;
            if (idx >= 0 && idx < numSamples)
                for (int c = 0; c < numChannels; ++c) s += channels[c][idx];
            spec[i] = s * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (n - 1)));
        }

        detail::fft (spec);
        bands = {};
        const auto binHz = sampleRate / (double) n;

        for (size_t k = 1; k < n / 2; ++k)
        {
            const auto hz = (double) k * binHz;
            const auto p = (double) std::norm (spec[k]);
            if (hz >= 35.0 && hz < 120.0)         bands[0] += p;
            else if (hz >= 150.0 && hz < 600.0)   bands[1] += p;
            else if (hz >= 1000.0 && hz < 5000.0) bands[2] += p;
            else if (hz >= 6000.0 && hz < 16000.0) bands[3] += p;
        }
    };

    std::array<double, 4> before {}, after {};
    bandEnergies (onset - (int64_t) n, before);
    bandEnergies (onset, after);
    auto rise = [] (double a, double b) { return (float) (10.0 * std::log10 ((a + 1.0e-9) / (b + 1.0e-9))); };
    r.low = rise (after[0], before[0]);
    r.body = rise (after[1], before[1]);
    r.snap = rise (after[2], before[2]);
    r.high = rise (after[3], before[3]);

    // How long the top band stays within 20 dB of its post-onset level (open hat / crash ring on).
    std::array<double, 4> later {};
    const auto peakHigh = after[3];

    for (int64_t t = onset + (int64_t) n; t < std::min<int64_t> (numSamples, onset + (int64_t) (2.0 * sampleRate)); t += (int64_t) n)
    {
        bandEnergies (t, later);
        if (later[3] < peakHigh * 0.01) break;
        r.highDecayMs = (float) ((double) (t - onset) / sampleRate * 1000.0);
    }

    return r;
}

// Classifies a hit inside a mixed drum loop from its band rises (the strongest new energy wins).
inline DrumClass classifyHit (const HitRise& r, const TimbreFeatures& f)
{
    if (std::max ({ r.low, r.body, r.snap, r.high }) < 3.0f)     // nothing clearly new: a ghost note / soft hit
        return DrumClass::perc;

    // Cymbals: the top band jumps most and clearly more than the snare crack.
    if (r.high >= r.snap + 3.0f && r.high >= r.low)
    {
        if (r.highDecayMs < 150.0f) return DrumClass::closedHat;
        if (r.highDecayMs < 700.0f) return DrumClass::openHat;
        return f.flatness > 0.05f ? DrumClass::crash : DrumClass::ride;
    }

    // Kick: the sub-low jump clearly dominates the crack (a beater click is small next to the boom).
    if (r.low >= r.snap + 6.0f && r.low >= r.body)
        return f.lowPeakHz > 105.0f && f.decayMs > 300.0f ? DrumClass::floorTom : DrumClass::kick;

    // Snare: a clear crack (wires / stick), even though the shell and room also lift the lows.
    if (r.snap >= 4.0f)
        return DrumClass::snare;

    if (r.body >= 3.0f)                                          // body without crack: a tom
        return f.lowPeakHz < 160.0f ? DrumClass::tom2 : DrumClass::tom1;

    return r.low >= r.high ? DrumClass::kick : DrumClass::closedHat;
}

// Rule-based drum classifier on the fingerprint. Heuristic (no trained model): verified on synthetic drums;
// real kits vary, so the UI offers a per-slice override. `pitched` = the YIN detector found a stable pitch.
inline DrumClass classify (const TimbreFeatures& f, bool pitched)
{
    if (f.silent)
        return DrumClass::perc;

    if (f.high > 0.45f && f.centroidHz > 4500.0f)                        // cymbal family
    {
        if (f.decayMs < 160.0f) return DrumClass::closedHat;
        if (f.decayMs < 700.0f) return DrumClass::openHat;
        return f.flatness > 0.25f ? DrumClass::crash : DrumClass::ride;
    }

    if (pitched && f.sustained() && f.high < 0.45f)                       // a held note, not a decaying drum
        return DrumClass::tonal;

    if (f.low > 0.5f && f.lowPeakHz > 0.0f)                               // low drums
    {
        if (f.lowPeakHz < 75.0f || (f.lowPeakHz < 105.0f && f.decayMs < 260.0f))
            return DrumClass::kick;

        return f.lowPeakHz < 110.0f ? DrumClass::floorTom : f.lowPeakHz < 160.0f ? DrumClass::tom2 : DrumClass::tom1;
    }

    if (f.flatness > 0.12f && f.centroidHz > 900.0f && f.decayMs < 600.0f) // noisy mid burst
        return DrumClass::snare;

    // High tom: energy concentrated near a 150-400 Hz fundamental (few overtones), unlike a tonal note whose
    // harmonics lift the centroid well above its fundamental.
    if (f.mid > 0.5f && f.lowPeakHz >= 150.0f && f.centroidHz < 400.0f && f.flatness < 0.12f)
        return DrumClass::tom1;

    if (pitched && f.decayMs > 250.0f)
        return DrumClass::tonal;

    return DrumClass::perc;
}

} // namespace neuro
