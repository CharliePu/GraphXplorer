//
// Created by charl on 6/17/2024.
//

#ifndef COMPUTEENGINE_H
#define COMPUTEENGINE_H
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>

#include "Interval.h"
#include "RasterizedPlot.h"
#include "../Util/ThreadPool.h"

class Window;
class GraphRasterizer;
class GraphProcessor;
class Formula;
struct Graph;

class ComputeEngine {
public:
    struct Task
    {
        std::shared_ptr<Graph> graph;
        std::shared_ptr<Formula> formula;
        Interval xRange;
        Interval yRange;
        int windowWidth;
        int windowHeight;
        uint64_t requestId{0};
    };

    using ComputeCompleteCallBack = std::function<void(
        std::vector<RasterChunk>,
        std::vector<RasterChunkTexture>,
        Interval,
        Interval,
        int,
        int,
        uint64_t)>;

    ComputeEngine(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);

    ~ComputeEngine();

    void addTask(const Task &task);

    void setComputeCompleteCallback(const ComputeCompleteCallBack &callback);

    void pollAsyncStates();

    void processTasks();
private:
    struct RasterizedData
    {
        uint64_t requestId{0};
        std::vector<RasterChunk> chunks;
        std::vector<RasterChunkTexture> chunkTextures;
        Interval xRange;
        Interval yRange;
        int windowWidth;
        int windowHeight;
    };

    std::shared_ptr<GraphProcessor> graphProcessor;
    std::shared_ptr<GraphRasterizer> graphRasterizer;

    ComputeCompleteCallBack computeCompleteCallback;

    std::shared_ptr<ThreadPool> threadPool;

    std::atomic<std::shared_ptr<Task>> currentTask;
    std::atomic<uint64_t> latestRequestedTaskId;
    std::atomic<bool> running;
    std::future<void> future;

    std::mutex rasterizedDataMutex;
    std::deque<std::shared_ptr<RasterizedData>> rasterizedDataDeque;
};



#endif //COMPUTEENGINE_H
