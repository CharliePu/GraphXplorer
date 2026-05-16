//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHRASTERIZER_H
#define GRAPHRASTERIZER_H
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

#include "Interval.h"
#include "RasterizedPlot.h"
#include "../Graph/Graph.h"

class ThreadPool;
class Window;
class Formula;
class ChunkRegionRasterizer;
class ChunkContourRasterizer;

class GraphRasterizer
{
public:
    using CancelFn = std::function<bool()>;

    GraphRasterizer(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);
    ~GraphRasterizer();

    RasterizedPlot rasterize(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                             const Interval &xRange,
                             const Interval &yRange, int windowWidth, int windowHeight,
                             const CancelFn &cancelled = {});

private:
    enum class LookupSource
    {
        Exact,
        Parent,
        Child,
        Missing
    };

    struct SampleResult
    {
        int state;
        LookupSource source;
        int levelUsed;
        int64_t chunkX;
        int64_t chunkY;
    };

    struct MixedChunkKey
    {
        int64_t chunkX;
        int64_t chunkY;
        int level;

        bool operator==(const MixedChunkKey &other) const
        {
            return chunkX == other.chunkX && chunkY == other.chunkY && level == other.level;
        }
    };

    struct MixedChunkKeyHash
    {
        size_t operator()(const MixedChunkKey &key) const
        {
            return chunkKeyHash(key.chunkX, key.chunkY, key.level);
        }
    };

    static std::optional<Interval> lookupAtLevel(const std::shared_ptr<Graph> &graph, double x, double y, int level);
    static int intervalToState(const Interval &interval);
    static SampleResult samplePoint(const std::shared_ptr<Graph> &graph, double x, double y, int targetLevel);
    std::unique_ptr<ChunkRegionRasterizer> chunkRegionRasterizer;
    std::unique_ptr<ChunkContourRasterizer> chunkContourRasterizer;
    const Formula *cachedFormula;
};

#endif //GRAPHRASTERIZER_H
