//
// Created by charl on 6/3/2024.
//

#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>


class ThreadPool
{
public:
    ThreadPool();

    ~ThreadPool();

    template<class F, class... Args>
    auto addTask(F &&f, Args &&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

private:
    std::vector<std::thread> threads;
    std::queue<std::function<void()> > tasks;

    std::mutex queueMutex;
    std::condition_variable cv;
    std::atomic<bool> threadsShouldStop;

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
        tasks.emplace([task]() { (*task)(); });
    }

    cv.notify_one();
    return res;
}


#endif //THREADPOOL_H
