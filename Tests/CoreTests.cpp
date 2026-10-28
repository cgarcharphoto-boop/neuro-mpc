#include "Core/KeyDetector.h"
#include "Core/PitchDetector.h"
#include "Core/RtShared.h"
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
    keyTests();
    std::printf (failures == 0 ? "All core tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
