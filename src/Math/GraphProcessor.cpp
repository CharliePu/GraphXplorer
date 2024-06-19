//
// Created by charl on 6/3/2024.
//

#include "GraphProcessor.h"

#include <cmath>
#include <iostream>
#include <stack>

#include "../Core/Window.h"
#include "Formula.h"
#include "../Graph/Graph.h"
#include "../Graph/GraphOperations.h"

GraphProcessor::GraphProcessor(const std::shared_ptr<Window> &window,
                               const std::shared_ptr<ThreadPool> &threadPool): window{window}, threadPool{threadPool}
{
}

void GraphProcessor::process(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                             const Interval<double> &xRange, const Interval<double> &yRange, int windowWidth,
                             int windowHeight)
{
    if (!formula)
    {
        throw std::invalid_argument("Formula must not be null");
    }

    auto [xRangeRounded, yRangeRounded] = getRoundedRanges(graph, xRange, yRange);

    // Skip if requested range is contained in graph
    if (graph->root && graph->root->xRange.contains(xRangeRounded) && graph->root->yRange.contains(yRangeRounded))
    {
        return;
    }

    // If the graph is empty, create a new root node
    if (!graph->root)
    {
        graph->root = createNode(nullptr, xRangeRounded, yRangeRounded);
    }

    // Expand the graph to contain the requested range when necessary
    if (xRangeRounded.strictlyContains(graph->root->xRange) || yRangeRounded.strictlyContains(graph->root->yRange))
    {
        expandGraph(graph, xRangeRounded, yRangeRounded);
        assert(graph->root->xRange.contains(xRange));
        assert(graph->root->yRange.contains(yRange));
    }

    recursiveComputeNodes(graph, formula, xRangeRounded, yRangeRounded, windowWidth, windowHeight);
}


std::pair<Interval<double>, Interval<double> > GraphProcessor::getRoundedRanges(
    const std::shared_ptr<Graph> &graph,
    const Interval<double> &xRange, const Interval<double> &yRange)
{
    assert(graph);

    Interval<double> xRangeUnion{}, yRangeUnion{};
    if (graph->root)
    {
        xRangeUnion = {
            std::min(xRange.lower, graph->root->xRange.lower),
            std::max(xRange.upper, graph->root->xRange.upper)
        };
        yRangeUnion = {
            std::min(yRange.lower, graph->root->yRange.lower),
            std::max(yRange.upper, graph->root->yRange.upper)
        };
    }
    else
    {
        xRangeUnion = xRange;
        yRangeUnion = yRange;
    }

    const auto size = std::exp2(std::ceil(std::log2(std::max(
        {
            std::abs(xRangeUnion.lower),
            std::abs(xRangeUnion.upper),
            std::abs(yRangeUnion.lower),
            std::abs(yRangeUnion.upper)
        }))));

    Interval<double> xRangeRounded{
        std::floor(xRangeUnion.lower / size) * size,
        std::ceil(xRangeUnion.upper / size) * size
    };

    Interval<double> yRangeRounded{
        std::floor(yRangeUnion.lower / size) * size,
        std::ceil(yRangeUnion.upper / size) * size
    };

    return {xRangeRounded, yRangeRounded};
}

void GraphProcessor::expandGraphToPlaceNode(std::unique_ptr<GraphNode> &nodeToExpand,
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

void GraphProcessor::expandGraph(const std::shared_ptr<Graph> &graph,
                                 const Interval<double> &targetXRange,
                                 const Interval<double> &targetYRange)
{
    assert(targetXRange.strictlyContains(getGraphXRange(graph)) || targetYRange.strictlyContains(getGraphYRange(graph)));
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

void GraphProcessor::recursiveComputeNodes(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                                           const Interval<double> &xRange, const Interval<double> &yRange,
                                           int windowWidth, int windowHeight)
{
    std::unordered_map<std::unique_ptr<GraphNode> *, std::future<void> > futuresMap;

    const auto rangePerPixel =
            std::exp2(std::floor(std::log2(
                std::min(xRange.size(), yRange.size()) / std::max(
                    windowWidth, windowHeight))));

    std::cout << xRange<<", "<<yRange<<" ("<<windowWidth<<"x"<<windowHeight<<"=>"<<rangePerPixel<<std::endl;

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

        if (auto it = futuresMap.find(curr); it != futuresMap.end())
        {
            it->second.wait();
        }

        if ((*curr)->solution == IntervalValues::True || (*curr)->solution == IntervalValues::False)
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

            if (child->solution == IntervalValues::Unknown_s)
            {
                futuresMap.emplace(&child, threadPool->addTask(computeTask, &child, formula));
            }
        }
    }

    // TODO: subpixel refinement

    // wait for rest of the nodes to finish
    for (auto &it: futuresMap)
    {
        it.second.wait();
    }
}

void GraphProcessor::computeTask(const std::unique_ptr<GraphNode> *node, const std::shared_ptr<Formula> &formula)
{
    if ((*node)->solution != IntervalValues::Unknown_s)
    {
        return;
    }

    const auto &postfixExpr = formula->getPostfixExpression();

    const auto x = ComputeInterval{(*node)->xRange};
    const auto y = ComputeInterval{(*node)->yRange};

    std::stack<ComputeInterval> valueStack;

    Interval<bool> result = IntervalValues::Unknown_s;

    for (const auto &[value, type]: postfixExpr)
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
            else if (value == ">=")
            {
                result = operand1 >= operand2;
                break;
            }
            else if (value == "<=")
            {
                result = operand1 <= operand2;
                break;
            }
            else if (value == "=")
            {
                result = operand1 == operand2;
                break;
            }
            else if (value == "!=")
            {
                result = operand1 != operand2;
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
