//
// Created by charl on 6/3/2024.
//

#ifndef COMPUTEENGINE_H
#define COMPUTEENGINE_H
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

struct ComputeRequest
{
    std::shared_ptr<Graph> graph;
    std::shared_ptr<Formula> formula;
    Interval<double> xRange;
    Interval<double> yRange;
    int windowWidth;
    int windowHeight;
};

struct ComputeTask
{
    ComputeRequest request;
    std::vector<std::unique_ptr<GraphNode> *> nodes;
};

class ComputeEngine
{
public:
    explicit ComputeEngine(const std::shared_ptr<Window> &window);

    void setComputeCompleteCallback(const std::function<void(const ComputeRequest &)> &callback);


    void processGraph(const ComputeRequest &request);

    void pollAsyncStates();

private:
    static void expandGraph(const std::shared_ptr<Graph> &graph, Interval<double> targetXRange,
                            Interval<double> targetYRange);

    static void computeTask(const std::unique_ptr<GraphNode> *node, const std::shared_ptr<Formula> &formula);

    static Interval<double> &getGraphXRange(const std::shared_ptr<Graph> &graph);

    static Interval<double> &getGraphYRange(const std::shared_ptr<Graph> &graph);

    static bool isPowerOfTwo(double size);

    static bool nodesIntervalMatches(const std::unique_ptr<GraphNode> &curr, const std::unique_ptr<GraphNode> &root);

    static std::pair<Interval<double>, Interval<double> > getRoundedRanges(const ComputeRequest &request);

    static bool nodeIsLeaf(const std::unique_ptr<GraphNode> &curr);

    static std::unique_ptr<GraphNode> *getMatchingChildNode(const std::unique_ptr<GraphNode> &parentNode,
                                                            const std::unique_ptr<GraphNode> &nodeToMatch);

    static void expandGraphToPlaceNode(std::unique_ptr<GraphNode> &nodeToExpand,
                                       std::unique_ptr<GraphNode> &nodeToPlace);

    static std::unique_ptr<GraphNode> createNode(GraphNode *parent, Interval<double> xRange, Interval<double> yRange);

    static void subdivideNode(const std::unique_ptr<GraphNode> &curr);

    void recursiveComputeNodes(const ComputeRequest &request, const std::shared_ptr<Graph> &graph);

    std::shared_ptr<Window> window;

    std::function<void(const ComputeRequest &)> computeCompleteCallback;

    ThreadPool threadPool;

    std::unordered_map<std::unique_ptr<GraphNode> *, std::future<void> > futuresMap;

    std::shared_ptr<ComputeTask> currentTask;
};


#endif //COMPUTEENGINE_H
