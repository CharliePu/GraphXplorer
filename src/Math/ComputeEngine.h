//
// Created by charl on 6/17/2024.
//

#ifndef COMPUTEENGINE_H
#define COMPUTEENGINE_H
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>

#include "Interval.h"
#include "RasterizedPlot.h"
#include "../Util/AsyncFrameInbox.h"
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
        std::vector<ChunkRenderData>,
        Interval,
        Interval,
        int,
        int,
        uint64_t)>;

    ComputeEngine(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);

    ~ComputeEngine();

    void addTask(const Task &task);

    void setComputeCompleteCallback(const ComputeCompleteCallBack &callback);

    enum class UpdateDrainMode
    {
        HandleAll,
        HandleN,
        LatestOnly
    };

    struct UpdateDrainPolicy
    {
        UpdateDrainMode mode{UpdateDrainMode::HandleN};
        size_t handleCount{8};
    };

    void setUpdateDrainPolicy(const UpdateDrainPolicy &policy);
    [[nodiscard]] UpdateDrainPolicy getUpdateDrainPolicy() const;

    void pollAsyncStates();

    void processTasks();
private:
    struct RasterizedData
    {
        uint64_t requestId{0};
        std::vector<ChunkRenderData> chunkRenderData;
        Interval xRange;
        Interval yRange;
        int windowWidth;
        int windowHeight;
    };
    using RasterizedInbox = AsyncFrameInbox<std::shared_ptr<RasterizedData>>;

    std::shared_ptr<GraphProcessor> graphProcessor;
    std::shared_ptr<GraphRasterizer> graphRasterizer;

    ComputeCompleteCallBack computeCompleteCallback;

    std::shared_ptr<ThreadPool> threadPool;

    std::atomic<std::shared_ptr<Task>> currentTask;
    std::atomic<uint64_t> latestRequestedTaskId;
    std::atomic<bool> running;
    std::future<void> future;
    RasterizedInbox rasterizedDataInbox;
};



#endif //COMPUTEENGINE_H
