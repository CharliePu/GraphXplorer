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
#include "../Graph/ChunkTree.h"
#include "../Graph/Graph.h"
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

class Plot: public UIComponent {

public:
    Plot(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Window> &window);

    void setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    void setPlotRangeChangedCallback(const std::function<void(const Interval &, const Interval &)> &callback);

    int getDepth() const override;

    void requestNewPlot(const std::string & input);

    void updateModelMat();

    void flushPendingMeshes();

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

    void onFramebufferResized(int width, int height);

    void onResizeSettled(int width, int height);

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
        [[nodiscard]] virtual const Mesh *contourMesh() const = 0;
        [[nodiscard]] virtual bool hasNormalMesh() const = 0;
    };

    class SolidChunkRenderItem final : public ChunkRenderItem
    {
    public:
        SolidChunkRenderItem(std::optional<Mesh> normalMesh, std::optional<Mesh> contourMesh, Mesh debugMesh);

        [[nodiscard]] const Mesh *meshForMode(bool debugMode) const override;
        [[nodiscard]] const Mesh *contourMesh() const override;
        [[nodiscard]] bool hasNormalMesh() const override;

    private:
        std::optional<Mesh> normalMesh;
        std::optional<Mesh> contour;
        Mesh debugMesh;
    };

    class TexturedChunkRenderItem final : public ChunkRenderItem
    {
    public:
        TexturedChunkRenderItem(Mesh normalMesh, std::optional<Mesh> contourMesh, Mesh debugMesh);

        [[nodiscard]] const Mesh *meshForMode(bool debugMode) const override;
        [[nodiscard]] const Mesh *contourMesh() const override;
        [[nodiscard]] bool hasNormalMesh() const override;

    private:
        Mesh normalMesh;
        std::optional<Mesh> contour;
        Mesh debugMesh;
    };

    static ChunkKey toChunkKey(const RasterChunk &chunk);

    Mesh createColoredChunkMesh(const RasterChunk &chunk, const glm::vec4 &color) const;
    Mesh createTexturedChunkMesh(const RasterChunk &chunk, const std::shared_ptr<staplegl::texture_2d> &texture) const;
    Mesh createContourMesh(const std::vector<RasterContourSegment> &segments) const;
    Mesh createMissingCellMesh(const TargetCell &cell) const;
    glm::vec4 getDebugChunkColor(const RasterChunk &chunk) const;
    std::optional<glm::vec4> getNormalSolidColor(const RasterChunk &chunk) const;
    std::unique_ptr<ChunkRenderItem> buildChunkRenderItem(const ChunkKey &key);

    [[nodiscard]] bool isChunkRenderable(const ChunkKey &key) const;
    [[nodiscard]] VisibleCover resolveVisibleCover() const;
    void applyChunkRenderData(const ChunkRenderData &chunkRenderData);
    void applyChunkRenderDataBatch(const std::vector<ChunkRenderData> &chunkRenderDataBatch);
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
    std::shared_ptr<staplegl::shader_program> contourShader;

    std::vector<Mesh> visibleChunkMeshes;
    std::vector<Mesh> meshes;

    ChunkTree chunkTree;
    std::unordered_map<ChunkKey, std::shared_ptr<staplegl::texture_2d>, ChunkKeyHash> chunkRegionTextureCache;
    std::unordered_map<ChunkKey, std::vector<RasterContourSegment>, ChunkKeyHash> chunkContourCache;
    std::unordered_map<ChunkKey, std::unique_ptr<ChunkRenderItem>, ChunkKeyHash> chunkRenderItems;

    glm::mat4 chunkModel;

    bool shouldRenderRegionForFormula;
    bool debug;
    bool pendingMeshesDirty;
};

#endif //PLOT_H
