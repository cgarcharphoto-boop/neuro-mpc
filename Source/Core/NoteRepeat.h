#pragma once

#include "NoteTracker.h"
#include <algorithm>
#include <array>
#include <cmath>

// Note Repeat (MPC-style): held pads retrigger on a beat grid from 1 bar down to 1/128 (with triplets), each hit lasting
// `gate` of the repeat step (short gates chop any sample into a stutter / glitch). A press plays at once; later hits
// lock to the grid (host bar grid when the transport runs, else the first press). Latch keeps repeating after release.
// Real-time safe: fixed capacity, no allocation.
namespace neuro
{

inline constexpr std::array<double, 14> repeatRateBeats { 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0,
                                                          0.125, 1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125 };
inline constexpr const char* repeatRateNames[] { "1 bar", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T",
                                                 "1/32", "1/32T", "1/64", "1/64T", "1/128" };

class NoteRepeat
{
public:
    struct Settings
    {
        double rateBeats = 0.25;
        float gate = 0.5f;          // fraction of the step, 0.01 .. 1
        float swing = 0.5f;         // 0.5 = straight .. 0.75: every second hit is late
        bool latch = false;
        bool gridFromHost = false;  // host transport running: grid locked to the host's beats
    };

    void reset() noexcept { held.fill (0.0f); numInputs = 0; originSet = false; lastHit.fill (-1.0e9); sounding = {}; }

    // Input during the block, in time order (MIDI order). velocity 0 = release.
    void input (int offset, int note, float velocity) noexcept
    {
        if (note >= 0 && note < 128 && numInputs < (int) inputs.size())
            inputs[(size_t) numInputs++] = { offset, note, velocity };
    }

    bool anyHeld() const noexcept { return std::any_of (held.begin(), held.end(), [] (float v) { return v > 0.0f; }); }

    // on (offset, note, velocity) / off (offset, note), in time order, for [beat, beat + numSamples * beatsPerSample).
    template <typename OnSink, typename OffSink>
    void process (int numSamples, double beat, double beatsPerSample, const Settings& s, OnSink&& on, OffSink&& off) noexcept
    {
        const auto rate = std::max (1.0 / 256.0, s.rateBeats);
        const auto gateSamples = (int64_t) std::max (16.0, std::clamp ((double) s.gate, 0.01, 1.0) * rate / std::max (1.0e-12, beatsPerSample));
        const auto swingDelay = (std::clamp ((double) s.swing, 0.5, 0.75) - 0.5) * 2.0 * rate;
        auto beatAt = [&] (int offset) { return beat + offset * beatsPerSample; };

        auto hit = [&] (int offset, int note)
        {
            sounding.releaseUntil (offset, true, off);
            sounding.start (note, offset, gateSamples, off);
            on (offset, note, held[(size_t) note]);
            lastHit[(size_t) note] = beatAt (offset);
        };

        // Grid points k * rate (+ swing on odd k) from the origin, in this block.
        auto gridBeat = [&] (int64_t k) { return origin + (double) k * rate + ((k & 1) ? swingDelay : 0.0); };
        auto nextGrid = [&] (double from)
        {
            auto k = (int64_t) std::floor ((from - origin) / rate) - 1;
            while (gridBeat (k) < from - 1.0e-9) ++k;
            return k;
        };

        if (s.gridFromHost) { origin = 0.0; originSet = true; }

        int in = 0;
        auto k = originSet ? nextGrid (beat) : 0;

        while (true)
        {
            const auto gridOffset = originSet ? (int) std::ceil ((gridBeat (k) - beat) / beatsPerSample - 1.0e-3) : numSamples;
            const auto inputOffset = in < numInputs ? inputs[(size_t) in].offset : numSamples;

            if (inputOffset >= numSamples && gridOffset >= numSamples)
                break;

            if (inputOffset <= gridOffset && in < numInputs)
            {
                const auto& e = inputs[(size_t) in++];

                if (e.velocity > 0.0f)
                {
                    if (! originSet || (! s.gridFromHost && ! anyHeld()))
                    {
                        origin = beatAt (e.offset); // free: the grid starts at the first press
                        originSet = true;
                        k = nextGrid (beatAt (e.offset) + 1.0e-9);
                    }

                    held[(size_t) e.note] = e.velocity;
                    hit (e.offset, e.note);
                }
                else if (! s.latch)
                {
                    held[(size_t) e.note] = 0.0f; // its last hit still ends after the gate
                }

                continue;
            }

            const auto gb = gridBeat (k++);

            for (int n = 0; n < 128; ++n) // a hit right after a press is skipped (no flam)
                if (held[(size_t) n] > 0.0f && gb - lastHit[(size_t) n] >= 0.5 * rate)
                    hit (gridOffset, n);
        }

        numInputs = 0;
        sounding.releaseUntil (numSamples, false, off);
    }

    void endBlock (int numSamples) noexcept { sounding.endBlock (numSamples); }

    // Repeat switched off or latch released: end everything.
    template <typename OffSink>
    void stop (int offset, OffSink&& off) noexcept
    {
        held.fill (0.0f);
        numInputs = 0;
        originSet = false;
        sounding.flush (offset, off);
    }

    void releaseLatched() noexcept { held.fill (0.0f); }

private:
    struct Input { int offset, note; float velocity; };
    std::array<Input, 256> inputs {};
    int numInputs = 0;
    std::array<float, 128> held {};
    std::array<double, 128> lastHit {};
    double origin = 0.0;
    bool originSet = false;
    NoteTracker<128> sounding;
};

} // namespace neuro
