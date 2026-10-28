#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace neuro
{

enum class ArpDirection { up, down, upDown, downUp, asPlayed, reversePlayed, random, randomNoRepeat, chord, octaveCycle };
enum class ArpRetrigger { off, firstNote, everyNote };

struct ArpSettings
{
    ArpDirection direction = ArpDirection::up;
    double stepBeats = 0.25;      // step length in quarter notes (1/16 = 0.25)
    float gate = 0.5f;            // note length as a fraction of the step (0.05 .. 1)
    int octaves = 1;              // 1 .. 4
    float swing = 0.5f;           // MPC-style: position of every 2nd step within its pair (0.5 straight .. 0.75)
    float probability = 1.0f;    // chance that a step plays (skipped steps still advance the pattern)
    int repeats = 1;              // times each pattern element repeats (1 .. 4)
    int fixedVelocity = 0;        // 0 = as played, else 1 .. 127
    bool latch = false;           // keep playing after all keys are released, until a new chord starts
    ArpRetrigger retrigger = ArpRetrigger::firstNote;
    uint32_t seed = 1;            // randomness (random modes, probability) restarts from this seed on each phrase
};

// Real-time-safe arpeggiator: fixed-size storage, no allocation. Timing is computed from the beat position of each
// sample, so it locks to the host grid; note-offs are scheduled in samples, so loops/seeks can never strand a note.
// Events reach a sink: sink (sampleOffset, note, velocity), velocity 0 = note-off. Within one process() call events are
// not guaranteed to be in sample order (sort by offset, note-offs first at equal offsets).
class Arpeggiator
{
public:
    static constexpr int maxHeld = 32, maxSounding = 64;

    void setSettings (const ArpSettings& s) noexcept
    {
        dirty = dirty || s.direction != settings.direction || s.octaves != settings.octaves;
        settings = s;
        settings.octaves = std::clamp (settings.octaves, 1, 4);
        settings.repeats = std::clamp (settings.repeats, 1, 4);
        settings.gate = std::clamp (settings.gate, 0.05f, 1.0f);
        settings.swing = std::clamp (settings.swing, 0.5f, 0.75f);
        settings.stepBeats = std::max (settings.stepBeats, 1.0 / 64.0);
    }

    const ArpSettings& getSettings() const noexcept { return settings; }
    int heldCount() const noexcept     { return numHeld; }
    int soundingCount() const noexcept { return numSounding; }

    // Returns true when this note starts a new phrase (first key after none were held, or latch replaced).
    bool noteOn (int note, float velocity) noexcept
    {
        const auto keysWereUp = physicallyHeld == 0;
        ++physicallyHeld;

        if (keysWereUp && settings.latch)
            numHeld = 0; // a new chord replaces the latched one

        const auto phraseStart = numHeld == 0;
        remove (note);

        if (numHeld < maxHeld)
            held[(size_t) numHeld++] = { note, velocity, ++orderCounter };

        dirty = true;

        if (phraseStart || settings.retrigger == ArpRetrigger::everyNote)
            restartPattern();

        return phraseStart;
    }

    void noteOff (int note) noexcept
    {
        physicallyHeld = std::max (0, physicallyHeld - 1);

        if (! settings.latch)
        {
            remove (note);
            dirty = true;
        }
    }

    void setLatch (bool shouldLatch) noexcept
    {
        if (settings.latch && ! shouldLatch && physicallyHeld == 0)
        {
            numHeld = 0; // releasing latch with no keys down stops the arp
            dirty = true;
        }

        settings.latch = shouldLatch;
    }

    // Generates events for block samples [from, to). beatAtFrom = beat position at `from`; beatsPerSample from tempo.
    template <typename Sink>
    void process (int from, int to, double beatAtFrom, double beatsPerSample, Sink&& sink) noexcept
    {
        if (to <= from || beatsPerSample <= 0.0)
            return;

        if (dirty)
            rebuild();

        const auto s = settings.stepBeats;
        const auto beatAtTo = beatAtFrom + (to - from) * beatsPerSample;
        const auto first = (int64_t) std::floor (beatAtFrom / s) - 1;
        const auto last = (int64_t) std::floor (beatAtTo / s) + 1;

        for (auto k = first; k <= last; ++k)
        {
            const auto stepBeat = stepStart (k);

            if (stepBeat < beatAtFrom || stepBeat >= beatAtTo)
                continue;

            const auto offset = std::clamp (from + (int) std::llround ((stepBeat - beatAtFrom) / beatsPerSample), from, to - 1);
            releaseUntil (offset, sink, true);

            if (seqLength > 0)
                fireStep (k, offset, beatsPerSample, sink);
        }

        releaseUntil (to, sink, false);
    }

    // Call once per block after process(): advances the sample clock used for note-off scheduling.
    void endBlock (int numSamples) noexcept { blockOrigin += numSamples; }

    // Emits note-offs for everything sounding (at `offset`). Held notes are kept unless clearHeld.
    template <typename Sink>
    void flush (int offset, Sink&& sink, bool clearHeld) noexcept
    {
        for (int i = 0; i < numSounding; ++i)
            sink (offset, sounding[(size_t) i].note, 0.0f);

        numSounding = 0;

        if (clearHeld)
        {
            numHeld = physicallyHeld = 0;
            dirty = true;
        }
    }

private:
    struct Held { int note; float velocity; uint32_t order; };
    struct Sounding { int note; int64_t endSample; };

    double stepStart (int64_t k) const noexcept
    {
        const auto s = settings.stepBeats;
        const auto pairStart = (double) (k - (k & 1)) * s;
        return (k & 1) ? pairStart + 2.0 * s * settings.swing : pairStart;
    }

    void remove (int note) noexcept
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note)
            {
                std::copy (held.begin() + i + 1, held.begin() + numHeld, held.begin() + i);
                --numHeld;
                return;
            }
    }

    void restartPattern() noexcept
    {
        position = 0;
        repeatCount = 0;
        lastNote = -1;
        rng = settings.seed != 0 ? settings.seed : 0x9e3779b9u;
    }

    uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    // Builds the note order for the current held notes and direction (chord mode stores one entry per octave).
    void rebuild() noexcept
    {
        dirty = false;
        seqLength = 0;

        if (numHeld == 0)
            return;

        std::array<Held, maxHeld> byPitch {}, byOrder {};
        std::copy (held.begin(), held.begin() + numHeld, byPitch.begin());
        std::copy (held.begin(), held.begin() + numHeld, byOrder.begin());
        std::sort (byPitch.begin(), byPitch.begin() + numHeld, [] (auto& a, auto& b) { return a.note < b.note; });
        std::sort (byOrder.begin(), byOrder.begin() + numHeld, [] (auto& a, auto& b) { return a.order < b.order; });

        auto push = [this] (int note) { if (seqLength < (int) seq.size()) seq[(size_t) seqLength++] = note; };
        const auto octaves = settings.octaves;
        const auto dir = settings.direction;

        if (dir == ArpDirection::chord)
        {
            for (int o = 0; o < octaves; ++o)
                push (12 * o); // octave offsets; the chord itself is all held notes
            return;
        }

        if (dir == ArpDirection::octaveCycle)
        {
            for (int i = 0; i < numHeld; ++i)
                for (int o = 0; o < octaves; ++o)
                    push (byPitch[(size_t) i].note + 12 * o);
            return;
        }

        const auto& source = (dir == ArpDirection::asPlayed || dir == ArpDirection::reversePlayed) ? byOrder : byPitch;

        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < numHeld; ++i)
                push (source[(size_t) i].note + 12 * o);

        if (dir == ArpDirection::down || dir == ArpDirection::reversePlayed)
            std::reverse (seq.begin(), seq.begin() + seqLength);

        if ((dir == ArpDirection::upDown || dir == ArpDirection::downUp) && seqLength > 1)
        {
            if (dir == ArpDirection::downUp)
                std::reverse (seq.begin(), seq.begin() + seqLength);

            // Mirror without repeating the turning points: C E G -> C E G E.
            for (int i = seqLength - 2; i >= 1; --i)
                push (seq[(size_t) i]);
        }

        if (seqLength > 0)
            position %= seqLength;
    }

    float velocityOf (int note) const noexcept
    {
        if (settings.fixedVelocity > 0)
            return (float) settings.fixedVelocity / 127.0f;

        for (int i = 0; i < numHeld; ++i)
            if ((held[(size_t) i].note - note) % 12 == 0)
                return held[(size_t) i].velocity;

        return numHeld > 0 ? held[0].velocity : 0.8f;
    }

    template <typename Sink>
    void fireStep (int64_t, int offset, double beatsPerSample, Sink& sink) noexcept
    {
        const auto plays = settings.probability >= 1.0f
                           || (float) (nextRandom() % 10000) / 10000.0f < settings.probability;

        // Choose the pattern element (random modes draw; the others walk the order).
        int element = position;

        if (settings.direction == ArpDirection::random || settings.direction == ArpDirection::randomNoRepeat)
        {
            element = (int) (nextRandom() % (uint32_t) seqLength);

            if (settings.direction == ArpDirection::randomNoRepeat && seqLength > 1 && seq[(size_t) element] == lastNote)
                element = (element + 1 + (int) (nextRandom() % (uint32_t) (seqLength - 1))) % seqLength;
        }

        if (++repeatCount >= settings.repeats)
        {
            repeatCount = 0;
            position = (position + 1) % seqLength;
        }

        if (! plays)
            return;

        const auto length = std::max<int64_t> (1, (int64_t) std::llround (settings.gate * settings.stepBeats / beatsPerSample));
        const auto start = blockOrigin + offset;

        auto play = [&] (int note)
        {
            if (note < 0 || note > 127)
                return;

            for (int i = 0; i < numSounding; ++i) // retrigger: end the previous instance of this note first
                if (sounding[(size_t) i].note == note)
                {
                    sink (offset, note, 0.0f);
                    sounding[(size_t) i] = sounding[(size_t) --numSounding];
                    break;
                }

            if (numSounding == maxSounding)
            {
                sink (offset, sounding[0].note, 0.0f);
                sounding[0] = sounding[(size_t) --numSounding];
            }

            sink (offset, note, velocityOf (note));
            sounding[(size_t) numSounding++] = { note, start + length };
        };

        if (settings.direction == ArpDirection::chord)
        {
            for (int i = 0; i < numHeld; ++i)
                play (held[(size_t) i].note + seq[(size_t) element]);
        }
        else
        {
            play (seq[(size_t) element]);
        }

        lastNote = seq[(size_t) element];
    }

    // Emits note-offs whose end falls before `offset` (inclusive when `inclusive`, so offs precede a same-sample on).
    template <typename Sink>
    void releaseUntil (int offset, Sink& sink, bool inclusive) noexcept
    {
        const auto limit = blockOrigin + offset;

        for (int i = 0; i < numSounding;)
        {
            const auto end = sounding[(size_t) i].endSample;

            if (inclusive ? end <= limit : end < limit)
            {
                sink ((int) std::max<int64_t> (0, end - blockOrigin), sounding[(size_t) i].note, 0.0f);
                sounding[(size_t) i] = sounding[(size_t) --numSounding];
            }
            else
            {
                ++i;
            }
        }
    }

    ArpSettings settings;
    std::array<Held, maxHeld> held {};
    std::array<int, maxHeld * 4 * 2> seq {};
    std::array<Sounding, maxSounding> sounding {};
    int numHeld = 0, physicallyHeld = 0, numSounding = 0, seqLength = 0, position = 0, repeatCount = 0, lastNote = -1;
    uint32_t orderCounter = 0, rng = 1;
    int64_t blockOrigin = 0;
    bool dirty = false;
};

} // namespace neuro
