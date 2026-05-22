#include "catch.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "../src/Util/ThreadPool.h"

using namespace std::chrono_literals;

TEST_CASE("ThreadPool runs lower numeric priority before queued lower-priority work",
          "[ThreadPool]")
{
    ThreadPool pool{1};
    std::mutex mutex;
    std::condition_variable cv;
    bool blockerStarted = false;
    bool releaseBlocker = false;
    std::vector<int> order;

    auto blocker = pool.addTask([&]
    {
        std::unique_lock lock(mutex);
        blockerStarted = true;
        cv.notify_all();
        cv.wait(lock, [&] { return releaseBlocker; });
    });

    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, 1s, [&] { return blockerStarted; }));
    }

    auto low = pool.addTaskWithPriority(100, [&]
    {
        std::lock_guard lock(mutex);
        order.push_back(100);
    });
    auto high = pool.addTaskWithPriority(-100, [&]
    {
        std::lock_guard lock(mutex);
        order.push_back(-100);
    });

    {
        std::lock_guard lock(mutex);
        releaseBlocker = true;
    }
    cv.notify_all();

    blocker.get();
    high.get();
    low.get();

    CHECK(order == std::vector<int>{-100, 100});
}
