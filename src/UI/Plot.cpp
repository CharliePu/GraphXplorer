//
// Created by charl on 6/2/2024.
//

#include "Plot.h"

#include <algorithm>
#include <array>
#include <chrono>
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
#include "../Util/PipelineLog.h"

namespace
{
bool hasValidViewportSize(const int width, const int height)
{
    return width > 0 && height > 0;
}

bool hasFinitePositiveRange(const Interval &range)
{
    return std::isfinite(range.lower)
        && std::isfinite(range.upper)
        && range.upper > range.lower
        && std::isfinite(range.size());
}

Interval clampViewportRange(const Interval &range, const Interval &fallback)
{
    auto size = range.size();
    if (!std::isfinite(size) || size <= 0.0)
    {
        size = fallback.size();
    }

    if (!std::isfinite(size) || size <= 0.0)
    {
        size = 40.0;
    }

    size = std::clamp(size, MIN_VIEWPORT_WORLD_SPAN, MAX_VIEWPORT_WORLD_SPAN);

    auto mid = range.mid();
    if (!std::isfinite(mid))
    {
        mid = fallback.mid();
    }
    if (!std::isfinite(mid))
    {
        mid = 0.0;
    }

    const auto halfSize = size * 0.5;
    const auto centerLimit = MAX_VIEWPORT_WORLD_SPAN - halfSize;
    if (centerLimit > 0.0)
    {
        mid = std::clamp(mid, -centerLimit, centerLimit);
    }
    else
    {
        mid = 0.0;
    }

    return {mid - halfSize, mid + halfSize};
}

bool normalizeViewport(Interval &xRange, Interval &yRange,
                       const Interval &fallbackXRange, const Interval &fallbackYRange)
{
    xRange = clampViewportRange(xRange, fallbackXRange);
    yRange = clampViewportRange(yRange, fallbackYRange);
    return hasFinitePositiveRange(xRange) && hasFinitePositiveRange(yRange);
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

ChunkKey Plot::toChunkKey(const RasterChunk &chunk)
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
                                                   chunkRegionTextureCache{},
                                                   chunkContourCache{},
                                                   chunkRenderItems{},
                                                   chunkModel{1.0f},
                                                   shouldRenderRegionForFormula{true},
                                                   debug{false},
                                                   pendingMeshesDirty{false}
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

            PipelineLog::log("plot.callback: reqId=%llu chunks=%zu treeSize=%zu",
                requestId, chunkRenderDataBatch.size(), chunkTree.size());
            applyChunkRenderDataBatch(chunkRenderDataBatch);
            pendingMeshesDirty = true;
            PipelineLog::log("plot.callback: DONE treeSize=%zu",
                chunkTree.size());
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

void Plot::flushPendingMeshes()
{
    if (!pendingMeshesDirty)
    {
        return;
    }

    pendingMeshesDirty = false;
    PipelineLog::log("plot.flush: treeSize=%zu",
        chunkTree.size());
    updateModelMat();
    rebuildAndPublishMeshes();
    PipelineLog::log("plot.flush: DONE meshes=%zu", meshes.size());
}

void Plot::requestNewPlot(const std::string &input)
{
    formula = std::make_shared<Formula>(input);
    shouldRenderRegionForFormula = !(formula && formula->isTopLevelOperator("="));
    graph = std::make_shared<Graph>();

    chunkTree.clear();
    chunkRegionTextureCache.clear();
    chunkContourCache.clear();
    chunkRenderItems.clear();
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

Mesh Plot::createMissingCellMesh(const TargetCell &cell) const
{
    const RasterChunk placeholder{
        cell.x,
        cell.y,
        cell.level,
        chunkIndexToRange(cell.x, cell.level),
        chunkIndexToRange(cell.y, cell.level),
        -1,
        RasterChunkSource::Exact
    };

    return createColoredChunkMesh(placeholder, glm::vec4{0.20f, 0.20f, 0.24f, 0.35f});
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
    if (chunk.state > 0)
    {
        return glm::vec4{0.0f, 0.47f, 0.95f, 1.0f};
    }

    return std::nullopt;
}

std::unique_ptr<Plot::ChunkRenderItem> Plot::buildChunkRenderItem(const ChunkKey &key)
{
    const auto *chunk = chunkTree.findChunk(key);
    if (!chunk)
    {
        return nullptr;
    }

    auto debugMesh = createColoredChunkMesh(*chunk, getDebugChunkColor(*chunk));
    std::optional<Mesh> contourMesh;
    if (const auto contourIt = chunkContourCache.find(key); contourIt != chunkContourCache.end()
        && !contourIt->second.empty())
    {
        contourMesh.emplace(createContourMesh(contourIt->second));
    }

    if (chunk->state < 0)
    {
        const auto textureIt = chunkRegionTextureCache.find(key);
        if (textureIt != chunkRegionTextureCache.end())
        {
            auto texturedMesh = createTexturedChunkMesh(*chunk, textureIt->second);
            return std::make_unique<TexturedChunkRenderItem>(
                std::move(texturedMesh), std::move(contourMesh), std::move(debugMesh));
        }

        return std::make_unique<SolidChunkRenderItem>(std::nullopt, std::move(contourMesh), std::move(debugMesh));
    }

    if (const auto color = getNormalSolidColor(*chunk))
    {
        std::optional<Mesh> normalMesh{createColoredChunkMesh(*chunk, *color)};
        return std::make_unique<SolidChunkRenderItem>(
            std::move(normalMesh), std::move(contourMesh), std::move(debugMesh));
    }

    return std::make_unique<SolidChunkRenderItem>(std::nullopt, std::move(contourMesh), std::move(debugMesh));
}

bool Plot::isChunkRenderable(const ChunkKey &key) const
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

void Plot::applyChunkRenderData(const ChunkRenderData &chunkRenderData)
{
    GRAPHX_PROFILE_SCOPE("plot.applyChunkRenderData");

    const auto &chunk = chunkRenderData.chunk;
    const auto key = toChunkKey(chunk);

    PipelineLog::log("  applyChunk: (%lld,%lld,%d) state=%d treeBefore=%zu",
        key.x, key.y, key.level, chunk.state, chunkTree.size());

    chunkTree.insert(chunk);

    PipelineLog::log("  applyChunk: (%lld,%lld,%d) treeAfter=%zu",
        key.x, key.y, key.level, chunkTree.size());

    chunkRenderItems.erase(key);

    if (chunk.state >= 0)
    {
        chunkRegionTextureCache.erase(key);
        chunkContourCache.erase(key);
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

VisibleCover Plot::resolveVisibleCover() const
{
    GRAPHX_PROFILE_SCOPE("plot.resolveVisibleCover");
    VisibleCover cover;
    if (!formula && chunkTree.size() == 0)
    {
        return cover;
    }

    const auto windowWidth = window->getWidth();
    const auto windowHeight = window->getHeight();
    if (!hasValidViewportSize(windowWidth, windowHeight))
    {
        cover.bounded = false;
        return cover;
    }

    const auto desiredLevel = targetLevel(viewXRange, viewYRange, windowWidth, windowHeight);
    const auto t0 = std::chrono::steady_clock::now();
    cover = chunkTree.selectVisibleCover(viewXRange, viewYRange, desiredLevel);
    const auto t1 = std::chrono::steady_clock::now();
    const auto resolveMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    PipelineLog::log("plot.cover: level=%d keys=%zu missing=%zu cells=%zu bounded=%d ms=%lld treeSize=%zu",
        desiredLevel,
        cover.keys.size(),
        cover.missingCells.size(),
        cover.targetCellCount,
        cover.bounded ? 1 : 0,
        resolveMs,
        chunkTree.size());

    return cover;
}

void Plot::rebuildVisibleChunkMeshes()
{
    GRAPHX_PROFILE_SCOPE("plot.rebuildVisibleChunkMeshes");
    constexpr auto contourOnlyMode = false;

    if (!formula && chunkTree.size() == 0)
    {
        visibleChunkMeshes.clear();
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto cover = resolveVisibleCover();
    const auto t1 = std::chrono::steady_clock::now();
    const auto selectMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    PipelineLog::log("plot.rebuildMeshes: treeSize=%zu selectedKeys=%zu missing=%zu selectMs=%lld",
        chunkTree.size(), cover.keys.size(), cover.missingCells.size(), selectMs);

    if (!cover.bounded)
    {
        PipelineLog::log("plot.rebuildMeshes: keep previous meshes because cover is unbounded");
        return;
    }

    visibleChunkMeshes.clear();
    std::vector<Mesh> deferredContourMeshes;
    deferredContourMeshes.reserve(cover.keys.size());

    const auto baseMeshCount = cover.keys.size() + cover.missingCells.size();
    visibleChunkMeshes.reserve(debug ? baseMeshCount * 3 : baseMeshCount * 2);

    for (const auto &missingCell : cover.missingCells)
    {
        visibleChunkMeshes.push_back(createMissingCellMesh(missingCell));
    }

    for (const auto &key : cover.keys)
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
        for (const auto &key : cover.keys)
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

    auto nextXRange = viewXRange + Interval{x * -deltaX};
    auto nextYRange = viewYRange + Interval{y * deltaY};
    if (!normalizeViewport(nextXRange, nextYRange, viewXRange, viewYRange))
    {
        return;
    }

    viewXRange = nextXRange;
    viewYRange = nextYRange;

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
        {
            GRAPHX_PROFILE_SCOPE("plot.onCursorDrag.rebuildCachedMeshes");
            rebuildAndPublishMeshes();
        }
        GRAPHX_PROFILE_SCOPE("plot.onCursorDrag.addTask");
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, windowWidth, windowHeight});
    }
}

void Plot::onWindowSizeChanged(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("plot.onWindowSizeChanged");
    onFramebufferResized(width, height);

    if (plotRangeChangedCallback)
    {
        plotRangeChangedCallback(viewXRange, viewYRange);
    }

    onResizeSettled(width, height);
}

void Plot::onFramebufferResized(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("plot.onFramebufferResized");
    if (!hasValidViewportSize(width, height))
    {
        return;
    }

    auto ratio = width / static_cast<double>(height);

    double xRangeSize = viewYRange.size() * ratio;

    double xRangeMid = (viewXRange.lower + viewXRange.upper) / 2.0;

    auto nextXRange = Interval{xRangeMid - xRangeSize / 2.0, xRangeMid + xRangeSize / 2.0};
    auto nextYRange = viewYRange;
    if (!normalizeViewport(nextXRange, nextYRange, viewXRange, viewYRange))
    {
        return;
    }

    viewXRange = nextXRange;
    viewYRange = nextYRange;

    updateModelMat();
}

void Plot::onResizeSettled(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("plot.onResizeSettled");
    if (!hasValidViewportSize(width, height) || !formula)
    {
        return;
    }

    rebuildAndPublishMeshes();
    computeEngine->addTask({graph, formula, viewXRange, viewYRange, width, height});
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
    const auto nextXRangeSize = std::clamp(
        viewXRange.size() * zoomFactor, MIN_VIEWPORT_WORLD_SPAN, MAX_VIEWPORT_WORLD_SPAN);
    const auto nextYRangeSize = std::clamp(
        viewYRange.size() * zoomFactor, MIN_VIEWPORT_WORLD_SPAN, MAX_VIEWPORT_WORLD_SPAN);

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

    auto nextXRange = Interval{nextXLower, nextXLower + nextXRangeSize};
    auto nextYRange = Interval{nextYLower, nextYLower + nextYRangeSize};
    if (!normalizeViewport(nextXRange, nextYRange, viewXRange, viewYRange))
    {
        return;
    }

    viewXRange = nextXRange;
    viewYRange = nextYRange;

    if (plotRangeChangedCallback)
    {
        plotRangeChangedCallback(viewXRange, viewYRange);
    }

    updateModelMat();

    if (formula)
    {
        {
            GRAPHX_PROFILE_SCOPE("plot.onMouseScrolled.rebuildCachedMeshes");
            rebuildAndPublishMeshes();
        }
        PipelineLog::log("plot.scroll: newRange=[%.1f,%.1f]x[%.1f,%.1f]",
            viewXRange.lower, viewXRange.upper, viewYRange.lower, viewYRange.upper);
        GRAPHX_PROFILE_SCOPE("plot.onMouseScrolled.addTask");
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
        if (!normalizeViewport(viewXRange, viewYRange, Interval{-20.0, 20.0}, Interval{-20.0, 20.0}))
        {
            return;
        }

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
