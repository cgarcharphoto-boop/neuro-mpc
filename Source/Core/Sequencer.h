#pragma once

#include "NoteTracker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace neuro
{

struct SeqStep
{
    bool on = false;
    float velocity = 0.8f;     // 0 .. 1
    float gate = 0.5f;         // fraction of the step (0.05 .. 1); ratchets divide it
    float probability = 1.0f;  // seeded per step and grid position: reproducible, varies per cycle
    int ratchet = 1;           // 1 .. 4 evenly spaced hits within the step
    float micro = 0.0f;        // microtiming: -0.5 .. +0.5 of a step, never quantised away
    float pitch = 0.0f;        // semitones added to the triggered pad / key (resampled)
};

struct SeqLane
{
    static constexpr int maxSteps = 64;

    int note = 36;             // MIDI note the lane plays (pads mode: the pad on that note)
    int length = 16;           // steps; lanes may differ (polymeter)
    bool mute = false;
    std::string name;
    std::array<SeqStep, maxSteps> steps {};
};

struct SeqPattern
{
    static constexpr int maxLanes = 16;

    std::string name;
    int numLanes = 8;
    std::array<SeqLane, maxLanes> lanes {};

    SeqPattern()
    {
        for (int i = 0; i < maxLanes; ++i)
            lanes[(size_t) i].note = 36 + i; // lane n -> pad n
    }

    bool isEmpty() const noexcept
    {
        for (const auto& lane : lanes)
            for (const auto& step : lane.steps)
                if (step.on)
                    return false;

        return true;
    }
};

struct SeqTiming
{
    double stepBeats = 0.25;   // 1/16
    float swing = 0.5f;        // MPC-style, 0.5 .. 0.75
    uint32_t seed = 1;
};

// Step sequencer. Timing is stateless: every hit's time (grid + swing + microtiming + ratchet) and its probability
// outcome are pure functions of the beat position, lane, step and seed, so playback is deterministic, block-size
// independent and robust to host loops and seeks. Only sounding notes are state (sample-scheduled note-offs).
// sink (sampleOffset, note, velocity, pitchSemitones); velocity 0 = note-off. Events within one call may be unordered.
class Sequencer
{
public:
    static constexpr int maxSounding = 128, maxHitsPerCall = 512;

    template <typename Sink>
    void process (const SeqPattern& pattern, const SeqTiming& timing, int from, int to, double beatAtFrom,
                  double beatsPerSample, Sink&& sink) noexcept
    {
        if (to <= from || beatsPerSample <= 0.0)
            return;

        const auto s = std::max (timing.stepBeats, 1.0 / 64.0);
        const auto swing = std::clamp (timing.swing, 0.5f, 0.75f);
        const auto beatAtTo = beatAtFrom + (to - from) * beatsPerSample;
        const auto first = (int64_t) std::floor (beatAtFrom / s) - 2;
        const auto last = (int64_t) std::floor (beatAtTo / s) + 2;
        auto off = [&] (int o, int n) { sink (o, n, 0.0f, 0.0f); };

        // Gather this call's hits, then emit them in time order: microtiming can move a step before the previous
        // step's late ratchet, and retriggers/note-offs must be applied in real time order.
        struct Hit { int offset; int note; float velocity, pitch; int64_t gateSamples; };
        std::array<Hit, maxHitsPerCall> hits;
        int numHits = 0;

        for (auto k = first; k <= last; ++k)
        {
            const auto pairStart = (double) (k - (k & 1)) * s;
            const auto grid = (k & 1) ? pairStart + 2.0 * s * swing : pairStart;

            for (int l = 0; l < std::min (pattern.numLanes, SeqPattern::maxLanes); ++l)
            {
                const auto& lane = pattern.lanes[(size_t) l];
                const auto length = std::clamp (lane.length, 1, SeqLane::maxSteps);
                const auto index = (int) (((k % length) + length) % length);
                const auto& step = lane.steps[(size_t) index];

                if (! step.on || lane.mute || lane.note < 0 || lane.note > 127)
                    continue;

                if (step.probability < 1.0f && unit (timing.seed, (uint32_t) k, (uint32_t) l) >= step.probability)
                    continue; // the whole step (all its ratchets) is skipped

                const auto ratchet = std::clamp (step.ratchet, 1, 4);
                const auto start = grid + std::clamp (step.micro, -0.5f, 0.5f) * s;

                for (int j = 0; j < ratchet && numHits < maxHitsPerCall; ++j)
                {
                    const auto hit = start + j * s / ratchet;

                    if (hit < beatAtFrom || hit >= beatAtTo)
                        continue;

                    const auto offset = std::clamp (from + (int) std::llround ((hit - beatAtFrom) / beatsPerSample), from, to - 1);
                    const auto gateSamples = (int64_t) std::llround (std::clamp (step.gate, 0.05f, 1.0f) * s / ratchet / beatsPerSample);
                    hits[(size_t) numHits++] = { offset, lane.note, std::clamp (step.velocity, 0.01f, 1.0f), step.pitch, gateSamples };
                }
            }
        }

        for (int i = 1; i < numHits; ++i) // stable insertion sort by time, no allocation
        {
            const auto h = hits[(size_t) i];
            auto j = i;

            for (; j > 0 && hits[(size_t) j - 1].offset > h.offset; --j)
                hits[(size_t) j] = hits[(size_t) j - 1];

            hits[(size_t) j] = h;
        }

        for (int i = 0; i < numHits; ++i)
        {
            const auto& h = hits[(size_t) i];
            sounding.releaseUntil (h.offset, true, off);
            sounding.start (h.note, h.offset, h.gateSamples, off);
            sink (h.offset, h.note, h.velocity, h.pitch);
        }

        sounding.releaseUntil (to, false, off);
    }

    void endBlock (int numSamples) noexcept { sounding.endBlock (numSamples); }

    template <typename Sink>
    void flush (int offset, Sink&& sink) noexcept
    {
        sounding.flush (offset, [&] (int o, int n) { sink (o, n, 0.0f, 0.0f); });
    }

    int soundingCount() const noexcept { return sounding.count(); }

    // Grid step index containing `beat` (for the UI playhead).
    static int64_t stepAt (double beat, double stepBeats) noexcept { return (int64_t) std::floor (beat / std::max (stepBeats, 1.0 / 64.0)); }

private:
    static float unit (uint32_t seed, uint32_t k, uint32_t lane) noexcept
    {
        uint64_t x = ((uint64_t) seed << 32 | k) ^ ((uint64_t) lane + 0x632BE59BD9B4E019ull) * 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return (float) ((x ^ (x >> 31)) >> 40) / 16777216.0f;
    }

    NoteTracker<maxSounding> sounding;
};

} // namespace neuro
