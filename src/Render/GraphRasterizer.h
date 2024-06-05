//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHRASTERIZER_H
#define GRAPHRASTERIZER_H
#include <functional>
#include <memory>

#include "../Math/Interval.h"


class Window;
class Mesh;
struct Graph;

class GraphRasterizer {
public:
    explicit GraphRasterizer(const std::shared_ptr<Window> &window);

    void rasterize(const std::shared_ptr<Graph> & graph, const Interval<double> & xRange, const Interval<double> & yRange);
    void setRasterizeCompleteCallback(const std::function<void(const std::vector<Interval<bool>> &)> &callback);
private:
    std::function<void(const std::vector<Interval<bool>> &)> rasterizeCompleteCallback;
    std::shared_ptr<Window> window;
};



#endif //GRAPHRASTERIZER_H
