#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// MIDI clock (24 PPQN) for running in unison with an MPC or any clock master/slave. Real-time safe, no allocation.
namespace neuro
{

// Follower: Start / Continue / Stop / Song Position Pointer / ticks -> transport state, tempo and beat position at any
// sample time. The first tick after Start (or Continue) is the start position; between ticks the position is
// extrapolated with the smoothed tick period, so it is sample-accurate rather than tick-quantised.
class MidiClockFollower
{
public:
    void prepare (double newSampleRate) noexcept { sampleRate = newSampleRate; reset(); }
    void reset() noexcept { running = waiting = false; songPosition = anchorPpq = 0.0; period = 0.0; lastTick = -1; }

    // time: absolute sample time of the message.
    void handle (int64_t time, uint8_t status, int songPositionSixteenths = 0) noexcept
    {
        switch (status)
        {
            case 0xfa: songPosition = 0.0; waiting = true; running = false; break;             // Start
            case 0xfb: waiting = true; running = false; break;                                 // Continue
            case 0xfc: running = waiting = false; break;                                       // Stop
            case 0xf2: songPosition = songPositionSixteenths / 4.0; break;                     // Song Position Pointer
            case 0xf8: tick (time); break;                                                     // Clock
            default: break;
        }
    }

    // Transport running: started, and ticks still arriving (a master that disappears stops us after 0.5 s).
    bool isRunning (int64_t now) const noexcept { return running && now - lastTick < (int64_t) (0.5 * sampleRate); }
    bool hasTempo() const noexcept { return period > 0.0; }
    double getBpm() const noexcept { return period > 0.0 ? 60.0 * sampleRate / (24.0 * period) : 120.0; }

    double ppqAt (int64_t time) const noexcept
    {
        const auto samplesPerBeat = (period > 0.0 ? period : sampleRate / 48.0) * 24.0; // 120 BPM until measured
        return anchorPpq + (double) (time - lastTick) / samplesPerBeat;
    }

private:
    void tick (int64_t time) noexcept
    {
        if (lastTick >= 0)
        {
            const auto interval = (double) (time - lastTick);

            // Smoothed tick period (jitter on USB MIDI is ~1 ms); a big tempo change re-locks immediately.
            if (interval > 0.0 && interval < sampleRate) // ignore gaps (stopped clock)
                period = period <= 0.0 || std::abs (interval - period) > 0.25 * period ? interval : period + 0.1 * (interval - period);
        }

        if (waiting)
        {
            waiting = false;
            running = true;
            anchorPpq = songPosition;
        }
        else if (running)
        {
            anchorPpq += 1.0 / 24.0;
        }

        lastTick = time;
    }

    double sampleRate = 48000.0;
    bool running = false, waiting = false;
    double songPosition = 0.0, anchorPpq = 0.0, period = 0.0;
    int64_t lastTick = -1;
};

// Leader: emits 24 PPQN ticks for a beat range (one block), plus Start / Stop around transport changes.
struct MidiClockLeader
{
    bool wasRunning = false;

    // sink (offset, status) for every message in [beat, beat + numSamples * beatsPerSample).
    template <typename Sink>
    void process (bool running, double beat, double beatsPerSample, int numSamples, Sink&& sink)
    {
        if (running && ! wasRunning)
            sink (0, (uint8_t) 0xfa);
        else if (! running && wasRunning)
            sink (0, (uint8_t) 0xfc);

        wasRunning = running;

        if (! running || beatsPerSample <= 0.0)
            return;

        const auto endBeat = beat + numSamples * beatsPerSample;

        for (auto k = (int64_t) std::ceil (beat * 24.0 - 1.0e-9); (double) k / 24.0 < endBeat; ++k)
            sink (std::clamp ((int) std::lround (((double) k / 24.0 - beat) / beatsPerSample), 0, numSamples - 1), (uint8_t) 0xf8);
    }
};

} // namespace neuro
