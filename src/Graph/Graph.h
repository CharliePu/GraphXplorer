//
// Created by charl on 6/3/2024.
//

#ifndef GRAPH_H
#define GRAPH_H
#include <array>
#include <memory>

#include "../Math/Interval.h"

struct GraphNode
{
    GraphNode *parent;
    std::array<std::unique_ptr<GraphNode>, 4> children;
    Interval<bool> solution;
    Interval<double> xRange;
    Interval<double> yRange;
};

struct Graph
{
    std::unique_ptr<GraphNode> root;
};



#endif //GRAPH_H
