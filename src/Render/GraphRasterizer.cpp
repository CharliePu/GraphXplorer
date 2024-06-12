//
// Created by charl on 6/3/2024.
//

#include "GraphRasterizer.h"

#include <iostream>
#include <queue>

#include "Mesh.h"
#include "../Math/Graph.h"
#include "../Core/Window.h"
#include "../Math/ComputeEngine.h"

GraphRasterizer::GraphRasterizer(const std::shared_ptr<Window> &window):
    window{window}
{
}

int GraphRasterizer::evaluateGraph(const std::unique_ptr<GraphNode> &node, const Interval<double> &xRange,
                                   const Interval<double> &yRange)
{
    if (nodeIsLeaf(node))
    {
        if (node->solution == IntervalValues::True)
        {
            return 1;
        }
        else if (node->solution == IntervalValues::False)
        {
            return 0;
        }
        else
        {
            return 2;
        }
    }

    for (const auto& child : node->children)
    {
        if (child->xRange.contains(xRange) && child->yRange.contains(yRange))
        {
            return evaluateGraph(child, xRange, yRange);
        }
    }

    throw std::invalid_argument("Failed to evaluate graph");
}

void GraphRasterizer::rasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange,
                                const Interval<double> &yRange, const int windowWidth, const int windowHeight)
{
    std::vector<int> image;
    image.reserve(windowWidth * windowHeight);

    const auto deltaX = xRange.size() / windowWidth;
    const auto deltaY = yRange.size() / windowHeight;

    for (int j = 0; j < windowHeight; ++j)
    {
        const auto y = yRange.lower + j * deltaY;
        for (int i = 0; i < windowWidth; ++i)
        {
            const auto x = xRange.lower + i * deltaX;

            // TODO: replace with {x, x+deltax} and {y, y+deltay}
            // This requires changes in evaluate to add sampling
            auto result = evaluateGraph(graph->root, {x, x},
                                        {y, y});

            image.push_back(result);
        }
    }

    rasterizeCompleteCallback(image);
}

void GraphRasterizer::setRasterizeCompleteCallback(const std::function<void(const std::vector<int> &)> &callback)
{
    rasterizeCompleteCallback = callback;
}

bool GraphRasterizer::nodeIsLeaf(const std::unique_ptr<GraphNode> &curr)
{
    return !curr->children[0] && !curr->children[1] && !curr->children[2] && !curr->children[3];
}
