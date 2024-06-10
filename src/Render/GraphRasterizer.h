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

class GraphRasterizer {
public:
    explicit GraphRasterizer(const std::shared_ptr<Window> &window);


    Interval<bool> evaluateGraph(const std::shared_ptr<Graph> & graph, Interval<double> xRange, Interval<double> yRange);

    void rasterize(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange, const Interval<double> &yRange, int windowWidth, int
                   windowHeight);
    void setRasterizeCompleteCallback(const std::function<void(const std::vector<Interval<bool>> &)> &callback);
private:
    static bool nodeIsLeaf(const std::unique_ptr<GraphNode> &curr);

    std::function<void(const std::vector<Interval<bool>> &)> rasterizeCompleteCallback;
    std::shared_ptr<Window> window;
};



#endif //GRAPHRASTERIZER_H
