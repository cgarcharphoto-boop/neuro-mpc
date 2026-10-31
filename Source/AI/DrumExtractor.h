#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// AI drum extraction with Demucs (htdemucs fine-tuned drums model, MIT) through demucs.cpp (MIT, Eigen): pulls the drum
// stem out of a full mix. Runs in-process, no Python. Slow (seconds per second of audio): call from a worker thread.
// This header is Eigen-free; the implementation is compiled in its own library with third-party warnings off.
namespace neuro::demucs
{

inline constexpr int sampleRate = 44100; // the model's rate: resample before and after

struct Job
{
    std::atomic<float> progress { 0.0f }; // 0..1
    std::atomic<bool> cancel { false };
};

// Stereo planar input at 44.1 kHz. Long input is split across threads (overlapping, cross-faded). The model file is
// loaded once and cached. Returns false on error or cancel (error says which).
bool extractDrums (const std::string& modelFile, const float* left, const float* right, int64_t numSamples,
                   std::vector<float>& outLeft, std::vector<float>& outRight, Job& job, std::string& error);

} // namespace neuro::demucs
