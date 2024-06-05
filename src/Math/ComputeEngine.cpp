//
// Created by charl on 6/3/2024.
//

#include "ComputeEngine.h"

#include <cmath>
#include <iostream>
#include <stack>

#include "../Core/Window.h"
#include "Formula.h"
#include "Graph.h"

ComputeEngine::ComputeEngine(const std::shared_ptr<Window> &window) : window{window}, newTask{false}
{
}

void ComputeEngine::setComputeCompleteCallback(const std::function<void()> &callback)
{
    computeCompleteCallback = callback;
}

void ComputeEngine::expandGraph(const std::shared_ptr<Graph> &graph, const Interval<double> xRange,
                                const Interval<double> yRange)
{
    std::queue<GraphNode *> nodeQueue;
    auto newRoot = std::make_unique<GraphNode>(nullptr, xRange, yRange);
    auto curr = newRoot.get();

    while (!curr->matches(graph->getXRange(), graph->getYRange()))
    {
        curr->subdivide();
        curr = curr->findChild(graph->getXRange(), graph->getYRange());
    }

    auto childIdx = curr->parent->findChildIndex(graph->getXRange(), graph->getYRange());
    curr->parent->children[childIdx] = std::move(graph->root);
    graph->root = std::move(newRoot);
}

void ComputeEngine::run(const std::shared_ptr<Graph> &graph, const ComputeRequest &request)
{
    if (!request.formula)
    {
        throw std::invalid_argument("Formula must not be null");
    }

    newTask = true;

    // Round the x range and y range to the nearest power of two
    auto toNearestPowerOfTwo = [](const double value) -> double {
        return std::exp2(std::ceil(std::log2(value)));
    };

    const auto gridSize{
        (std::max({
            toNearestPowerOfTwo(std::abs(request.xRange.lower)),
            toNearestPowerOfTwo(std::abs(request.xRange.upper)),
            toNearestPowerOfTwo(std::abs(request.yRange.lower)),
            toNearestPowerOfTwo(std::abs(request.yRange.upper))
        }))
    };

    const Interval<double> xRange{
        std::floor(request.xRange.lower / gridSize) * gridSize,
        std::ceil(request.xRange.upper / gridSize) * gridSize
    };
    const Interval<double> yRange{
        std::floor(request.yRange.lower / gridSize) * gridSize,
        std::ceil(request.yRange.upper / gridSize) * gridSize
    };


    if (graph->empty())
    {
        graph->root = std::make_unique<GraphNode>(nullptr, xRange, yRange);
    }
    else
    {
        if (xRange.strictlyContains(graph->getXRange()) && yRange.strictlyContains(graph->getYRange()))
        {
            expandGraph(graph, xRange, yRange);
        }
    }

    // Subdivide graph to pixel level
    const auto rangePerPixel =
            std::exp2(std::floor(std::log2(
                std::min(request.xRange.size(), request.yRange.size()) / std::max(
                    window->getWidth(), window->getHeight()))));

    std::queue<GraphNode *> nodeQueue;
    nodeQueue.push(graph->root.get());


    while (!nodeQueue.empty())
    {
        auto curr = nodeQueue.front();
        nodeQueue.pop();

        if (curr->xRange.size() <= rangePerPixel && curr->yRange.size() <= rangePerPixel)
        {
            break;
        }

        if (futures.contains(curr))
        {
            futures[curr].wait();
            // std::cout<<"x=["<<curr->xRange.lower<<", "<<curr->xRange.upper<<"] y=["<<curr->yRange.lower<<", "<<curr->yRange.upper<<"] => ";
            // if (curr->solution == IntervalValues::True)
            // {
            //     std::cout<<"True"<<std::endl;
            // } else if (curr->solution == IntervalValues::False)
            // {
            //     std::cout<<"False"<<std::endl;
            // } else
            // {
            //     std::cout<<"Unknown"<<std::endl;
            // }

            futures.erase(curr);
        }

        if (curr->solution == IntervalValues::Unknown && curr->isLeaf())
        {
            curr->subdivide();

            for (const auto &child: curr->children)
            {
                nodeQueue.push(child.get());

                futures.emplace(child.get(), threadPool.addTask(computeTask, child.get(), request.formula));
            }
        }
    }

    // TODO: subpixel refinement

    glfw::postEmptyEvent();
}

void ComputeEngine::pollAsyncStates()
{
    for (auto it = futures.begin(); it != futures.end();)
    {
        if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            it = futures.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (newTask)
    {
        if (futures.empty())
        {
            computeCompleteCallback();
            newTask = false;
        }
        else
        {
            glfw::postEmptyEvent();
        }
    }
}

// TODO: problem: if graph is deleted, threads will access deleted nodes
void ComputeEngine::computeTask(GraphNode *node, const std::shared_ptr<Formula> &formula)
{
    auto postfixExpr = formula->getPostfixExpression();

    const auto x = ComputeInterval{node->xRange};
    const auto y = ComputeInterval{node->yRange};

    std::stack<ComputeInterval> valueStack;

    Interval<bool> result = IntervalValues::Unknown;

    for (auto &[value, type]: postfixExpr)
    {
        if (type == Variable)
        {
            if (value == "x")
            {
                valueStack.push(x);
            }
            else if (value == "y")
            {
                valueStack.push(y);
            }
        }
        else if (type == Value)
        {
            valueStack.push({std::stod(value), std::stod(value)});
        }
        else
        {
            auto operand2 = valueStack.top();
            valueStack.pop();
            auto operand1 = valueStack.top();
            valueStack.pop();

            if (value == "+")
            {
                valueStack.push(operand1 + operand2);
            }
            else if (value == "-")
            {
                valueStack.push(operand1 - operand2);
            }
            else if (value == "*")
            {
                valueStack.push(operand1 * operand2);
            }
            else if (value == "/")
            {
                valueStack.push(operand1 / operand2);
            }
            else if (value == ">")
            {
                result = operand1 > operand2;
                break;
            }
            else if (value == "<")
            {
                result = operand1 < operand2;
                break;
            }
            else
            {
                throw std::runtime_error("Invalid operator");
            }
        }
    }

    node->solution = result;
}
