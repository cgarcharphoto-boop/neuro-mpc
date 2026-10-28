#pragma once

#include <atomic>
#include <memory>
#include <vector>

namespace neuro
{

// Lock-free handoff of immutable data from the message thread to the audio thread.
// The audio thread publishes the pointer it is using (a single hazard pointer); the message thread
// owns every allocation and frees only objects that are neither current nor in use, so the audio
// thread never allocates, frees or blocks.
template <typename T>
class RtShared
{
public:
    // Message thread only.
    void publish (std::shared_ptr<const T> next)
    {
        current.store (next.get());
        pool.push_back (std::move (next));
        collectGarbage();
    }

    // Message thread only.
    void collectGarbage()
    {
        const auto* cur  = current.load();
        const auto* used = inUse.load();
        std::erase_if (pool, [&] (const auto& p) { return p.get() != cur && p.get() != used; });
    }

    // Audio thread: call once per block. The pointer stays valid until the next acquire().
    const T* acquire() noexcept
    {
        const T* p;

        do
        {
            p = current.load();
            inUse.store (p);
        }
        while (p != current.load());

        return p;
    }

    size_t poolSize() const noexcept { return pool.size(); }

private:
    std::atomic<const T*> current { nullptr }, inUse { nullptr };
    std::vector<std::shared_ptr<const T>> pool;
};

} // namespace neuro
