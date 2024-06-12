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
            cv.wait(lock, [this] { return threadsShouldStop || !tasks.empty(); });
            if (threadsShouldStop && tasks.empty())
                return;
            task = tasks.top();
            tasks.pop();
        }
        task.func();
    }
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
