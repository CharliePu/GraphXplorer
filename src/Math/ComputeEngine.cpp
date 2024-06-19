//
// Created by charl on 6/17/2024.
//

#include "ComputeEngine.h"

#include <glfwpp/event.h>

#include "GraphRasterizer.h"
#include "GraphProcessor.h"

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
                                                                             running{true}, rasterizedData{nullptr}
{
    future = threadPool->addTask(&ComputeEngine::processTasks, this);
}

ComputeEngine::~ComputeEngine()
{
    running = false;

    // Create a dummy task to wake up the thread
    addTask({});
}

void ComputeEngine::addTask(const Task &task)
{
    currentTask = std::make_shared<Task>(task);
    currentTask.notify_one();
}

void ComputeEngine::setComputeCompleteCallback(
    const ComputeCompleteCallBack &callback)
{
    computeCompleteCallback = callback;
}

void ComputeEngine::pollAsyncStates()
{
    if (const auto data = rasterizedData.exchange(nullptr))
    {
        computeCompleteCallback(std::move(data->data), data->xRange, data->yRange, data->windowWidth,
                                data->windowHeight);
    }
}

void ComputeEngine::processTasks()
{
    while (running)
    {
        currentTask.wait(nullptr);
        const auto task = currentTask.exchange(nullptr);

        graphProcessor->process(task->graph, task->formula, task->xRange, task->yRange, task->windowWidth,
                                task->windowHeight);
        auto data = graphRasterizer->rasterize(task->graph, task->xRange, task->yRange, task->windowWidth,
                                               task->windowHeight);

        rasterizedData = std::make_shared<RasterizedData>(std::move(data), task->xRange, task->yRange,
                                                          task->windowWidth, task->windowHeight);
    }
}
