#ifndef RENDERRESOURCEMANAGER_H
#define RENDERRESOURCEMANAGER_H

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

struct OverlayTextRun
{
    std::string text{};
    float x{0.0f};
    float y{0.0f};
    float pixelHeight{14.0f};
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
    [[nodiscard]] PipelineHandle textPipeline() const;
    [[nodiscard]] MaterialHandle textMaterial() const;

    void setPlotViewport(const Interval &xRange, const Interval &yRange);
    void setPlotInstances(std::vector<RenderTileInstance> instances);
    void setDebugPlotInstances(std::vector<RenderTileInstance> instances);
    void beginRegionFrame(std::span<const RegionImageRef> visibleRefs);
    [[nodiscard]] TextureSlice findRegionImage(const RegionImageRef &ref) const;
    [[nodiscard]] TextureSlice registerRegionImage(const RegionImageRef &ref, std::span<const uint8_t> pixels);
    void setGridState(const Interval &xRange, const Interval &yRange, int framebufferWidth, int framebufferHeight);
    void setOverlayRects(std::vector<OverlayRect> rects);
    void setOverlayTextRuns(std::vector<OverlayTextRun> runs);

    [[nodiscard]] RenderProgress draw(const DrawCommand &command, const UploadBudget &uploadBudget = UploadBudget{});
    [[nodiscard]] size_t plotInstanceCount() const;
    [[nodiscard]] size_t debugPlotInstanceCount() const;
    [[nodiscard]] size_t overlayRectCount() const;
    [[nodiscard]] size_t overlayTextRunCount() const;
    [[nodiscard]] std::span<const OverlayRect> overlayRectData() const;
    [[nodiscard]] std::span<const OverlayTextRun> overlayTextRunData() const;

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

    struct TextGpuResources
    {
        uint32_t program{0};
        uint32_t vao{0};
        uint32_t vbo{0};
        uint32_t texture{0};
        size_t vertexCapacity{0};
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
        uint32_t desiredCapacity{0};
        uint32_t nextSlice{0};
        uint32_t maxArrayLayers{0};
        bool initialized{false};
        bool maxArrayLayersKnown{false};
        std::unordered_map<uint64_t, uint32_t> slices;
        std::unordered_map<uint32_t, uint64_t> refsBySlice;
        std::unordered_map<uint64_t, RegionImageRef> refs;
        std::unordered_map<uint64_t, std::vector<uint8_t>> pixels;
        std::vector<uint32_t> freeSlices;
        std::unordered_set<uint64_t> visibleRefs;
        std::unordered_set<uint64_t> uploadedRefs;
    };

    struct PendingRegionUpload
    {
        RegionImageRef ref{};
        TextureSlice slice{};
    };

    struct TextGlyph
    {
        float uMin{0.0f};
        float vMin{0.0f};
        float uMax{0.0f};
        float vMax{0.0f};
        int width{0};
        int height{0};
        int bearingX{0};
        int bearingY{0};
        int advance{0};
        bool loaded{false};
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
    static constexpr PipelineHandle TextPipeline{10};
    static constexpr MaterialHandle TextMaterial{11};

    void ensurePlotResources();
    void ensureGridResources();
    void ensureOverlayResources();
    void ensureTextResources();
    void bindPlotInstanceAttributes(uint32_t firstInstance);
    void bindOverlayInstanceAttributes(uint32_t firstInstance);
    void ensureRegionTextureArray(int width, int height);
    void queueRegionUpload(const RegionImageRef &ref, TextureSlice slice);
    void queueResidentRegionUploads();
    [[nodiscard]] uint32_t requiredRegionTextureCapacity() const;
    [[nodiscard]] uint32_t maxRegionArrayLayers();
    void uploadPlotInstancesIfDirty();
    void uploadPlotInstanceFloats(std::span<const float> floats);
    void uploadOverlayRectsIfDirty();
    void rebuildTextVerticesIfDirty();
    void uploadTextVerticesIfDirty();
    [[nodiscard]] RenderProgress uploadPendingRegionImages(const UploadBudget &budget);
    void destroyPlotResources();
    void destroyGridResources();
    void destroyOverlayResources();
    void destroyTextResources();
    void destroyRegionTextures();
    [[nodiscard]] bool loadTextAtlas();
    [[nodiscard]] static uint32_t compileShader(uint32_t type, const char *source);
    [[nodiscard]] static uint32_t linkProgram(uint32_t vertexShader, uint32_t fragmentShader);
    [[nodiscard]] static std::array<float, 16> viewportTransform(const Interval &xRange, const Interval &yRange);

    PlotGpuResources plotGpu{};
    GridGpuResources gridGpu{};
    OverlayGpuResources overlayGpu{};
    TextGpuResources textGpu{};
    RegionTextureArray regionTextures{};
    std::vector<PendingRegionUpload> pendingRegionUploads;
    std::vector<RenderTileInstance> plotInstances;
    std::vector<float> plotInstanceFloats;
    std::vector<RenderTileInstance> debugPlotInstances;
    std::vector<float> debugPlotInstanceFloats;
    GridState gridState{};
    std::vector<OverlayRect> overlayRects;
    std::vector<float> overlayRectFloats;
    std::vector<OverlayTextRun> overlayTextRuns;
    std::vector<float> textVertexFloats;
    std::array<TextGlyph, 128> textGlyphs{};
    std::array<float, 16> plotTransform{};
    bool plotInstancesDirty{true};
    bool overlayRectsDirty{true};
    bool textVerticesDirty{true};
    int textAtlasWidth{0};
    int textAtlasHeight{0};
    int textAtlasFontPixelSize{32};
    int textAscender{0};
};
}

#endif // RENDERRESOURCEMANAGER_H
