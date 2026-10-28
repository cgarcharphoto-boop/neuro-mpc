#pragma once

#include "NoteTracker.h"
#include "Scales.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace neuro
{

enum class ArpDirection { up, down, upDown, downUp, asPlayed, reversePlayed, random, randomNoRepeat, chord, octaveCycle };
enum class ArpRetrigger { off, firstNote, everyNote };

struct ArpSettings
{
    ArpDirection direction = ArpDirection::up;
    double stepBeats = 0.25;      // step length in quarter notes (1/16 = 0.25)
    float gate = 0.5f;            // note length as a fraction of the step (0.05 .. 1); ratchets divide it
    int octaves = 1;              // 1 .. 4
    float swing = 0.5f;           // MPC-style: position of every 2nd step within its pair (0.5 straight .. 0.75)
    float probability = 1.0f;    // chance that a step plays (a skipped step still uses up its note)
    int repeats = 1;              // times each pattern element repeats (1 .. 4)
    int fixedVelocity = 0;        // 0 = as played, else 1 .. 127
    bool latch = false;           // keep playing after all keys are released, until a new chord starts
    ArpRetrigger retrigger = ArpRetrigger::firstNote;
    uint32_t seed = 1;            // all randomness restarts from this seed on each phrase

    // Phase 11: generative controls
    float accentAmount = 0.3f;    // velocity boost for accented steps
    int phraseLength = 0;         // Phrase Memory: steps per phrase; 0 = off
    float variation = 0.0f;       // chance per step and attribute that a repeated phrase varies (0 .. 1)
    bool varyPitch = true, varyVelocity = true, varyRhythm = false;
    int returnEvery = 0;          // every Nth phrase cycle replays the original exactly; 0 = only the first
    double humanizeBeats = 0.0;   // max late shift of each step (beats)
    float humanizeVelocity = 0.0f; // +/- fraction of velocity
    uint16_t scaleMask = 0xFFF;   // for passing tones
    int scaleRoot = 0;
};

// One step of the rhythm pattern.
struct ArpStep
{
    bool on = true;
    bool accent = false;
    int ratchet = 1; // 1 .. 4 evenly spaced hits within the step
};

// Arpeggio Grammar rule: when <condition> at a step, apply <action>. Rules are applied in order and are inspectable.
struct ArpRule
{
    enum class When { every, range, phraseEnd, chance };
    enum class Action { transpose, rest, accent, ratchet, passing };

    When when = When::every;
    int n = 4;            // every/range: cycle length; phraseEnd: last n steps; chance: percent
    int offset = 0;       // every: step == offset (mod n); range: step >= offset (mod n)
    Action action = Action::transpose;
    int amount = 12;      // transpose: semitones; ratchet: hits; passing: scale steps (+/-)
    bool enabled = true;
};

inline std::string describe (const ArpRule& r)
{
    auto sign = [] (int v) { return (v > 0 ? "+" : "") + std::to_string (v); };
    std::string when;

    switch (r.when)
    {
        case ArpRule::When::every:     when = "step " + std::to_string (r.offset % std::max (1, r.n) + 1) + " of every " + std::to_string (r.n); break;
        case ArpRule::When::range:     when = "steps " + std::to_string (r.offset + 1) + "-" + std::to_string (r.n) + " of every " + std::to_string (r.n); break;
        case ArpRule::When::phraseEnd: when = "last " + std::to_string (r.n) + " step" + (r.n == 1 ? "" : "s") + " of the phrase"; break;
        case ArpRule::When::chance:    when = std::to_string (r.n) + "% of steps"; break;
    }

    std::string action;

    switch (r.action)
    {
        case ArpRule::Action::transpose: action = "transpose " + sign (r.amount) + " st"; break;
        case ArpRule::Action::rest:      action = "rest"; break;
        case ArpRule::Action::accent:    action = "accent"; break;
        case ArpRule::Action::ratchet:   action = "ratchet x" + std::to_string (std::clamp (r.amount, 1, 4)); break;
        case ArpRule::Action::passing:   action = "passing tone " + sign (r.amount) + " scale step" + (std::abs (r.amount) == 1 ? "" : "s"); break;
    }

    return std::string (r.enabled ? "" : "(off) ") + "On " + when + ": " + action;
}

// Rhythm pattern + grammar rules: edited on the message thread, published to the audio thread as an immutable copy.
struct ArpProgram
{
    static constexpr int maxSteps = 16, maxRules = 8;

    int length = maxSteps;
    std::array<ArpStep, maxSteps> steps {};
    std::array<ArpRule, maxRules> rules {};
    int numRules = 0;

    // Euclidean rhythm: `pulses` hits spread as evenly as possible over `count` steps, rotated left by `rotation`.
    void euclid (int pulses, int count, int rotation)
    {
        length = std::clamp (count, 1, maxSteps);
        pulses = std::clamp (pulses, 0, length);

        for (int i = 0; i < length; ++i)
        {
            const auto j = ((i + rotation) % length + length) % length;
            steps[(size_t) i] = { (j * pulses) % length < pulses, false, 1 };
        }
    }

    bool addRule (const ArpRule& r)
    {
        if (numRules >= maxRules)
            return false;

        rules[(size_t) numRules++] = r;
        return true;
    }
};

// Real-time-safe arpeggiator: fixed-size storage, no allocation. Timing is computed from the beat position of each
// sample, so it locks to the host grid; note-offs are scheduled in samples, so loops/seeks can never strand a note.
// Each grid step is decided once (cached by grid index), so ratchets and humanised timing that cross block boundaries
// stay consistent. Events reach a sink: sink (sampleOffset, note, velocity), velocity 0 = note-off. Within one
// process() call events are not guaranteed to be in sample order (sort by offset, note-offs first at equal offsets).
class Arpeggiator
{
public:
    static constexpr int maxHeld = 32, maxSounding = 64, maxPhrase = 64;

    void setSettings (const ArpSettings& s) noexcept
    {
        dirty = dirty || s.direction != settings.direction || s.octaves != settings.octaves;
        settings = s;
        settings.octaves = std::clamp (settings.octaves, 1, 4);
        settings.repeats = std::clamp (settings.repeats, 1, 4);
        settings.gate = std::clamp (settings.gate, 0.05f, 1.0f);
        settings.swing = std::clamp (settings.swing, 0.5f, 0.75f);
        settings.stepBeats = std::max (settings.stepBeats, 1.0 / 64.0);
        settings.phraseLength = std::clamp (settings.phraseLength, 0, maxPhrase);
        settings.variation = std::clamp (settings.variation, 0.0f, 1.0f);
        settings.humanizeBeats = std::clamp (settings.humanizeBeats, 0.0, 0.45 * settings.stepBeats);
    }

    // The program pointer must stay valid until the next call (the processor sets it every block).
    void setProgram (const ArpProgram* p) noexcept { program = p; }

    const ArpSettings& getSettings() const noexcept { return settings; }
    int heldCount() const noexcept     { return numHeld; }
    int soundingCount() const noexcept { return sounding.count(); }

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
        const auto first = (int64_t) std::floor (beatAtFrom / s) - 2;
        const auto last = (int64_t) std::floor (beatAtTo / s) + 1;

        for (auto k = first; k <= last; ++k)
        {
            const auto base = stepStart (k) + settings.humanizeBeats * unit (settings.seed, (uint32_t) k, 0x51, 0);

            if (k != decision.k)
            {
                if (base < beatAtFrom || base >= beatAtTo || seqLength == 0)
                    continue;

                decide (k);
            }

            if (decision.rest)
                continue;

            for (int j = 0; j < decision.ratchet; ++j)
            {
                const auto hit = base + j * s / decision.ratchet;

                if (hit < beatAtFrom || hit >= beatAtTo)
                    continue;

                const auto offset = std::clamp (from + (int) std::llround ((hit - beatAtFrom) / beatsPerSample), from, to - 1);
                sounding.releaseUntil (offset, true, [&] (int o, int n) { sink (o, n, 0.0f); });
                emitHit (offset, beatsPerSample, sink);
            }
        }

        sounding.releaseUntil (to, false, [&] (int o, int n) { sink (o, n, 0.0f); });
    }

    // Call once per block after process(): advances the sample clock used for note-off scheduling.
    void endBlock (int numSamples) noexcept { sounding.endBlock (numSamples); }

    // Emits note-offs for everything sounding (at `offset`). Held notes are kept unless clearHeld.
    template <typename Sink>
    void flush (int offset, Sink&& sink, bool clearHeld) noexcept
    {
        sounding.flush (offset, [&] (int o, int n) { sink (o, n, 0.0f); });

        if (clearHeld)
        {
            numHeld = physicallyHeld = 0;
            dirty = true;
        }
    }

private:
    struct Held { int note; float velocity; uint32_t order; };
    struct Memory { int element; bool rest, accent; int ratchet; };

    struct Decision
    {
        int64_t k = std::numeric_limits<int64_t>::min();
        bool rest = true;
        int ratchet = 1;
        std::array<int, maxHeld> notes {};
        int numNotes = 0;
        float velocity = 0.0f;
    };

    static uint32_t hash (uint32_t a, uint32_t b, uint32_t c, uint32_t d) noexcept
    {
        uint64_t x = ((uint64_t) a << 32 | b) ^ ((uint64_t) c << 32 | d) * 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return (uint32_t) (x ^ (x >> 31));
    }

    static float unit (uint32_t a, uint32_t b, uint32_t c, uint32_t d) noexcept
    {
        return (float) (hash (a, b, c, d) >> 8) / 16777216.0f;
    }

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
        stepCounter = 0;
        memoryValid = false;
        decision.k = std::numeric_limits<int64_t>::min();
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
        }
        else if (dir == ArpDirection::octaveCycle)
        {
            for (int i = 0; i < numHeld; ++i)
                for (int o = 0; o < octaves; ++o)
                    push (byPitch[(size_t) i].note + 12 * o);
        }
        else
        {
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
        }

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

    // Walks (or draws) the next pattern element.
    int chooseElement() noexcept
    {
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

        return element;
    }

    // Moves a note by `steps` scale degrees (chromatic when the scale is chromatic).
    int passingTone (int note, int steps) const noexcept
    {
        const auto dir = steps > 0 ? 1 : -1;

        for (int moved = 0; moved != steps && note > 0 && note < 127;)
        {
            note += dir;

            if (inScale (note, settings.scaleMask, settings.scaleRoot))
                moved += dir;
        }

        return note;
    }

    // Decides grid step k: pattern cell -> phrase memory (record / replay with variation) -> grammar rules -> notes.
    void decide (int64_t k) noexcept
    {
        const auto counter = stepCounter++;
        const auto patternLength = program != nullptr ? std::clamp (program->length, 1, ArpProgram::maxSteps) : 1;
        const auto cell = program != nullptr ? program->steps[(size_t) (counter % patternLength)] : ArpStep {};
        const auto phraseLength = settings.phraseLength;
        const auto phraseStep = phraseLength > 0 ? counter % phraseLength : counter;
        const auto cycle = phraseLength > 0 ? counter / phraseLength : 0;

        auto rest = ! cell.on;
        auto accent = cell.accent;
        auto ratchet = std::clamp (cell.ratchet, 1, 4);
        auto velocityScale = 1.0f;
        int element = 0;

        if (phraseLength > 0 && cycle > 0 && memoryValid)
        {
            // Phrase Memory replay: the recorded phrase, varied per attribute with probability `variation`.
            const auto& m = memory[(size_t) phraseStep];
            element = m.element % seqLength;
            rest = m.rest;
            accent = m.accent;
            ratchet = m.ratchet;
            const auto original = settings.returnEvery > 0 && cycle % settings.returnEvery == 0;
            const auto v = original ? 0.0f : settings.variation;
            auto chance = [&] (uint32_t attribute) { return unit (settings.seed, (uint32_t) cycle, (uint32_t) phraseStep, attribute); };

            if (settings.varyPitch && chance (1) < v)
                element = (int) (hash (settings.seed, (uint32_t) cycle, (uint32_t) phraseStep, 2) % (uint32_t) seqLength);

            if (settings.varyVelocity && chance (3) < v)
                velocityScale = 0.55f + 0.9f * chance (4);

            if (settings.varyRhythm && chance (5) < v)
            {
                if (rest)               rest = false;
                else if (chance (6) < 0.5f) rest = true;
                else                    ratchet = ratchet == 1 ? 2 : 1;
            }
        }
        else
        {
            // A pattern rest does not use up a note; a probability skip does (the note is replaced by silence).
            if (! rest)
            {
                element = chooseElement();

                if (settings.probability < 1.0f && (float) (nextRandom() % 10000) / 10000.0f >= settings.probability)
                    rest = true;
            }

            if (phraseLength > 0 && cycle == 0)
            {
                memory[(size_t) phraseStep] = { element, rest, accent, ratchet };
                memoryValid = phraseStep == phraseLength - 1;
            }
        }

        // Arpeggio Grammar.
        int transpose = 0, passing = 0;

        if (program != nullptr)
        {
            for (int r = 0; r < std::min (program->numRules, ArpProgram::maxRules); ++r)
            {
                const auto& rule = program->rules[(size_t) r];
                const auto n = std::max (1, rule.n);

                if (! rule.enabled)
                    continue;

                const auto cycleLength = phraseLength > 0 ? phraseLength : patternLength;
                const auto matches = rule.when == ArpRule::When::every     ? phraseStep % n == rule.offset % n
                                   : rule.when == ArpRule::When::range     ? phraseStep % n >= rule.offset
                                   : rule.when == ArpRule::When::phraseEnd ? counter % cycleLength >= cycleLength - n
                                   : hash (settings.seed, (uint32_t) counter, (uint32_t) r, 7) % 100 < (uint32_t) n;

                if (! matches)
                    continue;

                switch (rule.action)
                {
                    case ArpRule::Action::transpose: transpose += rule.amount; break;
                    case ArpRule::Action::rest:      rest = true; break;
                    case ArpRule::Action::accent:    accent = true; break;
                    case ArpRule::Action::ratchet:   ratchet = std::clamp (rule.amount, 1, 4); break;
                    case ArpRule::Action::passing:   passing += rule.amount; break;
                }
            }
        }

        decision.k = k;
        decision.rest = rest;
        decision.ratchet = ratchet;
        decision.numNotes = 0;

        if (rest)
            return;

        auto add = [&] (int note)
        {
            note = passingTone (note + transpose, passing);

            if (note >= 0 && note <= 127 && decision.numNotes < maxHeld)
                decision.notes[(size_t) decision.numNotes++] = note;
        };

        if (settings.direction == ArpDirection::chord)
            for (int i = 0; i < numHeld; ++i)
                add (held[(size_t) i].note + seq[(size_t) element]);
        else
            add (seq[(size_t) element]);

        lastNote = seq[(size_t) element];

        auto velocity = velocityOf (seq[(size_t) element] % 128) * velocityScale * (accent ? 1.0f + settings.accentAmount : 1.0f);

        if (settings.humanizeVelocity > 0.0f)
            velocity *= 1.0f + settings.humanizeVelocity * (2.0f * unit (settings.seed, (uint32_t) k, 0x77, 0) - 1.0f);

        decision.velocity = std::clamp (velocity, 0.01f, 1.0f);
    }

    template <typename Sink>
    void emitHit (int offset, double beatsPerSample, Sink& sink) noexcept
    {
        const auto length = (int64_t) std::llround (settings.gate * settings.stepBeats / decision.ratchet / beatsPerSample);

        for (int n = 0; n < decision.numNotes; ++n)
        {
            sounding.start (decision.notes[(size_t) n], offset, length, [&] (int o, int note) { sink (o, note, 0.0f); });
            sink (offset, decision.notes[(size_t) n], decision.velocity);
        }
    }

    ArpSettings settings;
    const ArpProgram* program = nullptr;
    std::array<Held, maxHeld> held {};
    std::array<int, maxHeld * 4 * 2> seq {};
    NoteTracker<maxSounding> sounding;
    std::array<Memory, maxPhrase> memory {};
    Decision decision;
    int numHeld = 0, physicallyHeld = 0, seqLength = 0, position = 0, repeatCount = 0, lastNote = -1;
    int stepCounter = 0;
    bool memoryValid = false;
    uint32_t orderCounter = 0, rng = 1;
    bool dirty = false;
};

} // namespace neuro
