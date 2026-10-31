#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

namespace neuro
{

// Virtual CoreMIDI source "NEURO MPC" (then "NEURO MPC 2", ...) that other apps and plug-ins (GW-1, KAOSS NEXUS, a DAW
// track, an MPC) can listen to: sequencer MIDI-out lanes and MIDI clock. The audio thread only pushes time-stamped
// messages into a lock-free FIFO; a 1 ms timer thread sends each one when its time comes, so the audio thread never
// touches CoreMIDI.
class MidiLinkPort : private juce::HighResolutionTimer
{
public:
    ~MidiLinkPort() override { close(); }

    bool open()
    {
        if (output != nullptr)
            return true;

        const auto n = ++openPorts;
        portName = n == 1 ? juce::String ("NEURO MPC") : "NEURO MPC " + juce::String (n);
        output = juce::MidiOutput::createNewDevice (portName);

        if (output == nullptr)
        {
            --openPorts;
            portName = {};
            return false;
        }

        fifo.reset();
        numPending = 0;
        isOpen = true;
        startTimer (1);
        return true;
    }

    void close()
    {
        isOpen = false;
        stopTimer(); // waits for a running callback

        if (output != nullptr)
        {
            output.reset();
            --openPorts;
        }

        portName = {};
    }

    bool opened() const noexcept { return isOpen; }
    juce::String getName() const { return portName; }

    // Audio thread: lock-free, no allocation; dropped when closed or full. timeMs: Time::getMillisecondCounterHiRes base.
    void push (const uint8_t* data, int size, double timeMs) noexcept
    {
        if (! isOpen || size < 1 || size > 3)
            return;

        const auto scope = fifo.write (1);

        if (scope.blockSize1 + scope.blockSize2 == 0)
            return;

        auto& m = ring[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        std::copy (data, data + size, m.data.begin());
        m.size = (uint8_t) size;
        m.timeMs = timeMs;
    }

private:
    struct Message { std::array<uint8_t, 3> data {}; uint8_t size = 0; double timeMs = 0.0; };

    void hiResTimerCallback() override
    {
        const auto scope = fifo.read (fifo.getNumReady());
        auto take = [this] (int start, int count)
        {
            for (int i = 0; i < count && numPending < (int) pending.size(); ++i)
                pending[(size_t) numPending++] = ring[(size_t) (start + i)];
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);

        // Due messages in time order (clock and notes of one block arrive interleaved).
        std::stable_sort (pending.begin(), pending.begin() + numPending, [] (const Message& a, const Message& b) { return a.timeMs < b.timeMs; });
        const auto now = juce::Time::getMillisecondCounterHiRes();
        int sent = 0;

        for (; sent < numPending && pending[(size_t) sent].timeMs <= now; ++sent)
            output->sendMessageNow (juce::MidiMessage (pending[(size_t) sent].data.data(), pending[(size_t) sent].size));

        std::move (pending.begin() + sent, pending.begin() + numPending, pending.begin());
        numPending -= sent;
    }

    static constexpr int capacity = 4096;
    inline static std::atomic<int> openPorts { 0 };
    std::unique_ptr<juce::MidiOutput> output;
    juce::String portName;
    std::atomic<bool> isOpen { false };
    juce::AbstractFifo fifo { capacity };
    std::array<Message, capacity> ring;
    std::array<Message, capacity> pending; // timer thread only
    int numPending = 0;
};

} // namespace neuro
