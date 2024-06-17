// Created by charl on 6/15/2024.

#ifndef GRAPHOPERATIONS_H
#define GRAPHOPERATIONS_H

#include <cassert>
#include <memory>
#include <cmath>
#include <limits>
#include "./Graph.h"
#include "../Math/Interval.h"

inline Interval<double> &getGraphXRange(const std::shared_ptr<Graph> &graph)
{
    assert(graph->root);
    return graph->root->xRange;
}

inline Interval<double> &getGraphYRange(const std::shared_ptr<Graph> &graph)
{
    assert(graph->root);
    return graph->root->yRange;
}

inline bool nodesIntervalMatches(const std::unique_ptr<GraphNode> &curr, const std::unique_ptr<GraphNode> &root)
{
    return curr->xRange == root->xRange && curr->yRange == root->yRange;
}

inline bool nodeIsLeaf(const std::unique_ptr<GraphNode> &curr)
{
    assert(curr);
    return !curr->children[0] && !curr->children[1] && !curr->children[2] && !curr->children[3];
}

inline bool isPowerOfTwo(double size)
{
    return std::abs(std::log2(size) - std::floor(std::log2(size))) < std::numeric_limits<double>::epsilon();
}

inline std::unique_ptr<GraphNode> *getMatchingChildNode(const std::unique_ptr<GraphNode> &parentNode,
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

inline std::unique_ptr<GraphNode> createNode(GraphNode *parent, Interval<double> xRange, Interval<double> yRange)
{
    auto node = std::make_unique<GraphNode>();
    node->parent = parent;
    node->solution = IntervalValues::Unknown_s;
    node->xRange = xRange;
    node->yRange = yRange;
    for (auto &child: node->children)
    {
        child = nullptr;
    }
    return node;
}

inline void subdivideNode(const std::unique_ptr<GraphNode> &curr)
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


#endif //GRAPHOPERATIONS_H
