//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHRASTERIZER_H
#define GRAPHRASTERIZER_H
#include <functional>
#include <memory>
#include <queue>

#include "../Math/Interval.h"


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
    explicit GraphRasterizer(const std::shared_ptr<Window> &window);


    static int evaluateGraph(const std::unique_ptr<GraphNode>& node, const Interval<double> &xRange, const Interval<double> &yRange);

    void rasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange, const Interval<double> &yRange, int windowWidth, int
                   windowHeight);
    void setRasterizeCompleteCallback(const std::function<void(const std::vector<int> &)> &callback);
private:
    static bool nodeIsLeaf(const std::unique_ptr<GraphNode> &curr);

    std::function<void(const std::vector<int> &)> rasterizeCompleteCallback;
    std::shared_ptr<Window> window;

    std::unordered_map<double, int> cache;
};



#endif //GRAPHRASTERIZER_H
