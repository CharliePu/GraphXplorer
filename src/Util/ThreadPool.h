//
// Created by charl on 6/3/2024.
//

#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>
#include <atomic>

// Custom comparator for the priority queue
struct TaskWrapper {
    std::function<void()> func;
    int priority;

    bool operator<(const TaskWrapper& other) const {
        return priority < other.priority; // Lower priority number means higher priority
    }
};

class ThreadPool
{
public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency());

    ~ThreadPool();

    template<class F, class... Args>
    auto addTask(F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...> >;

    template<class F>
    bool tryAddTask(F &&f, int priority = 0);

    [[nodiscard]] size_t workerCount() const;
    [[nodiscard]] size_t idleWorkerCount() const;

    void markAllTasksLowPriority();
    void clearAllTasks();

private:
    std::vector<std::thread> threads;
    std::priority_queue<TaskWrapper> tasks;

    std::mutex queueMutex;
    std::condition_variable cv;
    std::atomic<bool> threadsShouldStop;
    std::atomic<size_t> idleThreads{0};

    void threadLoop();
};

template<class F, class... Args>
auto ThreadPool::addTask(F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...> >
{
    using returnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<returnType()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<returnType> res = task->get_future();

    {
        std::scoped_lock lock(queueMutex);
        tasks.emplace(TaskWrapper{[task]() { (*task)(); }, 0});
    }

    cv.notify_one();
    return res;
}

template<class F>
bool ThreadPool::tryAddTask(F &&f, const int priority)
{
    {
        std::scoped_lock lock(queueMutex);
        const auto idle = idleThreads.load(std::memory_order_relaxed);
        if (threadsShouldStop.load(std::memory_order_relaxed)
            || idle == 0
            || tasks.size() >= idle)
        {
            return false;
        }

        tasks.emplace(TaskWrapper{std::forward<F>(f), priority});
    }

    cv.notify_one();
    return true;
}

#endif //THREADPOOL_H
