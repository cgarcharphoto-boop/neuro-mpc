#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace neuro
{

// Sounding generated notes with sample-scheduled note-offs (used by the arpeggiator and the step sequencer).
// Ends are absolute sample times, so host loops and seeks can never strand a note. Fixed capacity, no allocation.
// offSink (sampleOffset, note) is called for every note-off.
template <int Capacity>
class NoteTracker
{
public:
    int count() const noexcept { return numSounding; }

    // Starts `note` at block offset `offset` for `length` samples. A still-sounding instance of the same note is
    // ended first (retrigger); when full, the oldest entry is ended.
    template <typename OffSink>
    void start (int note, int offset, int64_t length, OffSink&& off) noexcept
    {
        for (int i = 0; i < numSounding; ++i)
            if (notes[(size_t) i].note == note)
            {
                off (offset, note);
                notes[(size_t) i] = notes[(size_t) --numSounding];
                break;
            }

        if (numSounding == Capacity)
        {
            off (offset, notes[0].note);
            notes[0] = notes[(size_t) --numSounding];
        }

        notes[(size_t) numSounding++] = { note, blockOrigin + offset + std::max<int64_t> (1, length) };
    }

    // Ends notes due before `offset` (inclusive when `inclusive`, so offs precede a same-sample note-on).
    template <typename OffSink>
    void releaseUntil (int offset, bool inclusive, OffSink&& off) noexcept
    {
        const auto limit = blockOrigin + offset;

        for (int i = 0; i < numSounding;)
        {
            const auto end = notes[(size_t) i].endSample;

            if (inclusive ? end <= limit : end < limit)
            {
                off ((int) std::max<int64_t> (0, end - blockOrigin), notes[(size_t) i].note);
                notes[(size_t) i] = notes[(size_t) --numSounding];
            }
            else
            {
                ++i;
            }
        }
    }

    template <typename OffSink>
    void flush (int offset, OffSink&& off) noexcept
    {
        for (int i = 0; i < numSounding; ++i)
            off (offset, notes[(size_t) i].note);

        numSounding = 0;
    }

    void endBlock (int numSamples) noexcept { blockOrigin += numSamples; }

private:
    struct Entry { int note; int64_t endSample; };
    std::array<Entry, Capacity> notes {};
    int numSounding = 0;
    int64_t blockOrigin = 0;
};

} // namespace neuro
