//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHRASTERIZER_H
#define GRAPHRASTERIZER_H
#include <functional>
#include <future>
#include <memory>
#include <queue>

#include "GraphProcessor.h"
#include "Interval.h"


class ThreadPool;
struct GraphNode;
class Window;
struct Mesh;
struct Graph;

class GraphRasterizer {
public:
    GraphRasterizer(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);


    static int evaluateGraph(const std::unique_ptr<GraphNode>& node, const Interval<double> &xRange, const Interval<double> &yRange);

    std::vector<int> rasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange,
                               const Interval<double> &yRange, int windowWidth, int
                               windowHeight);
private:
    std::shared_ptr<Window> window;

    std::shared_ptr<ThreadPool> threadPool;
};



#endif //GRAPHRASTERIZER_H
