//
// Created by charl on 6/2/2024.
//

#include "Plot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <tuple>

#include <glad/glad.h>
#include <staplegl/staplegl.hpp>

#include "../Core/Window.h"
#include "../Formula/Formula.h"
#include "../Graph/Graph.h"
#include "../Math/ComputeEngine.h"
#include "../Render/Mesh.h"
#include "../Util/PerformanceProfiler.h"

namespace
{
bool hasValidViewportSize(const int width, const int height)
{
    return width > 0 && height > 0;
}
}

Plot::SolidChunkRenderItem::SolidChunkRenderItem(std::optional<Mesh> normalMesh, std::optional<Mesh> contourMesh,
                                                 Mesh debugMesh): normalMesh{
                                                                                                       std::move(
                                                                                                           normalMesh)
                                                                                                   },
                                                                                                   contour{
                                                                                                       std::move(
                                                                                                           contourMesh)
                                                                                                   },
                                                                                                   debugMesh{
                                                                                                       std::move(
                                                                                                           debugMesh)
                                                                                                   }
{
}

const Mesh *Plot::SolidChunkRenderItem::meshForMode(const bool debugMode) const
{
    if (debugMode)
    {
        return &debugMesh;
    }

    if (!normalMesh.has_value())
    {
        return nullptr;
    }

    return &normalMesh.value();
}

const Mesh *Plot::SolidChunkRenderItem::contourMesh() const
{
    if (!contour.has_value())
    {
        return nullptr;
    }

    return &contour.value();
}

bool Plot::SolidChunkRenderItem::hasNormalMesh() const
{
    return normalMesh.has_value();
}

Plot::TexturedChunkRenderItem::TexturedChunkRenderItem(Mesh normalMesh, std::optional<Mesh> contourMesh,
                                                       Mesh debugMesh): normalMesh{
                                                                                              std::move(normalMesh)
                                                                                          },
                                                                                          contour{
                                                                                              std::move(contourMesh)
                                                                                          },
                                                                                          debugMesh{
                                                                                              std::move(debugMesh)
                                                                                          }
{
}

const Mesh *Plot::TexturedChunkRenderItem::meshForMode(const bool debugMode) const
{
    if (debugMode)
    {
        return &debugMesh;
    }

    return &normalMesh;
}

const Mesh *Plot::TexturedChunkRenderItem::contourMesh() const
{
    if (!contour.has_value())
    {
        return nullptr;
    }

    return &contour.value();
}

bool Plot::TexturedChunkRenderItem::hasNormalMesh() const
{
    return true;
}

int Plot::getTargetLevel(const Interval &xRange, const Interval &yRange, const int windowWidth,
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

std::pair<int64_t, int64_t> Plot::getChunkIndexBounds(const Interval &range, const int level)
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

PlotChunkKey Plot::toChunkKey(const RasterChunk &chunk)
{
    return {chunk.chunkX, chunk.chunkY, chunk.level};
}

Plot::Plot(const std::shared_ptr<ComputeEngine> &engine,
           const std::shared_ptr<Window> &window): graph{},
                                                   formula{},
                                                   computeEngine{engine},
                                                   window{window},
                                                   viewXRange{-20.0, 20.0},
                                                   viewYRange{-20.0, 20.0},
                                                   chunkShader{
                                                       new staplegl::shader_program{
                                                           "chunk_shader",
                                                           {
                                                               std::pair{
                                                                   staplegl::shader_type::vertex, "./shader/chunk.vert"
                                                               },
                                                               std::pair{
                                                                   staplegl::shader_type::fragment, "./shader/chunk.frag"
                                                               }
                                                           }
                                                       }
                                                   },
                                                   plotShader{
                                                       new staplegl::shader_program{
                                                           "plot_shader",
                                                           {
                                                               std::pair{
                                                                   staplegl::shader_type::vertex, "./shader/plot.vert"
                                                               },
                                                               std::pair{
                                                                   staplegl::shader_type::fragment, "./shader/plot.frag"
                                                               }
                                                           }
                                                       }
                                                   },
                                                   contourShader{
                                                       new staplegl::shader_program{
                                                           "contour_shader",
                                                           {
                                                               std::pair{
                                                                   staplegl::shader_type::vertex, "./shader/line.vert"
                                                               },
                                                               std::pair{
                                                                   staplegl::shader_type::fragment, "./shader/line.frag"
                                                               }
                                                           }
                                                       }
                                                   },
                                                   visibleChunkMeshes{},
                                                   meshes{},
                                                   sampledChunkCache{},
                                                   chunkRegionTextureCache{},
                                                   chunkContourCache{},
                                                   chunkRenderItems{},
                                                   sampledChunkLevels{},
                                                   chunkModel{1.0f},
                                                   shouldRenderRegionForFormula{true},
                                                   debug{false}
{
    computeEngine->setComputeCompleteCallback(
        [this](std::vector<ChunkRenderData> chunkRenderDataBatch,
               const Interval xRange, const Interval yRange, const int width, const int height,
               const uint64_t requestId)
        {
            GRAPHX_PROFILE_SCOPE("plot.asyncCallback.total");
            (void)xRange;
            (void)yRange;
            (void)width;
            (void)height;
            (void)requestId;

            applyChunkRenderDataBatch(chunkRenderDataBatch);
            updateModelMat();
            rebuildAndPublishMeshes();
        });
}

void Plot::setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    plotCompleteCallback = callback;
}

void Plot::setPlotRangeChangedCallback(
    const std::function<void(const Interval &, const Interval &)> &callback)
{
    plotRangeChangedCallback = callback;
}

int Plot::getDepth() const
{
    return 2;
}

void Plot::requestNewPlot(const std::string &input)
{
    formula = std::make_shared<Formula>(input);
    shouldRenderRegionForFormula = !(formula && formula->isTopLevelOperator("="));
    graph = std::make_shared<Graph>();

    sampledChunkCache.clear();
    chunkRegionTextureCache.clear();
    chunkContourCache.clear();
    chunkRenderItems.clear();
    sampledChunkLevels.clear();
    visibleChunkMeshes.clear();
    meshes.clear();

    updateModelMat();
    rebuildAndPublishMeshes();

    computeEngine->addTask({graph, formula, viewXRange, viewYRange, window->getWidth(), window->getHeight()});
}

Mesh Plot::createColoredChunkMesh(const RasterChunk &chunk, const glm::vec4 &color) const
{
    auto chunkVao = std::make_shared<staplegl::vertex_array>();

    std::array vertices{
        static_cast<float>(chunk.xRange.upper), static_cast<float>(chunk.yRange.upper), 0.0f, 1.0f, 1.0f, color.r,
        color.g, color.b, color.a, // top right
        static_cast<float>(chunk.xRange.upper), static_cast<float>(chunk.yRange.lower), 0.0f, 1.0f, 0.0f, color.r,
        color.g, color.b, color.a, // bottom right
        static_cast<float>(chunk.xRange.lower), static_cast<float>(chunk.yRange.lower), 0.0f, 0.0f, 0.0f, color.r,
        color.g, color.b, color.a, // bottom left
        static_cast<float>(chunk.xRange.lower), static_cast<float>(chunk.yRange.upper), 0.0f, 0.0f, 1.0f, color.r,
        color.g, color.b, color.a // top left
    };

    std::array<unsigned int, 6> indices{
        0, 1, 3,
        1, 2, 3
    };

    staplegl::vertex_buffer vbo{vertices, staplegl::driver_draw_hint::STATIC_DRAW};
    staplegl::index_buffer ebo{indices};
    staplegl::vertex_buffer_layout const layout{
        {staplegl::shader_data_type::u_type::vec3, "aPos"},
        {staplegl::shader_data_type::u_type::vec2, "aUv"},
        {staplegl::shader_data_type::u_type::vec4, "aColor"}
    };
    vbo.set_layout(layout);

    chunkVao->add_vertex_buffer(std::move(vbo));
    chunkVao->set_index_buffer(std::move(ebo));

    return {chunkShader, chunkVao, {}, MeshPrimitive::Triangles, static_cast<int>(indices.size())};
}

Mesh Plot::createTexturedChunkMesh(const RasterChunk &chunk, const std::shared_ptr<staplegl::texture_2d> &texture) const
{
    auto textureVao = std::make_shared<staplegl::vertex_array>();
    std::array vertices{
        static_cast<float>(chunk.xRange.upper), static_cast<float>(chunk.yRange.upper), 0.0f, 1.0f, 1.0f, // top right
        static_cast<float>(chunk.xRange.upper), static_cast<float>(chunk.yRange.lower), 0.0f, 1.0f, 0.0f, // bottom right
        static_cast<float>(chunk.xRange.lower), static_cast<float>(chunk.yRange.lower), 0.0f, 0.0f, 0.0f, // bottom left
        static_cast<float>(chunk.xRange.lower), static_cast<float>(chunk.yRange.upper), 0.0f, 0.0f, 1.0f // top left
    };

    std::array<unsigned int, 6> indices{
        0, 1, 3,
        1, 2, 3
    };

    staplegl::vertex_buffer vbo{vertices, staplegl::driver_draw_hint::STATIC_DRAW};
    staplegl::index_buffer ebo{indices};
    staplegl::vertex_buffer_layout const layout{
        {staplegl::shader_data_type::u_type::vec3, "aPos"},
        {staplegl::shader_data_type::u_type::vec2, "aUv"}
    };
    vbo.set_layout(layout);

    textureVao->add_vertex_buffer(std::move(vbo));
    textureVao->set_index_buffer(std::move(ebo));

    return {plotShader, textureVao, std::vector{texture}, MeshPrimitive::Triangles, static_cast<int>(indices.size())};
}

Mesh Plot::createContourMesh(const std::vector<RasterContourSegment> &segments) const
{
    constexpr auto contourLineWidth = 2.5f;
    auto contourVao = std::make_shared<staplegl::vertex_array>();
    if (segments.empty())
    {
        return {contourShader, contourVao, {}, MeshPrimitive::Lines, 0, contourLineWidth, false, 0};
    }

    std::vector<float> vertices;
    vertices.reserve(segments.size() * 6);
    for (const auto &segment : segments)
    {
        if (!std::isfinite(segment.x0) || !std::isfinite(segment.y0)
            || !std::isfinite(segment.x1) || !std::isfinite(segment.y1))
        {
            continue;
        }

        const auto dx = segment.x1 - segment.x0;
        const auto dy = segment.y1 - segment.y0;
        const auto length = std::hypot(dx, dy);
        if (length <= 1e-12)
        {
            continue;
        }

        vertices.push_back(static_cast<float>(segment.x0));
        vertices.push_back(static_cast<float>(segment.y0));
        vertices.push_back(0.0f);
        vertices.push_back(static_cast<float>(segment.x1));
        vertices.push_back(static_cast<float>(segment.y1));
        vertices.push_back(0.0f);
    }

    if (vertices.empty())
    {
        return {contourShader, contourVao, {}, MeshPrimitive::Lines, 0, contourLineWidth, false, 0};
    }

    staplegl::vertex_buffer vbo{std::span<const float>{vertices}, staplegl::driver_draw_hint::STATIC_DRAW};
    staplegl::vertex_buffer_layout const layout{
        {staplegl::shader_data_type::u_type::vec3, "aPos"}
    };
    vbo.set_layout(layout);

    contourVao->add_vertex_buffer(std::move(vbo));

    return {
        contourShader,
        contourVao,
        {},
        MeshPrimitive::Lines,
        0,
        contourLineWidth,
        false,
        static_cast<int>(vertices.size() / 3u)
    };
}

glm::vec4 Plot::getDebugChunkColor(const RasterChunk &chunk) const
{
    if (chunk.state < 0)
    {
        // Alpha > 2.5 marks "mixed" debug overlays in chunk.frag.
        return {1.0f, 0.72f, 0.24f, 3.0f};
    }

    // Alpha in (1.0, 2.5] marks "uniform" debug overlays in chunk.frag.
    return {0.58f, 0.62f, 0.72f, 2.0f};
}

std::optional<glm::vec4> Plot::getNormalSolidColor(const RasterChunk &chunk) const
{
    if (chunk.state >= 0)
    {
        const auto alpha = chunk.state > 0 ? 1.0f : 0.0f;
        return glm::vec4{0.0f, 0.47f, 0.95f, alpha};
    }

    return std::nullopt;
}

std::unique_ptr<Plot::ChunkRenderItem> Plot::buildChunkRenderItem(const PlotChunkKey &key)
{
    const auto chunkIt = sampledChunkCache.find(key);
    if (chunkIt == sampledChunkCache.end())
    {
        return nullptr;
    }

    const auto &chunk = chunkIt->second;
    auto debugMesh = createColoredChunkMesh(chunk, getDebugChunkColor(chunk));
    std::optional<Mesh> contourMesh;
    if (const auto contourIt = chunkContourCache.find(key); contourIt != chunkContourCache.end()
        && !contourIt->second.empty())
    {
        contourMesh.emplace(createContourMesh(contourIt->second));
    }

    if (chunk.state < 0)
    {
        const auto textureIt = chunkRegionTextureCache.find(key);
        if (textureIt != chunkRegionTextureCache.end())
        {
            auto texturedMesh = createTexturedChunkMesh(chunk, textureIt->second);
            return std::make_unique<TexturedChunkRenderItem>(
                std::move(texturedMesh), std::move(contourMesh), std::move(debugMesh));
        }

        return std::make_unique<SolidChunkRenderItem>(std::nullopt, std::move(contourMesh), std::move(debugMesh));
    }

    if (const auto color = getNormalSolidColor(chunk))
    {
        std::optional<Mesh> normalMesh{createColoredChunkMesh(chunk, *color)};
        return std::make_unique<SolidChunkRenderItem>(
            std::move(normalMesh), std::move(contourMesh), std::move(debugMesh));
    }

    return std::make_unique<SolidChunkRenderItem>(std::nullopt, std::move(contourMesh), std::move(debugMesh));
}

bool Plot::isChunkRenderable(const PlotChunkKey &key) const
{
    if (!shouldRenderRegionForFormula)
    {
        if (const auto contourIt = chunkContourCache.find(key); contourIt != chunkContourCache.end()
            && !contourIt->second.empty())
        {
            return true;
        }
    }

    const auto renderItemIt = chunkRenderItems.find(key);
    if (renderItemIt == chunkRenderItems.end())
    {
        return false;
    }

    return renderItemIt->second && renderItemIt->second->hasNormalMesh();
}

std::optional<PlotChunkKey> Plot::findBestChunkForTarget(const int64_t chunkX, const int64_t chunkY,
                                                          const int targetLevel) const
{
    const PlotChunkKey exactKey{chunkX, chunkY, targetLevel};
    if (isChunkRenderable(exactKey))
    {
        return exactKey;
    }

    const auto chunkXRange = chunkIndexToRange(chunkX, targetLevel);
    const auto chunkYRange = chunkIndexToRange(chunkY, targetLevel);
    const auto centerX = (chunkXRange.lower + chunkXRange.upper) * 0.5;
    const auto centerY = (chunkYRange.lower + chunkYRange.upper) * 0.5;

    for (auto it = sampledChunkLevels.lower_bound(targetLevel); it != sampledChunkLevels.end(); ++it)
    {
        const auto level = it->first;
        if (level == targetLevel)
        {
            continue;
        }

        const PlotChunkKey parentKey{
            worldToChunkIndex(centerX, level),
            worldToChunkIndex(centerY, level),
            level
        };
        if (isChunkRenderable(parentKey))
        {
            return parentKey;
        }
    }

    return std::nullopt;
}

std::vector<PlotChunkKey> Plot::findCompleteRenderableChildrenForTarget(const int64_t chunkX, const int64_t chunkY,
                                                                         const int targetLevel) const
{
    const auto targetXRange = chunkIndexToRange(chunkX, targetLevel);
    const auto targetYRange = chunkIndexToRange(chunkY, targetLevel);

    const auto begin = sampledChunkLevels.lower_bound(targetLevel);
    for (auto it = std::make_reverse_iterator(begin); it != sampledChunkLevels.rend(); ++it)
    {
        const auto level = it->first;

        auto [childMinX, childMaxX] = getChunkIndexBounds(targetXRange, level);
        auto [childMinY, childMaxY] = getChunkIndexBounds(targetYRange, level);

        std::vector<PlotChunkKey> candidateKeys;
        candidateKeys.reserve(
            static_cast<size_t>(childMaxX - childMinX + 1) * static_cast<size_t>(childMaxY - childMinY + 1));

        auto isComplete = true;
        for (auto childY = childMinY; childY <= childMaxY && isComplete; ++childY)
        {
            for (auto childX = childMinX; childX <= childMaxX; ++childX)
            {
                const PlotChunkKey childKey{childX, childY, level};
                if (!isChunkRenderable(childKey))
                {
                    isComplete = false;
                    break;
                }

                candidateKeys.push_back(childKey);
            }
        }

        if (isComplete && !candidateKeys.empty())
        {
            return candidateKeys;
        }
    }

    return {};
}

void Plot::applyChunkRenderData(const ChunkRenderData &chunkRenderData)
{
    GRAPHX_PROFILE_SCOPE("plot.applyChunkRenderData");

    const auto addLevelRef = [this](const int level)
    {
        sampledChunkLevels[level] += 1;
    };

    const auto removeLevelRef = [this](const int level)
    {
        const auto levelCountIt = sampledChunkLevels.find(level);
        if (levelCountIt == sampledChunkLevels.end())
        {
            return;
        }

        if (levelCountIt->second <= 1)
        {
            sampledChunkLevels.erase(level);
            return;
        }

        levelCountIt->second -= 1;
    };

    const auto floorDivByPow2 = [](const int64_t value, const int shift) -> int64_t
    {
        if (shift <= 0)
        {
            return value;
        }

        if (shift >= 62)
        {
            return value >= 0 ? 0 : -1;
        }

        const auto divisor = int64_t{1} << shift;
        if (value >= 0)
        {
            return value / divisor;
        }

        return -(((-value) + divisor - 1) / divisor);
    };

    const auto parentCoversChild = [floorDivByPow2](const PlotChunkKey &parent, const PlotChunkKey &child) -> bool
    {
        if (parent.level <= child.level)
        {
            return false;
        }

        const auto levelDelta = parent.level - child.level;
        const auto projectedX = floorDivByPow2(child.x, levelDelta);
        const auto projectedY = floorDivByPow2(child.y, levelDelta);
        return projectedX == parent.x && projectedY == parent.y;
    };

    const auto eraseCachedChunk = [this, &removeLevelRef](const PlotChunkKey &key)
    {
        if (sampledChunkCache.erase(key) == 0)
        {
            return;
        }

        chunkRenderItems.erase(key);
        chunkRegionTextureCache.erase(key);
        chunkContourCache.erase(key);
        removeLevelRef(key.level);
    };

    const auto &chunk = chunkRenderData.chunk;
    const auto key = toChunkKey(chunk);
    const auto isUniformChunk = chunk.state >= 0;

    if (isUniformChunk)
    {
        auto coveredByUniformParent = false;
        for (auto levelIt = sampledChunkLevels.upper_bound(chunk.level); levelIt != sampledChunkLevels.end(); ++levelIt)
        {
            const auto parentLevel = levelIt->first;
            const PlotChunkKey parentKey{
                floorDivByPow2(key.x, parentLevel - key.level),
                floorDivByPow2(key.y, parentLevel - key.level),
                parentLevel
            };

            const auto parentIt = sampledChunkCache.find(parentKey);
            if (parentIt == sampledChunkCache.end())
            {
                continue;
            }

            if (parentIt->second.state >= 0 && parentIt->second.state == chunk.state)
            {
                coveredByUniformParent = true;
                break;
            }
        }

        if (coveredByUniformParent)
        {
            return;
        }
    }

    const auto existingIt = sampledChunkCache.find(key);
    if (existingIt == sampledChunkCache.end())
    {
        sampledChunkCache.emplace(key, chunk);
        addLevelRef(chunk.level);
    }
    else
    {
        existingIt->second = chunk;
    }

    chunkRenderItems.erase(key);

    if (isUniformChunk)
    {
        chunkRegionTextureCache.erase(key);
        chunkContourCache.erase(key);

        std::vector<PlotChunkKey> removableChildren;
        removableChildren.reserve(16);
        for (const auto &[childKey, childChunk] : sampledChunkCache)
        {
            if (childKey.level >= key.level)
            {
                continue;
            }

            if (!parentCoversChild(key, childKey))
            {
                continue;
            }

            if (childChunk.state < 0 || childChunk.state != chunk.state)
            {
                continue;
            }

            removableChildren.push_back(childKey);
        }

        for (const auto &childKey : removableChildren)
        {
            eraseCachedChunk(childKey);
        }
    }
    else
    {
        if (chunkRenderData.region.has_value() && chunkRenderData.region->width > 0 && chunkRenderData.region->height > 0)
        {
            std::vector<unsigned char> textureData;
            textureData.reserve(chunkRenderData.region->pixels.size());
            for (const auto value : chunkRenderData.region->pixels)
            {
                textureData.push_back(value > 0 ? static_cast<unsigned char>(255) : static_cast<unsigned char>(0));
            }

            const auto resolution = staplegl::resolution{chunkRenderData.region->width, chunkRenderData.region->height};
            constexpr auto textureColor = staplegl::texture_color{GL_R8, GL_RED, GL_UNSIGNED_BYTE};
            constexpr auto textureFilter = staplegl::texture_filter{GL_NEAREST, GL_NEAREST};

            auto texture = std::make_shared<staplegl::texture_2d>(
                std::span<const unsigned char>{textureData}, resolution, textureColor, textureFilter);
            chunkRegionTextureCache.insert_or_assign(key, texture);
        }
        else
        {
            chunkRegionTextureCache.erase(key);
        }

        if (chunkRenderData.contour.has_value() && !chunkRenderData.contour->segments.empty())
        {
            chunkContourCache.insert_or_assign(key, chunkRenderData.contour->segments);
        }
        else
        {
            chunkContourCache.erase(key);
        }
    }

    if (auto renderItem = buildChunkRenderItem(key))
    {
        chunkRenderItems.insert_or_assign(key, std::move(renderItem));
    }
    else
    {
        chunkRenderItems.erase(key);
    }
}

void Plot::applyChunkRenderDataBatch(const std::vector<ChunkRenderData> &chunkRenderDataBatch)
{
    GRAPHX_PROFILE_SCOPE("plot.applyChunkRenderDataBatch");
    for (const auto &chunkRenderData : chunkRenderDataBatch)
    {
        applyChunkRenderData(chunkRenderData);
    }
}

std::vector<PlotChunkKey> Plot::selectVisibleChunkKeysAtLevel(const int targetLevel) const
{
    GRAPHX_PROFILE_SCOPE("plot.selectVisibleChunkKeysAtLevel");
    if (sampledChunkCache.empty())
    {
        return {};
    }

    const auto [minChunkX, maxChunkX] = getChunkIndexBounds(viewXRange, targetLevel);
    const auto [minChunkY, maxChunkY] = getChunkIndexBounds(viewYRange, targetLevel);

    struct TargetCellKey
    {
        int64_t x;
        int64_t y;

        bool operator==(const TargetCellKey &other) const
        {
            return x == other.x && y == other.y;
        }
    };

    struct TargetCellKeyHash
    {
        size_t operator()(const TargetCellKey &key) const
        {
            const auto h1 = std::hash<int64_t>{}(key.x);
            const auto h2 = std::hash<int64_t>{}(key.y);
            size_t seed = h1;
            seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct TargetCellRect
    {
        int64_t minX;
        int64_t maxX;
        int64_t minY;
        int64_t maxY;
    };

    const auto getTargetRectForKey = [this, targetLevel, minChunkX, maxChunkX, minChunkY, maxChunkY](
    const PlotChunkKey &key) -> std::optional<TargetCellRect>
    {
        const auto chunkIt = sampledChunkCache.find(key);
        if (chunkIt == sampledChunkCache.end())
        {
            return std::nullopt;
        }

        const auto &chunk = chunkIt->second;
        auto [coverMinX, coverMaxX] = getChunkIndexBounds(chunk.xRange, targetLevel);
        auto [coverMinY, coverMaxY] = getChunkIndexBounds(chunk.yRange, targetLevel);

        coverMinX = std::max(coverMinX, minChunkX);
        coverMaxX = std::min(coverMaxX, maxChunkX);
        coverMinY = std::max(coverMinY, minChunkY);
        coverMaxY = std::min(coverMaxY, maxChunkY);

        if (coverMinX > coverMaxX || coverMinY > coverMaxY)
        {
            return std::nullopt;
        }

        return TargetCellRect{coverMinX, coverMaxX, coverMinY, coverMaxY};
    };

    std::unordered_set<TargetCellKey, TargetCellKeyHash> coveredCells;
    coveredCells.reserve(
        static_cast<size_t>(maxChunkX - minChunkX + 1) * static_cast<size_t>(maxChunkY - minChunkY + 1));

    std::unordered_set<PlotChunkKey, PlotChunkKeyHash> chosenKeys;
    chosenKeys.reserve(
        static_cast<size_t>(maxChunkX - minChunkX + 1) * static_cast<size_t>(maxChunkY - minChunkY + 1));

    const auto markRectCovered = [&coveredCells](const TargetCellRect &rect)
    {
        for (auto y = rect.minY; y <= rect.maxY; ++y)
        {
            for (auto x = rect.minX; x <= rect.maxX; ++x)
            {
                coveredCells.insert({x, y});
            }
        }
    };

    const auto floorDivByPow2 = [](const int64_t value, const int shift) -> int64_t
    {
        if (shift <= 0)
        {
            return value;
        }

        if (shift >= 62)
        {
            return value >= 0 ? 0 : -1;
        }

        const auto divisor = int64_t{1} << shift;
        if (value >= 0)
        {
            return value / divisor;
        }

        return -(((-value) + divisor - 1) / divisor);
    };

    const auto parentCoversChild = [floorDivByPow2](const PlotChunkKey &parent, const PlotChunkKey &child) -> bool
    {
        if (parent.level <= child.level)
        {
            return false;
        }

        const auto levelDelta = parent.level - child.level;
        const auto projectedX = floorDivByPow2(child.x, levelDelta);
        const auto projectedY = floorDivByPow2(child.y, levelDelta);
        return projectedX == parent.x && projectedY == parent.y;
    };

    const auto evictCoveredFiner = [&chosenKeys, parentCoversChild](const PlotChunkKey &coarseKey)
    {
        for (auto it = chosenKeys.begin(); it != chosenKeys.end();)
        {
            if (!parentCoversChild(coarseKey, *it))
            {
                ++it;
                continue;
            }

            it = chosenKeys.erase(it);
        }
    };

    for (auto chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY)
    {
        for (auto chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX)
        {
            const TargetCellKey cell{chunkX, chunkY};
            if (coveredCells.contains(cell))
            {
                continue;
            }

            if (const auto chosen = findBestChunkForTarget(chunkX, chunkY, targetLevel))
            {
                if (chosen->level > targetLevel)
                {
                    evictCoveredFiner(*chosen);
                }

                chosenKeys.insert(*chosen);

                if (const auto chosenRect = getTargetRectForKey(*chosen))
                {
                    markRectCovered(*chosenRect);
                }
                else
                {
                    coveredCells.insert(cell);
                }

                continue;
            }

            const auto childKeys = findCompleteRenderableChildrenForTarget(chunkX, chunkY, targetLevel);
            if (!childKeys.empty())
            {
                for (const auto &childKey : childKeys)
                {
                    chosenKeys.insert(childKey);
                }
                coveredCells.insert(cell);
            }
        }
    }

    std::vector<PlotChunkKey> keys;
    keys.reserve(chosenKeys.size());
    for (const auto &key : chosenKeys)
    {
        keys.push_back(key);
    }

    std::ranges::sort(keys, [](const PlotChunkKey &lhs, const PlotChunkKey &rhs)
    {
        if (lhs.level != rhs.level)
        {
            // Draw coarse chunks first, then refined chunks on top.
            return lhs.level > rhs.level;
        }

        return std::tie(lhs.y, lhs.x) < std::tie(rhs.y, rhs.x);
    });

    // Final guard: once a parent chunk is selected, drop any covered finer chunks.
    std::vector<PlotChunkKey> prunedKeys;
    prunedKeys.reserve(keys.size());
    for (const auto &candidate : keys)
    {
        auto coveredByParent = false;
        for (const auto &selected : prunedKeys)
        {
            if (parentCoversChild(selected, candidate))
            {
                coveredByParent = true;
                break;
            }
        }

        if (!coveredByParent)
        {
            prunedKeys.push_back(candidate);
        }
    }

    return prunedKeys;
}

std::vector<PlotChunkKey> Plot::selectVisibleChunkKeys() const
{
    GRAPHX_PROFILE_SCOPE("plot.selectVisibleChunkKeys");
    if (sampledChunkCache.empty())
    {
        return {};
    }

    const auto desiredTargetLevel = getTargetLevel(viewXRange, viewYRange, window->getWidth(), window->getHeight());

    std::vector<int> candidateLevels;
    candidateLevels.reserve(sampledChunkLevels.size() + 1);
    candidateLevels.push_back(desiredTargetLevel);

    const auto finerBegin = sampledChunkLevels.lower_bound(desiredTargetLevel);
    for (auto it = std::make_reverse_iterator(finerBegin); it != sampledChunkLevels.rend(); ++it)
    {
        const auto level = it->first;
        if (level == desiredTargetLevel)
        {
            continue;
        }

        candidateLevels.push_back(level);
    }

    for (const auto candidateLevel : candidateLevels)
    {
        auto keys = selectVisibleChunkKeysAtLevel(candidateLevel);
        if (!keys.empty())
        {
            return keys;
        }
    }

    return {};
}

void Plot::rebuildVisibleChunkMeshes()
{
    GRAPHX_PROFILE_SCOPE("plot.rebuildVisibleChunkMeshes");
    visibleChunkMeshes.clear();
    constexpr auto contourOnlyMode = false;

    if (sampledChunkCache.empty())
    {
        return;
    }

    const auto keys = selectVisibleChunkKeys();
    std::vector<Mesh> deferredContourMeshes;
    deferredContourMeshes.reserve(keys.size());

    visibleChunkMeshes.reserve(debug ? keys.size() * 3 : keys.size() * 2);

    for (const auto &key : keys)
    {
        const auto renderIt = chunkRenderItems.find(key);
        if (renderIt == chunkRenderItems.end())
        {
            continue;
        }

        if (!contourOnlyMode)
        {
            if (const auto *mesh = renderIt->second->meshForMode(false))
            {
                visibleChunkMeshes.push_back(*mesh);
            }
        }

        if (const auto *contourMesh = renderIt->second->contourMesh())
        {
            deferredContourMeshes.push_back(*contourMesh);
        }
    }

    if (debug)
    {
        // Debug mode overlays chunk-selection visualization on top of normal meshes.
        for (const auto &key : keys)
        {
            const auto renderIt = chunkRenderItems.find(key);
            if (renderIt == chunkRenderItems.end())
            {
                continue;
            }

            if (const auto *debugMesh = renderIt->second->meshForMode(true))
            {
                visibleChunkMeshes.push_back(*debugMesh);
            }
        }
    }

    // Keep contour overlays on top of all chunk/debug meshes.
    visibleChunkMeshes.insert(
        visibleChunkMeshes.end(),
        deferredContourMeshes.begin(),
        deferredContourMeshes.end());
}

void Plot::rebuildAndPublishMeshes()
{
    GRAPHX_PROFILE_SCOPE("plot.rebuildAndPublishMeshes");
    rebuildVisibleChunkMeshes();

    meshes.clear();
    meshes.reserve(visibleChunkMeshes.size());
    meshes.insert(meshes.end(), visibleChunkMeshes.begin(), visibleChunkMeshes.end());

    uploadShaderUniforms();

    if (plotCompleteCallback)
    {
        plotCompleteCallback(meshes);
    }
}

void Plot::uploadShaderUniforms()
{
    chunkShader->bind();
    chunkShader->upload_uniform1i("debugMode", debug ? 1 : 0);
    chunkShader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(chunkModel), 16});

    plotShader->bind();
    plotShader->upload_uniform1i("texture1", 0);
    plotShader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(chunkModel), 16});

    contourShader->bind();
    contourShader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(chunkModel), 16});
    contourShader->upload_uniform4f("contourColor", 1.0f, 0.72f, 0.24f, 1.0f);
}

void Plot::updateModelMat()
{
    const auto worldToViewTranslate = glm::translate(
        glm::mat4(1.0f), glm::vec3{-viewXRange.mid(), -viewYRange.mid(), 0.0f});
    const auto viewToNDCMat = glm::scale(
        glm::mat4(1.0f), glm::vec3{2.0f / static_cast<float>(viewXRange.size()),
                                   2.0f / static_cast<float>(viewYRange.size()), 1.0f});
    const auto worldToNDC = viewToNDCMat * worldToViewTranslate;

    chunkModel = worldToNDC;

    uploadShaderUniforms();
}

void Plot::onCursorDrag(const double x, const double y)
{
    GRAPHX_PROFILE_SCOPE("plot.onCursorDrag");
    const auto windowWidth{window->getWidth()};
    const auto windowHeight{window->getHeight()};
    if (!hasValidViewportSize(windowWidth, windowHeight))
    {
        return;
    }

    const auto deltaX{viewXRange.size() / windowWidth};
    const auto deltaY{viewYRange.size() / windowHeight};

    viewXRange = viewXRange + Interval{x * -deltaX};
    viewYRange = viewYRange + Interval{y * deltaY};

    if (plotRangeChangedCallback)
    {
        GRAPHX_PROFILE_SCOPE("plot.onCursorDrag.rangeChangedCallback");
        plotRangeChangedCallback(viewXRange, viewYRange);
    }

    {
        GRAPHX_PROFILE_SCOPE("plot.onCursorDrag.updateModelMat");
        updateModelMat();
    }

    if (formula)
    {
        GRAPHX_PROFILE_SCOPE("plot.onCursorDrag.addTask");
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, windowWidth, windowHeight});
    }
}

void Plot::onWindowSizeChanged(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("plot.onWindowSizeChanged");
    if (!hasValidViewportSize(width, height))
    {
        return;
    }

    auto ratio = width / static_cast<double>(height);

    double xRangeSize = viewYRange.size() * ratio;

    double xRangeMid = (viewXRange.lower + viewXRange.upper) / 2.0;

    viewXRange = {xRangeMid - xRangeSize / 2.0, xRangeMid + xRangeSize / 2.0};

    if (plotRangeChangedCallback)
    {
        plotRangeChangedCallback(viewXRange, viewYRange);
    }

    updateModelMat();

    if (formula)
    {
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, width, height});
    }
}

Interval Plot::getXRanges() const
{
    return viewXRange;
}

Interval Plot::getYRanges() const
{
    return viewYRange;
}

void Plot::onMouseScrolled(const double offset)
{
    GRAPHX_PROFILE_SCOPE("plot.onMouseScrolled");
    const auto windowWidth{window->getWidth()};
    const auto windowHeight{window->getHeight()};
    if (!hasValidViewportSize(windowWidth, windowHeight))
    {
        return;
    }

    const auto zoomFactor = std::clamp(1.0 - offset * 0.1, 0.1, 10.0);
    const auto nextXRangeSize = viewXRange.size() * zoomFactor;
    const auto nextYRangeSize = viewYRange.size() * zoomFactor;

    const auto glfwWindow = window->getGlfwWindow();
    const auto [windowCoordWidth, windowCoordHeight] = glfwWindow->getSize();
    const auto [cursorX, cursorY] = glfwWindow->getCursorPos();

    auto xPercent = 0.5;
    auto yPercentFromTop = 0.5;
    if (windowCoordWidth > 0 && windowCoordHeight > 0)
    {
        xPercent = std::clamp(cursorX / static_cast<double>(windowCoordWidth), 0.0, 1.0);
        yPercentFromTop = std::clamp(cursorY / static_cast<double>(windowCoordHeight), 0.0, 1.0);
    }

    const auto anchorX = viewXRange.lower + xPercent * viewXRange.size();
    const auto anchorY = viewYRange.upper - yPercentFromTop * viewYRange.size();

    const auto nextXLower = anchorX - xPercent * nextXRangeSize;
    const auto nextYLower = anchorY - (1.0 - yPercentFromTop) * nextYRangeSize;

    viewXRange = {nextXLower, nextXLower + nextXRangeSize};
    viewYRange = {nextYLower, nextYLower + nextYRangeSize};

    if (plotRangeChangedCallback)
    {
        plotRangeChangedCallback(viewXRange, viewYRange);
    }

    updateModelMat();

    if (formula)
    {
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, windowWidth, windowHeight});
    }
}

void Plot::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    (void)scancode;
    (void)mods;
    if (action != glfw::KeyState::Press)
    {
        return;
    }

    if (key == glfw::KeyCode::D)
    {
        debug = !debug;
        updateModelMat();
        rebuildAndPublishMeshes();
    }
    else if (key == glfw::KeyCode::H)
    {
        const auto windowWidth{window->getWidth()};
        const auto windowHeight{window->getHeight()};
        if (!hasValidViewportSize(windowWidth, windowHeight))
        {
            return;
        }

        viewYRange = {-20.0, 20.0};
        const auto ratio = windowWidth / static_cast<double>(windowHeight);
        const auto xRangeSize = viewYRange.size() * ratio;
        viewXRange = {-xRangeSize / 2.0, xRangeSize / 2.0};

        if (plotRangeChangedCallback)
        {
            plotRangeChangedCallback(viewXRange, viewYRange);
        }

        updateModelMat();

        if (formula)
        {
            computeEngine->addTask({graph, formula, viewXRange, viewYRange, windowWidth, windowHeight});
        }
    }
}
