#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <map>
#include <mutex>

namespace neuro
{

// Single background thread running Job -> Result. Requests coalesce by id: a newer request for the same id replaces a
// pending one, so rapid edits only compute the latest state. Results are collected by polling (message thread).
template <typename Job, typename Result>
class BackgroundWorker : private juce::Thread
{
public:
    using Function = std::function<Result (const Job&)>;

    BackgroundWorker (const juce::String& name, Function f) : juce::Thread (name), fn (std::move (f))
    {
        startThread (juce::Thread::Priority::low);
    }

    ~BackgroundWorker() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (10000);
    }

    void request (int id, Job job)
    {
        {
            std::scoped_lock lock (mutex);
            pending[id] = std::move (job);
        }

        notify();
    }

    std::vector<Result> takeCompleted()
    {
        std::scoped_lock lock (mutex);
        return std::exchange (done, {});
    }

    bool isIdle() const
    {
        std::scoped_lock lock (mutex);
        return pending.empty() && ! working;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::optional<Job> job;

            {
                std::scoped_lock lock (mutex);

                if (! pending.empty())
                {
                    job = std::move (pending.begin()->second);
                    pending.erase (pending.begin());
                    working = true;
                }
            }

            if (! job)
            {
                wait (-1);
                continue;
            }

            auto result = fn (*job);
            std::scoped_lock lock (mutex);
            done.push_back (std::move (result));
            working = false;
        }
    }

    Function fn;
    mutable std::mutex mutex;
    std::map<int, Job> pending;
    std::vector<Result> done;
    bool working = false;
};

} // namespace neuro
