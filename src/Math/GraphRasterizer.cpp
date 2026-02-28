//
// Created by charl on 6/3/2024.
//

#include "GraphRasterizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "ChunkContourRasterizer.h"
#include "ChunkRegionRasterizer.h"
#include "../Formula/Formula.h"
#include "../Graph/Graph.h"

namespace
{
bool isTopLevelComparisonOperator(const std::string_view op)
{
    return op == "=" || op == "<=" || op == ">=" || op == "<" || op == ">" || op == "!=";
}

bool comparisonSupportsContourRendering(const std::string_view op)
{
    return op == "=" || op == "<=" || op == ">=";
}

bool comparisonSupportsRegionRendering(const std::string_view op)
{
    return op != "=";
}

size_t findRpnSubExpressionStart(const std::vector<Token> &tokens, const size_t endExclusive)
{
    if (endExclusive == 0 || endExclusive > tokens.size())
    {
        return std::numeric_limits<size_t>::max();
    }

    auto neededValues = 1;
    for (auto i = endExclusive; i-- > 0;)
    {
        const auto &token = tokens[i];
        if (token.type == TokenType::NUMBER || token.type == TokenType::VARIABLE)
        {
            --neededValues;
        }
        else if (token.type == TokenType::FUNCTION)
        {
            // Unary functions keep the same stack depth (consume one, produce one).
        }
        else if (token.type == TokenType::OPERATOR)
        {
            // Binary operators increase required operand count by one when walking backward.
            ++neededValues;
        }
        else
        {
            return std::numeric_limits<size_t>::max();
        }

        if (neededValues == 0)
        {
            return i;
        }
    }

    return std::numeric_limits<size_t>::max();
}

bool tryBuildTopLevelComparisonResidual(const Formula &formula, RPN &residualRpn, std::string &comparisonOperator)
{
    const auto &tokens = formula.getRPN().tokens;
    if (tokens.empty())
    {
        return false;
    }

    const auto &root = tokens.back();
    if (root.type != TokenType::OPERATOR || !isTopLevelComparisonOperator(root.value))
    {
        return false;
    }

    const auto rootIndex = tokens.size() - 1;
    const auto rhsStart = findRpnSubExpressionStart(tokens, rootIndex);
    if (rhsStart == std::numeric_limits<size_t>::max() || rhsStart >= rootIndex)
    {
        return false;
    }

    const auto lhsStart = findRpnSubExpressionStart(tokens, rhsStart);
    if (lhsStart != 0)
    {
        return false;
    }

    residualRpn.tokens.clear();
    residualRpn.tokens.reserve(rootIndex + 1);
    residualRpn.tokens.insert(residualRpn.tokens.end(), tokens.begin(), tokens.begin() + rhsStart);
    residualRpn.tokens.insert(residualRpn.tokens.end(), tokens.begin() + rhsStart, tokens.begin() + rootIndex);
    residualRpn.tokens.push_back(Token{TokenType::OPERATOR, "-"});

    comparisonOperator = root.value;
    return true;
}
}

GraphRasterizer::GraphRasterizer(const std::shared_ptr<Window> &window,
                                 const std::shared_ptr<ThreadPool> &threadPool): chunkRegionRasterizer{
                                                                                      std::make_unique<ChunkRegionRasterizer>()
                                                                                  },
                                                                                  chunkContourRasterizer{
                                                                                      std::make_unique<ChunkContourRasterizer>()
                                                                                  },
                                                                                  cachedFormula{nullptr}
{
    (void)window;
    (void)threadPool;
}

GraphRasterizer::~GraphRasterizer() = default;

int GraphRasterizer::getTargetLevel(const Interval &xRange, const Interval &yRange, const int windowWidth,
                                    const int windowHeight)
{
    const auto minRangeSize = std::min(xRange.size(), yRange.size());
    const auto maxWindowSize = std::max(windowWidth, windowHeight);

    if (minRangeSize <= 0.0 || maxWindowSize <= 0)
    {
        return 0;
    }

    const auto rangePerPixel = minRangeSize / static_cast<double>(maxWindowSize);
    const auto rangePerChunk = rangePerPixel * static_cast<double>(MIN_CHUNK_PIXELS);
    return static_cast<int>(std::floor(std::log2(rangePerChunk)));
}

std::pair<int64_t, int64_t> GraphRasterizer::getChunkIndexBounds(const Interval &range, const int level)
{
    if (range.upper <= range.lower)
    {
        const auto chunkIndex = worldToChunkIndex(range.lower, level);
        return {chunkIndex, chunkIndex};
    }

    const auto chunkSize = chunkSizeForLevel(level);
    const auto minIndex = worldToChunkIndex(range.lower, level);
    const auto scaledUpper = range.upper / chunkSize;
    const auto upperInclusiveScaled = std::nextafter(scaledUpper, -std::numeric_limits<double>::infinity());
    const auto maxIndex = static_cast<int64_t>(std::floor(upperInclusiveScaled));
    return {minIndex, maxIndex};
}

std::optional<Interval> GraphRasterizer::lookupAtLevel(const std::shared_ptr<Graph> &graph, const double x,
                                                       const double y, const int level)
{
    const auto chunkX = worldToChunkIndex(x, level);
    const auto chunkY = worldToChunkIndex(y, level);
    const TileKey key{chunkX, chunkY, level};

    if (const auto it = graph->tiles.find(key); it != graph->tiles.end())
    {
        return it->second.solution;
    }

    return std::nullopt;
}

int GraphRasterizer::intervalToState(const Interval &interval)
{
    if (interval.allTrue())
    {
        return 1;
    }

    if (interval.allFalse())
    {
        return 0;
    }

    return -1;
}

GraphRasterizer::SampleResult GraphRasterizer::samplePoint(const std::shared_ptr<Graph> &graph, const double x,
                                                           const double y, const int targetLevel)
{
    const auto exactChunkX = worldToChunkIndex(x, targetLevel);
    const auto exactChunkY = worldToChunkIndex(y, targetLevel);
    if (const auto exact = lookupAtLevel(graph, x, y, targetLevel))
    {
        return {intervalToState(*exact), LookupSource::Exact, targetLevel, exactChunkX, exactChunkY};
    }

    for (auto it = graph->activeLevels.lower_bound(targetLevel); it != graph->activeLevels.end(); ++it)
    {
        const auto level = *it;
        if (const auto parent = lookupAtLevel(graph, x, y, level))
        {
            return {intervalToState(*parent), LookupSource::Parent, level, worldToChunkIndex(x, level),
                    worldToChunkIndex(y, level)};
        }
    }

    const auto begin = graph->activeLevels.lower_bound(targetLevel);
    for (auto it = std::make_reverse_iterator(begin); it != graph->activeLevels.rend(); ++it)
    {
        const auto level = *it;
        if (const auto child = lookupAtLevel(graph, x, y, level))
        {
            return {intervalToState(*child), LookupSource::Child, level, worldToChunkIndex(x, level),
                    worldToChunkIndex(y, level)};
        }
    }

    return {-1, LookupSource::Missing, targetLevel, exactChunkX, exactChunkY};
}

RasterizedPlot GraphRasterizer::rasterize(const std::shared_ptr<Graph> &graph,
                                          const std::shared_ptr<Formula> &formula,
                                          const Interval &xRange,
                                          const Interval &yRange, const int windowWidth,
                                          const int windowHeight)
{
    if (!graph)
    {
        throw std::invalid_argument("Graph must not be null");
    }

    RasterizedPlot result;

    if (windowWidth <= 0 || windowHeight <= 0)
    {
        return result;
    }

    if (cachedFormula != formula.get())
    {
        chunkRegionRasterizer->clearCache();
        chunkContourRasterizer->clearCache();
        cachedFormula = formula.get();
    }

    const auto targetLevel = getTargetLevel(xRange, yRange, windowWidth, windowHeight);
    const auto [minChunkX, maxChunkX] = getChunkIndexBounds(xRange, targetLevel);
    const auto [minChunkY, maxChunkY] = getChunkIndexBounds(yRange, targetLevel);
    const auto targetChunkSize = chunkSizeForLevel(targetLevel);

    RPN residualRpn;
    std::string topComparisonOperator;
    const auto hasTopLevelComparisonResidual = formula
                                                   && tryBuildTopLevelComparisonResidual(
                                                       *formula, residualRpn, topComparisonOperator);
    const auto shouldRenderContour = hasTopLevelComparisonResidual
                                     && comparisonSupportsContourRendering(topComparisonOperator);
    const auto shouldRenderRegion = !hasTopLevelComparisonResidual
                                    || comparisonSupportsRegionRendering(topComparisonOperator);

    std::unordered_set<MixedChunkKey, MixedChunkKeyHash> emittedChunks;
    for (auto chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY)
    {
        for (auto chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX)
        {
            const auto x = (static_cast<double>(chunkX) + 0.5) * targetChunkSize;
            const auto y = (static_cast<double>(chunkY) + 0.5) * targetChunkSize;
            const auto sample = samplePoint(graph, x, y, targetLevel);
            if (sample.source == LookupSource::Missing)
            {
                continue;
            }

            const auto key = MixedChunkKey{sample.chunkX, sample.chunkY, sample.levelUsed};
            if (!emittedChunks.insert(key).second)
            {
                continue;
            }

            const auto chunkSource = sample.source == LookupSource::Parent
                                         ? RasterChunkSource::Parent
                                         : (sample.source == LookupSource::Child
                                                ? RasterChunkSource::Child
                                                : RasterChunkSource::Exact);

            const auto sampledXRange = chunkIndexToRange(sample.chunkX, sample.levelUsed);
            const auto sampledYRange = chunkIndexToRange(sample.chunkY, sample.levelUsed);
            RasterChunk chunk{
                sample.chunkX, sample.chunkY, sample.levelUsed, sampledXRange, sampledYRange, sample.state, chunkSource
            };

            ChunkRenderData renderData{chunk, std::nullopt, std::nullopt};
            if (sample.state < 0 && formula)
            {
                if (shouldRenderRegion)
                {
                    renderData.region = chunkRegionRasterizer->rasterizeChunkRegion(key.chunkX, key.chunkY, key.level,
                                                                                     formula);
                }

                if (shouldRenderContour)
                {
                    renderData.contour = chunkContourRasterizer->rasterizeChunkContour(
                        key.chunkX, key.chunkY, key.level, residualRpn);
                }

            }

            result.chunkRenderData.push_back(std::move(renderData));
        }
    }

    return result;
}
