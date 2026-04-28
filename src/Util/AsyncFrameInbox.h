//
// Created by Codex on 3/1/2026.
//

#ifndef ASYNCFRAMEINBOX_H
#define ASYNCFRAMEINBOX_H

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

template<typename T>
class AsyncFrameInbox
{
public:
    enum class Mode
    {
        HandleAll,
        HandleN,
        LatestOnly
    };

    struct DrainPolicy
    {
        Mode mode{Mode::HandleN};
        size_t handleCount{8};
    };

    explicit AsyncFrameInbox(const DrainPolicy &policy = {}): drainPolicy{normalize(policy)}
    {
    }

    void setDrainPolicy(const DrainPolicy &policy)
    {
        std::lock_guard lock(mutex);
        drainPolicy = normalize(policy);
    }

    [[nodiscard]] DrainPolicy getDrainPolicy() const
    {
        std::lock_guard lock(mutex);
        return drainPolicy;
    }

    void push(T item)
    {
        std::lock_guard lock(mutex);
        queue.push_back(std::move(item));
    }

    template<typename InputIt>
    void pushRange(InputIt first, InputIt last)
    {
        std::lock_guard lock(mutex);
        for (auto it = first; it != last; ++it)
        {
            queue.push_back(std::move(*it));
        }
    }

    [[nodiscard]] size_t pendingCount() const
    {
        std::lock_guard lock(mutex);
        return queue.size();
    }

    [[nodiscard]] bool empty() const
    {
        return pendingCount() == 0;
    }

    std::vector<T> drainForFrame()
    {
        std::deque<T> drained;
        {
            std::lock_guard lock(mutex);

            if (queue.empty())
            {
                return {};
            }

            switch (drainPolicy.mode)
            {
            case Mode::LatestOnly:
                drained.push_back(std::move(queue.back()));
                queue.clear();
                break;
            case Mode::HandleN:
            {
                const auto count = std::min(drainPolicy.handleCount, queue.size());
                for (size_t i = 0; i < count; ++i)
                {
                    drained.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
                break;
            }
            case Mode::HandleAll:
                drained.swap(queue);
                break;
            }
        }

        std::vector<T> result;
        result.reserve(drained.size());
        std::move(drained.begin(), drained.end(), std::back_inserter(result));
        return result;
    }

private:
    [[nodiscard]] static DrainPolicy normalize(DrainPolicy policy)
    {
        if (policy.mode == Mode::HandleN && policy.handleCount == 0)
        {
            policy.handleCount = 1;
        }
        return policy;
    }

    mutable std::mutex mutex;
    std::deque<T> queue;
    DrainPolicy drainPolicy;
};

#endif // ASYNCFRAMEINBOX_H

