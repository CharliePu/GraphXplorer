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

    void setPlotViewport(const Interval &xRange, const Interval &yRange);
    void setPlotInstances(std::vector<RenderTileInstance> instances);
    [[nodiscard]] TextureSlice registerRegionImage(const RegionImageRef &ref, std::span<const uint8_t> pixels);

    void draw(const DrawCommand &command);
    [[nodiscard]] size_t plotInstanceCount() const;

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

    void ensurePlotResources();
    void ensureRegionTextureArray(int width, int height);
    void uploadPlotInstancesIfDirty();
    void uploadPendingRegionImages();
    void destroyPlotResources();
    void destroyRegionTextures();
    [[nodiscard]] static uint32_t compileShader(uint32_t type, const char *source);
    [[nodiscard]] static uint32_t linkProgram(uint32_t vertexShader, uint32_t fragmentShader);
    [[nodiscard]] static std::array<float, 16> viewportTransform(const Interval &xRange, const Interval &yRange);

    PlotGpuResources plotGpu{};
    RegionTextureArray regionTextures{};
    std::vector<PendingRegionUpload> pendingRegionUploads;
    std::vector<RenderTileInstance> plotInstances;
    std::vector<float> plotInstanceFloats;
    std::array<float, 16> plotTransform{};
    bool plotInstancesDirty{true};
};
}

#endif // RENDERRESOURCEMANAGER_H
