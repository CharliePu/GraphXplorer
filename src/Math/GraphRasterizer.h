//
// Created by charl on 6/3/2024.
//

#ifndef GRAPHRASTERIZER_H
#define GRAPHRASTERIZER_H
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ChunkRenderer.h"
#include "Interval.h"
#include "RasterizedPlot.h"


class ThreadPool;
class Window;
class Formula;
struct Mesh;
struct Graph;

class GraphRasterizer {
public:
    GraphRasterizer(const std::shared_ptr<Window> &window, const std::shared_ptr<ThreadPool> &threadPool);


    RasterizedPlot rasterize(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                             const Interval &xRange,
                             const Interval &yRange, int windowWidth, int
                             windowHeight);

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

    struct MixedTextureKey
    {
        int64_t chunkX;
        int64_t chunkY;
        int level;

        bool operator==(const MixedTextureKey &other) const
        {
            return chunkX == other.chunkX && chunkY == other.chunkY && level == other.level;
        }
    };

    struct MixedTextureKeyHash
    {
        size_t operator()(const MixedTextureKey &key) const
        {
            const auto h1 = std::hash<int64_t>{}(key.chunkX);
            const auto h2 = std::hash<int64_t>{}(key.chunkY);
            const auto h3 = std::hash<int>{}(key.level);

            size_t seed = h1;
            seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    static int getTargetLevel(const Interval &xRange, const Interval &yRange, int windowWidth, int windowHeight);
    static std::pair<int64_t, int64_t> getChunkIndexBounds(const Interval &range, int level);
    static std::optional<Interval> lookupAtLevel(const std::shared_ptr<Graph> &graph, double x, double y, int level);
    static int intervalToState(const Interval &interval);
    static SampleResult samplePoint(const std::shared_ptr<Graph> &graph, double x, double y, int targetLevel);
private:
    std::shared_ptr<Window> window;

    std::shared_ptr<ThreadPool> threadPool;
    std::unique_ptr<ChunkRenderer> chunkRenderer;
    bool usingGpuChunkRenderer;
    const Formula *cachedFormula;
    std::unordered_map<MixedTextureKey, std::vector<int>, MixedTextureKeyHash> mixedChunkTextureCache;
};



#endif //GRAPHRASTERIZER_H
