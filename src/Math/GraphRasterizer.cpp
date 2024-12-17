//
// Created by charl on 6/3/2024.
//

#include "GraphRasterizer.h"

#include <stack>

#include "../Render/Mesh.h"
#include "../Graph/Graph.h"
#include "../Graph/GraphOperations.h"
#include "../Core/Window.h"
#include "../Util/ThreadPool.h"

GraphRasterizer::GraphRasterizer(const std::shared_ptr<Window> &window,
                                 const std::shared_ptr<ThreadPool> &threadPool): window{window},
    threadPool{threadPool}
{
}

int GraphRasterizer::evaluateGraph(const std::unique_ptr<GraphNode>& node, const Interval& xRange, const Interval& yRange, bool debug)
{
    std::stack<const std::unique_ptr<GraphNode>*> nodeStack;
    nodeStack.push(&node);

    while (!nodeStack.empty())
    {
        const std::unique_ptr<GraphNode>& currentNode = *nodeStack.top();
        nodeStack.pop();

        if (nodeIsLeaf(currentNode))
        {
            if (currentNode->solution.allTrue())
            {
                return 1;
            }
            else if (currentNode->solution.allFalse())
            {
                return 0;
            }
            else
            {
                return -1;
            }
        }

        bool found = false;
        for (const auto& child : currentNode->children)
        {
            if (child->xRange.contains(xRange) && child->yRange.contains(yRange))
            {
                nodeStack.push(&child);
                found = true;
                break;
            }
        }

        if (!found)
        {
            throw std::invalid_argument("Failed to evaluate graph");
        }
    }

    throw std::invalid_argument("Failed to evaluate graph");
}


std::vector<int> GraphRasterizer::rasterize(const std::shared_ptr<Graph> &graph, const Interval &xRange,
                                            const Interval &yRange, const int windowWidth,
                                            const int windowHeight, bool debug)
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
                                        {y, y}, debug);

            image.push_back(result);
        }
    }

    return image;
}