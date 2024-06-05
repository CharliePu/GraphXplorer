//
// Created by charl on 6/3/2024.
//

#ifndef GRAPH_H
#define GRAPH_H
#include <array>
#include <memory>

#include "Interval.h"

struct GraphNode
{
    GraphNode(GraphNode *parent, const Interval<double> &xRange, const Interval<double> &yRange);
    ~GraphNode() = default;

    GraphNode *parent;
    std::array<std::unique_ptr<GraphNode>, 4> children;
    Interval<bool> solution;
    Interval<double> xRange;
    Interval<double> yRange;

    [[nodiscard]] bool isLeaf() const;
    void subdivide();

    [[nodiscard]] GraphNode* findChild(const Interval<double>& xRange, const Interval<double>& yRange) const;
    [[nodiscard]] int findChildIndex(const Interval<double>& xRange, const Interval<double>& yRange) const;

    [[nodiscard]] bool matches(const Interval<double>& otherXRange, const Interval<double>& otherYRange) const;
};

struct Graph
{
    std::unique_ptr<GraphNode> root;

    [[nodiscard]] Interval<double> getXRange() const;
    [[nodiscard]] Interval<double> getYRange() const;
    [[nodiscard]] bool empty() const;

    [[nodiscard]] Interval<bool> evaluate(const Interval<double>& x, const Interval<double>& y) const;
};



#endif //GRAPH_H
