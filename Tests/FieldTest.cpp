// Field test (not part of ctest): real drum loops, KIT tab snapshot, and the installed VST3/AU hosted through JUCE.
// Usage: neuro_field_test <outputDir> <loop.caf/wav>...
#include "PluginEditor.h"
#include <CoreFoundation/CoreFoundation.h>

namespace
{
    // Hands NEURO's own state to a hosted instance the way a DAW does: wrapped in the host format's state container.
    bool loadNeuroState (juce::AudioPluginInstance& instance, const juce::MemoryBlock& ours, const juce::String& formatName)
    {
        juce::MemoryBlock hostState;
        instance.getStateInformation (hostState);

        if (formatName == "VST3")
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
            if (xml == nullptr || ! xml->hasTagName ("VST3PluginState")) return false;
            if (auto* component = xml->getChildByName ("IComponent")) xml->removeChildElement (component, true);
            xml->createNewChildElement ("IComponent")->addTextElement (ours.toBase64Encoding());
            juce::MemoryBlock wrapped;
            juce::AudioProcessor::copyXmlToBinary (*xml, wrapped);
            instance.setStateInformation (wrapped.getData(), (int) wrapped.getSize());
            return true;
        }

        // AudioUnit: the ClassInfo property list, with NEURO's bytes under JUCE's "jucePluginState" key.
        auto data = CFDataCreate (nullptr, (const UInt8*) hostState.getData(), (CFIndex) hostState.getSize());
        auto plist = CFPropertyListCreateWithData (nullptr, data, kCFPropertyListMutableContainersAndLeaves, nullptr, nullptr);
        CFRelease (data);
        if (plist == nullptr || CFGetTypeID (plist) != CFDictionaryGetTypeID()) return false;
        auto ourData = CFDataCreate (nullptr, (const UInt8*) ours.getData(), (CFIndex) ours.getSize());
        CFDictionarySetValue ((CFMutableDictionaryRef) plist, CFSTR ("jucePluginState"), ourData);
        CFRelease (ourData);
        auto out = CFPropertyListCreateData (nullptr, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
        CFRelease (plist);
        if (out == nullptr) return false;
        instance.setStateInformation (CFDataGetBytePtr (out), (int) CFDataGetLength (out));
        CFRelease (out);
        return true;
    }

    // Fraction of a class's hits on given 16th positions within the bar (16 steps).
    float fractionOn (const neuro::SeqLane& lane, std::initializer_list<int> positions)
    {
        int on = 0, hit = 0;
        for (int i = 0; i < lane.length; ++i)
            if (lane.steps[(size_t) i].on)
            {
                ++on;
                for (int p : positions) hit += (i % 16) == p ? 1 : 0;
            }
        return on > 0 ? (float) hit / (float) on : -1.0f;
    }

    void realLoops (const juce::StringArray& files)
    {
        std::printf ("\n== Real drum loops (Apple Logic Drummer recordings) ==\n");
        for (const auto& path : files)
        {
            NeuroMPCProcessor p;
            juce::String error;
            const juce::File f (path);
            std::unique_ptr<juce::AudioFormatReader> reader (p.formats.createReaderFor (f));
            double bpm = 120.0;
            if (reader != nullptr)
                for (auto key : reader->metadataValues.getAllKeys())
                    if (key.containsIgnoreCase ("tempo") || key.containsIgnoreCase ("bpm"))
                        bpm = juce::jmax (40.0, reader->metadataValues[key].getDoubleValue());
            if (! p.loadSample (f, error)) { std::printf ("%s: %s\n", f.getFileName().toRawUTF8(), error.toRawUTF8()); continue; }

            const auto seconds = (double) p.getSample()->length() / p.getSample()->sampleRate;
            for (int pass = 0; pass < 2; ++pass) // generic, then taught (examples a user would pick in bar 1)
            {
                if (pass == 1)
                {
                    const auto spb = p.getSample()->sampleRate * 60.0 / (bpm != 120.0 ? bpm : 32.0 * 60.0 / seconds);
                    p.loadSample (f, error);
                    p.teachDrumRange (neuro::nmf::kick, 0, (int64_t) (0.1 * p.getSample()->sampleRate));
                    p.teachDrumRange (neuro::nmf::snare, (int64_t) spb, (int64_t) (spb + 0.1 * p.getSample()->sampleRate));
                    p.teachDrumRange (neuro::nmf::cymbals, (int64_t) (spb / 2), (int64_t) (spb / 2 + 0.1 * p.getSample()->sampleRate));
                }
                const auto r = p.separateDrums (0); // NMF separation + transcription, auto tempo
                std::printf ("  tempo: estimated %.1f BPM, 8-bar ground truth %.1f BPM%s\n", r.bpm, 32.0 * 60.0 / seconds,
                             bpm != 120.0 ? (" (file tag " + juce::String (bpm) + ")").toRawUTF8() : "");
                const auto& pat = p.getSeqPattern (0);
                std::printf ("\n[Separate drums%s] %s  (%.2f s, %.0f BPM from file metadata%s) -> %d hits, %d steps\n", pass == 1 ? ", taught" : "", f.getFileNameWithoutExtension().toRawUTF8(),
                             seconds, bpm, bpm == 120.0 ? "?" : "", r.hits, r.steps);
                for (int c = 0; c < neuro::numDrumClasses; ++c)
                {
                    const auto& lane = pat.lanes[(size_t) c];
                    if (r.perClass[(size_t) c] == 0) continue;
                    juce::String grid;
                    for (int i = 0; i < juce::jmin (32, lane.length); ++i) grid << (lane.steps[(size_t) i].on ? (i % 4 == 0 ? "X" : "x") : (i % 4 == 0 ? "|" : "."));
                    std::printf ("  %-10s %3d  %s\n", neuro::drumClassName ((neuro::DrumClass) c), r.perClass[(size_t) c], grid.toRawUTF8());
                }
                const auto kickOnDownbeats = fractionOn (pat.lanes[(size_t) neuro::DrumClass::kick], { 0, 8 });
                const auto snareOnBackbeat = fractionOn (pat.lanes[(size_t) neuro::DrumClass::snare], { 4, 12 });
                std::printf ("  groove check: kicks on beats 1/3: %s   snares on beats 2/4: %s\n",
                             kickOnDownbeats < 0 ? "n/a" : juce::String (juce::roundToInt (kickOnDownbeats * 100)).toRawUTF8(),
                             snareOnBackbeat < 0 ? "n/a" : juce::String (juce::roundToInt (snareOnBackbeat * 100)).toRawUTF8());
            }
        }
    }

    void kitSnapshot (const juce::File& outDir, const juce::String& loop)
    {
        NeuroMPCProcessor p;
        juce::String error;
        p.loadSample (juce::File (loop), error);
        p.deconstructLoop (0, 120.0);
        p.waitForBackgroundWork (30000);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<NeuroMPCEditor*> (ed.get());
        for (int tab : { 4, 6 })
        {
            editor->showTab (tab);
            editor->setSize (1140, 840);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = outDir.getChildFile (tab == 6 ? "kit_tab.png" : "seq_tab.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
            std::printf ("snapshot: %s\n", file.getFullPathName().toRawUTF8());
        }
    }

    // Loads the installed plugin binary through the real VST3 / AU hosting APIs and checks the per-class outputs.
    void hostedOutputs (const juce::String& formatName, const juce::String& wav)
    {
        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* format : formats.getFormats())
        {
            if (format->getName() != formatName) continue;
            const auto location = formatName == "VST3" ? juce::File ("~/Library/Audio/Plug-Ins/VST3/NEURO MPC.vst3").getFullPathName()
                                                       : juce::String ("AudioUnit:Synths/aumu,Nm26,Nrmp");
            format->findAllTypesForFile (found, location);
        }
        std::printf ("\n== Hosted %s: %d plugin(s) found ==\n", formatName.toRawUTF8(), found.size());
        if (found.isEmpty()) return;

        juce::String error;
        auto instance = formats.createPluginInstance (*found[0], 48000.0, 512, error);
        if (instance == nullptr) { std::printf ("  load failed: %s\n", error.toRawUTF8()); return; }
        std::printf ("  loaded '%s', %d output buses:", instance->getName().toRawUTF8(), instance->getBusCount (false));
        for (int b = 0; b < instance->getBusCount (false); ++b) std::printf (" %s", instance->getBus (false, b)->getName().toRawUTF8());
        auto layout = instance->getBusesLayout();
        for (auto& bus : layout.outputBuses) bus = juce::AudioChannelSet::stereo();
        const auto enabled = instance->setBusesLayout (layout);
        std::printf ("\n  enable all 12 stereo outputs: %s (%d output channels)\n", enabled ? "OK" : "REFUSED", instance->getTotalNumOutputChannels());

        // State: a 4-slice kit (kick, snare, closed hat, kick) with explicit class overrides, pads following slices.
        auto xml = juce::parseXML ("<NEURO_MPC version=\"2\"><SAMPLE path=\"" + wav + "\"/><SLICES>"
                                   "<SLICE id=\"1\" start=\"0\" end=\"12000\" drumClass=\"0\"/><SLICE id=\"2\" start=\"12000\" end=\"24000\" drumClass=\"1\"/>"
                                   "<SLICE id=\"3\" start=\"24000\" end=\"36000\" drumClass=\"2\"/><SLICE id=\"4\" start=\"36000\" end=\"48000\" drumClass=\"0\"/></SLICES></NEURO_MPC>");
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
        std::printf ("  state delivered in %s format: %s\n", formatName.toRawUTF8(), loadNeuroState (*instance, state, formatName) ? "OK" : "FAILED");
        instance->prepareToPlay (48000.0, 512);
        for (auto [note, name] : { std::pair { 36, "kick" }, { 37, "snare" }, { 38, "closed hat" } })
        {
            juce::AudioBuffer<float> buffer (instance->getTotalNumOutputChannels(), 512);
            std::vector<float> busPeak ((size_t) instance->getBusCount (false), 0.0f);
            for (int block = 0; block < 30; ++block)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (block == 0) { midi.addEvent (juce::MidiMessage::allNotesOff (1), 0); midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0); }
                instance->processBlock (buffer, midi);
                for (int b = 0; b < instance->getBusCount (false); ++b)
                    busPeak[(size_t) b] = juce::jmax (busPeak[(size_t) b], instance->getBusBuffer (buffer, false, b).getMagnitude (0, 512));
            }
            std::printf ("  %-10s ->", name);
            for (int b = 0; b < instance->getBusCount (false); ++b)
                if (busPeak[(size_t) b] > 0.0f) std::printf (" %s %.3f", instance->getBus (false, b)->getName().toRawUTF8(), busPeak[(size_t) b]);
            std::printf ("\n");
        }
        instance->releaseResources();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4) { std::printf ("usage: neuro_field_test <outDir> <kitWav> <loop>...\n"); return 1; }
    const juce::File outDir (argv[1]);
    outDir.createDirectory();
    juce::StringArray loops;
    for (int i = 3; i < argc; ++i) loops.add (argv[i]);
    hostedOutputs ("VST3", argv[2]);
    hostedOutputs ("AudioUnit", argv[2]);
    realLoops (loops);
    kitSnapshot (outDir, loops[0]);
    return 0;
}
