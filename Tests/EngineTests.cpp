// Headless integration tests: drive the real NeuroMPCProcessor with files, MIDI and state.
#include "PluginProcessor.h"

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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
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
