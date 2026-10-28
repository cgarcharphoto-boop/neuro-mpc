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
    CHECK (p.waitForStretchRenders (20000));
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
    CHECK (p.waitForStretchRenders (20000));
    CHECK (render (p, 4).nonZero == 0);
    CHECK (std::abs (render (p, 120, { { 0, 37, 1.0f } }).nonZero - 36000) <= 360);

    // Stretch survives save/restore.
    {
        juce::MemoryBlock stretched;
        p.getStateInformation (stretched);
        NeuroMPCProcessor q;
        q.prepareToPlay (rate, block);
        q.setStateInformation (stretched.getData(), (int) stretched.getSize());
        CHECK (q.waitForStretchRenders (20000));
        CHECK (q.getSlices().getSlices()[1].timeStretchRatio == 3.0f);
        CHECK (std::abs (render (q, 120, { { 0, 37, 1.0f } }).nonZero - 36000) <= 360);
    }

    p.editSlices ([] (auto& s) { s.reset (s.getTotalLength()); });

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

    // Missing file on restore is reported, not substituted.
    wavFile.deleteFile();
    NeuroMPCProcessor m;
    m.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (m.getSample() == nullptr && m.getMissingSamplePath() == wavFile.getFullPathName());

    std::printf (failures == 0 ? "All engine tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
