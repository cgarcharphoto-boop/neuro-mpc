#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace neuro
{

// MIDI Learn bindings: a note or CC (on one channel or any) drives a target. Targets are strings:
// "param:<id>" (host parameter, applied on the audio thread) or "ui:<id>" (an editor control, run on the message thread).
// A source drives at most one target; a target may have several sources.
struct MidiBinding
{
    enum class Source { note, cc };

    Source source = Source::cc;
    int channel = 0;      // 0 = any, else 1..16
    int number = 0;       // note or controller number
    std::string target;

    bool matches (Source s, int ch, int num) const noexcept
    {
        return source == s && number == num && (channel == 0 || channel == ch);
    }
};

class MidiMap
{
public:
    const std::vector<MidiBinding>& getBindings() const noexcept { return bindings; }

    // Binds a source to a target, replacing any earlier binding of that source (channel-specific or any).
    void learn (const std::string& target, MidiBinding::Source source, int channel, int number)
    {
        std::erase_if (bindings, [&] (const MidiBinding& b)
        {
            return b.source == source && b.number == number && (b.channel == 0 || channel == 0 || b.channel == channel);
        });
        bindings.push_back ({ source, std::clamp (channel, 0, 16), std::clamp (number, 0, 127), target });
    }

    void removeTarget (const std::string& target)
    {
        std::erase_if (bindings, [&] (const MidiBinding& b) { return b.target == target; });
    }

    void clear() noexcept { bindings.clear(); }

    // Index of the binding for this message, or -1.
    int find (MidiBinding::Source source, int channel, int number) const noexcept
    {
        for (size_t i = 0; i < bindings.size(); ++i)
            if (bindings[i].matches (source, channel, number))
                return (int) i;

        return -1;
    }

    std::vector<const MidiBinding*> bindingsFor (const std::string& target) const
    {
        std::vector<const MidiBinding*> result;

        for (const auto& b : bindings)
            if (b.target == target)
                result.push_back (&b);

        return result;
    }

    static std::string describe (const MidiBinding& b)
    {
        static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto source = b.source == MidiBinding::Source::cc
                                ? "CC " + std::to_string (b.number)
                                : std::string ("Note ") + names[b.number % 12] + std::to_string (b.number / 12 - 2) + " (" + std::to_string (b.number) + ")";
        return source + (b.channel == 0 ? ", any channel" : ", ch " + std::to_string (b.channel));
    }

private:
    std::vector<MidiBinding> bindings;
};

// Audio-thread lookup: O(1) per message. Built on the message thread from a MidiMap and published via RtShared.
struct MidiMapTable
{
    static constexpr int16_t none = -1;

    // [source][channel 0..16][number] -> binding index; channel 0 holds the any-channel bindings.
    std::array<std::array<std::array<int16_t, 128>, 17>, 2> slots;

    MidiMapTable()
    {
        for (auto& source : slots)
            for (auto& channel : source)
                channel.fill (none);
    }

    explicit MidiMapTable (const MidiMap& map) : MidiMapTable()
    {
        const auto& b = map.getBindings();

        for (size_t i = 0; i < b.size() && i < 32000; ++i)
            slots[(size_t) b[i].source][(size_t) b[i].channel][(size_t) b[i].number] = (int16_t) i;
    }

    int find (MidiBinding::Source source, int channel, int number) const noexcept
    {
        if (number < 0 || number > 127)
            return none;

        const auto& s = slots[(size_t) source];
        const auto specific = channel >= 1 && channel <= 16 ? s[(size_t) channel][(size_t) number] : none;
        return specific != none ? specific : s[0][(size_t) number];
    }
};

} // namespace neuro
