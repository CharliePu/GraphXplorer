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

template <class T>
struct pairHash {
    std::size_t operator () (const std::pair<T, T> &pair) const {
        return std::hash<T>()(pair.first) ^ std::hash<T>()(pair.second);
    }
};

class GraphRasterizer {
public:
    GraphRasterizer(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);


    static int evaluateGraph(const std::unique_ptr<GraphNode>& node, const Interval<double> &xRange, const Interval<double> &yRange);

    void requestRasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange,
                          const Interval<double> &yRange, int windowWidth, int windowHeight);

    std::vector<int> rasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange,
                               const Interval<double> &yRange, int windowWidth, int
                               windowHeight);

    void pollAsyncStates();

    void setRasterizeCompleteCallback(const std::function<void(const std::vector<int> &)> &callback);

    void rasterizeTemp(const std::shared_ptr<Graph> & graph, Interval<double> interval, Interval<double> yRange, int windowWidth, int windowHeight);

private:
    static bool nodeIsLeaf(const std::unique_ptr<GraphNode> &curr);

    std::function<void(const std::vector<int> &)> rasterizeCompleteCallback;
    std::shared_ptr<Window> window;

    std::future<std::vector<int>> taskFuture;

    std::shared_ptr<ThreadPool> threadPool;
};



#endif //GRAPHRASTERIZER_H
