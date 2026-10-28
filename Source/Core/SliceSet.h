#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuro
{

// Persistent per-slice configuration. Runtime voice state lives in VoiceEngine, never here.
struct SliceDefinition
{
    int id = 0;
    int64_t startSample = 0;
    int64_t endSample = 0;

    float gain = 1.0f;
    float pitchSemitones = 0.0f;
    float timeStretchRatio = 1.0f; // 1.0 = original duration, 2.0 = twice as long
    bool preserveTransients = true; // stretch each transient-delimited segment separately (no pre-echo)
    bool reverse = false;
    bool enabled = true;

    int64_t length() const noexcept { return endSample - startSample; }
    bool isStretched() const noexcept { return std::abs (timeStretchRatio - 1.0f) > 1.0e-4f; }
};

// Contiguous, non-overlapping slices covering [0, totalLength).
// Plain value type: edited on the message thread, published to the audio thread as an immutable copy.
class SliceSet
{
public:
    static constexpr int64_t minSliceLength = 64;
    static constexpr float minStretch = 0.25f, maxStretch = 4.0f;

    SliceSet() = default;
    explicit SliceSet (int64_t total) { reset (total); }

    void reset (int64_t total)
    {
        totalLength = std::max<int64_t> (0, total);
        slices.clear();
        nextId = 1;

        if (totalLength > 0)
            slices.push_back (make (0, totalLength));
    }

    int64_t getTotalLength() const noexcept                 { return totalLength; }
    const std::vector<SliceDefinition>& getSlices() const   { return slices; }
    int size() const noexcept                               { return (int) slices.size(); }

    int indexAt (int64_t pos) const noexcept
    {
        if (pos < 0 || pos >= totalLength)
            return -1;

        auto it = std::upper_bound (slices.begin(), slices.end(), pos,
                                    [] (int64_t p, const SliceDefinition& s) { return p < s.startSample; });
        return (int) (it - slices.begin()) - 1;
    }

    // Inserts a boundary at pos, inheriting the parent slice's settings. Fails if either side would be too short.
    bool split (int64_t pos)
    {
        auto i = indexAt (pos);

        if (i < 0)
            return false;

        auto& s = slices[(size_t) i];

        if (pos - s.startSample < minSliceLength || s.endSample - pos < minSliceLength)
            return false;

        auto added = s;
        added.id = nextId++;
        added.startSample = pos;
        s.endSample = pos;
        slices.insert (slices.begin() + i + 1, added);
        return true;
    }

    // Moves the start of slice i (i >= 1), clamped so neither neighbour drops below minSliceLength.
    bool moveStart (int i, int64_t pos)
    {
        if (i < 1 || i >= size())
            return false;

        auto& prev = slices[(size_t) i - 1];
        auto& cur  = slices[(size_t) i];
        auto lo = prev.startSample + minSliceLength;
        auto hi = cur.endSample - minSliceLength;

        if (lo > hi)
            return false;

        prev.endSample = cur.startSample = std::clamp (pos, lo, hi);
        return true;
    }

    // Output duration = original duration * ratio. Affects only slice i.
    bool setStretch (int i, float ratio)
    {
        if (i < 0 || i >= size())
            return false;

        slices[(size_t) i].timeStretchRatio = std::clamp (ratio, minStretch, maxStretch);
        return true;
    }

    bool setPreserveTransients (int i, bool preserve)
    {
        if (i < 0 || i >= size())
            return false;

        slices[(size_t) i].preserveTransients = preserve;
        return true;
    }

    // Removes the boundary at the start of slice i (i >= 1), merging it into the previous slice.
    bool mergeWithPrevious (int i)
    {
        if (i < 1 || i >= size())
            return false;

        slices[(size_t) i - 1].endSample = slices[(size_t) i].endSample;
        slices.erase (slices.begin() + i);
        return true;
    }

    // Rebuilds from a set of start positions (any order); positions that violate minSliceLength are dropped.
    void setStarts (std::vector<int64_t> starts)
    {
        std::sort (starts.begin(), starts.end());
        reset (totalLength);

        for (auto s : starts)
            split (s);
    }

    void grid (int numSlices)
    {
        std::vector<int64_t> starts;

        for (int k = 1; k < numSlices; ++k)
            starts.push_back (totalLength * k / numSlices);

        setStarts (std::move (starts));
    }

    // Restores serialized slices; falls back to a single slice if the data is not a valid contiguous cover.
    bool restore (int64_t total, std::vector<SliceDefinition> restored)
    {
        reset (total);

        if (restored.empty() || restored.front().startSample != 0 || restored.back().endSample != total)
            return false;

        int maxId = 0;

        for (size_t i = 0; i < restored.size(); ++i)
        {
            auto& s = restored[i];
            s.timeStretchRatio = std::clamp (s.timeStretchRatio, minStretch, maxStretch);

            if (s.id <= 0 || s.length() < minSliceLength || (i > 0 && restored[i - 1].endSample != s.startSample))
                return false;

            maxId = std::max (maxId, s.id);
        }

        slices = std::move (restored);
        nextId = maxId + 1;
        return true;
    }

private:
    SliceDefinition make (int64_t start, int64_t end)
    {
        SliceDefinition s;
        s.id = nextId++;
        s.startSample = start;
        s.endSample = end;
        return s;
    }

    int64_t totalLength = 0;
    int nextId = 1;
    std::vector<SliceDefinition> slices;
};

} // namespace neuro
