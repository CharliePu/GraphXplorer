//
// Created by charl on 6/17/2024.
//

#include "ComputeEngine.h"

#include <algorithm>
#include <glfwpp/event.h>

#include "GraphRasterizer.h"
#include "GraphProcessor.h"
#include "../Util/PerformanceProfiler.h"

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
    currentTask = std::make_shared<Task>(queuedTask);
    // Keep enqueue path lock-free for interactive input; stale queued results
    // are filtered by requestId in pollAsyncStates().
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
    // Never dequeue updates before a callback is installed; otherwise prepared
    // chunks can be dropped without any notification reaching the UI thread.
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

    for (auto &data : updates)
    {
        if (!data)
        {
            continue;
        }

        if (data->requestId < targetRequestId)
        {
            // Drop stale work for superseded view requests.
            continue;
        }

        if (data->requestId > targetRequestId)
        {
            // If a newer request already produced results, switch to it and
            // discard any older partial batch.
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
            batchedChunkRenderData.insert(batchedChunkRenderData.end(),
                                          std::make_move_iterator(data->chunkRenderData.begin()),
                                          std::make_move_iterator(data->chunkRenderData.end()));
        }
    }

    if (hasBatchedUpdate && targetRequestId == latestRequestedTaskId.load())
    {
        computeCompleteCallback(std::move(batchedChunkRenderData),
                                batchedXRange,
                                batchedYRange,
                                batchedWindowWidth,
                                batchedWindowHeight,
                                targetRequestId);
    }

    // Keep driving incremental streaming even when there is no user input event.
    // If anything remains queued, wake the event loop again.
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

        {
            GRAPHX_PROFILE_SCOPE("compute.worker.graphProcess");
            graphProcessor->process(task->graph, task->formula, task->xRange, task->yRange, task->windowWidth,
                                    task->windowHeight);
        }

        auto rasterized = [&]()
        {
            GRAPHX_PROFILE_SCOPE("compute.worker.rasterize");
            return graphRasterizer->rasterize(task->graph, task->formula, task->xRange, task->yRange,
                                              task->windowWidth,
                                              task->windowHeight);
        }();

        if (task->requestId != latestRequestedTaskId.load())
        {
            continue;
        }

        std::vector<std::shared_ptr<RasterizedData>> pendingUpdates;
        pendingUpdates.reserve(std::max<size_t>(rasterized.chunkRenderData.size(), 1));
        for (auto &chunkRenderData : rasterized.chunkRenderData)
        {
            if (task->requestId != latestRequestedTaskId.load())
            {
                pendingUpdates.clear();
                break;
            }

            auto update = std::make_shared<RasterizedData>();
            update->requestId = task->requestId;
            update->xRange = task->xRange;
            update->yRange = task->yRange;
            update->windowWidth = task->windowWidth;
            update->windowHeight = task->windowHeight;
            update->chunkRenderData.push_back(std::move(chunkRenderData));

            pendingUpdates.push_back(std::move(update));
        }

        if (pendingUpdates.empty())
        {
            auto update = std::make_shared<RasterizedData>();
            update->requestId = task->requestId;
            update->xRange = task->xRange;
            update->yRange = task->yRange;
            update->windowWidth = task->windowWidth;
            update->windowHeight = task->windowHeight;
            pendingUpdates.push_back(std::move(update));
        }

        {
            GRAPHX_PROFILE_SCOPE("compute.worker.enqueueUpdates");
            rasterizedDataInbox.pushRange(std::make_move_iterator(pendingUpdates.begin()),
                                          std::make_move_iterator(pendingUpdates.end()));
        }

        // Wake the main loop so streamed chunks begin draining immediately.
        glfw::postEmptyEvent();
    }
}
