#pragma once

#include "NoteTracker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace neuro
{

// Deterministic trig conditions (evaluated from the grid position; no hidden state).
enum class SeqCondition { always, ratio, first, notFirst, fill, notFill, previous, notPrevious };

struct SeqStep
{
    bool on = false;
    float velocity = 0.8f;     // 0 .. 1
    float gate = 0.5f;         // fraction of the step (0.05 .. 1); ratchets divide it
    float probability = 1.0f;  // seeded per step and grid position: reproducible, varies per cycle
    int ratchet = 1;           // 1 .. 4 evenly spaced hits within the step
    float micro = 0.0f;        // microtiming: -0.5 .. +0.5 of a step, never quantised away
    float pitch = 0.0f;        // semitones added to the triggered pad / key (resampled)
    SeqCondition condition = SeqCondition::always;
    int condA = 1, condB = 2;  // ratio: plays on lane cycle A of every B (1-based), e.g. 1:2 = every other cycle
};

inline std::string describe (const SeqStep& st)
{
    switch (st.condition)
    {
        case SeqCondition::always:      return "always";
        case SeqCondition::ratio:       return std::to_string (st.condA) + ":" + std::to_string (st.condB);
        case SeqCondition::first:       return "first cycle";
        case SeqCondition::notFirst:    return "not first cycle";
        case SeqCondition::fill:        return "fill";
        case SeqCondition::notFill:     return "not fill";
        case SeqCondition::previous:    return "if previous step played";
        case SeqCondition::notPrevious: return "if previous step did not play";
    }

    return {};
}

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
    bool fill = false;         // performance fill switch (for fill / not-fill conditions)
};

// Phrase Weaving: other patterns contribute to the base pattern, lane by lane (lane n of a source affects lane n).
struct SeqWeave
{
    int accentSource = -1;     // pattern whose on-steps add accentAmount to the base hit's velocity
    float accentAmount = 0.3f;
    int fillSource = -1;       // pattern that replaces the base on the last bar of every fillEvery bars (4/4 bars)
    int fillEvery = 4;
    int pitchSource = -1;      // pattern whose on-steps add their pitch to the base hit
};

// Step sequencer. Timing is stateless: every hit's time (grid + swing + microtiming + ratchet) and its probability
// outcome are pure functions of the beat position, lane, step and seed, so playback is deterministic, block-size
// independent and robust to host loops and seeks. Only sounding notes are state (sample-scheduled note-offs).
// sink (sampleOffset, note, velocity, pitchSemitones); velocity 0 = note-off. Events within one call may be unordered.
class Sequencer
{
public:
    static constexpr int maxSounding = 128, maxHitsPerCall = 512;

    // Single pattern, no weaving.
    template <typename Sink>
    void process (const SeqPattern& pattern, const SeqTiming& timing, int from, int to, double beatAtFrom,
                  double beatsPerSample, Sink&& sink) noexcept
    {
        process (&pattern, 1, 0, {}, timing, from, to, beatAtFrom, beatsPerSample, sink);
    }

    // bank of numPatterns patterns; base = the playing pattern; weave sources index into the bank.
    template <typename Sink>
    void process (const SeqPattern* bank, int numPatterns, int base, const SeqWeave& weave, const SeqTiming& timing,
                  int from, int to, double beatAtFrom, double beatsPerSample, Sink&& sink) noexcept
    {
        if (to <= from || beatsPerSample <= 0.0 || bank == nullptr || numPatterns <= 0)
            return;

        auto patternAt = [&] (int index) -> const SeqPattern* { return index >= 0 && index < numPatterns ? bank + index : nullptr; };
        const auto* basePattern = patternAt (std::clamp (base, 0, numPatterns - 1));
        const auto* accentPattern = patternAt (weave.accentSource);
        const auto* fillPattern = patternAt (weave.fillSource);
        const auto* pitchPattern = patternAt (weave.pitchSource);

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
            const auto bar = (int64_t) std::floor ((double) k * s / 4.0);
            const auto fillBar = fillPattern != nullptr && weave.fillEvery > 0
                                 && ((bar % weave.fillEvery) + weave.fillEvery) % weave.fillEvery == weave.fillEvery - 1;
            const auto& pattern = fillBar ? *fillPattern : *basePattern;
            const auto fill = timing.fill || fillBar;

            for (int l = 0; l < std::min (pattern.numLanes, SeqPattern::maxLanes); ++l)
            {
                const auto& lane = pattern.lanes[(size_t) l];

                if (lane.mute || lane.note < 0 || lane.note > 127 || ! fires (lane, l, k, timing.seed, fill, 0))
                    continue;

                const auto& step = lane.steps[(size_t) stepIndex (lane, k)];
                auto velocity = std::clamp (step.velocity, 0.01f, 1.0f);
                auto pitch = step.pitch;

                if (accentPattern != nullptr && isOn (accentPattern->lanes[(size_t) l], k))
                    velocity = std::min (1.0f, velocity + weave.accentAmount);

                if (pitchPattern != nullptr && isOn (pitchPattern->lanes[(size_t) l], k))
                    pitch += pitchPattern->lanes[(size_t) l].steps[(size_t) stepIndex (pitchPattern->lanes[(size_t) l], k)].pitch;

                const auto ratchet = std::clamp (step.ratchet, 1, 4);
                const auto start = grid + std::clamp (step.micro, -0.5f, 0.5f) * s;

                for (int j = 0; j < ratchet && numHits < maxHitsPerCall; ++j)
                {
                    const auto hit = start + j * s / ratchet;

                    if (hit < beatAtFrom || hit >= beatAtTo)
                        continue;

                    const auto offset = std::clamp (from + (int) std::llround ((hit - beatAtFrom) / beatsPerSample), from, to - 1);
                    const auto gateSamples = (int64_t) std::llround (std::clamp (step.gate, 0.05f, 1.0f) * s / ratchet / beatsPerSample);
                    hits[(size_t) numHits++] = { offset, lane.note, velocity, pitch, gateSamples };
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

    // Whether lane step at grid index k plays: on, its condition holds, and its probability roll passes.
    // "previous" conditions look back to the lane's previous on-step and evaluate it the same way (bounded depth).
    static bool fires (const SeqLane& lane, int laneIndex, int64_t k, uint32_t seed, bool fill, int depth) noexcept
    {
        const auto& step = lane.steps[(size_t) stepIndex (lane, k)];

        if (! step.on)
            return false;

        const auto length = std::clamp (lane.length, 1, SeqLane::maxSteps);
        const auto cycle = (k >= 0 ? k : k - length + 1) / length;
        bool condition = true;

        switch (step.condition)
        {
            case SeqCondition::always:   break;
            case SeqCondition::ratio:
            {
                const auto b = std::max (1, step.condB);
                condition = ((cycle % b) + b) % b == std::clamp (step.condA, 1, b) - 1;
                break;
            }
            case SeqCondition::first:    condition = cycle == 0; break;
            case SeqCondition::notFirst: condition = cycle != 0; break;
            case SeqCondition::fill:     condition = fill; break;
            case SeqCondition::notFill:  condition = ! fill; break;
            case SeqCondition::previous:
            case SeqCondition::notPrevious:
            {
                bool played = false;

                if (depth < 16)
                    for (int back = 1; back <= length; ++back)
                        if (lane.steps[(size_t) stepIndex (lane, k - back)].on)
                        {
                            played = fires (lane, laneIndex, k - back, seed, fill, depth + 1);
                            break;
                        }

                condition = (step.condition == SeqCondition::previous) == played;
                break;
            }
        }

        return condition && (step.probability >= 1.0f || unit (seed, (uint32_t) k, (uint32_t) laneIndex) < step.probability);
    }

private:
    static int stepIndex (const SeqLane& lane, int64_t k) noexcept
    {
        const auto length = std::clamp (lane.length, 1, SeqLane::maxSteps);
        return (int) (((k % length) + length) % length);
    }

    static bool isOn (const SeqLane& lane, int64_t k) noexcept { return lane.steps[(size_t) stepIndex (lane, k)].on && ! lane.mute; }

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
