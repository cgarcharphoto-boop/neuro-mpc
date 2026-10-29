// Headless integration tests: drive the real NeuroMPCProcessor with files, MIDI and state.
#include "PluginProcessor.h"
#include "Core/TransientDetector.h"

static int failures = 0;

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    // Four 0.25 s regions of constant DC level 0.1, 0.2, 0.3, 0.4: output level identifies the playing slice.
    juce::File writeTestWav (const juce::File& file)
    {
        juce::AudioBuffer<float> buf (1, (int) rate);

        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, 0.1f * (float) (i / 12000 + 1));

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24);

        if (auto writer = wav.createWriterFor (out, options))
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());

        return file;
    }

    juce::File writeToneWav (const juce::File& file, double hz, double seconds)
    {
        juce::AudioBuffer<float> buf (2, (int) (rate * seconds));

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            float v = 0.0f;
            for (int h = 1; h <= 5; ++h)
                v += 0.25f / (float) h * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * i / rate);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);

        if (auto writer = wav.createWriterFor (out, options))
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());

        return file;
    }

    struct Run { int nonZero = 0; float peak = 0.0f; bool finite = true; };

    // Renders numBlocks blocks; events = (block index, note, velocity) at sample offset 0.
    Run render (NeuroMPCProcessor& p, int numBlocks, std::vector<std::tuple<int, int, float>> events = {})
    {
        Run r;
        juce::AudioBuffer<float> buf (2, block);

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::MidiBuffer midi;

            for (auto [at, note, vel] : events)
                if (at == b)
                    midi.addEvent (vel > 0.0f ? juce::MidiMessage::noteOn (1, note, vel) : juce::MidiMessage::noteOff (1, note), 0);

            p.processBlock (buf, midi);

            for (int i = 0; i < block; ++i)
            {
                auto v = buf.getSample (0, i);
                r.finite = r.finite && std::isfinite (v);
                r.peak = std::max (r.peak, std::abs (v));
                r.nonZero += std::abs (v) > 1.0e-6f ? 1 : 0;
            }
        }

        return r;
    }

    // Renders numBlocks and returns channel 0. Events: (block, note, velocity); wheel: (block, 14-bit value).
    std::vector<float> capture (NeuroMPCProcessor& p, int numBlocks, std::vector<std::tuple<int, int, float>> events,
                                std::vector<std::pair<int, int>> wheel = {})
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::MidiBuffer midi;

            if (b == 0)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0); // isolate from earlier one-shots

            for (auto [at, value] : wheel)
                if (at == b)
                    midi.addEvent (juce::MidiMessage::pitchWheel (1, value), 0);

            for (auto [at, note, vel] : events)
                if (at == b)
                    midi.addEvent (vel > 0.0f ? juce::MidiMessage::noteOn (1, note, vel) : juce::MidiMessage::noteOff (1, note), 0);

            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
        }

        return out;
    }

    int audibleLength (const std::vector<float>& x)
    {
        int n = 0;
        for (auto v : x) n += std::abs (v) > 1.0e-6f ? 1 : 0;
        return n;
    }

    // Pitch (fractional MIDI) of x[start, start + length) via the plugin's own YIN detector.
    double midiOf (const std::vector<float>& x, int start, int length)
    {
        const float* ch[] = { x.data() + start };
        auto est = neuro::detectPitch (ch, 1, length, rate);
        return est.status == neuro::PitchEstimate::Status::pitched ? est.midi : -1.0;
    }

    // Host transport stand-in for sync tests.
    struct MockPlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = false;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (playing);
            info.setPpqPosition (ppq);
            return info;
        }
    };

    void setParam (NeuroMPCProcessor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
}

// Zero-crossing frequency estimate over the middle half of a channel.
static double zeroCrossingHz (const juce::AudioBuffer<float>& b, int ch, double sr)
{
    const auto n = b.getNumSamples();
    const auto* x = b.getReadPointer (ch);
    int crossings = 0;

    for (int i = n / 4 + 1; i < 3 * n / 4; ++i)
        crossings += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;

    return crossings / 2.0 / ((n / 2.0) / sr);
}

// DSP-level evaluation of the stretch algorithm; prints measurements recorded in Documentation/TIME_STRETCHING.md.
static void stretchQualityTests()
{
    constexpr double sr = 48000.0;
    juce::AudioBuffer<float> tone (2, (int) sr);

    for (int i = 0; i < tone.getNumSamples(); ++i)
    {
        auto v = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);
        tone.setSample (0, i, v);
        tone.setSample (1, i, 0.5f * v);
    }

    for (float ratio : { 0.25f, 0.5f, 1.5f, 2.0f, 4.0f })
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = neuro::renderStretch (tone, 0, tone.getNumSamples(), sr, ratio, false);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto expected = (int) std::lround (sr * ratio);
        const auto hz = zeroCrossingHz (*out, 0, sr);

        // Stereo coherence: R input = 0.5 * L, so R output should stay 0.5 * L output.
        double err = 0.0, peak = 0.0;
        bool finite = true;

        for (int i = 0; i < out->getNumSamples(); ++i)
        {
            auto l = out->getSample (0, i), r = out->getSample (1, i);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            err = std::max (err, (double) std::abs (r - 0.5f * l));
            peak = std::max (peak, (double) std::abs (l));
        }

        std::printf ("  x%.2f peak per 10%% of output:", ratio);
        for (int k = 0; k < 10; ++k)
            std::printf (" %.2f", out->getMagnitude (0, k * out->getNumSamples() / 10, out->getNumSamples() / 10));
        std::printf ("\n");
        std::printf ("  stretch x%.2f: %d samples (expected %d), %.1f Hz (440), peak %.3f, L/R error %.4f, render %.1f ms\n",
                     ratio, out->getNumSamples(), expected, hz, peak, err, ms);
        CHECK (out->getNumSamples() == expected);
        CHECK (std::abs (hz - 440.0) < 440.0 * 0.01);
        // Input peak is 0.5. Steady state must match; the abrupt on/off edges of the test tone may ring up to +3 dB.
        const auto steady = out->getMagnitude (0, out->getNumSamples() / 10, out->getNumSamples() * 8 / 10);
        std::printf ("    steady-state peak %.3f\n", steady);
        CHECK (finite && steady > 0.48f && steady < (ratio > 2.0f ? 0.55f : 0.53f)); // >x2: phase randomisation ripple
        CHECK (peak < 0.5 * 1.42);
        CHECK (err < 0.02 * peak);
    }

    // Deterministic renders: identical settings give identical audio, including the phase-randomised range above x2.
    {
        auto a1 = neuro::renderStretch (tone, 0, tone.getNumSamples(), sr, 4.0f, false);
        auto a2 = neuro::renderStretch (tone, 0, tone.getNumSamples(), sr, 4.0f, false);
        bool identical = a1->getNumSamples() == a2->getNumSamples();
        for (int i = 0; identical && i < a1->getNumSamples(); ++i)
            identical = juce::exactlyEqual (a1->getSample (0, i), a2->getSample (0, i));
        CHECK (identical);
    }

    // Transient position scales with the ratio: hits at 0.25/0.5/0.75 s should land at ratio * that time.
    juce::AudioBuffer<float> hits (1, (int) sr);
    hits.clear();
    juce::Random rng (7);

    for (int h : { 12000, 24000, 36000 })
        for (int i = 0; i < 3000; ++i)
            hits.setSample (0, h + i, (rng.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-(float) i / 500.0f));

    for (float ratio : { 0.5f, 2.0f })
    {
        auto plain = neuro::renderStretch (hits, 0, hits.getNumSamples(), sr, ratio, false);
        auto plainOnsets = neuro::detectTransients (plain->getArrayOfReadPointers(), 1, plain->getNumSamples(), sr, 0.6f);
        std::printf ("  transients x%.1f, preserve off:", ratio);
        for (auto o : plainOnsets) std::printf (" %lld", (long long) o);
        std::printf ("\n");

        auto out = neuro::renderStretch (hits, 0, hits.getNumSamples(), sr, ratio, true);
        auto onsets = neuro::detectTransients (out->getArrayOfReadPointers(), 1, out->getNumSamples(), sr, 0.6f);
        std::printf ("  transients x%.1f, preserve on: ", ratio);
        for (auto o : onsets) std::printf (" %lld", (long long) o);
        std::printf ("  (expected %d %d %d)\n", (int) (12000 * ratio), (int) (24000 * ratio), (int) (36000 * ratio));
        CHECK (onsets.size() == 3);

        for (size_t i = 0; i < std::min<size_t> (3, onsets.size()); ++i)
            CHECK (std::llabs (onsets[i] - (int64_t) (12000 * (i + 1) * ratio)) < (int64_t) (0.02 * sr)); // within 20 ms
    }

    // Very short slice (10 ms) and sub-region extraction.
    auto shortOut = neuro::renderStretch (tone, 1000, 1480, sr, 2.0f);
    CHECK (shortOut->getNumSamples() == 960);
    auto silent = neuro::renderStretch (juce::AudioBuffer<float> (1, 4800), 0, 0, sr, 2.0f);
    CHECK (silent->getNumSamples() == 1);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    stretchQualityTests();
    auto wavFile = writeTestWav (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neuro_test.wav"));

    NeuroMPCProcessor p;
    p.prepareToPlay (rate, block);
    CHECK (render (p, 4, { { 0, 36, 1.0f } }).nonZero == 0); // no sample: silent, no crash

    juce::String error;
    CHECK (! p.loadSample (juce::File ("/nonexistent.wav"), error) && error.isNotEmpty());
    CHECK (p.loadSample (wavFile, error));
    CHECK (p.getSample()->length() == 48000);

    p.editSlices ([] (auto& s) { s.grid (4); });
    CHECK (p.getSlices().size() == 4);
    setParam (p, ParamIDs::attack, 0.0f);

    // One-shot: slice 3 (note 38) plays its full 12000 samples at level ~0.3, ignoring an early note-off.
    auto r = render (p, 40, { { 0, 38, 1.0f }, { 1, 38, 0.0f } });
    CHECK (r.finite);
    CHECK (std::abs (r.peak - 0.3f) < 0.01f);
    CHECK (std::abs (r.nonZero - 12000) <= 2);

    // Gate mode: note-off after 4 blocks stops playback within the release time (30 ms = 1440 samples).
    setParam (p, ParamIDs::playMode, 1.0f);
    r = render (p, 40, { { 0, 39, 1.0f }, { 4, 39, 0.0f } });
    CHECK (std::abs (r.peak - 0.4f) < 0.01f);
    CHECK (r.nonZero <= 4 * block + 1440 + 2 && r.nonZero >= 4 * block);
    setParam (p, ParamIDs::playMode, 0.0f);

    // Notes outside the slice range are ignored.
    CHECK (render (p, 4, { { 0, 36 + 4, 1.0f }, { 0, 20, 1.0f } }).nonZero == 0);

    // Velocity scales output.
    CHECK (std::abs (render (p, 30, { { 0, 36, 0.5f } }).peak - 0.05f) < 0.005f);

    // Polyphony is bounded: 100 simultaneous notes never exceed 32 voices' worth of level.
    {
        std::vector<std::tuple<int, int, float>> flood;
        for (int i = 0; i < 100; ++i) flood.emplace_back (0, 36, 1.0f);
        r = render (p, 2, flood);
        CHECK (r.finite && r.peak <= 32 * 0.1f + 0.01f);
        render (p, 40);
    }

    // Sample rate conversion: at 96 kHz host rate a slice lasts twice as many output samples.
    p.prepareToPlay (rate * 2, block);
    CHECK (std::abs (render (p, 80, { { 0, 36, 1.0f } }).nonZero - 24000) <= 4);
    p.prepareToPlay (rate, block);

    // Sample swap mid-note: voices are cut, no crash, new sample plays.
    render (p, 2, { { 0, 36, 1.0f } });
    CHECK (p.loadSample (wavFile, error));
    CHECK (render (p, 4).nonZero == 0);

    // Per-slice stretch through the processor: renders in the background, other slices unaffected.
    p.editSlices ([] (auto& s) { s.grid (4); s.setStretch (1, 2.0f); s.setStretch (3, 0.5f); });
    CHECK (p.waitForBackgroundWork (20000));
    CHECK (p.getStretchStatus (1) == NeuroMPCProcessor::StretchStatus::ready);
    CHECK (p.getStretchStatus (0) == NeuroMPCProcessor::StretchStatus::original);
    r = render (p, 80, { { 0, 37, 1.0f } });
    std::printf ("  slice 2 x2.0 played %d samples (expected 24000), peak %.3f\n", r.nonZero, r.peak);
    CHECK (r.finite && std::abs (r.nonZero - 24000) <= 240);
    CHECK (std::abs (render (p, 40, { { 0, 36, 1.0f } }).nonZero - 12000) <= 2);
    CHECK (std::abs (render (p, 40, { { 0, 39, 1.0f } }).nonZero - 6000) <= 60);

    // Changing a ratio while its old render is playing stops that voice safely; the new render plays.
    render (p, 2, { { 0, 37, 1.0f } });
    p.editSlices ([] (auto& s) { s.setStretch (1, 3.0f); });
    CHECK (p.waitForBackgroundWork (20000));
    CHECK (render (p, 4).nonZero == 0);
    CHECK (std::abs (render (p, 120, { { 0, 37, 1.0f } }).nonZero - 36000) <= 360);

    // Stretch survives save/restore.
    {
        juce::MemoryBlock stretched;
        p.getStateInformation (stretched);
        NeuroMPCProcessor q;
        q.prepareToPlay (rate, block);
        q.setStateInformation (stretched.getData(), (int) stretched.getSize());
        CHECK (q.waitForBackgroundWork (20000));
        CHECK (q.getSlices().getSlices()[1].timeStretchRatio == 3.0f);
        CHECK (std::abs (render (q, 120, { { 0, 37, 1.0f } }).nonZero - 36000) <= 360);
    }

    p.editSlices ([] (auto& s) { s.reset (s.getTotalLength()); });

    // Pitch analysis runs in the background for the sample and every slice, and follows slice edits.
    {
        auto toneFile = writeToneWav (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neuro_tone.wav"), 220.0, 2.0);
        NeuroMPCProcessor a;
        CHECK (a.loadSample (toneFile, error));
        a.editSlices ([] (auto& s) { s.grid (2); });
        CHECK (a.getSliceAnalysis (0) == nullptr); // pending
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        CHECK (a.waitForBackgroundWork (20000));
        std::printf ("  analysis of 2 s stereo + 2 slices: %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);

        const auto* whole = a.getSampleAnalysis();
        CHECK (whole != nullptr && whole->pitch.status == neuro::PitchEstimate::Status::pitched && whole->pitch.nearestNote() == 57);
        CHECK (whole != nullptr && whole->key.status != neuro::KeyEstimate::Status::key); // one note is not a key

        for (int i = 0; i < 2; ++i)
        {
            const auto* sa = a.getSliceAnalysis (i);
            CHECK (sa != nullptr && sa->pitch.nearestNote() == 57 && std::abs (sa->pitch.cents()) < 2.0);
        }

        a.editSlices ([] (auto& s) { s.split (12000); });
        CHECK (a.getSliceAnalysis (0) == nullptr); // region changed: stale result is not reported
        CHECK (a.waitForBackgroundWork (20000));
        CHECK (a.getSliceAnalysis (0) != nullptr && a.getSliceAnalysis (1) != nullptr && a.getSliceAnalysis (2) != nullptr);

        // Manual correction persists.
        a.editSlices ([] (auto& s) { s.setRootNote (1, 60.0f); });
        juce::MemoryBlock st;
        a.getStateInformation (st);
        NeuroMPCProcessor b;
        b.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (b.getSlices().getSlices()[1].rootNote == 60.0f && b.getSlices().getSlices()[0].rootNote < 0.0f);
        CHECK (b.waitForBackgroundWork (20000) && b.getSliceAnalysis (2) != nullptr);
        toneFile.deleteFile();
    }

    // ---- Phase 8: chromatic pitch ----
    {
        auto toneFile = writeToneWav (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neuro_tone8.wav"), 220.0, 2.0);
        NeuroMPCProcessor k;
        k.prepareToPlay (rate, block);
        CHECK (k.loadSample (toneFile, error));
        setParam (k, ParamIDs::attack, 0.0f);
        CHECK (k.waitForBackgroundWork (20000));
        const int len = 96000;

        // Resample: +7 st raises pitch and shortens by 2^(7/12).
        k.editSlices ([] (auto& s) { s.setPitch (0, 7.0f); });
        auto x = capture (k, 200, { { 0, 36, 1.0f } });
        std::printf ("  resample +7: midi %.2f, %d samples (expected %.0f)\n", midiOf (x, 4800, 9600), audibleLength (x), len / std::exp2 (7.0 / 12.0));
        CHECK (std::abs (midiOf (x, 4800, 9600) - 64.0) < 0.05);
        CHECK (std::abs (audibleLength (x) - len / std::exp2 (7.0 / 12.0)) < 10);

        // Keep length: same pitch, original duration (rendered in the background).
        k.editSlices ([] (auto& s) { s.setPitchMode (0, neuro::PitchMode::keepLength); });
        CHECK (k.waitForBackgroundWork (20000));
        x = capture (k, 200, { { 0, 36, 1.0f } });
        std::printf ("  keep length +7: midi %.2f, %d samples (expected %d)\n", midiOf (x, 4800, 9600), audibleLength (x), len);
        CHECK (std::abs (midiOf (x, 4800, 9600) - 64.0) < 0.05);
        CHECK (std::abs (audibleLength (x) - len) < len / 100);

        // Fine tuning: +50 cents.
        k.editSlices ([] (auto& s) { s.setPitch (0, 0.5f); s.setPitchMode (0, neuro::PitchMode::resample); });
        CHECK (std::abs (midiOf (capture (k, 100, { { 0, 36, 1.0f } }), 4800, 9600) - 57.5) < 0.05);
        k.editSlices ([] (auto& s) { s.setPitch (0, 0.0f); });

        // Keys mode: the slice plays chromatically from its detected root (A3).
        setParam (k, ParamIDs::keyMode, 1.0f);
        k.setKeyboardSlice (0);
        CHECK (std::abs (k.getKeyboardRoot() - 57.0f) < 0.05f);
        for (int note : { 45, 57, 64, 69, 81 })
        {
            auto m = midiOf (capture (k, 60, { { 0, note, 1.0f } }), 2400, 4800);
            std::printf ("  keys note %d -> midi %.2f\n", note, m);
            CHECK (std::abs (m - note) < 0.05);
        }

        CHECK (k.padNote (0) == 57 && k.padNote (5) == 62); // pads play chromatically from the root

        // Scale snapping (C major): C# -> C, F# -> F.
        setParam (k, ParamIDs::scale, 1.0f);
        CHECK (std::abs (midiOf (capture (k, 60, { { 0, 61, 1.0f } }), 2400, 4800) - 60.0) < 0.05);
        CHECK (std::abs (midiOf (capture (k, 60, { { 0, 66, 1.0f } }), 2400, 4800) - 65.0) < 0.05);
        setParam (k, ParamIDs::scale, 0.0f);

        // Manual root overrides detection: root C4 means note 60 plays the sample unshifted (A3).
        k.editSlices ([] (auto& s) { s.setRootNote (0, 60.0f); });
        CHECK (std::abs (midiOf (capture (k, 60, { { 0, 60, 1.0f } }), 2400, 4800) - 57.0) < 0.05);
        k.editSlices ([] (auto& s) { s.setRootNote (0, -1.0f); });

        // Global transpose and fine tune.
        setParam (k, ParamIDs::transpose, 12.0f);
        setParam (k, ParamIDs::fineTune, -50.0f);
        CHECK (std::abs (midiOf (capture (k, 60, { { 0, 57, 1.0f } }), 2400, 4800) - 68.5) < 0.05);
        setParam (k, ParamIDs::transpose, 0.0f);
        setParam (k, ParamIDs::fineTune, 0.0f);

        // Pitch bend: full up with a 2-semitone range, applied to a held note, then centred again.
        x = capture (k, 120, { { 0, 57, 1.0f } }, { { 40, 16383 }, { 80, 8192 } });
        const auto before = midiOf (x, 2400, 4800), bent = midiOf (x, 50 * block, 4800), after = midiOf (x, 100 * block, 4800);
        std::printf ("  bend: %.2f -> %.2f -> %.2f\n", before, bent, after);
        CHECK (std::abs (before - 57.0) < 0.05 && std::abs (bent - 59.0) < 0.05 && std::abs (after - 57.0) < 0.05);

        // Pitch LFO: +/-1 semitone sine at 2 Hz sweeps the measured pitch across ~56..58.
        setParam (k, ParamIDs::lfoDepth, 1.0f);
        setParam (k, ParamIDs::lfoRate, 2.0f);
        x = capture (k, 100, { { 0, 57, 1.0f } });
        double lo = 200.0, hi = 0.0;
        for (int start = 0; start + 3000 < 48000; start += 600)
            if (auto m = midiOf (x, start, 3000); m > 0.0) { lo = std::min (lo, m); hi = std::max (hi, m); }
        std::printf ("  LFO +/-1 st: measured %.2f .. %.2f\n", lo, hi);
        CHECK (lo < 56.3 && lo > 55.8 && hi > 57.7 && hi < 58.2);
        setParam (k, ParamIDs::lfoDepth, 0.0f);

        // State: pitch, mode and keyboard slice persist.
        k.editSlices ([] (auto& s) { s.setPitch (0, -3.25f); s.setPitchMode (0, neuro::PitchMode::keepLength); });
        juce::MemoryBlock st;
        k.getStateInformation (st);
        NeuroMPCProcessor r2;
        r2.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (std::abs (r2.getSlices().getSlices()[0].pitchSemitones + 3.25f) < 1.0e-6f);
        CHECK (r2.getSlices().getSlices()[0].pitchMode == neuro::PitchMode::keepLength);
        CHECK (r2.isKeyboardMode() && r2.getKeyboardSlice() == 0);
        toneFile.deleteFile();
    }

    // DC test sample has no pitch.
    CHECK (p.waitForBackgroundWork (20000));
    CHECK (p.getSampleAnalysis() != nullptr && p.getSampleAnalysis()->pitch.status != neuro::PitchEstimate::Status::pitched);

    // UI audition path (lock-free FIFO) triggers playback.
    p.editSlices ([] (auto& s) { s.grid (2); });
    p.auditionNote (37, 1.0f);
    CHECK (std::abs (render (p, 60).nonZero - 24000) <= 2);

    // State round-trip: parameters, sample path and slice ids/boundaries.
    p.editSlices ([] (auto& s) { s.split (5000); });
    setParam (p, ParamIDs::gain, -6.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);

    NeuroMPCProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getSample() != nullptr && q.getSample()->file == wavFile);
    CHECK (q.getSlices().size() == 3);

    for (int i = 0; i < 3; ++i)
    {
        CHECK (q.getSlices().getSlices()[(size_t) i].id == p.getSlices().getSlices()[(size_t) i].id);
        CHECK (q.getSlices().getSlices()[(size_t) i].startSample == p.getSlices().getSlices()[(size_t) i].startSample);
    }

    CHECK (std::abs (q.apvts.getRawParameterValue (ParamIDs::gain)->load() + 6.0f) < 0.01f);

    // ---- Phase 9: pads ----
    {
        writeTestWav (wavFile);
        NeuroMPCProcessor d;
        d.prepareToPlay (rate, block);
        CHECK (d.loadSample (wavFile, error));
        setParam (d, ParamIDs::attack, 0.0f);
        d.editSlices ([] (auto& s) { s.grid (4); }); // levels 0.1, 0.2, 0.3, 0.4; pads follow
        const auto peakOf = [] (const std::vector<float>& x, int from, int to)
        {
            float pk = 0.0f;
            for (int i = from; i < to; ++i) pk = std::max (pk, std::abs (x[(size_t) i]));
            return pk;
        };

        CHECK (std::abs (render (d, 30, { { 0, 37, 1.0f } }).peak - 0.2f) < 0.01f); // follow mode: pad 2 = slice 2

        // Custom note + layering: pads A1 and A2 both on note 60 play slices 1 and 2 together.
        d.editPads ([] (auto& b) { b[0].note = 60; b[1].note = 60; });
        CHECK (render (d, 30, { { 0, 36, 1.0f } }).nonZero == 0);
        CHECK (std::abs (render (d, 30, { { 0, 60, 1.0f } }).peak - 0.3f) < 0.01f);
        d.editPads ([] (auto& b) { b[0].note = 36; b[1].note = 37; });

        // Pad level, velocity sensitivity and tuning.
        d.editPads ([] (auto& b) { b[0].gain = 0.5f; });
        CHECK (std::abs (render (d, 30, { { 0, 36, 1.0f } }).peak - 0.05f) < 0.002f);
        d.editPads ([] (auto& b) { b[0].velocitySensitivity = 0.0f; });
        CHECK (std::abs (render (d, 30, { { 0, 36, 0.1f } }).peak - 0.05f) < 0.002f);
        d.editPads ([] (auto& b) { b[0].gain = 1.0f; b[0].velocitySensitivity = 1.0f; b[0].pitch = 12.0f; });
        CHECK (std::abs (render (d, 30, { { 0, 36, 1.0f } }).nonZero - 6000) <= 2); // +12 st resampled: half length
        d.editPads ([] (auto& b) { b[0].pitch = 0.0f; });

        // Choke group: pad 2 cuts pad 1 off within the 5 ms choke fade.
        d.editPads ([] (auto& b) { b[0].chokeGroup = 1; b[1].chokeGroup = 1; });
        auto x = capture (d, 10, { { 0, 36, 1.0f }, { 2, 37, 1.0f } });
        std::printf ("  choke: before %.3f, during %.3f, after %.3f\n", peakOf (x, 0, 2 * block), peakOf (x, 2 * block, 2 * block + 240),
                     peakOf (x, 2 * block + 300, 4 * block));
        CHECK (std::abs (peakOf (x, 0, 2 * block) - 0.1f) < 0.005f);
        CHECK (std::abs (peakOf (x, 2 * block + 300, 4 * block) - 0.2f) < 0.005f); // only pad 2 remains
        d.editPads ([] (auto& b) { b[1].chokeGroup = 0; });
        x = capture (d, 10, { { 0, 36, 1.0f }, { 2, 37, 1.0f } });
        CHECK (std::abs (peakOf (x, 2 * block + 300, 4 * block) - 0.3f) < 0.005f); // no choke: both sound
        d.editPads ([] (auto& b) { b[0].chokeGroup = 0; });

        // Manual assignment stops following; a pad whose slice is removed goes silent; unassigned slices still audition.
        capture (d, 1, {}); // all-notes-off: flush voices left from the choke captures
        d.editPads ([&] (auto& b) { b.assign (0, d.getSlices().getSlices()[3].id); });
        CHECK (std::abs (render (d, 30, { { 0, 36, 1.0f } }).peak - 0.4f) < 0.01f);
        d.editSlices ([] (auto& s) { s.mergeWithPrevious (3); });
        CHECK (! d.getPads().followSlices && render (d, 30, { { 0, 36, 1.0f } }).nonZero == 0);
        d.editPads ([] (auto& b) { b.clear (1); });
        capture (d, 1, {});
        const auto tag = d.startSliceAudition (1);
        CHECK (std::abs (render (d, 60).peak - 0.9f * 0.2f) < 0.01f); // slice 2 is on no pad but auditions (velocity 0.9)
        d.stopAudition (tag);

        // Pads persist; MIDI learn sees incoming notes.
        d.editPads ([] (auto& b) { b[4].name = "Kick"; b[4].colour = 0xffc0392b; b[4].chokeGroup = 3; b[4].note = 70; });
        juce::MemoryBlock st;
        d.getStateInformation (st);
        NeuroMPCProcessor e;
        e.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (! e.getPads().followSlices && e.getPads()[4].name == "Kick" && e.getPads()[4].colour == 0xffc0392bu);
        CHECK (e.getPads()[4].chokeGroup == 3 && e.getPads()[4].note == 70 && e.getPads()[0].sliceId == d.getPads()[0].sliceId);

        const auto count = d.getMidiNoteCount();
        render (d, 1, { { 0, 71, 1.0f } });
        CHECK (d.getLastMidiNote() == 71 && d.getMidiNoteCount() == count + 1);
    }

    // ---- Phase 10: arpeggiator, end to end through the audio output ----
    {
        writeTestWav (wavFile);
        NeuroMPCProcessor g;
        g.prepareToPlay (rate, block);
        CHECK (g.loadSample (wavFile, error));
        g.editSlices ([] (auto& s) { s.grid (4); }); // pads 36-39 -> levels 0.1 .. 0.4
        setParam (g, ParamIDs::attack, 0.0f);
        setParam (g, ParamIDs::playMode, 1.0f);       // gate mode, so arp note lengths are audible
        setParam (g, ParamIDs::arpOn, 1.0f);          // defaults: Up, 1/16 (6000 samples at 120 BPM), gate 50%

        // Hold three pads (input notes start later than sample 0 to check the phrase starts on the key press).
        auto x = capture (g, 60, { { 1, 36, 1.0f }, { 1, 37, 1.0f }, { 1, 38, 1.0f } });
        const int t0 = block; // key press at block 1, sample 0
        const auto at = [&] (int sample) { return std::abs (x[(size_t) sample]); };
        std::printf ("  arp levels: %.2f %.2f %.2f %.2f | gap %.3f\n", at (t0 + 1500), at (t0 + 7500), at (t0 + 13500),
                     at (t0 + 19500), at (t0 + 5500));
        CHECK (at (t0 - 10) == 0.0f);                        // nothing before the key press
        CHECK (std::abs (at (t0 + 1500) - 0.1f) < 0.005f);   // step 1: pad 1
        CHECK (std::abs (at (t0 + 7500) - 0.2f) < 0.005f);   // step 2: pad 2
        CHECK (std::abs (at (t0 + 13500) - 0.3f) < 0.005f);  // step 3: pad 3
        CHECK (std::abs (at (t0 + 19500) - 0.1f) < 0.005f);  // wraps to pad 1
        CHECK (at (t0 + 5500) == 0.0f);                      // gate 50% (3000) + 30 ms release, then silence

        // Releasing the keys stops new steps; switching the arp off releases everything (no stuck notes).
        x = capture (g, 2, { { 0, 36, 1.0f }, { 0, 37, 1.0f } });
        setParam (g, ParamIDs::arpOn, 0.0f);
        x = capture (g, 40, {});
        CHECK (std::abs (x.back()) == 0.0f && g.getArpHeldCount() == 0);

        // Arp off: notes pass straight through again.
        CHECK (std::abs (render (g, 2, { { 0, 39, 1.0f } }).peak - 0.4f) < 0.005f);

        // Settings persist as ordinary parameters.
        setParam (g, ParamIDs::arpDirection, 2.0f);
        setParam (g, ParamIDs::arpRate, 4.0f);
        juce::MemoryBlock st;
        g.getStateInformation (st);
        NeuroMPCProcessor h;
        h.setStateInformation (st.getData(), (int) st.getSize());
        CHECK ((int) h.apvts.getRawParameterValue (ParamIDs::arpDirection)->load() == 2
               && (int) h.apvts.getRawParameterValue (ParamIDs::arpRate)->load() == 4);
    }

    // ---- Phase 11: rhythm pattern, ratchets and grammar through the audio output ----
    {
        writeTestWav (wavFile);
        NeuroMPCProcessor g;
        g.prepareToPlay (rate, block);
        CHECK (g.loadSample (wavFile, error));
        g.editSlices ([] (auto& s) { s.grid (4); });
        setParam (g, ParamIDs::attack, 0.0f);
        setParam (g, ParamIDs::release, 1.0f);
        setParam (g, ParamIDs::playMode, 1.0f);
        setParam (g, ParamIDs::arpOn, 1.0f);
        // Pattern: step 1 ratchet x2, step 2 rest. Grammar: every 3rd step transposes +1 (pad 36 -> pad 37).
        g.editArpProgram ([] (auto& prog)
        {
            prog.length = 2;
            prog.steps[0].ratchet = 2;
            prog.steps[1].on = false;
            prog.addRule ({ neuro::ArpRule::When::every, 3, 2, neuro::ArpRule::Action::transpose, 1, true });
        });

        auto x = capture (g, 70, { { 0, 36, 1.0f } });
        const auto at = [&] (int sample) { return std::abs (x[(size_t) sample]); };
        std::printf ("  pattern: hit1 %.2f gap %.2f hit2 %.2f rest %.2f step3 %.2f step5 %.2f\n",
                     at (500), at (2200), at (3500), at (8000), at (12500), at (24500));
        CHECK (std::abs (at (500) - 0.1f) < 0.005f && at (2200) == 0.0f && std::abs (at (3500) - 0.1f) < 0.005f); // ratchet
        CHECK (at (8000) == 0.0f);                       // step 2 rests
        CHECK (std::abs (at (12500) - 0.2f) < 0.005f);   // step 3: grammar +1 -> pad 37 (0.2)
        CHECK (std::abs (at (24500) - 0.1f) < 0.005f);   // step 5: pattern step 1 again, no rule

        juce::MemoryBlock st;
        g.getStateInformation (st);
        NeuroMPCProcessor h;
        h.setStateInformation (st.getData(), (int) st.getSize());
        const auto& prog = h.getArpProgram();
        CHECK (prog.length == 2 && prog.steps[0].ratchet == 2 && ! prog.steps[1].on && prog.numRules == 1
               && prog.rules[0].offset == 2 && prog.rules[0].amount == 1);
    }

    // ---- Phase 12: step sequencer through the audio output ----
    {
        writeTestWav (wavFile);
        NeuroMPCProcessor sq;
        sq.prepareToPlay (rate, block);
        CHECK (sq.loadSample (wavFile, error));
        sq.editSlices ([] (auto& s) { s.grid (4); });
        setParam (sq, ParamIDs::attack, 0.0f);
        setParam (sq, ParamIDs::release, 1.0f);
        setParam (sq, ParamIDs::playMode, 1.0f);
        sq.editSeqPattern (0, [] (auto& pat)
        {
            pat.lanes[0].steps[0].on = pat.lanes[0].steps[8].on = true;                // pad 1 (0.1) on steps 1 and 9
            pat.lanes[1].steps[4] = { true, 0.5f, 0.5f, 1.0f, 1, 0.5f, 12.0f };     // pad 2 (0.2), vel 0.5, +50% late, +12 st
            pat.lanes[2].steps[12] = { true, 1.0f, 0.5f, 1.0f, 2, 0.0f, 0.0f };     // pad 3 (0.3), ratchet x2
        });

        CHECK (capture (sq, 200, {})[1500] == 0.0f); // mode Off: silent
        setParam (sq, ParamIDs::seqMode, 2.0f);       // free running from bar 1
        auto x = capture (sq, 200, {});
        const auto at = [&] (int sample) { return std::abs (x[(size_t) sample]); };
        std::printf ("  seq: %.2f | %.2f %.2f | %.2f | %.2f %.2f %.2f\n", at (1500), at (25000), at (28000), at (49500),
                     at (72500), at (74500), at (75500));
        CHECK (std::abs (at (1500) - 0.8f * 0.1f) < 0.005f);    // step 1 (default velocity 0.8)
        CHECK (at (25000) == 0.0f);                              // step 5 is microtimed 3000 samples late...
        CHECK (std::abs (at (28000) - 0.5f * 0.2f) < 0.005f);   // ...at velocity 0.5
        CHECK (std::abs (at (49500) - 0.8f * 0.1f) < 0.005f);   // step 9
        CHECK (std::abs (at (72500) - 0.3f) < 0.005f && at (74500) == 0.0f && std::abs (at (75500) - 0.3f) < 0.005f); // ratchet x2

        // Step pitch reaches the voice: in one-shot mode a +12 st step plays its 12000-sample slice in 6000 samples.
        setParam (sq, ParamIDs::seqMode, 0.0f);
        render (sq, 1);                                 // one block in Off, so Free restarts from bar 1
        setParam (sq, ParamIDs::playMode, 0.0f);
        sq.editSeqPattern (4, [] (auto& pat) { pat.lanes[1].steps[0] = { true, 1.0f, 0.5f, 1.0f, 1, 0.0f, 12.0f }; });
        setParam (sq, ParamIDs::seqPattern, 5.0f);
        setParam (sq, ParamIDs::seqMode, 2.0f);
        x = capture (sq, 60, {});                       // 30720 samples: one hit (the next is at 96000)
        int audible = 0;
        for (auto v : x) audible += std::abs (v) > 1.0e-6f ? 1 : 0;
        std::printf ("  step pitch +12 one-shot: %d samples audible\n", audible);
        CHECK (std::abs (audible - 6000) <= 2);
        setParam (sq, ParamIDs::playMode, 1.0f);

        // Switching to an empty pattern and to Off both stop cleanly.
        setParam (sq, ParamIDs::seqPattern, 2.0f);
        x = capture (sq, 100, {});
        CHECK (at (50000) == 0.0f && sq.getSeqPlayStep() >= 0);
        setParam (sq, ParamIDs::seqPattern, 1.0f);
        setParam (sq, ParamIDs::seqMode, 0.0f);
        CHECK (render (sq, 4).nonZero == 0 && sq.getSeqPlayStep() == -1);

        // Patterns persist (only non-empty ones are written).
        sq.editSeqPattern (3, [] (auto& pat) { pat.name = "Fill"; pat.lanes[5].length = 12; pat.lanes[5].steps[11].on = true; });
        juce::MemoryBlock st;
        sq.getStateInformation (st);
        NeuroMPCProcessor restored;
        restored.setStateInformation (st.getData(), (int) st.getSize());
        const auto& p0 = restored.getSeqPattern (0);
        CHECK (p0.lanes[1].steps[4].on && std::abs (p0.lanes[1].steps[4].micro - 0.5f) < 1e-6f && p0.lanes[2].steps[12].ratchet == 2);
        CHECK (restored.getSeqPattern (3).name == "Fill" && restored.getSeqPattern (3).lanes[5].length == 12 && restored.getSeqPattern (3).lanes[5].steps[11].on);
        CHECK (restored.getSeqPattern (1).isEmpty());
    }

    // ---- Phase 13: conditions, weaving and undo through the processor ----
    {
        writeTestWav (wavFile);
        NeuroMPCProcessor w;
        w.prepareToPlay (rate, block);
        CHECK (w.loadSample (wavFile, error));
        w.editSlices ([] (auto& s) { s.grid (4); });
        setParam (w, ParamIDs::attack, 0.0f);
        setParam (w, ParamIDs::release, 1.0f);
        setParam (w, ParamIDs::playMode, 1.0f);
        // Pattern 1: pad 1 on step 1 of every bar; a 1:2 pad-3 hit on step 9. Pattern 2 (fill): pad 2 on step 1.
        w.editSeqPattern (0, [] (auto& pat)
        {
            pat.lanes[0].steps[0] = { true, 1.0f };
            pat.lanes[2].steps[8] = { true, 1.0f };
            pat.lanes[2].steps[8].condition = neuro::SeqCondition::ratio;
            pat.lanes[2].steps[8].condA = 1;
            pat.lanes[2].steps[8].condB = 2;
        });
        w.editSeqPattern (1, [] (auto& pat) { pat.lanes[1].steps[0] = { true, 1.0f }; });
        setParam (w, ParamIDs::weaveFill, 2.0f);       // fills from pattern 2...
        setParam (w, ParamIDs::weaveFillEvery, 2.0f);  // ...on every 2nd bar
        setParam (w, ParamIDs::seqMode, 2.0f);
        auto x = capture (w, 4 * 96000 / block + 1, {});
        const auto at = [&] (int bar, int step) { return std::abs (x[(size_t) (bar * 96000 + step * 6000 + 1000)]); };
        std::printf ("  weave bars: %.2f %.2f %.2f %.2f\n", at (0, 0), at (1, 0), at (2, 0), at (3, 0));
        CHECK (std::abs (at (0, 0) - 0.1f) < 0.005f && std::abs (at (1, 0) - 0.2f) < 0.005f);  // bar 2 is the woven fill
        CHECK (std::abs (at (2, 0) - 0.1f) < 0.005f && std::abs (at (3, 0) - 0.2f) < 0.005f);

        // Without weaving, the 1:2 step plays on lane cycles 1 and 3 only (bars 1, 3; not 2, 4).
        setParam (w, ParamIDs::weaveFill, 0.0f);
        setParam (w, ParamIDs::seqMode, 0.0f);
        render (w, 1);
        setParam (w, ParamIDs::seqMode, 2.0f);
        x = capture (w, 4 * 96000 / block + 1, {});
        std::printf ("  1:2 step 9 per bar: %.2f %.2f %.2f %.2f\n", at (0, 8), at (1, 8), at (2, 8), at (3, 8));
        CHECK (std::abs (at (0, 8) - 0.3f) < 0.005f && at (1, 8) == 0.0f && std::abs (at (2, 8) - 0.3f) < 0.005f && at (3, 8) == 0.0f);
        setParam (w, ParamIDs::weaveFill, 2.0f);

        // Undo / redo per pattern; transformations are separate undo steps.
        CHECK (w.canUndoSeqPattern (0) && ! w.canRedoSeqPattern (0));
        w.editSeqPattern (0, [] (auto& pat) { neuro::pattern::reverse (pat.lanes[0]); }, NeuroMPCProcessor::Undo::always);
        CHECK (w.getSeqPattern (0).lanes[0].steps[15].on && ! w.getSeqPattern (0).lanes[0].steps[0].on);
        CHECK (w.undoSeqPattern (0) && w.getSeqPattern (0).lanes[0].steps[0].on && w.canRedoSeqPattern (0));
        CHECK (w.redoSeqPattern (0) && w.getSeqPattern (0).lanes[0].steps[15].on);
        CHECK (w.undoSeqPattern (0));

        // Conditions and weave settings persist.
        juce::MemoryBlock st;
        w.getStateInformation (st);
        NeuroMPCProcessor restored;
        restored.setStateInformation (st.getData(), (int) st.getSize());
        const auto& cond = restored.getSeqPattern (0).lanes[2].steps[8];
        CHECK (cond.condition == neuro::SeqCondition::ratio && cond.condA == 1 && cond.condB == 2);
        CHECK ((int) restored.apvts.getRawParameterValue (ParamIDs::weaveFill)->load() == 2);
    }

    // ---- Phase 14: parameter IDs, state versions, robustness, relink, presets, host sync ----
    {
        // Frozen automation IDs: renaming or removing any of these breaks saved automation in user projects.
        const juce::StringArray frozen { "gain", "attack", "release", "playMode", "transpose", "fineTune", "bendRange", "lfoDepth",
            "lfoRate", "lfoShape", "lfoSync", "lfoPhase", "keyMode", "scale", "scaleRoot", "arpOn", "arpLatch", "arpDirection", "arpRate",
            "arpGate", "arpOctaves", "arpSwing", "arpProbability", "arpRepeats", "arpVelocity", "arpRetrigger", "arpSeed", "arpAccent",
            "arpPhrase", "arpVariation", "arpVaryPitch", "arpVaryVelocity", "arpVaryRhythm", "arpReturn", "arpHumanTime",
            "arpHumanVelocity", "seqMode", "seqPattern", "seqRate", "seqSwing", "seqFill", "weaveAccent", "weaveAccentAmount",
            "weaveFill", "weaveFillEvery", "weavePitch" };
        NeuroMPCProcessor a;
        juce::StringArray ids;
        for (auto* param : a.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                ids.add (withId->paramID);
        std::printf ("  %d host parameters\n", ids.size());
        CHECK (ids.size() == frozen.size());
        for (const auto& id : frozen)
            CHECK (ids.contains (id));
        auto unique = ids;
        unique.removeDuplicates (false);
        CHECK (unique.size() == ids.size());

        // Every parameter survives a state round trip at a non-default value.
        for (auto* param : a.getParameters())
            param->setValueNotifyingHost (0.37f);
        juce::MemoryBlock st;
        a.getStateInformation (st);
        NeuroMPCProcessor b;
        b.setStateInformation (st.getData(), (int) st.getSize());
        for (int i = 0; i < a.getParameters().size(); ++i)
        {
            // Compare meaning, not raw floats: e.g. a bool holding a host's raw 0.37 is "off" and saves as off.
            const auto before = a.getParameters()[i]->getCurrentValueAsText(), after = b.getParameters()[i]->getCurrentValueAsText();
            if (before != after)
                std::printf ("    param %s: %s vs %s\n", ids[i].toRawUTF8(), before.toRawUTF8(), after.toRawUTF8());
            CHECK (before == after);
        }

        // Version 1 state (Milestone 1 format) loads; a newer version loads best-effort and is flagged.
        writeTestWav (wavFile);
        auto v1 = juce::ValueTree::fromXml ("<NEURO_MPC version=\"1\"><PARAMS><PARAM id=\"gain\" value=\"-6.0\"/></PARAMS>"
                                            "<SAMPLE path=\"" + wavFile.getFullPathName() + "\"/><SLICES>"
                                            "<SLICE id=\"1\" start=\"0\" end=\"20000\"/><SLICE id=\"2\" start=\"20000\" end=\"48000\"/></SLICES></NEURO_MPC>");
        NeuroMPCProcessor c;
        CHECK (c.applyStateTree (v1) && c.getSlices().size() == 2 && ! c.wasLoadedFromNewerVersion());
        CHECK (std::abs (c.apvts.getRawParameterValue (ParamIDs::gain)->load() + 6.0f) < 0.01f);
        CHECK ((int) c.createStateTree().getProperty ("version") == NeuroMPCProcessor::stateVersion);
        v1.setProperty ("version", 99, nullptr);
        v1.appendChild (juce::ValueTree ("SOMETHING_FROM_THE_FUTURE"), nullptr);
        NeuroMPCProcessor future;
        CHECK (future.applyStateTree (v1) && future.wasLoadedFromNewerVersion() && future.getSlices().size() == 2);

        // Corrupt input never crashes and never changes the current state.
        juce::Random rng (42);
        for (int trial = 0; trial < 200; ++trial)
        {
            juce::MemoryBlock junk ((size_t) rng.nextInt (2000));
            rng.fillBitsRandomly (junk.getData(), junk.getSize());
            c.setStateInformation (junk.getData(), (int) junk.getSize());
        }
        auto truncated = st;
        truncated.setSize (st.getSize() / 2);
        c.setStateInformation (truncated.getData(), (int) truncated.getSize());
        c.setStateInformation (nullptr, 0);
        CHECK (! c.applyStateTree (juce::ValueTree ("NOT_OURS")));
        CHECK (c.getSlices().size() == 2 && c.getSample() != nullptr);

        // Missing sample: slices are kept through a save while missing and restored on relink (and by folder search).
        c.editSlices ([] (auto& s) { s.split (30000); });
        c.getStateInformation (st);
        const auto moved = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neuro_relink");
        moved.deleteRecursively();
        moved.getChildFile ("deep/er").createDirectory();
        const auto relocated = moved.getChildFile ("deep/er").getChildFile (wavFile.getFileName());
        CHECK (wavFile.moveFileTo (relocated));
        NeuroMPCProcessor m;
        m.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (m.getSample() == nullptr && m.getMissingSamplePath() == wavFile.getFullPathName());
        juce::MemoryBlock whileMissing;
        m.getStateInformation (whileMissing);
        NeuroMPCProcessor m2;
        m2.setStateInformation (whileMissing.getData(), (int) whileMissing.getSize());
        const auto found = m2.findMissingSampleIn (moved);
        CHECK (found == relocated);
        CHECK (m2.relinkSample (found, error) && m2.getSlices().size() == 3 && m2.getMissingSamplePath().isEmpty());
        CHECK (m2.getSlices().getSlices()[2].startSample == 30000);
        CHECK (relocated.moveFileTo (wavFile));
        moved.deleteRecursively();

        // Presets: save, list, load (state including slices and parameters).
        const auto presetDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neuro_presets");
        presetDir.deleteRecursively();
        NeuroMPCProcessor pr;
        pr.setPresetDirectory (presetDir);
        CHECK (pr.loadSample (wavFile, error));
        pr.editSlices ([] (auto& s) { s.grid (8); });
        setParam (pr, ParamIDs::arpRate, 2.0f);
        CHECK (pr.savePreset ("My Kit / v1") && pr.getPresets().size() == 1);
        pr.editSlices ([] (auto& s) { s.grid (2); });
        setParam (pr, ParamIDs::arpRate, 6.0f);
        CHECK (pr.loadPreset (pr.getPresets()[0]) && pr.getSlices().size() == 8);
        CHECK ((int) pr.apvts.getRawParameterValue (ParamIDs::arpRate)->load() == 2 && pr.getCurrentPresetName().isNotEmpty());
        CHECK (! pr.loadPreset (presetDir.getChildFile ("missing.neurompc")));
        presetDir.deleteRecursively();

        // Host sync: the sequencer follows host PPQ, tempo changes and loops, and stops cleanly with the transport.
        NeuroMPCProcessor hs;
        MockPlayHead host;
        hs.setPlayHead (&host);
        hs.prepareToPlay (rate, block);
        CHECK (hs.loadSample (wavFile, error));
        hs.editSlices ([] (auto& s) { s.grid (4); });
        setParam (hs, ParamIDs::attack, 0.0f);
        setParam (hs, ParamIDs::release, 1.0f);
        setParam (hs, ParamIDs::playMode, 1.0f);
        setParam (hs, ParamIDs::seqMode, 1.0f); // host transport
        hs.editSeqPattern (0, [] (auto& pat) { for (int i = 0; i < 16; i += 4) pat.lanes[0].steps[(size_t) i] = { true, 1.0f }; });

        auto hostRun = [&] (int blocks, double loopEnd = 0.0)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buf (2, block);
            for (int blk = 0; blk < blocks; ++blk)
            {
                juce::MidiBuffer midi;
                hs.processBlock (buf, midi);
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
                host.ppq += block * host.bpm / 60.0 / rate;
                if (loopEnd > 0.0 && host.ppq >= loopEnd)
                    host.ppq -= loopEnd; // loop back to the start
            }
            return out;
        };
        auto onsets = [] (const std::vector<float>& y)
        {
            std::vector<int> v;
            for (size_t i = 0; i < y.size(); ++i) if (y[i] != 0.0f && (i == 0 || y[i - 1] == 0.0f)) v.push_back ((int) i);
            return v;
        };

        CHECK (onsets (hostRun (100)).empty()); // host stopped: silent
        host.playing = true;
        host.ppq = 0.0;
        auto on = onsets (hostRun (200));
        CHECK (on.size() >= 4 && on[0] == 0 && on[1] == 24000 && on[2] == 48000); // quarter notes at 120 BPM
        host.ppq = 0.0;
        host.bpm = 60.0;
        on = onsets (hostRun (200));
        CHECK (on.size() >= 2 && on[0] == 0 && on[1] == 48000);                   // 60 BPM: twice as far apart
        host.bpm = 120.0;
        host.ppq = 0.0;
        on = onsets (hostRun (400, 2.0));                                          // 2-beat host loop
        std::printf ("  host loop onsets: %zu (first %d %d %d)\n", on.size(), on.size() > 0 ? on[0] : -1, on.size() > 1 ? on[1] : -1,
                     on.size() > 2 ? on[2] : -1);
        CHECK (on.size() >= 8);
        host.playing = false;
        auto tail = hostRun (20);
        CHECK (std::all_of (tail.begin() + block * 2, tail.end(), [] (float v) { return v == 0.0f; })); // stop: no stuck notes
        hs.setPlayHead (nullptr);
    }

    // ---- Performance (spec 28.3): worst-case load vs real time ----
    {
        writeTestWav (wavFile);
        NeuroMPCProcessor perf;
        perf.prepareToPlay (rate, block);
        CHECK (perf.loadSample (wavFile, error));
        perf.editSlices ([] (auto& s) { s.grid (16); });
        setParam (perf, ParamIDs::lfoDepth, 2.0f);           // per-sample pitch modulation on every voice
        setParam (perf, ParamIDs::arpOn, 1.0f);
        setParam (perf, ParamIDs::seqMode, 2.0f);
        setParam (perf, ParamIDs::seqRate, 5.0f);            // 1/32
        perf.editSeqPattern (0, [] (auto& pat)
        {
            pat.numLanes = 16;
            for (auto& lane : pat.lanes)
                for (int i = 0; i < 16; ++i)
                    lane.steps[(size_t) i] = { true, 0.8f, 1.0f, 1.0f, 4, 0.0f, 3.0f }; // every step, ratchet x4
        });
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer held;
        for (int n = 40; n < 52; ++n) held.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 0);
        perf.processBlock (buf, held);

        const int blocks = (int) (10.0 * rate / block); // 10 s of audio
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer none;
            perf.processBlock (buf, none);
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto load = ms / 10000.0 * 100.0;
        std::printf ("  worst case (32 voices + LFO + 16-lane 1/32 x4 ratchets + arp): 10 s audio in %.0f ms = %.2f%% of one core\n", ms, load);
        CHECK (load < 25.0);
    }

    // Missing file on restore is reported, not substituted.
    wavFile.deleteFile();
    NeuroMPCProcessor m;
    m.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (m.getSample() == nullptr && m.getMissingSamplePath() == wavFile.getFullPathName());

    std::printf (failures == 0 ? "All engine tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
