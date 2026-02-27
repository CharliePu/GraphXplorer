//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHPROCESSOR_H
#define GRAPHPROCESSOR_H

#include <functional>
#include <cstdint>
#include <memory>
#include <utility>

#include "Interval.h"
#include "../Graph/Graph.h"
#include "../Util/ThreadPool.h"

class Window;

class Formula;

class GraphProcessor
{
public:
    GraphProcessor(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);

    void process(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula, const Interval &xRange,
                 const Interval &yRange, int windowWidth, int windowHeight);

private:
    static int getTargetLevel(const Interval &xRange, const Interval &yRange, int windowWidth, int windowHeight);
    static int getCoarsestViewportLevel(const Interval &xRange, const Interval &yRange);

    static std::pair<int64_t, int64_t> getChunkIndexBounds(const Interval &range, int level);
    static bool intersects(const Interval &lhs, const Interval &rhs);

    static Tile &getOrComputeTile(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                                  int64_t chunkX, int64_t chunkY, int level);

    void refineTile(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                    int64_t chunkX, int64_t chunkY, int level, int targetLevel,
                    const Interval &viewXRange, const Interval &viewYRange) const;

    std::shared_ptr<Window> window;

    std::shared_ptr<ThreadPool> threadPool;
};


#endif //GRAPHPROCESSOR_H
