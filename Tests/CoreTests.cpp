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

int main()
{
    sliceSetTests();
    transientTests();
    rtSharedTests();
    std::printf (failures == 0 ? "All core tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
