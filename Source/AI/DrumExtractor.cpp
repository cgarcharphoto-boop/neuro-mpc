#include "DrumExtractor.h"

#include "model.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

namespace neuro::demucs
{

namespace
{
    struct Cancelled {};

    // demucs_model is large (Eigen tensors): loaded once per file and kept for later runs.
    std::mutex modelMutex;
    std::unique_ptr<demucscpp::demucs_model> cachedModel;
    std::string cachedPath;

    const demucscpp::demucs_model* loadModel (const std::string& file, std::string& error)
    {
        const std::scoped_lock lock (modelMutex);

        if (cachedModel == nullptr || cachedPath != file)
        {
            auto model = std::make_unique<demucscpp::demucs_model>();

            if (! demucscpp::load_demucs_model (file, model.get()) || ! model->is_4sources)
            {
                error = "Could not load the Demucs model (" + file + ")";
                return nullptr;
            }

            cachedModel = std::move (model);
            cachedPath = file;
        }

        return cachedModel.get();
    }
}

bool extractDrums (const std::string& modelFile, const float* left, const float* right, int64_t numSamples,
                   std::vector<float>& outLeft, std::vector<float>& outRight, Job& job, std::string& error)
{
    const auto* model = loadModel (modelFile, error);

    if (model == nullptr || numSamples <= 0)
        return false;

    // Split long input across threads: each chunk gets 1 s of context on both sides, chunks are cross-faded there.
    const auto n = (int) numSamples;
    const auto overlap = sampleRate;
    const auto cores = (int) std::max (1u, std::thread::hardware_concurrency());
    const auto numChunks = std::clamp (n / (20 * sampleRate), 1, std::max (1, cores / 2));
    const auto chunk = (n + numChunks - 1) / numChunks;

    std::vector<float> sum ((size_t) n * 2, 0.0f), weight ((size_t) n, 0.0f);
    std::vector<float> progress ((size_t) numChunks, 0.0f);
    std::vector<std::thread> threads;
    std::mutex mergeMutex;
    std::atomic<bool> failed { false };

    for (int c = 0; c < numChunks; ++c)
        threads.emplace_back ([&, c]
        {
            const auto from = std::max (0, c * chunk - overlap), to = std::min (n, (c + 1) * chunk + overlap);
            Eigen::MatrixXf audio (2, to - from);

            for (int i = from; i < to; ++i)
            {
                audio (0, i - from) = left[i];
                audio (1, i - from) = right[i];
            }

            try
            {
                const auto out = demucscpp::demucs_inference (*model, audio, [&] (float p, const std::string&)
                {
                    progress[(size_t) c] = p;
                    float total = 0.0f;
                    for (auto v : progress) total += v;
                    job.progress = total / (float) numChunks;
                    if (job.cancel) throw Cancelled {};
                });

                const std::scoped_lock lock (mergeMutex);

                for (int i = from; i < to; ++i)
                {
                    // Trapezoid: full weight in the chunk's own range, linear fade across the context on each side.
                    const auto core0 = c * chunk, core1 = std::min (n, (c + 1) * chunk);
                    auto w = 1.0f;
                    if (i < core0) w = 1.0f - (float) (core0 - i) / (float) (overlap + 1);
                    else if (i >= core1) w = 1.0f - (float) (i - core1 + 1) / (float) (overlap + 1);

                    sum[(size_t) i * 2] += w * out (0, 0, i - from); // target 0 = drums
                    sum[(size_t) i * 2 + 1] += w * out (0, 1, i - from);
                    weight[(size_t) i] += w;
                }
            }
            catch (...)
            {
                failed = true;
            }
        });

    for (auto& t : threads)
        t.join();

    if (failed)
    {
        error = job.cancel ? "Cancelled" : "Demucs inference failed";
        return false;
    }

    outLeft.resize ((size_t) n);
    outRight.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const auto w = std::max (1.0e-6f, weight[(size_t) i]);
        outLeft[(size_t) i] = sum[(size_t) i * 2] / w;
        outRight[(size_t) i] = sum[(size_t) i * 2 + 1] / w;
    }

    job.progress = 1.0f;
    return true;
}

} // namespace neuro::demucs
