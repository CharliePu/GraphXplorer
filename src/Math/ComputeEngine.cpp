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

ComputeEngine::ComputeEngine(const std::shared_ptr<Window> &window) : window{window}
{
}

void ComputeEngine::setComputeCompleteCallback(const std::function<void(const ComputeRequest &)> &callback)
{
    computeCompleteCallback = callback;
}

std::pair<Interval<double>, Interval<double>> ComputeEngine::getRoundedRanges(const ComputeRequest &request)
{
    assert(request.graph);

    Interval<double> xRange{}, yRange{};
    if (request.graph->root)
    {
        xRange = {std::min(request.xRange.lower, request.graph->root->xRange.lower),
    std::max(request.xRange.upper, request.graph->root->xRange.upper)};
        yRange = {std::min(request.yRange.lower, request.graph->root->yRange.lower),
        std::max(request.yRange.upper, request.graph->root->yRange.upper)};
    }
    else
    {
        xRange = request.xRange;
        yRange = request.yRange;
    }

    const auto size = std::exp2(std::ceil(std::log2(std::max(
        {
            std::abs(xRange.lower),
            std::abs(xRange.upper),
            std::abs(yRange.lower),
            std::abs(yRange.upper)
        }))));

    xRange = {
        std::floor(xRange.lower / size) * size,
        std::ceil(xRange.upper / size) * size
    };

    yRange = {
        std::floor(yRange.lower / size) * size,
        std::ceil(yRange.upper / size) * size
    };

    return {xRange, yRange};
}

bool ComputeEngine::isPowerOfTwo(double size)
{
    return std::abs(std::log2(size) - std::floor(std::log2(size))) < std::numeric_limits<double>::epsilon();
}

bool ComputeEngine::nodesIntervalMatches(const std::unique_ptr<GraphNode> &curr, const std::unique_ptr<GraphNode> &root)
{
    return curr->xRange == root->xRange && curr->yRange == root->yRange;
}

bool ComputeEngine::nodeIsLeaf(const std::unique_ptr<GraphNode> &curr)
{
    assert(curr);

    return !curr->children[0] && !curr->children[1] && !curr->children[2] && !curr->children[3];
}

std::unique_ptr<GraphNode> *ComputeEngine::getMatchingChildNode(const std::unique_ptr<GraphNode> &parentNode,
                                                                const std::unique_ptr<GraphNode> &nodeToMatch)
{
    assert(parentNode->xRange.contains(nodeToMatch->xRange));
    assert(parentNode->yRange.contains(nodeToMatch->yRange));
    assert(isPowerOfTwo(nodeToMatch->xRange.size()));
    assert(isPowerOfTwo(nodeToMatch->yRange.size()));
    assert(!nodeIsLeaf(parentNode));

    const auto index1 = parentNode->children[0]->xRange.upper < nodeToMatch->xRange.upper;
    const auto index2 = parentNode->children[0]->yRange.upper < nodeToMatch->yRange.upper;
    const auto childIndex = index1 + 2 * index2;

    return &parentNode->children[childIndex];
}

void ComputeEngine::expandGraphToPlaceNode(std::unique_ptr<GraphNode> &nodeToExpand,
                                           std::unique_ptr<GraphNode> &nodeToPlace)
{
    auto curr = &nodeToExpand;

    while (!nodesIntervalMatches(*curr, nodeToPlace))
    {
        if (nodeIsLeaf(*curr))
        {
            subdivideNode(*curr);
        }
        curr = getMatchingChildNode(*curr, nodeToPlace);
    }

    *curr = std::move(nodeToPlace);
}

std::unique_ptr<GraphNode> ComputeEngine::createNode(GraphNode *parent, Interval<double> xRange,
                                                     Interval<double> yRange)
{
    auto node = std::make_unique<GraphNode>();
    node->parent = parent;
    node->solution = IntervalValues::Unknown;
    node->xRange = xRange;
    node->yRange = yRange;
    for (auto &child: node->children)
    {
        child = nullptr;
    }
    return node;
}


void ComputeEngine::expandGraph(const std::shared_ptr<Graph> &graph,
                                const Interval<double> targetXRange,
                                const Interval<double> targetYRange)
{
    assert(targetXRange.strictlyContains(getGraphXRange(graph)));
    assert(targetYRange.strictlyContains(getGraphYRange(graph)));
    assert(isPowerOfTwo(targetXRange.size()));
    assert(isPowerOfTwo(targetYRange.size()));
    assert(isPowerOfTwo(getGraphXRange(graph).size()));
    assert(isPowerOfTwo(getGraphYRange(graph).size()));

    auto newRoot = createNode(nullptr, targetXRange, targetYRange);
    auto &oldRoot = graph->root;

    // If old root is not computed yet, just replace it with the new root
    if (nodeIsLeaf(oldRoot))
    {
        graph->root = std::move(newRoot);
        return;
    }

    if (oldRoot->xRange.crossesZero() || oldRoot->yRange.crossesZero())
    {
        for (auto &child: oldRoot->children)
        {
            expandGraphToPlaceNode(newRoot, child);
        }
    }
    else
    {
        expandGraphToPlaceNode(newRoot, oldRoot);
    }

    graph->root = std::move(newRoot);
}

void ComputeEngine::subdivideNode(const std::unique_ptr<GraphNode> &curr)
{
    assert(nodeIsLeaf(curr));

    const auto &[xLower, xUpper] = curr->xRange;
    const auto &[yLower, yUpper] = curr->yRange;

    const auto midX = (xLower + xUpper) / 2.0;
    const auto midY = (yLower + yUpper) / 2.0;

    auto &children = curr->children;

    children[0] = createNode(curr.get(), {xLower, midX}, {yLower, midY});
    children[1] = createNode(curr.get(), {midX, xUpper}, {yLower, midY});
    children[2] = createNode(curr.get(), {xLower, midX}, {midY, yUpper});
    children[3] = createNode(curr.get(), {midX, xUpper}, {midY, yUpper});
}

void ComputeEngine::recursiveComputeNodes(const ComputeRequest &request, const std::shared_ptr<Graph> &graph)
{
    const auto rangePerPixel =
            std::exp2(std::floor(std::log2(
                std::min(request.xRange.size(), request.yRange.size()) / std::max(
                    request.windowWidth, request.windowHeight))));

    std::vector<std::unique_ptr<GraphNode> *> nodes;
    std::queue<std::unique_ptr<GraphNode> *> nodeQueue;
    nodeQueue.push(&graph->root);


    while (!nodeQueue.empty())
    {
        auto curr = nodeQueue.front();
        nodeQueue.pop();

        if ((*curr)->xRange.size() <= rangePerPixel && (*curr)->yRange.size() <= rangePerPixel)
        {
            continue;
        }

        if (futuresMap.contains(curr))
        {
            futuresMap[curr].wait();
            futuresMap.erase(curr);
        }

        if ((*curr)->solution != IntervalValues::Unknown)
        {
            continue;
        }

        if (nodeIsLeaf(*curr))
        {
            subdivideNode(*curr);
        }

        for (auto &child: (*curr)->children)
        {
            nodeQueue.push(&child);

            if (child->solution == IntervalValues::Unknown)
            {
                futuresMap.emplace(&child, threadPool.addTask(computeTask, &child, request.formula));
                nodes.push_back(&child);
            }
        }
    }

    // TODO: subpixel refinement
    currentTask = std::make_shared<ComputeTask>(request, nodes);
    glfw::postEmptyEvent();
}

void ComputeEngine::processGraph(const ComputeRequest &request)
{
    if (!request.formula)
    {
        throw std::invalid_argument("Formula must not be null");
    }

    // Threads will still be running for previous task
    // However, the corresponding callback will not be called
    if (currentTask)
    {
        currentTask.reset();
    }


    auto [xRange, yRange] = getRoundedRanges(request);

    // Skip if requested range is contained in graph
    if (request.graph->root && request.graph->root->xRange.contains(xRange) && request.graph->root->yRange.contains(yRange))
    {
        std::vector<std::unique_ptr<GraphNode> *> nodes;
        currentTask = std::make_shared<ComputeTask>(request, nodes);
        glfw::postEmptyEvent();
        return;
    }

    // If the graph is empty, create a new root node
    if (!request.graph->root)
    {
        request.graph->root = createNode(nullptr, xRange, yRange);
    }

    // Expand the graph to contain the requested range when necessary
    if (xRange.strictlyContains(request.graph->root->xRange) || yRange.strictlyContains(request.graph->root->yRange))
    {
        expandGraph(request.graph, xRange, yRange);
        assert(request.graph->root->xRange.contains(request.xRange));
        assert(request.graph->root->yRange.contains(request.yRange));
    }

    recursiveComputeNodes(request, request.graph);
}

void ComputeEngine::pollAsyncStates()
{
    if (!currentTask)
    {
        return;
    }

    bool taskCompleted{true};

    for (const auto &node: currentTask->nodes)
    {
        if (auto futureResult = futuresMap.find(node);
            futureResult == futuresMap.end() || futureResult->second.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready)
        {
            futuresMap.erase(node);
        }
        else
        {
            taskCompleted = false;
            break;
        }
    }

    if (taskCompleted)
    {
        computeCompleteCallback(currentTask->request);
        currentTask.reset();
    }
    else
    {
        glfw::postEmptyEvent();
    }
}

void ComputeEngine::computeTask(const std::unique_ptr<GraphNode> *node, const std::shared_ptr<Formula> &formula)
{
    auto postfixExpr = formula->getPostfixExpression();

    const auto x = ComputeInterval{(*node)->xRange};
    const auto y = ComputeInterval{(*node)->yRange};

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

    (*node)->solution = result;
}

Interval<double> &ComputeEngine::getGraphXRange(const std::shared_ptr<Graph> &graph)
{
    assert(graph->root);

    return graph->root->xRange;
}

Interval<double> &ComputeEngine::getGraphYRange(const std::shared_ptr<Graph> &graph)
{
    assert(graph->root);

    return graph->root->yRange;
}
