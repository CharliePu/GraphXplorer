#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t numThreads) : threadsShouldStop(false)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(&ThreadPool::threadLoop, this);
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::scoped_lock lock(queueMutex);
        threadsShouldStop = true;
        while (!tasks.empty())
        {
            tasks.pop();
        }
    }
    cv.notify_all();
    for (auto &thread : threads)
    {
        if (thread.joinable())
            thread.join();
    }
}

void ThreadPool::threadLoop()
{
    while (true)
    {
        TaskWrapper task;
        {
            std::unique_lock lock(queueMutex);
            idleThreads.fetch_add(1, std::memory_order_relaxed);
            cv.wait(lock, [this] { return threadsShouldStop || !tasks.empty(); });
            idleThreads.fetch_sub(1, std::memory_order_relaxed);
            if (threadsShouldStop)
            {
                return;
            }
            task = tasks.top();
            tasks.pop();
        }
        task.func();
    }
}

size_t ThreadPool::workerCount() const
{
    return threads.size();
}

size_t ThreadPool::idleWorkerCount() const
{
    return idleThreads.load(std::memory_order_relaxed);
}

void ThreadPool::markAllTasksLowPriority()
{
    std::scoped_lock lock(queueMutex);
    std::priority_queue<TaskWrapper> newTasks;
    while (!tasks.empty())
    {
        TaskWrapper task = tasks.top();
        tasks.pop();
        task.priority = INT_MAX; // Set to lowest priority
        newTasks.push(task);
    }
    std::swap(tasks, newTasks);
}

void ThreadPool::clearAllTasks()
{
    std::scoped_lock lock(queueMutex);
    while (!tasks.empty())
    {
        tasks.pop();
    }
}
