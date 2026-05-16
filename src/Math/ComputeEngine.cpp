//
// Created by charl on 6/17/2024.
//

#include "ComputeEngine.h"

#include <algorithm>
#include <glfwpp/event.h>

#include "GraphRasterizer.h"
#include "GraphProcessor.h"
#include "../Util/PerformanceProfiler.h"
#include "../Util/PipelineLog.h"

ComputeEngine::ComputeEngine(const std::shared_ptr<Window> &window,
                             const std::shared_ptr<ThreadPool> &threadPool): graphProcessor{
                                                                                 std::make_shared<GraphProcessor>(
                                                                                     window, threadPool)
                                                                             },
                                                                              graphRasterizer{
                                                                                  std::make_shared<GraphRasterizer>(
                                                                                      window, threadPool)
                                                                              },
                                                                              threadPool{threadPool},
                                                                              currentTask{nullptr},
                                                                              latestRequestedTaskId{0},
                                                                              running{true},
                                                                              rasterizedDataInbox{
                                                                                  RasterizedInbox::DrainPolicy{
                                                                                      RasterizedInbox::Mode::HandleN,
                                                                                      8
                                                                                  }
                                                                              }
{
    future = threadPool->addTask(&ComputeEngine::processTasks, this);
}

ComputeEngine::~ComputeEngine()
{
    running = false;
    currentTask.notify_one();
    if (future.valid())
    {
        future.wait();
    }
}

void ComputeEngine::addTask(const Task &task)
{
    auto queuedTask = task;
    queuedTask.requestId = latestRequestedTaskId.fetch_add(1) + 1;
    PipelineLog::log("addTask: reqId=%llu range=[%.1f,%.1f]x[%.1f,%.1f] %dx%d",
        queuedTask.requestId, task.xRange.lower, task.xRange.upper,
        task.yRange.lower, task.yRange.upper, task.windowWidth, task.windowHeight);
    currentTask = std::make_shared<Task>(queuedTask);
    currentTask.notify_one();
}

void ComputeEngine::setComputeCompleteCallback(
    const ComputeCompleteCallBack &callback)
{
    computeCompleteCallback = callback;

    // If work completed before callback registration, wake the main loop so
    // queued updates are drained immediately once callback is available.
    if (computeCompleteCallback)
    {
        if (!rasterizedDataInbox.empty())
        {
            glfw::postEmptyEvent();
        }
    }
}

void ComputeEngine::setUpdateDrainPolicy(const UpdateDrainPolicy &policy)
{
    auto mode = RasterizedInbox::Mode::HandleN;
    switch (policy.mode)
    {
    case UpdateDrainMode::HandleAll:
        mode = RasterizedInbox::Mode::HandleAll;
        break;
    case UpdateDrainMode::LatestOnly:
        mode = RasterizedInbox::Mode::LatestOnly;
        break;
    case UpdateDrainMode::HandleN:
    default:
        mode = RasterizedInbox::Mode::HandleN;
        break;
    }

    rasterizedDataInbox.setDrainPolicy({mode, policy.handleCount});
}

ComputeEngine::UpdateDrainPolicy ComputeEngine::getUpdateDrainPolicy() const
{
    const auto policy = rasterizedDataInbox.getDrainPolicy();
    auto mode = UpdateDrainMode::HandleN;
    switch (policy.mode)
    {
    case RasterizedInbox::Mode::HandleAll:
        mode = UpdateDrainMode::HandleAll;
        break;
    case RasterizedInbox::Mode::LatestOnly:
        mode = UpdateDrainMode::LatestOnly;
        break;
    case RasterizedInbox::Mode::HandleN:
    default:
        mode = UpdateDrainMode::HandleN;
        break;
    }

    return {mode, policy.handleCount};
}

void ComputeEngine::pollAsyncStates()
{
    GRAPHX_PROFILE_SCOPE("compute.pollAsyncStates");
    if (!computeCompleteCallback)
    {
        return;
    }

    auto hasBatchedUpdate = false;
    auto targetRequestId = latestRequestedTaskId.load();
    Interval batchedXRange{};
    Interval batchedYRange{};
    auto batchedWindowWidth = 0;
    auto batchedWindowHeight = 0;
    std::vector<ChunkRenderData> batchedChunkRenderData;
    auto updates = rasterizedDataInbox.drainForFrame();

    PipelineLog::log("pollAsyncStates: drained %zu items, latestReqId=%llu", updates.size(), targetRequestId);

    for (auto &data : updates)
    {
        if (!data)
        {
            continue;
        }

        if (data->requestId < targetRequestId)
        {
            PipelineLog::log("  DROP stale reqId=%llu (latest=%llu)", data->requestId, targetRequestId);
            continue;
        }

        if (data->requestId > targetRequestId)
        {
            PipelineLog::log("  SWITCH to newer reqId=%llu (was %llu)", data->requestId, targetRequestId);
            targetRequestId = data->requestId;
            hasBatchedUpdate = false;
            batchedChunkRenderData.clear();
        }

        if (!hasBatchedUpdate)
        {
            hasBatchedUpdate = true;
            batchedXRange = data->xRange;
            batchedYRange = data->yRange;
            batchedWindowWidth = data->windowWidth;
            batchedWindowHeight = data->windowHeight;
        }

        batchedXRange = data->xRange;
        batchedYRange = data->yRange;
        batchedWindowWidth = data->windowWidth;
        batchedWindowHeight = data->windowHeight;

        if (!data->chunkRenderData.empty())
        {
            PipelineLog::log("  BATCH reqId=%llu chunks=%zu", data->requestId, data->chunkRenderData.size());
            batchedChunkRenderData.insert(batchedChunkRenderData.end(),
                                          std::make_move_iterator(data->chunkRenderData.begin()),
                                          std::make_move_iterator(data->chunkRenderData.end()));
        }
    }

    if (hasBatchedUpdate && targetRequestId == latestRequestedTaskId.load())
    {
        PipelineLog::log("pollAsyncStates: INVOKE callback reqId=%llu totalChunks=%zu",
            targetRequestId, batchedChunkRenderData.size());
        computeCompleteCallback(std::move(batchedChunkRenderData),
                                batchedXRange,
                                batchedYRange,
                                batchedWindowWidth,
                                batchedWindowHeight,
                                targetRequestId);
    }
    else if (hasBatchedUpdate)
    {
        PipelineLog::log("pollAsyncStates: SKIP callback (reqId=%llu != latest=%llu)",
            targetRequestId, latestRequestedTaskId.load());
    }

    if (!rasterizedDataInbox.empty())
    {
        glfw::postEmptyEvent();
    }
}

void ComputeEngine::processTasks()
{
    while (true)
    {
        currentTask.wait(nullptr);
        if (!running.load())
        {
            break;
        }

        const auto task = currentTask.exchange(nullptr);
        if (!task || !task->graph || !task->formula)
        {
            continue;
        }

        PipelineLog::log("worker: START reqId=%llu range=[%.1f,%.1f]x[%.1f,%.1f]",
            task->requestId, task->xRange.lower, task->xRange.upper,
            task->yRange.lower, task->yRange.upper);

        const auto requestId = task->requestId;
        const auto cancelled = [this, requestId]
        {
            return !running.load() || requestId != latestRequestedTaskId.load();
        };

        if (cancelled())
        {
            PipelineLog::log("worker: CANCEL before graph reqId=%llu latest=%llu",
                requestId, latestRequestedTaskId.load());
            continue;
        }

        {
            GRAPHX_PROFILE_SCOPE("compute.worker.graphProcess");
            graphProcessor->process(task->graph, task->formula, task->xRange, task->yRange, task->windowWidth,
                                    task->windowHeight, cancelled);
        }

        if (cancelled())
        {
            PipelineLog::log("worker: CANCEL after graph reqId=%llu latest=%llu",
                requestId, latestRequestedTaskId.load());
            continue;
        }

        PipelineLog::log("worker: GRAPH DONE reqId=%llu", task->requestId);

        auto rasterized = [&]()
        {
            GRAPHX_PROFILE_SCOPE("compute.worker.rasterize");
            return graphRasterizer->rasterize(task->graph, task->formula, task->xRange, task->yRange,
                                              task->windowWidth,
                                              task->windowHeight,
                                              cancelled);
        }();

        if (cancelled())
        {
            PipelineLog::log("worker: CANCEL after raster reqId=%llu latest=%llu",
                requestId, latestRequestedTaskId.load());
            continue;
        }

        PipelineLog::log("worker: RASTERIZE DONE reqId=%llu chunks=%zu",
            task->requestId, rasterized.chunkRenderData.size());

        if (task->requestId != latestRequestedTaskId.load())
        {
            PipelineLog::log("worker: DISCARD reqId=%llu (latest=%llu)",
                task->requestId, latestRequestedTaskId.load());
            continue;
        }

        auto batchUpdate = std::make_shared<RasterizedData>();
        batchUpdate->requestId = task->requestId;
        batchUpdate->xRange = task->xRange;
        batchUpdate->yRange = task->yRange;
        batchUpdate->windowWidth = task->windowWidth;
        batchUpdate->windowHeight = task->windowHeight;
        batchUpdate->chunkRenderData = std::move(rasterized.chunkRenderData);

        {
            GRAPHX_PROFILE_SCOPE("compute.worker.enqueueUpdates");
            rasterizedDataInbox.push(std::move(batchUpdate));
        }

        PipelineLog::log("worker: PUSH reqId=%llu", task->requestId);
        glfw::postEmptyEvent();
    }
}
