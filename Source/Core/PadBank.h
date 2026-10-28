#pragma once

#include "SliceSet.h"
#include <array>
#include <string>

namespace neuro
{

// What the audio thread needs from a pad (published as an immutable PadMap).
struct PadPlay
{
    int sliceId = 0;                  // stable slice id; 0 = empty pad
    int note = -1;                    // MIDI note that triggers the pad
    float gain = 1.0f;                // linear
    float pitch = 0.0f;               // semitones, added to the slice's own pitch
    int chokeGroup = 0;               // 0 = none; a new note in a group fades out the group's other voices
    float velocitySensitivity = 1.0f; // 0 = always full level, 1 = level follows velocity

    float levelFor (float velocity) const noexcept { return gain * (1.0f - velocitySensitivity + velocitySensitivity * velocity); }
};

struct PadDefinition : PadPlay
{
    std::string name;                 // empty = show the slice number
    uint32_t colour = 0xff2a2f3a;
};

inline constexpr int padsPerBank = 16, numBanks = 4, numPads = padsPerBank * numBanks;
using PadMap = std::array<PadPlay, numPads>;

// 64 pads (banks A-D). Pads reference slices by id, so slice edits never silently re-point a pad.
// followSlices: pad n tracks slice n (re-applied on every slice edit) until a pad is assigned by hand.
class PadBank
{
public:
    static constexpr int baseNote = 36; // C1 = pad A1, MPC convention

    PadBank()
    {
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i].note = baseNote + i;
    }

    bool followSlices = true;

    const PadDefinition& operator[] (int i) const noexcept { return pads[(size_t) i]; }
    PadDefinition& operator[] (int i) noexcept             { return pads[(size_t) i]; }

    // Slice k -> pad k. Only the slice reference changes; notes, names, levels and choke groups are kept.
    void autoAssign (const SliceSet& slices)
    {
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i].sliceId = i < slices.size() ? slices.getSlices()[(size_t) i].id : 0;
    }

    void assign (int pad, int sliceId)
    {
        if (pad >= 0 && pad < numPads)
        {
            pads[(size_t) pad].sliceId = sliceId;
            followSlices = false;
        }
    }

    // Copies everything except the MIDI note (so the target keeps its own trigger).
    void copy (int from, int to)
    {
        if (from < 0 || from >= numPads || to < 0 || to >= numPads || from == to)
            return;

        const auto note = pads[(size_t) to].note;
        pads[(size_t) to] = pads[(size_t) from];
        pads[(size_t) to].note = note;
        followSlices = false;
    }

    // Copies pad `from` to the next empty pad; returns its index or -1.
    int duplicate (int from)
    {
        for (int i = 1; i < numPads; ++i)
        {
            const auto to = (from + i) % numPads;

            if (pads[(size_t) to].sliceId == 0)
            {
                copy (from, to);
                return to;
            }
        }

        return -1;
    }

    void clear (int pad)
    {
        if (pad >= 0 && pad < numPads)
        {
            const auto note = pads[(size_t) pad].note;
            pads[(size_t) pad] = {};
            pads[(size_t) pad].note = note;
            followSlices = false;
        }
    }

    PadMap playMap() const
    {
        PadMap map;

        for (int i = 0; i < numPads; ++i)
            map[(size_t) i] = pads[(size_t) i];

        return map;
    }

private:
    std::array<PadDefinition, numPads> pads;
};

} // namespace neuro
