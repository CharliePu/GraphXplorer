//
// Created by charl on 6/3/2024.
//

#include "ThreadPool.h"

#include <future>
#include <mutex>

ThreadPool::ThreadPool():
    threadsShouldStop{false}
{
    for (unsigned int i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        threads.emplace_back(&ThreadPool::threadLoop, this);
    }
}

ThreadPool::~ThreadPool()
{
    threadsShouldStop = true;
    cv.notify_all();

    for (auto &thread : threads)
    {
        thread.join();
    }
}

void ThreadPool::threadLoop()
{
    while (!threadsShouldStop)
    {
        std::function<void()> task;

        {
            std::unique_lock lock{queueMutex};

            cv.wait(lock, [this] { return !tasks.empty() || threadsShouldStop; });

            if (threadsShouldStop)
            {
                return;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }

        task();
    }
}
