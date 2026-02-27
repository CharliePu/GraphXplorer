//
// Created by charl on 6/17/2024.
//

#include "ComputeEngine.h"

#include <iostream>
#include <unordered_map>
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
    {
        std::lock_guard lock(rasterizedDataMutex);
        rasterizedDataDeque.clear();
    }
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

    constexpr size_t maxUpdatesPerPoll = 8;
    auto hasBatchedUpdate = false;
    uint64_t batchedRequestId = 0;
    Interval batchedXRange{};
    Interval batchedYRange{};
    auto batchedWindowWidth = 0;
    auto batchedWindowHeight = 0;
    std::vector<RasterChunk> batchedChunks;
    std::vector<RasterChunkTexture> batchedChunkTextures;
    std::shared_ptr<RasterizedData> deferredUpdate;

    for (size_t i = 0; i < maxUpdatesPerPoll; ++i)
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

        if (!hasBatchedUpdate)
        {
            hasBatchedUpdate = true;
            batchedRequestId = data->requestId;
            batchedXRange = data->xRange;
            batchedYRange = data->yRange;
            batchedWindowWidth = data->windowWidth;
            batchedWindowHeight = data->windowHeight;
        }
        else if (data->requestId != batchedRequestId)
        {
            // Keep callback data request-consistent. If a newer request appears
            // mid-drain, defer it to the next poll cycle.
            deferredUpdate = std::move(data);
            break;
        }

        batchedXRange = data->xRange;
        batchedYRange = data->yRange;
        batchedWindowWidth = data->windowWidth;
        batchedWindowHeight = data->windowHeight;

        if (!data->chunks.empty())
        {
            batchedChunks.insert(batchedChunks.end(), std::make_move_iterator(data->chunks.begin()),
                                 std::make_move_iterator(data->chunks.end()));
        }

        if (!data->chunkTextures.empty())
        {
            batchedChunkTextures.insert(batchedChunkTextures.end(), std::make_move_iterator(data->chunkTextures.begin()),
                                        std::make_move_iterator(data->chunkTextures.end()));
        }
    }

    if (deferredUpdate)
    {
        std::lock_guard lock(rasterizedDataMutex);
        rasterizedDataDeque.push_front(std::move(deferredUpdate));
    }

    if (hasBatchedUpdate)
    {
        computeCompleteCallback(std::move(batchedChunks), std::move(batchedChunkTextures), batchedXRange, batchedYRange,
                                batchedWindowWidth, batchedWindowHeight, batchedRequestId);
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
    struct ChunkTextureKey
    {
        int64_t x;
        int64_t y;
        int level;

        bool operator==(const ChunkTextureKey &other) const
        {
            return x == other.x && y == other.y && level == other.level;
        }
    };

    struct ChunkTextureKeyHash
    {
        size_t operator()(const ChunkTextureKey &key) const
        {
            const auto h1 = std::hash<int64_t>{}(key.x);
            const auto h2 = std::hash<int64_t>{}(key.y);
            const auto h3 = std::hash<int>{}(key.level);

            size_t seed = h1;
            seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

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

        std::unordered_map<ChunkTextureKey, size_t, ChunkTextureKeyHash> textureIndexByKey;
        textureIndexByKey.reserve(rasterized.chunkTextures.size());

        for (size_t textureIndex = 0; textureIndex < rasterized.chunkTextures.size(); ++textureIndex)
        {
            const auto &texture = rasterized.chunkTextures[textureIndex];
            textureIndexByKey.insert_or_assign({texture.chunkX, texture.chunkY, texture.level}, textureIndex);
        }

        std::vector<char> consumedTextures(rasterized.chunkTextures.size(), 0);
        std::deque<std::shared_ptr<RasterizedData>> pendingUpdates;
        for (auto &chunk : rasterized.chunks)
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
            update->chunks.push_back(std::move(chunk));

            const ChunkTextureKey key{update->chunks.front().chunkX, update->chunks.front().chunkY,
                                      update->chunks.front().level};
            if (const auto textureIt = textureIndexByKey.find(key); textureIt != textureIndexByKey.end())
            {
                const auto textureIndex = textureIt->second;
                if (!consumedTextures[textureIndex])
                {
                    consumedTextures[textureIndex] = 1;
                    update->chunkTextures.push_back(std::move(rasterized.chunkTextures[textureIndex]));
                }
            }

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
