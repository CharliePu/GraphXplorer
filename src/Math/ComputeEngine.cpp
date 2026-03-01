//
// Created by charl on 6/17/2024.
//

#include "ComputeEngine.h"

#include <iostream>
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
                                                                             rasterizedDataMutex{},
                                                                             rasterizedDataDeque{}
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
        std::lock_guard lock(rasterizedDataMutex);
        if (!rasterizedDataDeque.empty())
        {
            glfw::postEmptyEvent();
        }
    }
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

    while (true)
    {
        std::shared_ptr<RasterizedData> data;
        {
            std::lock_guard lock(rasterizedDataMutex);
            if (rasterizedDataDeque.empty())
            {
                break;
            }

            data = std::move(rasterizedDataDeque.front());
            rasterizedDataDeque.pop_front();
        }

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
    {
        std::lock_guard lock(rasterizedDataMutex);
        if (!rasterizedDataDeque.empty())
        {
            glfw::postEmptyEvent();
        }
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

        std::deque<std::shared_ptr<RasterizedData>> pendingUpdates;
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
            std::lock_guard lock(rasterizedDataMutex);
            rasterizedDataDeque.clear();
            while (!pendingUpdates.empty())
            {
                rasterizedDataDeque.push_back(std::move(pendingUpdates.front()));
                pendingUpdates.pop_front();
            }
        }

        // Wake the main loop so streamed chunks begin draining immediately.
        glfw::postEmptyEvent();
    }
}
