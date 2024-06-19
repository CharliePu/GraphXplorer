//
// Created by charl on 6/17/2024.
//

#ifndef COMPUTEENGINE_H
#define COMPUTEENGINE_H
#include <functional>
#include <future>
#include <memory>
#include <queue>

#include "Interval.h"
#include "../Util/ThreadPool.h"

class Window;
class GraphRasterizer;
class GraphProcessor;
struct Graph;
class Formula;

class ComputeEngine {
public:
    struct Task
    {
        std::shared_ptr<Graph> graph;
        std::shared_ptr<Formula> formula;
        Interval<double> xRange;
        Interval<double> yRange;
        int windowWidth;
        int windowHeight;
    };

    using ComputeCompleteCallBack = std::function<void(std::vector<int>, Interval<double>, Interval<double>, int, int)>;

    ComputeEngine(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);

    ~ComputeEngine();

    void addTask(const Task &task);

    void setComputeCompleteCallback(const ComputeCompleteCallBack &callback);

    void pollAsyncStates();

    void processTasks();
private:
    struct RasterizedData
    {
        std::vector<int> data;
        Interval<double> xRange;
        Interval<double> yRange;
        int windowWidth;
        int windowHeight;
    };

    std::shared_ptr<GraphProcessor> graphProcessor;
    std::shared_ptr<GraphRasterizer> graphRasterizer;

    ComputeCompleteCallBack computeCompleteCallback;

    std::shared_ptr<ThreadPool> threadPool;

    std::atomic<std::shared_ptr<Task>> currentTask;
    std::atomic<bool> running;
    std::future<void> future;

    std::atomic<std::shared_ptr<RasterizedData>> rasterizedData;
};



#endif //COMPUTEENGINE_H
