#pragma once

#include "Timbre.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Drum transcription and separation for mixed drum loops with semi-adaptive NMF (after Wu & Lerch, ISMIR 2015:
// partially fixed NMF). A magnitude spectrogram (log-spaced bands) is factorised as V ~= W H with drum templates
// (kick, snare, hats/cymbals, as in the paper; initialised from spectral shapes, then adapted to the loop) plus free components
// that absorb everything else. Activations give per-drum onsets (simultaneous hits are detected as both); Wiener-style
// soft masks (summing to 1) split the audio into stems that add back up to the original.
namespace neuro::nmf
{

// No tom component: in tests on real loops it captured the attack of kicks and snares (shell body). Toms land in the
// nearest drum or the residual.
enum Component { kick, snare, cymbals, numDrumComponents };
inline constexpr int numFree = 2;
inline constexpr int numComponents = numDrumComponents + numFree;
inline constexpr int numStems = numDrumComponents + 1; // + residual

struct Event { int64_t sample; float strength; DrumClass drumClass; };

// "Teach the kit": an example hit of one drum component (sample range in the loop, attack first). Its spectrum replaces
// the generic template; other drums sounding at the same time are explained away by their own templates.
struct Example { int component; int64_t start, end; };

struct Result
{
    std::vector<Event> events;                         // all classes, time order
    std::array<std::vector<std::vector<float>>, numStems> stems; // [stem][channel][sample]; stem 4 = residual
    std::array<std::vector<float>, numDrumComponents> activations; // per frame (hop 512), for display/diagnostics
};

namespace detail
{
    constexpr int fftSize = 2048, hop = 512, bins = fftSize / 2 + 1, bands = 96;

    inline double bandCentre (int b) { return 30.0 * std::pow (16000.0 / 30.0, (double) b / (bands - 1)); }

    // Initial templates (band magnitudes) from typical drum spectra.
    inline std::array<std::array<float, bands>, numDrumComponents> templates()
    {
        std::array<std::array<float, bands>, numDrumComponents> w {};
        auto gauss = [] (double hz, double centre, double octaves) { const auto d = std::log2 (hz / centre) / octaves; return std::exp (-0.5 * d * d); };

        for (int b = 0; b < bands; ++b)
        {
            const auto hz = bandCentre (b);
            w[kick][(size_t) b]    = (float) (gauss (hz, 80.0, 0.75) + 0.04 * gauss (hz, 3000.0, 1.0)); // whole attack sweep, ~45-150 Hz
            // Snare wires/crack mostly below ~6 kHz; above that belongs to hats/cymbals.
            w[snare][(size_t) b]   = (float) (0.6 * gauss (hz, 200.0, 0.4)
                                              + 0.8 * (hz < 900.0 ? gauss (hz, 900.0, 0.7) : hz < 4500.0 ? 1.0 : gauss (hz, 4500.0, 0.3)));
            w[cymbals][(size_t) b] = (float) (hz > 5000.0 ? 1.0 : gauss (hz, 5000.0, 0.5));
        }

        for (auto& t : w)
        {
            float sum = 0.0f;
            for (auto v : t) sum += v;
            for (auto& v : t) v /= sum;
        }

        return w;
    }

    // Maps a bin (Hz) to fractional band position for band <-> bin interpolation.
    inline double bandPosition (double hz)
    {
        if (hz <= 30.0) return 0.0;
        return std::min ((double) bands - 1.0, std::log (hz / 30.0) / std::log (16000.0 / 30.0) * (bands - 1));
    }
}

inline Result separate (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate, int iterations = 60,
                        const std::vector<Example>& examples = {})
{
    using namespace detail;
    Result result;
    const auto frames = (int) ((numSamples + hop - 1) / hop) + 1;

    if (numChannels <= 0 || numSamples < fftSize || sampleRate <= 0.0)
        return result;

    // ---- STFT per channel ----
    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) fftSize); // periodic Hann

    std::vector<std::vector<std::complex<float>>> spec ((size_t) numChannels, std::vector<std::complex<float>> ((size_t) frames * bins));
    std::vector<std::complex<float>> buf ((size_t) fftSize);

    for (int c = 0; c < numChannels; ++c)
        for (int t = 0; t < frames; ++t)
        {
            for (int i = 0; i < fftSize; ++i)
            {
                const auto idx = (int64_t) t * hop + i - fftSize / 2; // centred frames
                buf[(size_t) i] = idx >= 0 && idx < numSamples ? channels[c][idx] * window[(size_t) i] : 0.0f;
            }

            neuro::detail::fft (buf);
            std::copy (buf.begin(), buf.begin() + bins, spec[(size_t) c].begin() + (ptrdiff_t) t * bins);
        }

    // ---- Band magnitude spectrogram V (bands x frames), mono sum ----
    std::vector<int> bandOfBin ((size_t) bins);
    std::vector<float> fracOfBin ((size_t) bins);

    for (int k = 0; k < bins; ++k)
    {
        const auto pos = bandPosition ((double) k * sampleRate / fftSize);
        bandOfBin[(size_t) k] = std::min (bands - 2, (int) pos);
        fracOfBin[(size_t) k] = (float) (pos - bandOfBin[(size_t) k]);
    }

    std::vector<float> V ((size_t) (bands * frames), 1.0e-9f);

    for (int t = 0; t < frames; ++t)
        for (int k = 1; k < bins; ++k)
        {
            float mag = 0.0f;
            for (int c = 0; c < numChannels; ++c) mag += std::abs (spec[(size_t) c][(size_t) (t * bins + k)]);
            const auto b = bandOfBin[(size_t) k];
            const auto f = fracOfBin[(size_t) k];
            V[(size_t) (b * frames + t)] += mag * (1.0f - f);
            V[(size_t) ((b + 1) * frames + t)] += mag * f;
        }

    // ---- Semi-adaptive NMF (KL divergence, multiplicative updates) ----
    auto init = templates();
    std::array<bool, numDrumComponents> taught {};

    // Taught templates: KL fit of one free template on the example frames while fixed templates explain the rest of
    // the mix there: the other drums (generic, and taught when already learned) plus a flat broadband "room" template.
    // So a kick example with a crash on top, or a snare example with the kick under it, still learns just that drum.
    // Order hats -> kick -> snare: a snare example often contains a kick, a kick example often a hat.
    const auto generic = init;

    for (const auto j : { (int) cymbals, (int) kick, (int) snare })
    {
        std::vector<int> cols;

        for (const auto& e : examples)
            if (e.component == j)
                for (auto t = (int) (e.start / hop); t <= (int) (std::min (e.end, e.start + (int64_t) (0.1 * sampleRate)) / hop) + 1 && t < frames; ++t)
                    if (t >= 0) cols.push_back (t);

        if (cols.empty())
            continue;

        std::vector<std::array<float, bands>> fixed;
        for (int c = 0; c < numDrumComponents; ++c)
            if (c != j)
            {
                fixed.push_back (generic[(size_t) c]);
                if (taught[(size_t) c]) fixed.push_back (init[(size_t) c]);
            }
        fixed.emplace_back().fill (1.0f / bands);

        const auto n = cols.size();
        const auto numCols = fixed.size() + 1; // last = the template being learned
        std::array<float, bands> w {};
        std::vector<float> h (numCols * n, 1.0f), ratio ((size_t) bands * n);

        for (int b = 0; b < bands; ++b)
        {
            for (auto t : cols) w[(size_t) b] += V[(size_t) (b * frames + t)];
            w[(size_t) b] += 1.0e-6f;
        }

        auto column = [&] (size_t c, int b) { return c < fixed.size() ? fixed[c][(size_t) b] : w[(size_t) b]; };

        // Soft range of the drum (1 inside, log-frequency fall-off outside): the example shapes that drum's sound, while
        // whatever else rings there (a crash on the downbeat, room) cannot become part of it.
        std::array<float, bands> range {};
        for (int b = 0; b < bands; ++b)
        {
            const auto hz = bandCentre (b);
            auto fall = [hz] (double edge, double octaves) { const auto d = std::log2 (hz / edge) / octaves; return std::exp (-0.5 * d * d); };
            range[(size_t) b] = (float) (j == kick ? (hz < 250.0 ? 1.0 : std::max (0.03, fall (250.0, 1.0)))
                                       : j == snare ? (hz < 120.0 ? fall (120.0, 0.7) : hz < 8000.0 ? 1.0 : fall (8000.0, 0.7))
                                                    : (hz > 2000.0 ? 1.0 : fall (2000.0, 1.0)));
        }

        for (int it = 0; it < 60; ++it)
        {
            float wSum = 0.0f;
            for (auto v : w) wSum += v;
            for (auto& v : w) v /= wSum;

            for (size_t i = 0; i < n; ++i)
                for (int b = 0; b < bands; ++b)
                {
                    float model = 1.0e-9f;
                    for (size_t c = 0; c < numCols; ++c) model += column (c, b) * h[c * n + i];
                    ratio[(size_t) b * n + i] = V[(size_t) (b * frames + cols[i])] / model;
                }

            for (size_t c = 0; c < numCols; ++c)
                for (size_t i = 0; i < n; ++i)
                {
                    float num = 0.0f, den = 1.0e-9f;
                    for (int b = 0; b < bands; ++b) { num += column (c, b) * ratio[(size_t) b * n + i]; den += column (c, b); }
                    h[c * n + i] *= num / den;
                }

            const auto self = numCols - 1;
            float hSum = 1.0e-9f;
            for (size_t i = 0; i < n; ++i) hSum += h[self * n + i];

            for (int b = 0; b < bands; ++b)
            {
                float num = 0.0f;
                for (size_t i = 0; i < n; ++i) num += ratio[(size_t) b * n + i] * h[self * n + i];
                w[(size_t) b] *= num / hSum * range[(size_t) b];
            }
        }

        float wSum = 0.0f;
        for (auto v : w) wSum += v;
        for (int b = 0; b < bands; ++b) init[(size_t) j][(size_t) b] = w[(size_t) b] / wSum;
        taught[(size_t) j] = true;
    }

    std::vector<float> W ((size_t) (bands * numComponents)), H ((size_t) (numComponents * frames), 1.0f);
    uint32_t rng = 12345u;
    auto random = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return 0.5f + (float) (rng % 1000) / 1000.0f; };

    for (int b = 0; b < bands; ++b)
    {
        for (int j = 0; j < numDrumComponents; ++j) W[(size_t) (b * numComponents + j)] = init[(size_t) j][(size_t) b];
        for (int j = numDrumComponents; j < numComponents; ++j) W[(size_t) (b * numComponents + j)] = random() / bands;
    }

    float vMean = 0.0f;
    for (auto v : V) vMean += v;
    vMean /= (float) V.size();
    for (auto& h : H) h = vMean * random();

    std::vector<float> WH ((size_t) (bands * frames)), ratio ((size_t) (bands * frames));

    auto product = [&]
    {
        for (int b = 0; b < bands; ++b)
            for (int t = 0; t < frames; ++t)
            {
                float s = 1.0e-9f;
                for (int j = 0; j < numComponents; ++j) s += W[(size_t) (b * numComponents + j)] * H[(size_t) (j * frames + t)];
                WH[(size_t) (b * frames + t)] = s;
            }
    };

    for (int it = 0; it < iterations; ++it)
    {
        product();
        for (size_t i = 0; i < V.size(); ++i) ratio[i] = V[i] / WH[i];

        for (int j = 0; j < numComponents; ++j) // H update
        {
            float colSum = 1.0e-9f;
            for (int b = 0; b < bands; ++b) colSum += W[(size_t) (b * numComponents + j)];

            for (int t = 0; t < frames; ++t)
            {
                float num = 0.0f;
                for (int b = 0; b < bands; ++b) num += W[(size_t) (b * numComponents + j)] * ratio[(size_t) (b * frames + t)];
                H[(size_t) (j * frames + t)] *= num / colSum;
            }
        }

        product();
        for (size_t i = 0; i < V.size(); ++i) ratio[i] = V[i] / WH[i];
        const auto keep = std::max (0.3f, 1.0f - (float) it / (float) iterations); // generic templates adapt gradually

        for (int j = 0; j < numComponents; ++j) // W update
        {
            float rowSum = 1.0e-9f;
            for (int t = 0; t < frames; ++t) rowSum += H[(size_t) (j * frames + t)];

            float norm = 0.0f;

            for (int b = 0; b < bands; ++b)
            {
                float num = 0.0f;
                for (int t = 0; t < frames; ++t) num += ratio[(size_t) (b * frames + t)] * H[(size_t) (j * frames + t)];
                auto& w = W[(size_t) (b * numComponents + j)];
                w *= num / rowSum;

                if (j < numDrumComponents) // taught templates are the real drum: they stay close to the example
                {
                    const auto anchor = taught[(size_t) j] ? std::max (0.5f, keep) : keep;
                    w = anchor * init[(size_t) j][(size_t) b] + (1.0f - anchor) * w;
                }

                norm += w;
            }

            for (int b = 0; b < bands; ++b) // unit-sum columns, scale moved into H
            {
                W[(size_t) (b * numComponents + j)] /= norm;
            }

            for (int t = 0; t < frames; ++t) H[(size_t) (j * frames + t)] *= norm;
        }
    }

    product();

    // ---- Onsets per drum component: peaks of the log-activation rise (a jump stands out even on a decaying tail) ----
    const auto framesPerSecond = sampleRate / hop;
    const auto minGap = (int) std::ceil (0.05 * framesPerSecond);

    // Fine time grid (64-sample log energy) to place each detected hit on its actual attack.
    constexpr int fineHop = 64;
    std::vector<float> fine ((size_t) (numSamples / fineHop + 1), -120.0f);

    for (size_t f = 0; f < fine.size(); ++f)
    {
        double e = 0.0;
        for (int i = 0; i < fineHop; ++i)
            if (const auto idx = (int64_t) f * fineHop + i; idx < numSamples)
                for (int c = 0; c < numChannels; ++c) e += (double) channels[c][idx] * channels[c][idx];
        fine[f] = (float) (10.0 * std::log10 (e / fineHop + 1.0e-12));
    }

    auto refine = [&] (int64_t coarse)
    {
        const auto from = std::max<int64_t> (0, (coarse - (int64_t) (0.032 * sampleRate)) / fineHop);
        const auto to = std::min<int64_t> ((int64_t) fine.size() - 1, (coarse + (int64_t) (0.032 * sampleRate)) / fineHop);
        auto best = coarse;
        auto bestJump = 3.0f; // dB: keep the coarse time unless there is a clear attack

        for (auto f = from; f <= to; ++f)
            if (const auto jump = fine[(size_t) f] - (f > 0 ? fine[(size_t) f - 1] : -120.0f); jump > bestJump) // silence before the start
            {
                bestJump = jump;
                best = f * fineHop;
            }

        return best;
    };

    std::vector<int> eventFrames, eventComponents;

    for (int j = 0; j < numDrumComponents; ++j)
    {
        const auto* h = H.data() + (size_t) (j * frames);
        const auto top = *std::max_element (h, h + frames);
        const auto floor = 1.0e-3f * top;
        std::vector<float> rise ((size_t) frames, 0.0f);

        for (int t = 0; t < frames; ++t)
        {
            // Against the lowest of the two previous frames: on a decaying tail (often another drum's, absorbed by this
            // component) a real hit still stands out as a clear jump.
            const auto before = t >= 2 ? std::min (h[t - 1], h[t - 2]) : t == 1 ? h[0] : 0.0f;
            rise[(size_t) t] = std::max (0.0f, std::log2 ((h[t] + floor) / (before + floor))); // octaves of increase
        }

        int last = -minGap;

        for (int t = 0; t < frames; ++t)
        {
            // Local peak of the rise; >= 0.5 octave (x1.41) jump; the hit itself loud enough (>= 6 % of the loudest).
            const auto r = rise[(size_t) t];
            const auto peakAt = std::min (frames - 1, t + 2);
            const auto level = *std::max_element (h + t, h + peakAt + 1);

            if (r < (j == cymbals ? 0.35f : 0.5f) || level < 0.06f * top || (t > 0 && r < rise[(size_t) t - 1]) || (t + 1 < frames && r <= rise[(size_t) t + 1])
                || t - last < minGap)
                continue;

            last = t;
            auto cls = j == kick ? DrumClass::kick : j == snare ? DrumClass::snare : DrumClass::closedHat;

            if (j == cymbals) // ring length decides hat (closed/open) vs crash/ride
            {
                int ring = 0;
                while (t + ring + 1 < frames && h[t + ring + 1] > 0.5f * level) ++ring; // half level: robust to other drums' tails
                const auto ms = ring * 1000.0 / framesPerSecond;
                cls = ms < 150.0 ? DrumClass::closedHat : ms < 450.0 ? DrumClass::openHat : ms < 1000.0 ? DrumClass::ride : DrumClass::crash;
            }

            // Frames are centred on their timestamp: the hit begins about half a hop before.
            result.events.push_back ({ refine (std::max<int64_t> (0, (int64_t) t * hop - hop / 2)), std::min (1.0f, level / top), cls });
            eventFrames.push_back (t);
            eventComponents.push_back (j);
        }
    }

    // Crosstalk: a kick also lifts the snare component a little (and vice versa). When kick and snare fire together, each
    // is kept only if it is strong in its own right (>= 40 % of that component's loudest hit). Hats are left alone.
    {
        std::vector<Event> kept;

        for (size_t i = 0; i < result.events.size(); ++i)
        {
            const auto j = eventComponents[i];
            auto coincides = false;

            for (size_t o = 0; o < result.events.size() && j != cymbals; ++o)
                coincides |= o != i && eventComponents[o] != j && eventComponents[o] != cymbals && std::abs (eventFrames[o] - eventFrames[i]) <= 2;

            if (! coincides || result.events[i].strength >= 0.4f)
                kept.push_back (result.events[i]);
        }

        result.events = std::move (kept);
    }

    for (int j = 0; j < numDrumComponents; ++j)
        result.activations[(size_t) j].assign (H.begin() + (ptrdiff_t) j * frames, H.begin() + (ptrdiff_t) (j + 1) * frames);

    std::sort (result.events.begin(), result.events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });

    // ---- Soft masks -> stems (inverse STFT, weighted overlap-add) ----
    for (auto& stem : result.stems)
        stem.assign ((size_t) numChannels, std::vector<float> ((size_t) numSamples, 0.0f));

    std::vector<float> norm ((size_t) numSamples, 0.0f);

    for (int t = 0; t < frames; ++t)
        for (int i = 0; i < fftSize; ++i)
            if (const auto idx = (int64_t) t * hop + i - fftSize / 2; idx >= 0 && idx < numSamples)
                norm[(size_t) idx] += window[(size_t) i] * window[(size_t) i];

    for (int s = 0; s < numStems; ++s)
    {
        for (int c = 0; c < numChannels; ++c)
            for (int t = 0; t < frames; ++t)
            {
                for (int k = 0; k < bins; ++k)
                {
                    const auto b = bandOfBin[(size_t) k];
                    const auto f = fracOfBin[(size_t) k];
                    auto part = [&] (int band)
                    {
                        float mine = 0.0f;
                        if (s < numDrumComponents) mine = W[(size_t) (band * numComponents + s)] * H[(size_t) (s * frames + t)];
                        else for (int j = numDrumComponents; j < numComponents; ++j) mine += W[(size_t) (band * numComponents + j)] * H[(size_t) (j * frames + t)];
                        return mine / WH[(size_t) (band * frames + t)];
                    };
                    const auto mask = part (b) * (1.0f - f) + part (b + 1) * f;
                    const auto value = spec[(size_t) c][(size_t) (t * bins + k)] * mask;
                    buf[(size_t) k] = value;
                    if (k > 0 && k < bins - 1) buf[(size_t) (fftSize - k)] = std::conj (value);
                }

                for (auto& x : buf) x = std::conj (x); // inverse FFT via conj(FFT(conj(X))) / N
                neuro::detail::fft (buf);

                for (int i = 0; i < fftSize; ++i)
                    if (const auto idx = (int64_t) t * hop + i - fftSize / 2; idx >= 0 && idx < numSamples)
                        result.stems[(size_t) s][(size_t) c][(size_t) idx] += buf[(size_t) i].real() / (float) fftSize * window[(size_t) i];
            }

        for (int c = 0; c < numChannels; ++c)
            for (int64_t i = 0; i < numSamples; ++i)
                result.stems[(size_t) s][(size_t) c][(size_t) i] /= std::max (norm[(size_t) i], 1.0e-6f);
    }

    return result;
}

} // namespace neuro::nmf
