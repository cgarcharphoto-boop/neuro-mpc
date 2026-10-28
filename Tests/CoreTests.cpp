#include "Core/Arpeggiator.h"
#include "Core/KeyDetector.h"
#include "Core/PadBank.h"
#include "Core/PitchDetector.h"
#include "Core/RtShared.h"
#include "Core/Scales.h"
#include "Core/SliceSet.h"
#include "Core/TransientDetector.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>

static int failures = 0;

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

using namespace neuro;

static void sliceSetTests()
{
    SliceSet s (10000);
    CHECK (s.size() == 1 && s.getSlices()[0].endSample == 10000);

    CHECK (s.split (4000));
    CHECK (s.split (7000));
    CHECK (s.size() == 3);
    CHECK (s.indexAt (0) == 0 && s.indexAt (3999) == 0 && s.indexAt (4000) == 1 && s.indexAt (9999) == 2);
    CHECK (s.indexAt (10000) == -1 && s.indexAt (-1) == -1);

    // Too close to an existing boundary or the ends.
    CHECK (! s.split (4010));
    CHECK (! s.split (10));
    CHECK (s.size() == 3);

    // Ids are stable and unique across edits.
    auto id1 = s.getSlices()[1].id;
    CHECK (s.moveStart (1, 5000));
    CHECK (s.getSlices()[1].id == id1 && s.getSlices()[0].endSample == 5000 && s.getSlices()[1].startSample == 5000);

    // Moves clamp so neighbours keep the minimum length.
    CHECK (s.moveStart (1, 9999));
    CHECK (s.getSlices()[1].startSample == 7000 - SliceSet::minSliceLength);
    CHECK (! s.moveStart (0, 100));

    CHECK (s.mergeWithPrevious (2));
    CHECK (s.size() == 2 && s.getSlices()[1].endSample == 10000);

    s.grid (8);
    CHECK (s.size() == 8);

    for (int i = 0; i < s.size(); ++i)
    {
        CHECK (s.getSlices()[(size_t) i].startSample == 10000LL * i / 8);
        if (i > 0) CHECK (s.getSlices()[(size_t) i - 1].endSample == s.getSlices()[(size_t) i].startSample);
    }

    s.setStarts ({ 9000, 3000, 3010, 0, 20000 });
    CHECK (s.size() == 3); // 3010 too close, 0 and 20000 out of range

    // Stretch is per-slice and clamped.
    SliceSet st (10000);
    st.grid (4);
    CHECK (st.setStretch (1, 2.0f) && st.getSlices()[1].isStretched() && ! st.getSlices()[0].isStretched());
    CHECK (st.getSlices()[0].timeStretchRatio == 1.0f && st.getSlices()[2].timeStretchRatio == 1.0f);
    CHECK (st.setStretch (2, 100.0f) && st.getSlices()[2].timeStretchRatio == SliceSet::maxStretch);
    CHECK (st.setStretch (2, 0.0f) && st.getSlices()[2].timeStretchRatio == SliceSet::minStretch);
    CHECK (! st.setStretch (4, 2.0f));
    auto bad = st.getSlices();
    bad[3].timeStretchRatio = -5.0f;
    CHECK (st.restore (10000, bad) && st.getSlices()[3].timeStretchRatio == SliceSet::minStretch);

    // Settings inherited on split.
    SliceSet t (1000);
    auto copy = t.getSlices();
    copy[0].pitchSemitones = 7.0f;
    CHECK (t.restore (1000, copy));
    CHECK (t.split (500) && t.getSlices()[1].pitchSemitones == 7.0f && t.getSlices()[1].id != t.getSlices()[0].id);

    // Restore round-trip and rejection of invalid data.
    auto saved = t.getSlices();
    SliceSet r;
    CHECK (r.restore (1000, saved) && r.size() == 2 && r.getSlices()[1].id == saved[1].id);
    CHECK (r.split (800) && r.getSlices()[2].id > saved[1].id);
    saved[1].startSample = 600; // gap
    CHECK (! r.restore (1000, saved) && r.size() == 1);
    CHECK (! r.restore (999, t.getSlices()));
}

static void transientTests()
{
    constexpr double sr = 44100.0;
    constexpr int64_t n = (int64_t) (sr * 2);
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    const int64_t hits[] = { 22050, 44100, 66150 };

    for (auto h : hits)
        for (int64_t i = 0; i < 4000; ++i)
        {
            auto v = 0.8f * noise (rng) * std::exp (-(float) i / 800.0f);
            l[(size_t) (h + i)] = v;
            r[(size_t) (h + i)] = v * 0.5f;
        }

    const float* ch[] = { l.data(), r.data() };
    auto onsets = detectTransients (ch, 2, n, sr, 0.6f);
    CHECK (onsets.size() == 3);

    for (size_t i = 0; i < std::min<size_t> (3, onsets.size()); ++i)
        CHECK (std::llabs (onsets[i] - hits[i]) <= 256);

    std::vector<float> silence ((size_t) n, 0.0f);
    const float* sil[] = { silence.data() };
    CHECK (detectTransients (sil, 1, n, sr, 1.0f).empty());

    std::vector<float> sine ((size_t) n);
    for (int64_t i = 0; i < n; ++i) sine[(size_t) i] = 0.5f * std::sin ((float) i * 0.05f);
    const float* sn[] = { sine.data() };
    CHECK (detectTransients (sn, 1, n, sr, 0.6f).empty());

    CHECK (detectTransients (nullptr, 0, 0, sr, 0.5f).empty());
}

static void rtSharedTests()
{
    struct Payload { int a = 0, b = 0; };
    RtShared<Payload> shared;
    CHECK (shared.acquire() == nullptr);

    shared.publish (std::make_shared<const Payload> (Payload { 1, -1 }));
    auto* first = shared.acquire();
    CHECK (first != nullptr && first->a == 1);

    shared.publish (std::make_shared<const Payload> (Payload { 2, -2 }));
    CHECK (shared.poolSize() == 2); // first is still in use by the "audio thread"
    shared.acquire();
    shared.collectGarbage();
    CHECK (shared.poolSize() == 1);

    // Stress: reader must always see a consistent, live object while the writer publishes and frees.
    std::atomic<bool> stop { false }, corrupt { false };
    std::thread reader ([&]
    {
        while (! stop.load())
            if (auto* p = shared.acquire(); p != nullptr && p->a != -p->b)
                corrupt = true;
    });

    for (int i = 3; i < 200000; ++i)
        shared.publish (std::make_shared<const Payload> (Payload { i, -i }));

    stop = true;
    reader.join();
    CHECK (! corrupt.load());
    CHECK (shared.poolSize() <= 2);
}

namespace
{
    constexpr double sr = 48000.0;
    constexpr double twoPi = 6.283185307179586;

    // Sum of harmonic tones (MIDI notes) with 1/k harmonic rolloff, for `seconds`.
    std::vector<float> tones (std::initializer_list<double> notes, double seconds, int harmonics = 4)
    {
        std::vector<float> x ((size_t) (seconds * sr), 0.0f);

        for (auto note : notes)
            for (int h = 1; h <= harmonics; ++h)
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] += 0.2f / (float) h * (float) std::sin (twoPi * midiToFrequency (note) * h * (double) i / sr);

        return x;
    }

    std::vector<float> sequence (std::initializer_list<std::initializer_list<double>> chords, double secondsEach)
    {
        std::vector<float> out;

        for (auto c : chords)
        {
            auto part = tones (c, secondsEach);
            out.insert (out.end(), part.begin(), part.end());
        }

        return out;
    }

    PitchEstimate pitchOf (const std::vector<float>& x)
    {
        const float* ch[] = { x.data() };
        return detectPitch (ch, 1, (int64_t) x.size(), sr);
    }

    KeyEstimate keyOf (const std::vector<float>& x)
    {
        const float* ch[] = { x.data() };
        return detectKey (ch, 1, (int64_t) x.size(), sr);
    }

    const char* statusName (PitchEstimate::Status s)
    {
        return s == PitchEstimate::Status::pitched ? "pitched" : s == PitchEstimate::Status::uncertain ? "uncertain"
             : s == PitchEstimate::Status::unpitched ? "unpitched" : "tooShort";
    }
}

static void pitchTests()
{
    for (double note : { 33.0, 45.0, 57.0, 69.0, 81.0, 93.0, 69.3 }) // A1..A6 and A4 +30 cents
    {
        auto p = pitchOf (tones ({ note }, 1.0, 1));
        std::printf ("  sine %.1f (%.1f Hz): %s midi %.3f conf %.2f\n", note, midiToFrequency (note), statusName (p.status), p.midi, p.confidence);
        CHECK (p.status == PitchEstimate::Status::pitched && ! p.ambiguous && std::abs (p.midi - note) < 0.05 && p.confidence > 0.9f);
    }

    auto harmonic = pitchOf (tones ({ 55.0 }, 1.0, 8)); // G3 with 8 harmonics
    CHECK (harmonic.status == PitchEstimate::Status::pitched && harmonic.nearestNote() == 55 && std::abs (harmonic.cents()) < 5.0);

    // Missing fundamental but strong 2nd harmonic (e.g. bass through small speakers) is still a single tone.
    {
        auto x = tones ({ 45.0 }, 1.0, 1);
        for (auto& v : x) v *= 0.1f;
        auto upper = tones ({ 57.0 }, 1.0, 1); // octave = 2nd harmonic
        auto third = tones ({ 64.02 }, 1.0, 1); // 3rd harmonic (A2 * 3 = E4 + 2 cents)
        for (size_t i = 0; i < x.size(); ++i) x[i] += upper[i] + 0.5f * third[i];
        auto weak = pitchOf (x);
        std::printf ("  weak fundamental A2: %s midi %.2f conf %.2f\n", statusName (weak.status), weak.midi, weak.confidence);
        CHECK (weak.status == PitchEstimate::Status::pitched && weak.nearestNote() == 45);
    }

    auto detuned = pitchOf (tones ({ 69.3 }, 1.0));
    CHECK (detuned.nearestNote() == 69 && std::abs (detuned.cents() - 30.0) < 3.0);

    std::vector<float> noise ((size_t) sr);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    for (auto& v : noise) v = u (rng);
    auto pn = pitchOf (noise);
    std::printf ("  noise: %s conf %.2f\n", statusName (pn.status), pn.confidence);
    CHECK (pn.status == PitchEstimate::Status::unpitched || pn.status == PitchEstimate::Status::uncertain);
    CHECK (pn.confidence < 0.6f);

    CHECK (pitchOf (std::vector<float> ((size_t) sr, 0.0f)).status == PitchEstimate::Status::unpitched);
    CHECK (pitchOf (tones ({ 69.0 }, 0.01)).status == PitchEstimate::Status::tooShort);

    for (auto chord : { std::vector<double> { 57.0, 61.0, 64.0 }, std::vector<double> { 69.0, 70.0 }, std::vector<double> { 60.0, 66.0 } })
    {
        std::vector<float> x ((size_t) sr, 0.0f);
        for (auto note : chord) { auto t = tones ({ note }, 1.0); for (size_t i = 0; i < x.size(); ++i) x[i] += t[i]; }
        auto pc = pitchOf (x);
        std::printf ("  chord (%zu notes from %.0f): %s%s midi %.2f conf %.2f\n", chord.size(), chord[0], statusName (pc.status),
                     pc.ambiguous ? " (ambiguous)" : "", pc.midi, pc.confidence);
        CHECK (pc.status != PitchEstimate::Status::pitched && pc.ambiguous);
    }
}

static void scaleTests()
{
    const auto major = scales[1].mask, minorPent = scales[6].mask;
    CHECK (snapToScale (61, scales[0].mask, 0) == 61);      // chromatic: unchanged
    CHECK (snapToScale (61, major, 0) == 60);               // C# -> C (tie resolves down)
    CHECK (snapToScale (66, major, 0) == 65);               // F# -> F
    CHECK (snapToScale (64, major, 0) == 64);
    CHECK (snapToScale (63, major, 2) == 62);               // D major: Eb -> D
    CHECK (snapToScale (61, minorPent, 9) == 60);           // A minor pentatonic: C# -> C
    CHECK (snapToScale (59, minorPent, 9) == 60);           // B -> C (A or C: tie down would be... B-1=Bb no, B+1=C yes)

    for (int note = 0; note < 128; ++note)
        for (const auto& sc : scales)
            CHECK (inScale (snapToScale (note, sc.mask, 4), sc.mask, 4));

    // 12-note selector: root A3 (57), no shift; choosing E -> +7 (nearest), C -> +3, A -> 0; fine part kept.
    CHECK (shiftForPitchClass (57.0f, 0.0f, 4) == -5.0f);   // E: -5 is nearer than +7
    CHECK (shiftForPitchClass (57.0f, 0.0f, 0) == 3.0f);
    CHECK (shiftForPitchClass (57.0f, 0.0f, 9) == 0.0f);
    CHECK (shiftForPitchClass (57.0f, 12.25f, 0) == 15.25f); // stays in the current octave, keeps +25 c
    CHECK (shiftForPitchClass (57.3f, 0.0f, 9) == 0.0f);     // detuned root still maps to A

    SliceSet s (10000);
    CHECK (s.setPitch (0, 30.0f) && s.getSlices()[0].pitchSemitones == SliceSet::maxPitch);
    CHECK (s.setPitch (0, -7.5f) && s.getSlices()[0].pitchSemitones == -7.5f);
    CHECK (! s.getSlices()[0].needsRender());               // resample mode
    CHECK (s.setPitchMode (0, PitchMode::keepLength) && s.getSlices()[0].needsRender() && s.getSlices()[0].renderedPitch() == -7.5f);
}

static void padTests()
{
    SliceSet slices (10000);
    slices.grid (4);
    PadBank pads;
    CHECK (pads.followSlices && pads[0].note == 36 && pads[63].note == 99);

    pads.autoAssign (slices);
    CHECK (pads[0].sliceId == slices.getSlices()[0].id && pads[3].sliceId == slices.getSlices()[3].id && pads[4].sliceId == 0);

    // Assignment by id survives slice edits that do not remove the slice.
    pads.assign (5, slices.getSlices()[2].id);
    CHECK (! pads.followSlices);
    const auto id = pads[5].sliceId;
    slices.split (100);
    CHECK (slices.indexOfId (id) == 3 && pads[5].sliceId == id);

    // Copy keeps the target's note; duplicate fills the next empty pad.
    pads[5].gain = 0.5f;
    pads[5].chokeGroup = 2;
    pads.copy (5, 6);
    CHECK (pads[6].sliceId == id && pads[6].gain == 0.5f && pads[6].chokeGroup == 2 && pads[6].note == 42);
    CHECK (pads.duplicate (6) == 7 && pads[7].sliceId == id && pads[7].note == 43);
    pads.clear (7);
    CHECK (pads[7].sliceId == 0 && pads[7].note == 43 && pads[7].gain == 1.0f);

    // Velocity response.
    PadPlay play;
    play.gain = 0.5f;
    CHECK (std::abs (play.levelFor (0.4f) - 0.2f) < 1e-6f);
    play.velocitySensitivity = 0.0f;
    CHECK (std::abs (play.levelFor (0.1f) - 0.5f) < 1e-6f);
    CHECK (pads.playMap()[6].chokeGroup == 2);
}

namespace
{
    struct ArpEvent { int64_t sample; int note; float velocity; };

    // Runs the arp in free time at 120 BPM / 48 kHz (1 beat = 24000 samples). keys: (sample, note, velocity; 0 = off).
    // beatJump: if > 0, the beat position wraps every `beatJump` beats (a host loop).
    std::vector<ArpEvent> runArp (const ArpSettings& settings, std::vector<std::tuple<int64_t, int, float>> keys,
                                  int64_t totalSamples, int blockSize = 512, double beatJump = 0.0,
                                  const ArpProgram* program = nullptr)
    {
        Arpeggiator arp;
        arp.setSettings (settings);
        arp.setProgram (program);
        std::vector<ArpEvent> events;
        const double bps = 2.0 / 48000.0;
        size_t nextKey = 0;

        for (int64_t blockStart = 0; blockStart < totalSamples; blockStart += blockSize)
        {
            auto sink = [&] (int offset, int note, float vel) { events.push_back ({ blockStart + offset, note, vel }); };
            const auto beatOf = [&] (int64_t sample) { auto b = sample * bps; return beatJump > 0.0 ? std::fmod (b, beatJump) : b; };
            int from = 0;

            while (nextKey < keys.size() && std::get<0> (keys[nextKey]) < blockStart + blockSize)
            {
                auto [at, note, vel] = keys[nextKey++];
                const auto offset = (int) (at - blockStart);
                arp.process (from, offset, beatOf (blockStart + from), bps, sink);
                from = offset;
                if (vel > 0.0f) arp.noteOn (note, vel); else arp.noteOff (note);
            }

            // A host loop jump happens at the block boundary, never inside a block.
            arp.process (from, blockSize, beatOf (blockStart) + from * bps, bps, sink);
            arp.endBlock (blockSize);
        }

        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b)
                          { return a.sample != b.sample ? a.sample < b.sample : (a.velocity == 0.0f) > (b.velocity == 0.0f); });
        return events;
    }

    std::vector<int> onNotes (const std::vector<ArpEvent>& ev, size_t count = 64)
    {
        std::vector<int> notes;
        for (auto& e : ev) if (e.velocity > 0.0f && notes.size() < count) notes.push_back (e.note);
        return notes;
    }

    // Every on has exactly one later off, no off without an on, nothing left sounding.
    bool balanced (const std::vector<ArpEvent>& ev)
    {
        std::array<int, 128> open {};
        for (auto& e : ev)
        {
            if (e.velocity > 0.0f) { if (open[(size_t) e.note]++ != 0) return false; }
            else if (open[(size_t) e.note]-- != 1) return false;
        }
        return std::all_of (open.begin(), open.end(), [] (int n) { return n == 0; });
    }
}

static void arpTests()
{
    ArpSettings s;
    const std::vector<std::tuple<int64_t, int, float>> cegHeld { { 0, 60, 0.8f }, { 0, 64, 0.8f }, { 0, 67, 0.8f } };
    auto withRelease = [] (std::vector<std::tuple<int64_t, int, float>> k, int64_t at)
    {
        for (int n : { 60, 64, 67 }) k.emplace_back (at, n, 0.0f);
        return k;
    };

    // Up, 1/16 at 120 BPM: a step every 6000 samples, gate 50% -> offs 3000 later.
    auto ev = runArp (s, withRelease (cegHeld, 100000), 150000);
    CHECK ((onNotes (ev, 5) == std::vector<int> { 60, 64, 67, 60, 64 }));
    CHECK (ev[0].sample == 0 && ev[0].velocity > 0.0f && ev[1].sample == 3000 && ev[1].velocity == 0.0f);
    CHECK (ev[2].sample == 6000 && ev[4].sample == 12000);
    CHECK (balanced (ev));

    auto order = [&] (ArpDirection d, int octaves = 1, std::vector<std::tuple<int64_t, int, float>> keys = {})
    {
        ArpSettings t;
        t.direction = d;
        t.octaves = octaves;
        return onNotes (runArp (t, keys.empty() ? cegHeld : keys, 60000), 8);
    };

    CHECK ((order (ArpDirection::down) == std::vector<int> { 67, 64, 60, 67, 64, 60, 67, 64 }));
    CHECK ((order (ArpDirection::upDown) == std::vector<int> { 60, 64, 67, 64, 60, 64, 67, 64 }));
    CHECK ((order (ArpDirection::downUp) == std::vector<int> { 67, 64, 60, 64, 67, 64, 60, 64 }));
    const std::vector<std::tuple<int64_t, int, float>> played { { 0, 67, 0.8f }, { 0, 60, 0.8f }, { 0, 64, 0.8f } };
    CHECK ((order (ArpDirection::asPlayed, 1, played) == std::vector<int> { 67, 60, 64, 67, 60, 64, 67, 60 }));
    CHECK ((order (ArpDirection::reversePlayed, 1, played) == std::vector<int> { 64, 60, 67, 64, 60, 67, 64, 60 }));
    CHECK ((order (ArpDirection::up, 2) == std::vector<int> { 60, 64, 67, 72, 76, 79, 60, 64 }));
    CHECK ((order (ArpDirection::octaveCycle, 2) == std::vector<int> { 60, 72, 64, 76, 67, 79, 60, 72 }));

    // Chord: all notes together, next step an octave up.
    {
        ArpSettings t;
        t.direction = ArpDirection::chord;
        t.octaves = 2;
        auto c = runArp (t, cegHeld, 20000);
        std::vector<int64_t> at;
        for (auto& e : c) if (e.velocity > 0.0f) at.push_back (e.sample);
        CHECK (at.size() >= 6 && at[0] == 0 && at[2] == 0 && at[3] == 6000);
        CHECK ((onNotes (c, 6) == std::vector<int> { 60, 64, 67, 72, 76, 79 }));
    }

    // Random: reproducible per seed, differs across seeds; no-repeat never repeats consecutively.
    {
        ArpSettings t;
        t.direction = ArpDirection::random;
        t.octaves = 2;
        const auto a = onNotes (runArp (t, cegHeld, 600000), 100), b = onNotes (runArp (t, cegHeld, 600000), 100);
        t.seed = 99;
        const auto c = onNotes (runArp (t, cegHeld, 600000), 100);
        CHECK (a == b && a != c && a.size() == 100);
        t.direction = ArpDirection::randomNoRepeat;
        const auto d = onNotes (runArp (t, cegHeld, 600000), 100);
        CHECK (std::adjacent_find (d.begin(), d.end()) == d.end());
    }

    // Probability 50%: about half the steps play, repeatably.
    {
        ArpSettings t;
        t.probability = 0.5f;
        const auto a = onNotes (runArp (t, cegHeld, 6000 * 400), 1000);
        CHECK (a.size() > 160 && a.size() < 240 && a == onNotes (runArp (t, cegHeld, 6000 * 400), 1000));
    }

    // Swing 66%: the 2nd step of each pair lands at 0.66 of the pair (2 x 6000 x 0.66 = 7920).
    {
        ArpSettings t;
        t.swing = 0.66f;
        std::vector<int64_t> at;
        for (auto& e : runArp (t, cegHeld, 30000)) if (e.velocity > 0.0f) at.push_back (e.sample);
        CHECK (at.size() >= 4 && at[1] == 7920 && at[2] == 12000 && at[3] == 19920);
    }

    // Repeats, gate, fixed velocity, 1/8 triplets.
    {
        ArpSettings t;
        t.repeats = 2;
        t.gate = 1.0f;
        t.fixedVelocity = 127;
        t.stepBeats = 1.0 / 3.0; // 1/8T = 8000 samples
        auto r = runArp (t, cegHeld, 50000);
        CHECK ((onNotes (r, 6) == std::vector<int> { 60, 60, 64, 64, 67, 67 }));
        CHECK (r[0].velocity == 1.0f && r[1].sample == 8000);
    }

    // Retrigger: a new phrase after silence restarts the pattern; latch keeps playing and is replaced by a new chord.
    {
        ArpSettings t;
        auto k = withRelease (cegHeld, 9000); // plays 60, 64 then stops
        k.emplace_back (30000, 60, 0.8f);
        k.emplace_back (30000, 64, 0.8f);
        auto r = runArp (t, k, 40000);
        CHECK ((onNotes (r) == std::vector<int> { 60, 64, 60, 64 }) && balanced (r)); // restarts at 60; gated offs

        t.latch = true;
        auto l = runArp (t, { { 0, 60, 0.8f }, { 0, 64, 0.8f }, { 1000, 60, 0.0f }, { 1000, 64, 0.0f },
                              { 40000, 67, 0.8f }, { 41000, 67, 0.0f } }, 60000);
        const auto n = onNotes (l);
        // Latched C/E keeps playing after release (steps 0..36000), then the new G replaces it from the 42000 step.
        CHECK ((std::vector<int> (n.begin(), n.begin() + 9) == std::vector<int> { 60, 64, 60, 64, 60, 64, 60, 67, 67 }));
    }

    // Robustness: balanced on/off across a 1-beat host loop and with any block size; block size never changes timing.
    {
        auto looped = runArp (s, withRelease (cegHeld, 200000), 260000, 512, 1.0);
        CHECK (balanced (looped) && onNotes (looped).size() > 30);
        auto small = runArp (s, withRelease (cegHeld, 100000), 150000, 37);
        auto large = runArp (s, withRelease (cegHeld, 100000), 150000, 4096);
        CHECK (small.size() == large.size() && balanced (small) && balanced (large));
        bool same = small.size() == large.size();
        for (size_t i = 0; same && i < small.size(); ++i)
            same = small[i].sample == large[i].sample && small[i].note == large[i].note;
        CHECK (same);
    }
}

static void arpGenerativeTests()
{
    const std::vector<std::tuple<int64_t, int, float>> ceg { { 0, 60, 0.5f }, { 0, 64, 0.5f }, { 0, 67, 0.5f } };
    auto ons = [] (const std::vector<ArpEvent>& ev)
    {
        std::vector<std::pair<int64_t, int>> v;
        for (auto& e : ev) if (e.velocity > 0.0f) v.emplace_back (e.sample, e.note);
        return v;
    };

    // Pattern rests skip time but not notes: x . x x -> 60 @0, 64 @12000, 67 @18000, 60 @24000.
    {
        ArpProgram prog;
        prog.length = 4;
        prog.steps[1].on = false;
        auto v = ons (runArp ({}, ceg, 30000, 512, 0.0, &prog));
        CHECK ((v.size() >= 4 && v[0] == std::pair<int64_t, int> { 0, 60 } && v[1] == std::pair<int64_t, int> { 12000, 64 }
                && v[2] == std::pair<int64_t, int> { 18000, 67 } && v[3] == std::pair<int64_t, int> { 24000, 60 }));
    }

    // Accent boosts velocity; ratchet x3 = three hits per step with a third of the gate each.
    {
        ArpProgram prog;
        prog.length = 2;
        prog.steps[0].accent = true;
        prog.steps[1].ratchet = 3;
        ArpSettings s;
        s.accentAmount = 0.5f;
        auto ev = runArp (s, ceg, 11000, 512, 0.0, &prog); // whole blocks: stay clear of the 12000 step
        std::vector<ArpEvent> on;
        for (auto& e : ev) if (e.velocity > 0.0f) on.push_back (e);
        CHECK (on.size() == 4 && std::abs (on[0].velocity - 0.75f) < 1e-5f && std::abs (on[1].velocity - 0.5f) < 1e-5f);
        CHECK (on[1].sample == 6000 && on[2].sample == 8000 && on[3].sample == 10000 && on[1].note == on[3].note);
        CHECK (ev.size() > 2 && ev[2].sample == 6000 && ev[3].sample == 7000); // ratchet gate: 6000 x 0.5 / 3
    }

    // Euclid (3, 8) = x..x..x.
    {
        ArpProgram prog;
        prog.euclid (3, 8, 0);
        std::string p;
        for (int i = 0; i < prog.length; ++i) p += prog.steps[(size_t) i].on ? 'x' : '.';
        CHECK (p == "x..x..x." && prog.length == 8);
        prog.euclid (5, 8, 1);
        int pulses = 0;
        for (int i = 0; i < 8; ++i) pulses += prog.steps[(size_t) i].on ? 1 : 0;
        CHECK (pulses == 5);
    }

    // Grammar: octave jump on the 2nd of every 2 steps; passing tone in C major; phrase-end ratchet; chance rest.
    {
        ArpProgram prog;
        prog.addRule ({ ArpRule::When::every, 2, 1, ArpRule::Action::transpose, 12, true });
        auto notes = onNotes (runArp ({}, ceg, 40000, 512, 0.0, &prog), 6);
        CHECK ((notes == std::vector<int> { 60, 76, 67, 72, 64, 79 }));
        CHECK (describe (prog.rules[0]) == "On step 2 of every 2: transpose +12 st");

        ArpProgram passing;
        passing.addRule ({ ArpRule::When::range, 4, 2, ArpRule::Action::passing, 1, true });
        ArpSettings major;
        major.scaleMask = scales[1].mask;
        notes = onNotes (runArp (major, ceg, 40000, 512, 0.0, &passing), 4);
        CHECK ((notes == std::vector<int> { 60, 64, 69, 62 })); // G->A, C->D on steps 3-4

        ArpProgram ending;
        ending.length = 4;
        ending.addRule ({ ArpRule::When::phraseEnd, 1, 0, ArpRule::Action::ratchet, 2, true });
        CHECK (onNotes (runArp ({}, ceg, 23000, 512, 0.0, &ending)).size() == 5); // 4 steps, the last doubled

        ArpProgram chance;
        chance.addRule ({ ArpRule::When::chance, 50, 0, ArpRule::Action::rest, 0, true });
        const auto a = onNotes (runArp ({}, ceg, 6000 * 200, 512, 0.0, &chance), 1000);
        CHECK (a.size() > 70 && a.size() < 130 && a == onNotes (runArp ({}, ceg, 6000 * 200, 512, 0.0, &chance), 1000));

        prog.rules[0].enabled = false;
        CHECK ((onNotes (runArp ({}, ceg, 20000, 512, 0.0, &prog), 3) == std::vector<int> { 60, 64, 67 }));
    }

    // Phrase Memory: a random phrase repeats exactly; variation changes pitches but keeps the rhythm; returnEvery restores.
    {
        ArpSettings s;
        s.direction = ArpDirection::random;
        s.octaves = 2;
        s.phraseLength = 8;
        ArpProgram prog;
        prog.euclid (5, 8, 0);
        const auto exact = ons (runArp (s, ceg, 6000 * 8 * 4, 512, 0.0, &prog));
        CHECK (exact.size() == 20);
        bool repeats = true;
        for (size_t i = 5; i < exact.size(); ++i)
            repeats = repeats && exact[i].second == exact[i - 5].second && exact[i].first - exact[i - 5].first == 48000;
        CHECK (repeats);

        s.variation = 1.0f;
        s.varyPitch = true;
        s.varyVelocity = false;
        s.returnEvery = 2;
        const auto varied = ons (runArp (s, ceg, 6000 * 8 * 4, 512, 0.0, &prog));
        CHECK (varied.size() == 20);
        bool sameRhythm = true, pitchChanged = false, cycle2Original = true;
        for (size_t i = 0; i < 20; ++i)
        {
            sameRhythm = sameRhythm && varied[i].first == exact[i].first;
            if (i >= 5 && i < 10) pitchChanged = pitchChanged || varied[i].second != exact[i].second;
            if (i >= 10 && i < 15) cycle2Original = cycle2Original && varied[i].second == exact[i - 10].second;
        }
        CHECK (sameRhythm && pitchChanged && cycle2Original);

        s.varyPitch = false;
        s.varyRhythm = true;
        s.returnEvery = 0;
        CHECK (ons (runArp (s, ceg, 6000 * 8 * 4, 512, 0.0, &prog)) != exact); // rhythm variation changes timing
    }

    // Humanise: every step is late by at most the amount, deterministic; everything stays balanced and block-size-proof.
    {
        ArpSettings s;
        s.humanizeBeats = 0.05; // 1200 samples
        s.humanizeVelocity = 0.2f;
        const auto a = ons (runArp (s, ceg, 60000));
        bool bounded = true, moved = false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const auto late = a[i].first - (int64_t) i * 6000;
            bounded = bounded && late >= 0 && late <= 1200;
            moved = moved || late > 0;
        }
        CHECK (bounded && moved && a == ons (runArp (s, ceg, 60000)));

        ArpProgram prog;
        prog.euclid (5, 7, 2);
        prog.steps[3].ratchet = 4;
        prog.addRule ({ ArpRule::When::every, 3, 0, ArpRule::Action::transpose, 7, true });
        s.phraseLength = 12;
        s.variation = 0.5f;
        s.varyRhythm = true;
        std::vector<std::tuple<int64_t, int, float>> keys { { 0, 60, 0.8f }, { 0, 64, 0.8f }, { 0, 67, 0.8f },
                                                            { 200000, 60, 0.0f }, { 200000, 64, 0.0f }, { 200000, 67, 0.0f } };
        auto small = runArp (s, keys, 260000, 61, 0.0, &prog), large = runArp (s, keys, 260000, 2048, 0.0, &prog);
        auto looped = runArp (s, keys, 260000, 512, 2.0, &prog);
        CHECK (balanced (small) && balanced (large) && balanced (looped) && small.size() == large.size());
        bool same = small.size() == large.size();
        for (size_t i = 0; same && i < small.size(); ++i)
            same = small[i].sample == large[i].sample && small[i].note == large[i].note;
        CHECK (same);
    }
}

static void keyTests()
{
    auto print = [] (const char* name, const KeyEstimate& k)
    {
        std::printf ("  key %s: %s  %s %s r=%.2f margin=%.2f  (alt %s %s)\n", name,
                     k.status == KeyEstimate::Status::key ? "key" : k.status == KeyEstimate::Status::uncertain ? "uncertain" : "unavailable",
                     pitchClassName (k.candidates[0].tonic), k.candidates[0].minor ? "minor" : "major", k.candidates[0].correlation, k.margin,
                     pitchClassName (k.candidates[1].tonic), k.candidates[1].minor ? "minor" : "major");
    };

    // I-IV-V-I in C major.
    auto cMajor = keyOf (sequence ({ { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 }, { 60, 64, 67 } }, 1.0));
    print ("C major I-IV-V-I", cMajor);
    CHECK (cMajor.status == KeyEstimate::Status::key && cMajor.candidates[0].tonic == 0 && ! cMajor.candidates[0].minor);

    // i-iv-V-i in A minor (harmonic minor dominant).
    auto aMinor = keyOf (sequence ({ { 57, 60, 64 }, { 62, 65, 69 }, { 64, 68, 71 }, { 57, 60, 64 } }, 1.0));
    print ("A minor i-iv-V-i", aMinor);
    CHECK (aMinor.status == KeyEstimate::Status::key && aMinor.candidates[0].tonic == 9 && aMinor.candidates[0].minor);

    // Same progression transposed to F# major.
    auto fsMajor = keyOf (sequence ({ { 66, 70, 73 }, { 71, 75, 78 }, { 73, 77, 80 }, { 66, 70, 73 } }, 1.0));
    print ("F# major I-IV-V-I", fsMajor);
    CHECK (fsMajor.status == KeyEstimate::Status::key && fsMajor.candidates[0].tonic == 6 && ! fsMajor.candidates[0].minor);

    auto single = keyOf (tones ({ 69.0 }, 2.0));
    print ("single A4", single);
    CHECK (single.status != KeyEstimate::Status::key);

    std::vector<float> chromatic;
    for (int note = 60; note < 72; ++note) { auto t = tones ({ (double) note }, 0.3); chromatic.insert (chromatic.end(), t.begin(), t.end()); }
    auto chrom = keyOf (chromatic);
    print ("chromatic scale", chrom);
    CHECK (chrom.status != KeyEstimate::Status::key);

    CHECK (keyOf (std::vector<float> ((size_t) sr, 0.0f)).status == KeyEstimate::Status::unavailable);
}

int main()
{
    sliceSetTests();
    transientTests();
    rtSharedTests();
    pitchTests();
    scaleTests();
    padTests();
    arpTests();
    arpGenerativeTests();
    keyTests();
    std::printf (failures == 0 ? "All core tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
