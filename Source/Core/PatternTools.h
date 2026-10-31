#pragma once

#include "Sequencer.h"
#include <string>
#include <vector>

// Pattern transformations, Pattern Genome and Pattern Mutation (message thread; deterministic; JUCE-free).
namespace neuro::pattern
{

inline int lengthOf (const SeqLane& lane) noexcept { return std::clamp (lane.length, 1, SeqLane::maxSteps); }

// ---- Transformations (per lane; patterns apply them to one lane or all) ----

inline void reverse (SeqLane& lane)
{
    const auto n = lengthOf (lane);
    std::reverse (lane.steps.begin(), lane.steps.begin() + n);

    for (int i = 0; i < n; ++i)
        lane.steps[(size_t) i].micro = -lane.steps[(size_t) i].micro; // early becomes late when time runs backwards
}

// Positive = later (shift right), negative = earlier; wraps within the lane length.
inline void rotate (SeqLane& lane, int amount)
{
    const auto n = lengthOf (lane);
    const auto r = ((amount % n) + n) % n;
    std::rotate (lane.steps.begin(), lane.steps.begin() + (n - r) % n, lane.steps.begin() + n);
}

// Repeats the lane once (length x2, capped at 64).
inline void doubleLength (SeqLane& lane)
{
    const auto n = lengthOf (lane);
    const auto m = std::min (n * 2, SeqLane::maxSteps);

    for (int i = n; i < m; ++i)
        lane.steps[(size_t) i] = lane.steps[(size_t) (i - n)];

    lane.length = m;
}

// Keeps the first half.
inline void halve (SeqLane& lane) { lane.length = std::max (1, lengthOf (lane) / 2); }

// Spreads steps out in time: step i -> 2i (length x2, capped).
inline void expand (SeqLane& lane)
{
    const auto n = std::min (lengthOf (lane), SeqLane::maxSteps / 2);
    std::array<SeqStep, SeqLane::maxSteps> out {};

    for (int i = 0; i < n; ++i)
        out[(size_t) (2 * i)] = lane.steps[(size_t) i];

    lane.steps = out;
    lane.length = 2 * n;
}

// Squeezes steps together: steps 2i and 2i+1 -> i (the first that is on wins). Inverse of expand.
inline void contract (SeqLane& lane)
{
    const auto n = lengthOf (lane);
    std::array<SeqStep, SeqLane::maxSteps> out {};

    for (int i = 0; i < (n + 1) / 2; ++i)
    {
        const auto& a = lane.steps[(size_t) (2 * i)];
        const auto& b = 2 * i + 1 < n ? lane.steps[(size_t) (2 * i + 1)] : SeqStep {};
        out[(size_t) i] = a.on || ! b.on ? a : b;
    }

    lane.steps = out;
    lane.length = std::max (1, (n + 1) / 2);
}

enum class VelocityCurve { rampUp, rampDown, downbeats, offbeats, flat };

// Reshapes velocities of the on-steps; depth 0..1 blends from the current values to the curve.
inline void applyVelocityCurve (SeqLane& lane, VelocityCurve curve, float depth)
{
    const auto n = lengthOf (lane);

    for (int i = 0; i < n; ++i)
    {
        auto& st = lane.steps[(size_t) i];
        const auto t = n > 1 ? (float) i / (float) (n - 1) : 0.0f;
        const auto target = curve == VelocityCurve::rampUp    ? 0.3f + 0.7f * t
                          : curve == VelocityCurve::rampDown  ? 1.0f - 0.7f * t
                          : curve == VelocityCurve::downbeats ? (i % 4 == 0 ? 1.0f : 0.55f)
                          : curve == VelocityCurve::offbeats  ? (i % 4 == 2 ? 1.0f : 0.55f)
                                                              : 0.8f;
        st.velocity = std::clamp (st.velocity + depth * (target - st.velocity), 0.01f, 1.0f);
    }
}

// Downbeats (every 4th step) always play; other steps get `offbeatProbability`.
inline void applyProbabilityProfile (SeqLane& lane, float offbeatProbability)
{
    for (int i = 0; i < lengthOf (lane); ++i)
        lane.steps[(size_t) i].probability = i % 4 == 0 ? 1.0f : std::clamp (offbeatProbability, 0.0f, 1.0f);
}

// ---- Pattern Genome ----

enum Gene : uint32_t
{
    rhythm = 1, velocity = 2, pitch = 4, groove = 8, chance = 16, ratchet = 32, gate = 64, timbre = 128, allGenes = 255
};

// A lane as explicit properties: where events happen (rhythm) and per-event contours, in event order.
// Contours are cycled when expressed onto a rhythm with a different number of events, so e.g. an accent
// contour survives a density change in the same order.
struct Genome
{
    int length = 16;
    std::vector<int> rhythm;
    std::vector<float> velocity, pitch, groove, chance, gate;
    std::vector<int> ratchet;
    std::vector<SeqStep> extras; // per-event conditions (not a gene; travel with the events)
};

inline Genome extract (const SeqLane& lane)
{
    Genome g;
    g.length = lengthOf (lane);

    for (int i = 0; i < g.length; ++i)
    {
        const auto& st = lane.steps[(size_t) i];

        if (! st.on)
            continue;

        g.rhythm.push_back (i);
        g.velocity.push_back (st.velocity);
        g.pitch.push_back (st.pitch);
        g.groove.push_back (st.micro);
        g.chance.push_back (st.probability);
        g.gate.push_back (st.gate);
        g.ratchet.push_back (st.ratchet);
        g.extras.push_back (st);
    }

    return g;
}

inline void express (SeqLane& lane, const Genome& g)
{
    lane.length = std::clamp (g.length, 1, SeqLane::maxSteps);
    lane.steps = {};

    auto pick = [] (const auto& contour, size_t i, auto fallback) { return contour.empty() ? fallback : contour[i % contour.size()]; };

    for (size_t e = 0; e < g.rhythm.size(); ++e)
    {
        if (g.rhythm[e] < 0 || g.rhythm[e] >= lane.length)
            continue;

        auto& st = lane.steps[(size_t) g.rhythm[e]];
        st = g.extras.empty() ? SeqStep {} : g.extras[e % g.extras.size()];
        st.on = true;
        st.velocity = pick (g.velocity, e, 0.8f);
        st.pitch = pick (g.pitch, e, 0.0f);
        st.micro = pick (g.groove, e, 0.0f);
        st.probability = pick (g.chance, e, 1.0f);
        st.gate = pick (g.gate, e, 0.5f);
        st.ratchet = pick (g.ratchet, e, 1);
    }
}

// Takes the genes in `fromDonor` from donor, everything else from base. Rhythm also takes the donor's length.
inline Genome combine (const Genome& base, const Genome& donor, uint32_t fromDonor)
{
    auto g = base;

    if (fromDonor & rhythm)   { g.rhythm = donor.rhythm; g.length = donor.length; g.extras = donor.extras; }
    if (fromDonor & velocity) g.velocity = donor.velocity;
    if (fromDonor & pitch)    g.pitch = donor.pitch;
    if (fromDonor & groove)   g.groove = donor.groove;
    if (fromDonor & chance)   g.chance = donor.chance;
    if (fromDonor & ratchet)  g.ratchet = donor.ratchet;
    if (fromDonor & gate)     g.gate = donor.gate;

    if ((fromDonor & timbre) && ! donor.extras.empty())   // sound targets travel per event, in event order
        for (size_t e = 0; e < g.extras.size(); ++e)
        {
            const auto& d = donor.extras[e % donor.extras.size()];
            g.extras[e].sliceId = d.sliceId;
            g.extras[e].timbreX = d.timbreX;
            g.extras[e].timbreY = d.timbreY;
        }

    return g;
}

inline std::string describe (const Genome& g)
{
    std::string r ((size_t) std::max (0, g.length), '.');

    for (auto i : g.rhythm)
        r[(size_t) i] = 'x';

    std::string vel, pit;

    for (size_t e = 0; e < g.rhythm.size() && e < 8; ++e)
    {
        vel += std::to_string ((int) std::lround (g.velocity[e] * 127.0f)) + " ";
        pit += (g.pitch[e] >= 0.0f ? "+" : "") + std::to_string ((int) std::lround (g.pitch[e])) + " ";
    }

    return "Rhythm " + r + "  |  " + std::to_string (g.rhythm.size()) + " events  |  vel " + vel + (g.rhythm.size() > 8 ? "... " : "")
           + "|  pitch " + pit;
}

// ---- Pattern Mutation ----

struct MutationSettings
{
    float intensity = 0.3f;      // chance that an unpreserved step / event mutates, per gene
    uint32_t genes = rhythm | velocity | pitch; // genes allowed to change; all others are preserved exactly
    float preservation = 0.5f;   // fraction of steps that are never touched (chosen by seed)
    float densityBias = 0.0f;    // -1 thins out .. +1 fills in (rhythm gene)
    int pitchRange = 7;          // +/- semitones
    float velocityRange = 0.3f;  // +/-
    float timingRange = 0.15f;   // +/- fraction of a step
    uint32_t seed = 1;
};

inline uint32_t mix (uint32_t a, uint32_t b, uint32_t c)
{
    uint64_t x = ((uint64_t) a << 32 | b) ^ (uint64_t) c * 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t) (x ^ (x >> 31));
}

inline float unit (uint32_t a, uint32_t b, uint32_t c) { return (float) (mix (a, b, c) >> 8) / 16777216.0f; }

// Mutates a lane through its genome: only genes in settings.genes change, preserved steps never change.
inline void mutate (SeqLane& lane, const MutationSettings& m)
{
    auto g = extract (lane);
    const auto p = std::clamp (m.intensity, 0.0f, 1.0f);
    auto preserved = [&] (int step) { return unit (m.seed, (uint32_t) step, 1) < m.preservation; };
    auto roll = [&] (int step, uint32_t gene) { return unit (m.seed, (uint32_t) step, 100 + gene) < p; };
    auto spread = [&] (int step, uint32_t gene) { return 2.0f * unit (m.seed, (uint32_t) step, 200 + gene) - 1.0f; };

    if (m.genes & rhythm)
    {
        std::vector<bool> on ((size_t) g.length, false);

        for (auto i : g.rhythm)
            on[(size_t) i] = true;

        const auto addChance = std::clamp (0.5f + 0.5f * m.densityBias, 0.0f, 1.0f);

        for (int i = 0; i < g.length; ++i)
            if (! preserved (i) && roll (i, rhythm))
                on[(size_t) i] = unit (m.seed, (uint32_t) i, 300) < (on[(size_t) i] ? 1.0f - addChance : addChance) ? ! on[(size_t) i]
                                                                                                                  : on[(size_t) i];
        // Events keep their contour order; positions change.
        std::vector<int> positions;

        for (int i = 0; i < g.length; ++i)
            if (on[(size_t) i])
                positions.push_back (i);

        g.rhythm = positions;
    }

    for (size_t e = 0; e < g.rhythm.size(); ++e)
    {
        const auto step = g.rhythm[e];

        if (preserved (step))
            continue;

        if ((m.genes & velocity) && e < g.velocity.size() && roll (step, velocity))
            g.velocity[e] = std::clamp (g.velocity[e] + m.velocityRange * spread (step, velocity), 0.05f, 1.0f);

        if ((m.genes & pitch) && e < g.pitch.size() && roll (step, pitch))
            g.pitch[e] = std::clamp (g.pitch[e] + std::round ((float) m.pitchRange * spread (step, pitch)), -24.0f, 24.0f);

        if ((m.genes & groove) && e < g.groove.size() && roll (step, groove))
            g.groove[e] = std::clamp (g.groove[e] + m.timingRange * spread (step, groove), -0.5f, 0.5f);

        if ((m.genes & chance) && e < g.chance.size() && roll (step, chance))
            g.chance[e] = std::clamp (g.chance[e] + 0.4f * spread (step, chance), 0.1f, 1.0f);

        if ((m.genes & ratchet) && e < g.ratchet.size() && roll (step, ratchet))
            g.ratchet[e] = 1 + (int) (mix (m.seed, (uint32_t) step, 400) % 4);

        if ((m.genes & gate) && e < g.gate.size() && roll (step, gate))
            g.gate[e] = std::clamp (g.gate[e] + 0.3f * spread (step, gate), 0.05f, 1.0f);

        // Timbre: moves a timbre-addressed step through sound space (on the 2D map).
        if ((m.genes & timbre) && e < g.extras.size() && g.extras[e].timbreX >= 0.0f && roll (step, timbre))
        {
            g.extras[e].timbreX = std::clamp (g.extras[e].timbreX + 0.25f * spread (step, timbre), 0.0f, 1.0f);
            g.extras[e].timbreY = std::clamp (g.extras[e].timbreY + 0.25f * spread (step, 300 + timbre), 0.0f, 1.0f);
        }
    }

    express (lane, g);
}

} // namespace neuro::pattern
