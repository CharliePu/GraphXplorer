//
// Created by charl on 6/3/2024.
//

#ifndef COMPUTEENGINE_H
#define COMPUTEENGINE_H
#include <functional>
#include <memory>

#include "Interval.h"
#include "../Util/ThreadPool.h"


struct GraphNode;
class Window;

class Formula;
struct Graph;

struct ComputeRequest
{
    std::shared_ptr<Formula> formula;
    Interval<double> xRange;
    Interval<double> yRange;
};

class ComputeEngine {
public:
    explicit ComputeEngine(const std::shared_ptr<Window> &window);

    void setComputeCompleteCallback(const std::function<void()> &callback);

    void expandGraph(const std::shared_ptr<Graph> &graph, Interval<double> xRange, Interval<double> yRange);

    void run(const std::shared_ptr<Graph> & graph, const ComputeRequest & request);

    void pollAsyncStates();

private:

    static void computeTask(GraphNode * node, const std::shared_ptr<Formula> &formula);

    std::shared_ptr<Window> window;

    std::function<void()> computeCompleteCallback;
    ThreadPool threadPool;
    std::unordered_map<GraphNode *, std::future<void>> futures;
    bool newTask;
};



#endif //COMPUTEENGINE_H
