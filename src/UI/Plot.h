//
// Created by charl on 6/2/2024.
//

#ifndef PLOT_H
#define PLOT_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "UIComponent.h"
#include "../Core/Input.h"
#include "../Math/Interval.h"
#include "../Math/RasterizedPlot.h"
#include "../Render/Mesh.h"

class ComputeEngine;

namespace staplegl
{
    class shader_program;
    class texture_2d;
    class vertex_array;
}

class Formula;
struct Graph;

struct PlotChunkKey
{
    int64_t x;
    int64_t y;
    int level;

    bool operator==(const PlotChunkKey &other) const
    {
        return x == other.x && y == other.y && level == other.level;
    }
};

struct PlotChunkKeyHash
{
    size_t operator()(const PlotChunkKey &key) const
    {
        const auto h1 = std::hash<int64_t>{}(key.x);
        const auto h2 = std::hash<int64_t>{}(key.y);
        const auto h3 = std::hash<int>{}(key.level);

        size_t seed = h1;
        seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

class Plot: public UIComponent {

public:
    Plot(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Window> &window);

    void setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    void setPlotRangeChangedCallback(const std::function<void(const Interval &, const Interval &)> &callback);

    int getDepth() const override;

    void requestNewPlot(const std::string & input);

    void updateModelMat();

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

    Interval getXRanges() const;

    Interval getYRanges() const;

    void onMouseScrolled(double offset) override;

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

private:
    class ChunkRenderItem
    {
    public:
        virtual ~ChunkRenderItem() = default;
        [[nodiscard]] virtual const Mesh *meshForMode(bool debugMode) const = 0;
        [[nodiscard]] virtual bool hasNormalMesh() const = 0;
    };

    class SolidChunkRenderItem final : public ChunkRenderItem
    {
    public:
        SolidChunkRenderItem(std::optional<Mesh> normalMesh, Mesh debugMesh);

        [[nodiscard]] const Mesh *meshForMode(bool debugMode) const override;
        [[nodiscard]] bool hasNormalMesh() const override;

    private:
        std::optional<Mesh> normalMesh;
        Mesh debugMesh;
    };

    class TexturedChunkRenderItem final : public ChunkRenderItem
    {
    public:
        TexturedChunkRenderItem(Mesh normalMesh, Mesh debugMesh);

        [[nodiscard]] const Mesh *meshForMode(bool debugMode) const override;
        [[nodiscard]] bool hasNormalMesh() const override;

    private:
        Mesh normalMesh;
        Mesh debugMesh;
    };

    static int getTargetLevel(const Interval &xRange, const Interval &yRange, int windowWidth, int windowHeight);
    static std::pair<int64_t, int64_t> getChunkIndexBounds(const Interval &range, int level);

    static PlotChunkKey toChunkKey(const RasterChunk &chunk);

    Mesh createColoredChunkMesh(const RasterChunk &chunk, const glm::vec4 &color) const;
    Mesh createTexturedChunkMesh(const RasterChunk &chunk, const std::shared_ptr<staplegl::texture_2d> &texture) const;
    glm::vec4 getDebugChunkColor(const RasterChunk &chunk) const;
    std::optional<glm::vec4> getNormalSolidColor(const RasterChunk &chunk) const;
    std::unique_ptr<ChunkRenderItem> buildChunkRenderItem(const PlotChunkKey &key);

    [[nodiscard]] bool isChunkRenderable(const PlotChunkKey &key) const;
    [[nodiscard]] std::optional<PlotChunkKey> findBestChunkForTarget(int64_t chunkX, int64_t chunkY,
                                                                      int targetLevel) const;
    [[nodiscard]] std::vector<PlotChunkKey> findCompleteRenderableChildrenForTarget(int64_t chunkX, int64_t chunkY,
                                                                                     int targetLevel) const;
    [[nodiscard]] std::vector<PlotChunkKey> selectVisibleChunkKeysAtLevel(int targetLevel) const;
    std::vector<PlotChunkKey> selectVisibleChunkKeys() const;
    void mergeSampledChunks(const std::vector<RasterChunk> &chunks);
    void mergeChunkTextures(const std::vector<RasterChunkTexture> &chunkTextures);
    void rebuildChunkRenderItems();
    void rebuildVisibleChunkMeshes();
    void rebuildAndPublishMeshes();
    void uploadShaderUniforms();

    std::shared_ptr<Graph> graph;
    std::shared_ptr<Formula> formula;
    std::shared_ptr<ComputeEngine> computeEngine;
    std::shared_ptr<Window> window;

    std::function<void(const std::vector<Mesh> &)> plotCompleteCallback;
    std::function<void(const Interval &, const Interval &)> plotRangeChangedCallback;

    Interval viewXRange, viewYRange;

    std::shared_ptr<staplegl::shader_program> chunkShader;
    std::shared_ptr<staplegl::shader_program> plotShader;

    std::vector<Mesh> visibleChunkMeshes;
    std::vector<Mesh> meshes;

    std::unordered_map<PlotChunkKey, RasterChunk, PlotChunkKeyHash> sampledChunkCache;
    std::unordered_map<PlotChunkKey, std::shared_ptr<staplegl::texture_2d>, PlotChunkKeyHash> chunkTextureCache;
    std::unordered_map<PlotChunkKey, std::unique_ptr<ChunkRenderItem>, PlotChunkKeyHash> chunkRenderItems;
    std::map<int, size_t> sampledChunkLevels;

    glm::mat4 chunkModel;

    bool debug;
};

#endif //PLOT_H
