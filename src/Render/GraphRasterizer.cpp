//
// Created by charl on 6/3/2024.
//

#include "GraphRasterizer.h"

#include "Mesh.h"
#include "../Math/Graph.h"
#include "../Core/Window.h"

GraphRasterizer::GraphRasterizer(const std::shared_ptr<Window> &window):
    window{window}
{
}

void GraphRasterizer::rasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange,
                                const Interval<double> &yRange)
{
    std::vector<Interval<bool>> image;

    // TODO: pack width and heigh together with image to pass to callback
    const auto deltaX = xRange.size() / window->getWidth();
    const auto deltaY = yRange.size() / window->getHeight();

    for (auto y = yRange.lower; y < yRange.upper; )
    {
        for (auto x = xRange.lower; x < xRange.upper; )
        {
            // TODO: replace with {x, x+deltax} and {y, y+deltay}
            // This requires changes in evaluate to add sampling
            auto result = graph->evaluate({x, x}, {y, y});

            image.push_back(result);

            x += deltaX;
        }
        y += deltaY;
    }

    rasterizeCompleteCallback(image);
}

void GraphRasterizer::setRasterizeCompleteCallback(const std::function<void(const std::vector<Interval<bool>> &)> &callback)
{
    rasterizeCompleteCallback = callback;
}
