#ifndef RENDERRESOURCEMANAGER_H
#define RENDERRESOURCEMANAGER_H

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "../Math/Interval.h"
#include "../Util/Contracts.h"

namespace gx
{
struct OverlayRect
{
    float xMin{0.0f};
    float xMax{0.0f};
    float yMin{0.0f};
    float yMax{0.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

class RenderResourceManager
{
public:
    RenderResourceManager();
    ~RenderResourceManager();

    RenderResourceManager(const RenderResourceManager &) = delete;
    RenderResourceManager &operator=(const RenderResourceManager &) = delete;
    RenderResourceManager(RenderResourceManager &&) = delete;
    RenderResourceManager &operator=(RenderResourceManager &&) = delete;

    [[nodiscard]] PipelineHandle plotPipeline() const;
    [[nodiscard]] GeometryHandle staticQuadGeometry() const;
    [[nodiscard]] MaterialHandle plotMaterial() const;
    [[nodiscard]] TextureSetHandle regionTextureSet() const;
    [[nodiscard]] PipelineHandle debugPlotPipeline() const;
    [[nodiscard]] PipelineHandle gridPipeline() const;
    [[nodiscard]] MaterialHandle gridMaterial() const;
    [[nodiscard]] PipelineHandle overlayPipeline() const;
    [[nodiscard]] MaterialHandle overlayMaterial() const;

    void setPlotViewport(const Interval &xRange, const Interval &yRange);
    void setPlotInstances(std::vector<RenderTileInstance> instances);
    void setDebugPlotInstances(std::vector<RenderTileInstance> instances);
    [[nodiscard]] TextureSlice registerRegionImage(const RegionImageRef &ref, std::span<const uint8_t> pixels);
    void setGridState(const Interval &xRange, const Interval &yRange, int framebufferWidth, int framebufferHeight);
    void setOverlayRects(std::vector<OverlayRect> rects);

    void draw(const DrawCommand &command);
    [[nodiscard]] size_t plotInstanceCount() const;
    [[nodiscard]] size_t debugPlotInstanceCount() const;
    [[nodiscard]] size_t overlayRectCount() const;

private:
    struct PlotGpuResources
    {
        uint32_t program{0};
        uint32_t vao{0};
        uint32_t quadVbo{0};
        uint32_t indexBuffer{0};
        uint32_t instanceVbo{0};
        size_t instanceCapacity{0};
        bool initialized{false};
    };

    struct GridGpuResources
    {
        uint32_t program{0};
        uint32_t vao{0};
        uint32_t quadVbo{0};
        uint32_t indexBuffer{0};
        bool initialized{false};
    };

    struct OverlayGpuResources
    {
        uint32_t program{0};
        uint32_t vao{0};
        uint32_t quadVbo{0};
        uint32_t indexBuffer{0};
        uint32_t instanceVbo{0};
        size_t instanceCapacity{0};
        bool initialized{false};
    };

    struct GridState
    {
        Interval xRange{-20.0, 20.0};
        Interval yRange{-20.0, 20.0};
        int framebufferWidth{800};
        int framebufferHeight{800};
    };

    struct RegionTextureArray
    {
        uint32_t texture{0};
        int width{0};
        int height{0};
        uint32_t capacity{0};
        uint32_t nextSlice{0};
        bool initialized{false};
        std::unordered_map<uint64_t, uint32_t> slices;
    };

    struct PendingRegionUpload
    {
        RegionImageRef ref{};
        TextureSlice slice{};
        std::vector<uint8_t> pixels;
    };

    static constexpr PipelineHandle PlotPipeline{1};
    static constexpr GeometryHandle StaticQuadGeometry{2};
    static constexpr MaterialHandle PlotMaterial{3};
    static constexpr TextureSetHandle RegionTextureSet{4};
    static constexpr PipelineHandle GridPipeline{5};
    static constexpr MaterialHandle GridMaterial{6};
    static constexpr PipelineHandle OverlayPipeline{7};
    static constexpr MaterialHandle OverlayMaterial{8};
    static constexpr PipelineHandle DebugPlotPipeline{9};

    void ensurePlotResources();
    void ensureGridResources();
    void ensureOverlayResources();
    void ensureRegionTextureArray(int width, int height);
    void uploadPlotInstancesIfDirty();
    void uploadPlotInstanceFloats(std::span<const float> floats);
    void uploadOverlayRectsIfDirty();
    void uploadPendingRegionImages();
    void destroyPlotResources();
    void destroyGridResources();
    void destroyOverlayResources();
    void destroyRegionTextures();
    [[nodiscard]] static uint32_t compileShader(uint32_t type, const char *source);
    [[nodiscard]] static uint32_t linkProgram(uint32_t vertexShader, uint32_t fragmentShader);
    [[nodiscard]] static std::array<float, 16> viewportTransform(const Interval &xRange, const Interval &yRange);

    PlotGpuResources plotGpu{};
    GridGpuResources gridGpu{};
    OverlayGpuResources overlayGpu{};
    RegionTextureArray regionTextures{};
    std::vector<PendingRegionUpload> pendingRegionUploads;
    std::vector<RenderTileInstance> plotInstances;
    std::vector<float> plotInstanceFloats;
    std::vector<RenderTileInstance> debugPlotInstances;
    std::vector<float> debugPlotInstanceFloats;
    GridState gridState{};
    std::vector<OverlayRect> overlayRects;
    std::vector<float> overlayRectFloats;
    std::array<float, 16> plotTransform{};
    bool plotInstancesDirty{true};
    bool overlayRectsDirty{true};
};
}

#endif // RENDERRESOURCEMANAGER_H
