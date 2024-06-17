//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHPROCESSOR_H
#define GRAPHPROCESSOR_H
#include <functional>
#include <memory>
#include <unordered_set>

#include "Interval.h"
#include "../Util/ThreadPool.h"


class Plot;
struct GraphNode;
class Window;

class Formula;
struct Graph;

class GraphProcessor
{
public:
    GraphProcessor(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);

    void process(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula, const Interval<double> &xRange,
                 const Interval<double> &yRange, int windowWidth, int windowHeight);

private:
    static void computeTask(const std::unique_ptr<GraphNode> *node, const std::shared_ptr<Formula> &formula);

    static void expandGraph(const std::shared_ptr<Graph> &graph, const Interval<double> &targetXRange,
                        const Interval<double> &targetYRange);


    static std::pair<Interval<double>, Interval<double> > getRoundedRanges(const std::shared_ptr<Graph> &graph, const Interval<double> &xRange, const
                                                                           Interval<double> &yRange);

    static void expandGraphToPlaceNode(std::unique_ptr<GraphNode> &nodeToExpand,
                                       std::unique_ptr<GraphNode> &nodeToPlace);

    void recursiveComputeNodes(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula, const Interval<double> &xRange, const
                               Interval<double> &yRange, int windowWidth, int windowHeight);

    std::shared_ptr<Window> window;

    std::shared_ptr<ThreadPool> threadPool;
};


#endif //GRAPHPROCESSOR_H
