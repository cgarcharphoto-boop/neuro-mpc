#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace neuro
{

// 12-bit pitch-class masks relative to the scale root (bit 0 = root).
struct Scale { const char* name; uint16_t mask; };

inline constexpr std::array<Scale, 8> scales {{
    { "Chromatic",        0b111111111111 },
    { "Major",            0b101010110101 },
    { "Natural minor",    0b010110101101 },
    { "Harmonic minor",   0b100110101101 },
    { "Melodic minor",    0b101010101101 },
    { "Major pentatonic", 0b001010010101 },
    { "Minor pentatonic", 0b010010101001 },
    { "Blues",            0b010011101001 },
}};

inline bool inScale (int note, uint16_t mask, int root) noexcept
{
    return (mask >> (((note - root) % 12 + 12) % 12)) & 1u;
}

// Nearest note in the scale; ties resolve downwards. Chromatic (or empty) masks return the note unchanged.
inline int snapToScale (int note, uint16_t mask, int root) noexcept
{
    if ((mask & 0xFFF) == 0xFFF || (mask & 0xFFF) == 0)
        return note;

    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (note - d, mask, root)) return note - d;
        if (inScale (note + d, mask, root)) return note + d;
    }

    return note;
}

// Semitone shift (fine part preserved) that moves `rootMidi` to pitch class `targetPc`, choosing the octave closest to
// the current shift. Used by the 12-note selector.
inline float shiftForPitchClass (float rootMidi, float currentShift, int targetPc) noexcept
{
    const auto fine = currentShift - std::round (currentShift);
    const auto current = (int) std::lround (rootMidi + currentShift - fine);
    auto delta = ((targetPc - current) % 12 + 12) % 12;

    if (delta > 6)
        delta -= 12;

    return std::round (currentShift) + (float) delta + fine;
}

} // namespace neuro
