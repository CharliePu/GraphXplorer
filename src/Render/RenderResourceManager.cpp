#include "RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <glad/glad.h>
#include <limits>
#include <string>
#include <utility>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "../Util/PipelineLog.h"

namespace gx
{
namespace
{
constexpr auto PlotInstanceFloatCount = 16u;
constexpr auto OverlayInstanceFloatCount = 8u;

uint32_t nextPowerOfTwoAtLeast(const uint32_t value)
{
    if (value <= 1)
    {
        return 1;
    }

    auto result = uint32_t{1};
    while (result < value && result <= (std::numeric_limits<uint32_t>::max() / 2u))
    {
        result *= 2u;
    }

    return result < value ? value : result;
}

constexpr const char *PlotVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 iBounds;
layout(location = 2) in vec4 iColor;
layout(location = 3) in vec4 iMeta;
layout(location = 4) in vec4 iUvRect;

uniform mat4 uTransform;

out vec4 vColor;
out vec2 vUv;
flat out int vMode;
flat out int vSlice;

void main()
{
    float x = mix(iBounds.x, iBounds.y, aCorner.x);
    float y = mix(iBounds.z, iBounds.w, aCorner.y);
    gl_Position = uTransform * vec4(x, y, 0.0, 1.0);
    vColor = iColor;
    vUv = mix(iUvRect.xy, iUvRect.zw, aCorner);
    vMode = int(iMeta.x + 0.5);
    vSlice = int(iMeta.y + 0.5);
}
)GLSL";

constexpr const char *PlotFragmentShader = R"GLSL(
#version 330 core
in vec4 vColor;
in vec2 vUv;
flat in int vMode;
flat in int vSlice;
out vec4 FragColor;

uniform sampler2DArray uRegionTexture;
uniform bool uHasRegionTexture;

void main()
{
    if (vMode == 5 || vMode == 6 || vMode == 7)
    {
        float borderWidth = (vMode == 7) ? 0.018 : 0.010;
        float edgeDistance = min(min(vUv.x, vUv.y), min(1.0 - vUv.x, 1.0 - vUv.y));
        if (edgeDistance >= borderWidth)
        {
            discard;
        }
        FragColor = vec4(vColor.rgb, 1.0);
        return;
    }

    if (vMode == 3)
    {
        if (!uHasRegionTexture || vSlice < 0)
        {
            discard;
        }
        float sampleValue = texture(uRegionTexture, vec3(vUv.x, vUv.y, float(vSlice))).r;
        vec4 falseColor = vec4(0.12, 0.12, 0.14, 1.0);
        vec4 trueColor = vec4(0.0, 0.47, 0.95, 1.0);
        FragColor = mix(falseColor, trueColor, sampleValue);
        return;
    }

    if (vColor.a <= 0.0)
    {
        discard;
    }
    FragColor = vColor;
}
)GLSL";

constexpr const char *GridVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aCorner;

out vec2 vUv;

void main()
{
    vUv = aCorner;
    gl_Position = vec4(aCorner * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

constexpr const char *GridFragmentShader = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;

uniform vec2 uXRange;
uniform vec2 uYRange;
uniform vec2 uMajorGrid;
uniform vec2 uMinorGrid;

float gridLine(float value, float step, float scale)
{
    if (step <= 0.0)
    {
        return 0.0;
    }
    float coord = value / step;
    float dist = abs(fract(coord + 0.5) - 0.5);
    float width = max(fwidth(coord) * scale, 0.0008);
    return 1.0 - smoothstep(0.0, width, dist);
}

float zeroLine(float value, float span)
{
    float width = max(fwidth(value) * 1.8, span * 0.0004);
    return 1.0 - smoothstep(0.0, width, abs(value));
}

void main()
{
    vec2 world = vec2(
        mix(uXRange.x, uXRange.y, vUv.x),
        mix(uYRange.x, uYRange.y, vUv.y)
    );
    float minor = max(gridLine(world.x, uMinorGrid.x, 1.0), gridLine(world.y, uMinorGrid.y, 1.0));
    float major = max(gridLine(world.x, uMajorGrid.x, 1.35), gridLine(world.y, uMajorGrid.y, 1.35));
    float axis = max(zeroLine(world.x, uXRange.y - uXRange.x), zeroLine(world.y, uYRange.y - uYRange.x));
    vec4 minorColor = vec4(0.88, 0.92, 0.98, 0.09);
    vec4 majorColor = vec4(0.92, 0.96, 1.0, 0.21);
    vec4 axisColor = vec4(1.0, 1.0, 1.0, 0.56);
    FragColor = minorColor * minor;
    FragColor = mix(FragColor, majorColor, major);
    FragColor = mix(FragColor, axisColor, axis);
    if (FragColor.a <= 0.01)
    {
        discard;
    }
}
)GLSL";

constexpr const char *OverlayVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 iBounds;
layout(location = 2) in vec4 iColor;

out vec4 vColor;

void main()
{
    float x = mix(iBounds.x, iBounds.y, aCorner.x);
    float y = mix(iBounds.z, iBounds.w, aCorner.y);
    gl_Position = vec4(x, y, 0.0, 1.0);
    vColor = iColor;
}
)GLSL";

constexpr const char *OverlayFragmentShader = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main()
{
    if (vColor.a <= 0.0)
    {
        discard;
    }
    FragColor = vColor;
}
)GLSL";

constexpr const char *TextVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;

out vec2 vUv;
out vec4 vColor;

void main()
{
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
}
)GLSL";

constexpr const char *TextFragmentShader = R"GLSL(
#version 330 core
in vec2 vUv;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2D uTextAtlas;

void main()
{
    float alpha = texture(uTextAtlas, vUv).r;
    if (alpha <= 0.01)
    {
        discard;
    }
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";

std::array<float, 4> colorForVisualState(const TileVisualState visualState)
{
    switch (visualState)
    {
    case TileVisualState::UniformTrue:
        return {0.0f, 0.47f, 0.95f, 1.0f};
    case TileVisualState::UniformFalse:
        return {0.0f, 0.0f, 0.0f, 0.0f};
    case TileVisualState::MixedRegion:
        return {0.0f, 0.0f, 0.0f, 0.0f};
    case TileVisualState::ContourOnly:
        return {1.0f, 0.72f, 0.24f, 1.0f};
    case TileVisualState::DebugOverlay:
    case TileVisualState::DebugUniform:
        return {0.58f, 0.62f, 0.72f, 0.65f};
    case TileVisualState::DebugMixed:
        return {1.0f, 0.72f, 0.24f, 0.85f};
    case TileVisualState::DebugMissing:
        return {0.20f, 0.20f, 0.24f, 0.35f};
    case TileVisualState::Missing:
    default:
        return {0.20f, 0.20f, 0.24f, 0.35f};
    }
}

uint32_t rangedInstanceCount(const BufferRange &range, const size_t available)
{
    if (range.offset >= available)
    {
        return 0;
    }
    const auto remaining = available - range.offset;
    const auto requested = range.count == 0 ? remaining : std::min<size_t>(range.count, remaining);
    return static_cast<uint32_t>(std::min<size_t>(requested, std::numeric_limits<uint32_t>::max()));
}
}

RenderResourceManager::RenderResourceManager()
{
    plotTransform = viewportTransform(Interval{-20.0, 20.0}, Interval{-20.0, 20.0});
}

RenderResourceManager::~RenderResourceManager()
{
    destroyPlotResources();
    destroyGridResources();
    destroyOverlayResources();
    destroyTextResources();
    destroyRegionTextures();
}

PipelineHandle RenderResourceManager::plotPipeline() const
{
    return PlotPipeline;
}

GeometryHandle RenderResourceManager::staticQuadGeometry() const
{
    return StaticQuadGeometry;
}

MaterialHandle RenderResourceManager::plotMaterial() const
{
    return PlotMaterial;
}

TextureSetHandle RenderResourceManager::regionTextureSet() const
{
    return RegionTextureSet;
}

PipelineHandle RenderResourceManager::debugPlotPipeline() const
{
    return DebugPlotPipeline;
}

PipelineHandle RenderResourceManager::gridPipeline() const
{
    return GridPipeline;
}

MaterialHandle RenderResourceManager::gridMaterial() const
{
    return GridMaterial;
}

PipelineHandle RenderResourceManager::overlayPipeline() const
{
    return OverlayPipeline;
}

MaterialHandle RenderResourceManager::overlayMaterial() const
{
    return OverlayMaterial;
}

PipelineHandle RenderResourceManager::textPipeline() const
{
    return TextPipeline;
}

MaterialHandle RenderResourceManager::textMaterial() const
{
    return TextMaterial;
}

void RenderResourceManager::setPlotViewport(const Interval &xRange, const Interval &yRange)
{
    plotTransform = viewportTransform(xRange, yRange);
}

void RenderResourceManager::setPlotInstances(std::vector<RenderTileInstance> instances)
{
    plotInstances = std::move(instances);
    plotInstanceFloats.clear();
    plotInstanceFloats.reserve(plotInstances.size() * 16);

    for (const auto &instance : plotInstances)
    {
        const auto color = colorForVisualState(instance.visualState);
        const auto hasRegionSlice = instance.regionSlice.textureId == RegionTextureSet.id;
        plotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.xMin));
        plotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.xMax));
        plotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.yMin));
        plotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.yMax));
        plotInstanceFloats.insert(plotInstanceFloats.end(), color.begin(), color.end());
        plotInstanceFloats.push_back(static_cast<float>(static_cast<int>(instance.visualState)));
        plotInstanceFloats.push_back(hasRegionSlice
            ? static_cast<float>(instance.regionSlice.slice)
            : -1.0f);
        plotInstanceFloats.push_back(0.0f);
        plotInstanceFloats.push_back(0.0f);
        plotInstanceFloats.insert(plotInstanceFloats.end(), instance.uvRect.begin(), instance.uvRect.end());
    }

    plotInstancesDirty = true;
}

void RenderResourceManager::setDebugPlotInstances(std::vector<RenderTileInstance> instances)
{
    debugPlotInstances = std::move(instances);
    debugPlotInstanceFloats.clear();
    debugPlotInstanceFloats.reserve(debugPlotInstances.size() * 16);

    for (const auto &instance : debugPlotInstances)
    {
        const auto color = colorForVisualState(instance.visualState);
        debugPlotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.xMin));
        debugPlotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.xMax));
        debugPlotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.yMin));
        debugPlotInstanceFloats.push_back(static_cast<float>(instance.worldBounds.yMax));
        debugPlotInstanceFloats.insert(debugPlotInstanceFloats.end(), color.begin(), color.end());
        debugPlotInstanceFloats.push_back(static_cast<float>(static_cast<int>(instance.visualState)));
        debugPlotInstanceFloats.push_back(-1.0f);
        debugPlotInstanceFloats.push_back(0.0f);
        debugPlotInstanceFloats.push_back(0.0f);
        debugPlotInstanceFloats.insert(debugPlotInstanceFloats.end(), instance.uvRect.begin(), instance.uvRect.end());
    }
}

void RenderResourceManager::beginRegionFrame(const std::span<const RegionImageRef> visibleRefs)
{
    regionTextures.visibleRefs.clear();
    for (const auto &ref : visibleRefs)
    {
        if (ref.id != 0)
        {
            regionTextures.visibleRefs.insert(ref.id);
        }
    }
    regionTextures.desiredCapacity = static_cast<uint32_t>(regionTextures.visibleRefs.size());

    for (auto it = regionTextures.slices.begin(); it != regionTextures.slices.end();)
    {
        if (regionTextures.visibleRefs.contains(it->first))
        {
            ++it;
            continue;
        }

        const auto slice = it->second;
        regionTextures.refsBySlice.erase(slice);
        regionTextures.refs.erase(it->first);
        regionTextures.pixels.erase(it->first);
        regionTextures.freeSlices.push_back(slice);
        std::erase_if(pendingRegionUploads, [slice](const PendingRegionUpload &upload)
        {
            return upload.slice.slice == slice;
        });
        it = regionTextures.slices.erase(it);
    }
}

TextureSlice RenderResourceManager::findRegionImage(const RegionImageRef &ref) const
{
    if (ref.id == 0)
    {
        return {};
    }

    const auto it = regionTextures.slices.find(ref.id);
    if (it == regionTextures.slices.end())
    {
        return {};
    }

    return TextureSlice{RegionTextureSet.id, it->second};
}

TextureSlice RenderResourceManager::registerRegionImage(const RegionImageRef &ref, const std::span<const uint8_t> pixels)
{
    if (ref.id == 0 || ref.width <= 0 || ref.height <= 0 || pixels.empty())
    {
        return {};
    }

    auto it = regionTextures.slices.find(ref.id);
    if (it == regionTextures.slices.end())
    {
        uint32_t slice = 0;
        if (!regionTextures.freeSlices.empty())
        {
            slice = regionTextures.freeSlices.back();
            regionTextures.freeSlices.pop_back();
        }
        else
        {
            slice = regionTextures.nextSlice++;
            regionTextures.desiredCapacity = std::max(regionTextures.desiredCapacity, regionTextures.nextSlice);
        }

        it = regionTextures.slices.emplace(ref.id, slice).first;
        regionTextures.refsBySlice[slice] = ref.id;
    }

    regionTextures.refs[ref.id] = ref;
    regionTextures.pixels[ref.id] = std::vector<uint8_t>{pixels.begin(), pixels.end()};
    queueRegionUpload(ref, TextureSlice{RegionTextureSet.id, it->second}, pixels);

    return TextureSlice{RegionTextureSet.id, it->second};
}

void RenderResourceManager::setGridState(const Interval &xRange,
                                         const Interval &yRange,
                                         const int framebufferWidth,
                                         const int framebufferHeight)
{
    gridState = {xRange, yRange, framebufferWidth, framebufferHeight};
}

void RenderResourceManager::setOverlayRects(std::vector<OverlayRect> rects)
{
    overlayRects = std::move(rects);
    overlayRectFloats.clear();
    overlayRectFloats.reserve(overlayRects.size() * 8);
    for (const auto &rect : overlayRects)
    {
        overlayRectFloats.push_back(rect.xMin);
        overlayRectFloats.push_back(rect.xMax);
        overlayRectFloats.push_back(rect.yMin);
        overlayRectFloats.push_back(rect.yMax);
        overlayRectFloats.insert(overlayRectFloats.end(), rect.color.begin(), rect.color.end());
    }
    overlayRectsDirty = true;
}

void RenderResourceManager::setOverlayTextRuns(std::vector<OverlayTextRun> runs)
{
    overlayTextRuns = std::move(runs);
    textVerticesDirty = true;
}

void RenderResourceManager::draw(const DrawCommand &command)
{
    if (command.pipeline == GridPipeline && command.geometry == StaticQuadGeometry)
    {
        ensureGridResources();

        const auto span = [](const Interval &range, const int pixels) {
            const auto denominator = std::max(1, pixels);
            const auto rough = range.size() / static_cast<double>(denominator) * 96.0;
            const auto power = std::pow(10.0, std::floor(std::log10(std::max(rough, 1e-9))));
            const auto normalized = rough / power;
            auto nice = 10.0;
            if (normalized <= 1.0) nice = 1.0;
            else if (normalized <= 2.0) nice = 2.0;
            else if (normalized <= 5.0) nice = 5.0;
            return nice * power;
        };

        const auto xMajor = span(gridState.xRange, gridState.framebufferWidth);
        const auto yMajor = span(gridState.yRange, gridState.framebufferHeight);
        glUseProgram(gridGpu.program);
        glUniform2f(glGetUniformLocation(gridGpu.program, "uXRange"),
                    static_cast<float>(gridState.xRange.lower),
                    static_cast<float>(gridState.xRange.upper));
        glUniform2f(glGetUniformLocation(gridGpu.program, "uYRange"),
                    static_cast<float>(gridState.yRange.lower),
                    static_cast<float>(gridState.yRange.upper));
        glUniform2f(glGetUniformLocation(gridGpu.program, "uMajorGrid"),
                    static_cast<float>(xMajor),
                    static_cast<float>(yMajor));
        glUniform2f(glGetUniformLocation(gridGpu.program, "uMinorGrid"),
                    static_cast<float>(xMajor * 0.2),
                    static_cast<float>(yMajor * 0.2));
        glBindVertexArray(gridGpu.vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        return;
    }

    if (command.pipeline == OverlayPipeline && command.geometry == StaticQuadGeometry)
    {
        ensureOverlayResources();
        uploadOverlayRectsIfDirty();
        if (overlayRects.empty())
        {
            return;
        }
        const auto count = rangedInstanceCount(command.instanceRange, overlayRects.size());
        if (count == 0)
        {
            return;
        }
        glUseProgram(overlayGpu.program);
        glBindVertexArray(overlayGpu.vao);
        bindOverlayInstanceAttributes(command.instanceRange.offset);
        glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
        glBindVertexArray(0);
        return;
    }

    if (command.pipeline == TextPipeline)
    {
        ensureTextResources();
        uploadTextVerticesIfDirty();
        if (!textGpu.initialized || textGpu.texture == 0 || textVertexFloats.empty())
        {
            return;
        }

        glUseProgram(textGpu.program);
        const auto textureLocation = glGetUniformLocation(textGpu.program, "uTextAtlas");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textGpu.texture);
        glUniform1i(textureLocation, 0);
        glBindVertexArray(textGpu.vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(textVertexFloats.size() / 8));
        glBindVertexArray(0);
        return;
    }

    if (command.pipeline == DebugPlotPipeline && command.geometry == StaticQuadGeometry)
    {
        ensurePlotResources();
        if (debugPlotInstances.empty())
        {
            return;
        }
        const auto count = rangedInstanceCount(command.instanceRange, debugPlotInstances.size());
        if (count == 0)
        {
            return;
        }

        uploadPlotInstanceFloats(debugPlotInstanceFloats);
        glUseProgram(plotGpu.program);
        const auto transformLocation = glGetUniformLocation(plotGpu.program, "uTransform");
        glUniformMatrix4fv(transformLocation, 1, GL_FALSE, plotTransform.data());
        const auto textureLocation = glGetUniformLocation(plotGpu.program, "uRegionTexture");
        const auto hasRegionTextureLocation = glGetUniformLocation(plotGpu.program, "uHasRegionTexture");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glUniform1i(textureLocation, 0);
        glUniform1i(hasRegionTextureLocation, 0);
        glBindVertexArray(plotGpu.vao);
        bindPlotInstanceAttributes(command.instanceRange.offset);
        glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
        glBindVertexArray(0);
        plotInstancesDirty = true;
        return;
    }

    if (command.pipeline != PlotPipeline || command.geometry != StaticQuadGeometry)
    {
        return;
    }

    ensurePlotResources();
    uploadPlotInstancesIfDirty();
    uploadPendingRegionImages();
    if (plotInstances.empty())
    {
        return;
    }
    const auto count = rangedInstanceCount(command.instanceRange, plotInstances.size());
    if (count == 0)
    {
        return;
    }

    glUseProgram(plotGpu.program);
    const auto transformLocation = glGetUniformLocation(plotGpu.program, "uTransform");
    glUniformMatrix4fv(transformLocation, 1, GL_FALSE, plotTransform.data());
    const auto textureLocation = glGetUniformLocation(plotGpu.program, "uRegionTexture");
    const auto hasRegionTextureLocation = glGetUniformLocation(plotGpu.program, "uHasRegionTexture");
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, regionTextures.texture);
    glUniform1i(textureLocation, 0);
    glUniform1i(hasRegionTextureLocation, regionTextures.initialized ? 1 : 0);

    glBindVertexArray(plotGpu.vao);
    bindPlotInstanceAttributes(command.instanceRange.offset);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
    glBindVertexArray(0);
}

size_t RenderResourceManager::plotInstanceCount() const
{
    return plotInstances.size();
}

size_t RenderResourceManager::debugPlotInstanceCount() const
{
    return debugPlotInstances.size();
}

size_t RenderResourceManager::overlayRectCount() const
{
    return overlayRects.size();
}

size_t RenderResourceManager::overlayTextRunCount() const
{
    return overlayTextRuns.size();
}

std::span<const OverlayRect> RenderResourceManager::overlayRectData() const
{
    return overlayRects;
}

std::span<const OverlayTextRun> RenderResourceManager::overlayTextRunData() const
{
    return overlayTextRuns;
}

void RenderResourceManager::ensurePlotResources()
{
    if (plotGpu.initialized)
    {
        return;
    }

    const auto vertexShader = compileShader(GL_VERTEX_SHADER, PlotVertexShader);
    const auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, PlotFragmentShader);
    plotGpu.program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    constexpr std::array quadVertices{
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f,
        0.0f, 1.0f
    };
    constexpr std::array<unsigned int, 6> indices{0, 1, 3, 1, 2, 3};

    glGenVertexArrays(1, &plotGpu.vao);
    glBindVertexArray(plotGpu.vao);

    glGenBuffers(1, &plotGpu.quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, plotGpu.quadVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quadVertices.size() * sizeof(float)), quadVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glGenBuffers(1, &plotGpu.indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, plotGpu.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &plotGpu.instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, plotGpu.instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void *>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void *>(8 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void *>(12 * sizeof(float)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
    plotGpu.initialized = true;
    plotInstancesDirty = true;
}

void RenderResourceManager::bindPlotInstanceAttributes(const uint32_t firstInstance)
{
    const auto stride = static_cast<GLsizei>(PlotInstanceFloatCount * sizeof(float));
    const auto base = static_cast<uintptr_t>(firstInstance) * PlotInstanceFloatCount * sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, plotGpu.instanceVbo);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(base));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(base + 4u * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(base + 8u * sizeof(float)));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(base + 12u * sizeof(float)));
}

void RenderResourceManager::ensureGridResources()
{
    if (gridGpu.initialized)
    {
        return;
    }

    const auto vertexShader = compileShader(GL_VERTEX_SHADER, GridVertexShader);
    const auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, GridFragmentShader);
    gridGpu.program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    constexpr std::array quadVertices{
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f,
        0.0f, 1.0f
    };
    constexpr std::array<unsigned int, 6> indices{0, 1, 3, 1, 2, 3};

    glGenVertexArrays(1, &gridGpu.vao);
    glBindVertexArray(gridGpu.vao);

    glGenBuffers(1, &gridGpu.quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gridGpu.quadVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quadVertices.size() * sizeof(float)), quadVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glGenBuffers(1, &gridGpu.indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridGpu.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    gridGpu.initialized = true;
}

void RenderResourceManager::ensureOverlayResources()
{
    if (overlayGpu.initialized)
    {
        return;
    }

    const auto vertexShader = compileShader(GL_VERTEX_SHADER, OverlayVertexShader);
    const auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, OverlayFragmentShader);
    overlayGpu.program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    constexpr std::array quadVertices{
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f,
        0.0f, 1.0f
    };
    constexpr std::array<unsigned int, 6> indices{0, 1, 3, 1, 2, 3};

    glGenVertexArrays(1, &overlayGpu.vao);
    glBindVertexArray(overlayGpu.vao);

    glGenBuffers(1, &overlayGpu.quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, overlayGpu.quadVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quadVertices.size() * sizeof(float)), quadVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glGenBuffers(1, &overlayGpu.indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, overlayGpu.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &overlayGpu.instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, overlayGpu.instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);
    overlayGpu.initialized = true;
    overlayRectsDirty = true;
}

void RenderResourceManager::bindOverlayInstanceAttributes(const uint32_t firstInstance)
{
    const auto stride = static_cast<GLsizei>(OverlayInstanceFloatCount * sizeof(float));
    const auto base = static_cast<uintptr_t>(firstInstance) * OverlayInstanceFloatCount * sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, overlayGpu.instanceVbo);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(base));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(base + 4u * sizeof(float)));
}

void RenderResourceManager::ensureTextResources()
{
    if (textGpu.initialized)
    {
        return;
    }

    const auto vertexShader = compileShader(GL_VERTEX_SHADER, TextVertexShader);
    const auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, TextFragmentShader);
    textGpu.program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glGenVertexArrays(1, &textGpu.vao);
    glBindVertexArray(textGpu.vao);

    glGenBuffers(1, &textGpu.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, textGpu.vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(4 * sizeof(float)));

    glBindVertexArray(0);

    if (!loadTextAtlas())
    {
        destroyTextResources();
        return;
    }

    textGpu.initialized = true;
    textVerticesDirty = true;
}

uint32_t RenderResourceManager::maxRegionArrayLayers()
{
    if (!regionTextures.maxArrayLayersKnown)
    {
        GLint maxLayers = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        regionTextures.maxArrayLayers = maxLayers > 0 ? static_cast<uint32_t>(maxLayers) : 1u;
        regionTextures.maxArrayLayersKnown = true;
        PipelineLog::log("render.regionTexture maxArrayLayers=%u", regionTextures.maxArrayLayers);
    }

    return regionTextures.maxArrayLayers;
}

void RenderResourceManager::queueRegionUpload(const RegionImageRef &ref,
                                              const TextureSlice slice,
                                              const std::span<const uint8_t> pixels)
{
    if (ref.id == 0 || slice.textureId == 0 || pixels.empty())
    {
        return;
    }

    std::erase_if(pendingRegionUploads, [&ref](const PendingRegionUpload &upload)
    {
        return upload.ref.id == ref.id;
    });
    pendingRegionUploads.push_back({
        ref,
        slice,
        std::vector<uint8_t>{pixels.begin(), pixels.end()}
    });
}

void RenderResourceManager::queueResidentRegionUploads()
{
    for (const auto &[id, slice] : regionTextures.slices)
    {
        if (slice >= regionTextures.capacity)
        {
            continue;
        }
        const auto refIt = regionTextures.refs.find(id);
        const auto pixelsIt = regionTextures.pixels.find(id);
        if (refIt == regionTextures.refs.end() || pixelsIt == regionTextures.pixels.end())
        {
            continue;
        }
        queueRegionUpload(refIt->second, TextureSlice{RegionTextureSet.id, slice}, pixelsIt->second);
    }
}

void RenderResourceManager::ensureRegionTextureArray(const int width, const int height)
{
    const auto requiredCapacity = std::max({1u, regionTextures.desiredCapacity, regionTextures.nextSlice});
    const auto maxLayers = maxRegionArrayLayers();
    const auto targetCapacity = std::min(nextPowerOfTwoAtLeast(requiredCapacity), maxLayers);
    if (targetCapacity < requiredCapacity)
    {
        PipelineLog::log(
            "render.regionTexture capacity limited required=%u allocated=%u maxArrayLayers=%u",
            requiredCapacity,
            targetCapacity,
            maxLayers);
    }

    if (regionTextures.initialized
        && regionTextures.width == width
        && regionTextures.height == height
        && regionTextures.capacity >= requiredCapacity)
    {
        return;
    }

    if (regionTextures.initialized)
    {
        glDeleteTextures(1, &regionTextures.texture);
        regionTextures.texture = 0;
        regionTextures.initialized = false;
    }

    regionTextures.width = width;
    regionTextures.height = height;
    regionTextures.capacity = targetCapacity;

    glGenTextures(1, &regionTextures.texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, regionTextures.texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_R8,
        width,
        height,
        static_cast<GLsizei>(regionTextures.capacity),
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        nullptr);
    regionTextures.initialized = true;
    queueResidentRegionUploads();
    PipelineLog::log(
        "render.regionTexture allocated width=%d height=%d capacity=%u required=%u visible=%zu resident=%zu",
        width,
        height,
        regionTextures.capacity,
        requiredCapacity,
        regionTextures.visibleRefs.size(),
        regionTextures.slices.size());
}

void RenderResourceManager::uploadPlotInstancesIfDirty()
{
    if (!plotInstancesDirty)
    {
        return;
    }

    uploadPlotInstanceFloats(plotInstanceFloats);
    plotInstancesDirty = false;
}

void RenderResourceManager::uploadPlotInstanceFloats(const std::span<const float> floats)
{
    glBindBuffer(GL_ARRAY_BUFFER, plotGpu.instanceVbo);
    const auto requiredBytes = floats.size() * sizeof(float);
    if (requiredBytes > plotGpu.instanceCapacity)
    {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(requiredBytes), floats.data(), GL_DYNAMIC_DRAW);
        plotGpu.instanceCapacity = requiredBytes;
    }
    else if (requiredBytes > 0)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(requiredBytes), floats.data());
    }
}

void RenderResourceManager::uploadOverlayRectsIfDirty()
{
    if (!overlayRectsDirty)
    {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, overlayGpu.instanceVbo);
    const auto requiredBytes = overlayRectFloats.size() * sizeof(float);
    if (requiredBytes > overlayGpu.instanceCapacity)
    {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(requiredBytes), overlayRectFloats.data(), GL_DYNAMIC_DRAW);
        overlayGpu.instanceCapacity = requiredBytes;
    }
    else if (requiredBytes > 0)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(requiredBytes), overlayRectFloats.data());
    }
    overlayRectsDirty = false;
}

void RenderResourceManager::rebuildTextVerticesIfDirty()
{
    if (!textVerticesDirty)
    {
        return;
    }

    textVertexFloats.clear();
    if (overlayTextRuns.empty() || textAtlasWidth <= 0 || textAtlasHeight <= 0
        || gridState.framebufferWidth <= 0 || gridState.framebufferHeight <= 0)
    {
        textVerticesDirty = false;
        return;
    }

    const auto framebufferWidth = static_cast<float>(gridState.framebufferWidth);
    const auto framebufferHeight = static_cast<float>(gridState.framebufferHeight);
    const auto appendVertex = [&](const float x, const float y, const float u, const float v,
                                  const std::array<float, 4> &color)
    {
        textVertexFloats.push_back(x);
        textVertexFloats.push_back(y);
        textVertexFloats.push_back(u);
        textVertexFloats.push_back(v);
        textVertexFloats.insert(textVertexFloats.end(), color.begin(), color.end());
    };
    const auto screenToNdcX = [framebufferWidth](const float x)
    {
        return (x / framebufferWidth) * 2.0f - 1.0f;
    };
    const auto screenToNdcY = [framebufferHeight](const float y)
    {
        return 1.0f - (y / framebufferHeight) * 2.0f;
    };

    for (const auto &run : overlayTextRuns)
    {
        if (run.text.empty() || run.pixelHeight <= 0.0f || run.color[3] <= 0.0f)
        {
            continue;
        }

        const auto scale = run.pixelHeight / static_cast<float>(textAtlasFontPixelSize);
        const auto lineAdvance = run.pixelHeight * 1.28f;
        const auto startX = ((run.x + 1.0f) * 0.5f) * framebufferWidth;
        const auto startY = ((1.0f - run.y) * 0.5f) * framebufferHeight;
        auto penX = startX;
        auto baselineY = startY + static_cast<float>(textAscender) * scale;

        for (const auto ch : run.text)
        {
            if (ch == '\n')
            {
                penX = startX;
                baselineY += lineAdvance;
                continue;
            }

            const auto codepoint = static_cast<unsigned char>(ch);
            if (codepoint >= textGlyphs.size())
            {
                continue;
            }

            const auto &glyph = textGlyphs[codepoint];
            if (!glyph.loaded)
            {
                continue;
            }

            if (glyph.width > 0 && glyph.height > 0)
            {
                const auto xMinPx = penX + static_cast<float>(glyph.bearingX) * scale;
                const auto yMinPx = baselineY - static_cast<float>(glyph.bearingY) * scale;
                const auto xMaxPx = xMinPx + static_cast<float>(glyph.width) * scale;
                const auto yMaxPx = yMinPx + static_cast<float>(glyph.height) * scale;

                const auto xMin = screenToNdcX(xMinPx);
                const auto xMax = screenToNdcX(xMaxPx);
                const auto yTop = screenToNdcY(yMinPx);
                const auto yBottom = screenToNdcY(yMaxPx);

                appendVertex(xMin, yTop, glyph.uMin, glyph.vMin, run.color);
                appendVertex(xMax, yBottom, glyph.uMax, glyph.vMax, run.color);
                appendVertex(xMin, yBottom, glyph.uMin, glyph.vMax, run.color);
                appendVertex(xMin, yTop, glyph.uMin, glyph.vMin, run.color);
                appendVertex(xMax, yTop, glyph.uMax, glyph.vMin, run.color);
                appendVertex(xMax, yBottom, glyph.uMax, glyph.vMax, run.color);
            }

            penX += static_cast<float>(glyph.advance) * scale;
        }
    }

    textVerticesDirty = false;
}

void RenderResourceManager::uploadTextVerticesIfDirty()
{
    rebuildTextVerticesIfDirty();
    glBindBuffer(GL_ARRAY_BUFFER, textGpu.vbo);
    const auto requiredBytes = textVertexFloats.size() * sizeof(float);
    if (requiredBytes > textGpu.vertexCapacity)
    {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(requiredBytes), textVertexFloats.data(), GL_DYNAMIC_DRAW);
        textGpu.vertexCapacity = requiredBytes;
    }
    else if (requiredBytes > 0)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(requiredBytes), textVertexFloats.data());
    }
}

void RenderResourceManager::uploadPendingRegionImages()
{
    if (pendingRegionUploads.empty())
    {
        return;
    }

    const auto &first = pendingRegionUploads.front().ref;
    ensureRegionTextureArray(first.width, first.height);

    glBindTexture(GL_TEXTURE_2D_ARRAY, regionTextures.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    auto skippedForCapacity = size_t{0};
    auto skippedForSize = size_t{0};
    for (const auto &upload : pendingRegionUploads)
    {
        if (upload.slice.slice >= regionTextures.capacity)
        {
            ++skippedForCapacity;
            continue;
        }
        if (upload.ref.width != regionTextures.width || upload.ref.height != regionTextures.height)
        {
            ++skippedForSize;
            continue;
        }
        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0,
            0,
            static_cast<GLint>(upload.slice.slice),
            upload.ref.width,
            upload.ref.height,
            1,
            GL_RED,
            GL_UNSIGNED_BYTE,
            upload.pixels.data());
    }
    if (skippedForCapacity > 0 || skippedForSize > 0)
    {
        PipelineLog::log(
            "render.regionTexture upload skipped capacity=%zu size=%zu pending=%zu allocated=%u",
            skippedForCapacity,
            skippedForSize,
            pendingRegionUploads.size(),
            regionTextures.capacity);
    }
    pendingRegionUploads.clear();
}

void RenderResourceManager::destroyPlotResources()
{
    if (!plotGpu.initialized)
    {
        return;
    }

    glDeleteBuffers(1, &plotGpu.instanceVbo);
    glDeleteBuffers(1, &plotGpu.indexBuffer);
    glDeleteBuffers(1, &plotGpu.quadVbo);
    glDeleteVertexArrays(1, &plotGpu.vao);
    glDeleteProgram(plotGpu.program);
    plotGpu = {};
}

void RenderResourceManager::destroyGridResources()
{
    if (!gridGpu.initialized)
    {
        return;
    }

    glDeleteBuffers(1, &gridGpu.indexBuffer);
    glDeleteBuffers(1, &gridGpu.quadVbo);
    glDeleteVertexArrays(1, &gridGpu.vao);
    glDeleteProgram(gridGpu.program);
    gridGpu = {};
}

void RenderResourceManager::destroyOverlayResources()
{
    if (!overlayGpu.initialized)
    {
        return;
    }

    glDeleteBuffers(1, &overlayGpu.instanceVbo);
    glDeleteBuffers(1, &overlayGpu.indexBuffer);
    glDeleteBuffers(1, &overlayGpu.quadVbo);
    glDeleteVertexArrays(1, &overlayGpu.vao);
    glDeleteProgram(overlayGpu.program);
    overlayGpu = {};
}

void RenderResourceManager::destroyTextResources()
{
    if (textGpu.texture != 0)
    {
        glDeleteTextures(1, &textGpu.texture);
    }
    if (textGpu.vbo != 0)
    {
        glDeleteBuffers(1, &textGpu.vbo);
    }
    if (textGpu.vao != 0)
    {
        glDeleteVertexArrays(1, &textGpu.vao);
    }
    if (textGpu.program != 0)
    {
        glDeleteProgram(textGpu.program);
    }
    textGpu = {};
}

void RenderResourceManager::destroyRegionTextures()
{
    if (!regionTextures.initialized)
    {
        return;
    }
    glDeleteTextures(1, &regionTextures.texture);
    regionTextures = {};
}

bool RenderResourceManager::loadTextAtlas()
{
    const std::array fontCandidates{
        std::filesystem::path{"font"} / "FiraCode-Regular.ttf",
        std::filesystem::path{"build"} / "font" / "FiraCode-Regular.ttf",
        std::filesystem::path{".."} / "font" / "FiraCode-Regular.ttf"
    };

    std::filesystem::path fontPath;
    for (const auto &candidate : fontCandidates)
    {
        if (std::filesystem::exists(candidate))
        {
            fontPath = candidate;
            break;
        }
    }
    if (fontPath.empty())
    {
        std::fprintf(stderr, "[RenderResourceManager] Font not found: FiraCode-Regular.ttf\n");
        return false;
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0)
    {
        std::fprintf(stderr, "[RenderResourceManager] Failed to initialize FreeType.\n");
        return false;
    }

    FT_Face face = nullptr;
    const auto fontPathString = fontPath.string();
    if (FT_New_Face(library, fontPathString.c_str(), 0, &face) != 0)
    {
        std::fprintf(stderr, "[RenderResourceManager] Failed to load font: %s\n", fontPathString.c_str());
        FT_Done_FreeType(library);
        return false;
    }

    textAtlasFontPixelSize = 32;
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(textAtlasFontPixelSize));
    textAscender = static_cast<int>(face->size->metrics.ascender >> 6);
    textAtlasWidth = 1024;
    textAtlasHeight = 256;

    std::vector<uint8_t> atlas(static_cast<size_t>(textAtlasWidth) * static_cast<size_t>(textAtlasHeight), 0);
    auto penX = 1;
    auto penY = 1;
    auto rowHeight = 0;
    for (auto codepoint = 32; codepoint < 127; ++codepoint)
    {
        if (FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_RENDER) != 0)
        {
            continue;
        }

        const auto &bitmap = face->glyph->bitmap;
        const auto width = static_cast<int>(bitmap.width);
        const auto height = static_cast<int>(bitmap.rows);
        if (penX + width + 1 >= textAtlasWidth)
        {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + height + 1 >= textAtlasHeight)
        {
            std::fprintf(stderr, "[RenderResourceManager] Text atlas is too small.\n");
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return false;
        }

        auto &glyph = textGlyphs[static_cast<size_t>(codepoint)];
        glyph.width = width;
        glyph.height = height;
        glyph.bearingX = face->glyph->bitmap_left;
        glyph.bearingY = face->glyph->bitmap_top;
        glyph.advance = static_cast<int>(face->glyph->advance.x >> 6);
        glyph.uMin = static_cast<float>(penX) / static_cast<float>(textAtlasWidth);
        glyph.vMin = static_cast<float>(penY) / static_cast<float>(textAtlasHeight);
        glyph.uMax = static_cast<float>(penX + width) / static_cast<float>(textAtlasWidth);
        glyph.vMax = static_cast<float>(penY + height) / static_cast<float>(textAtlasHeight);
        glyph.loaded = true;

        for (auto row = 0; row < height; ++row)
        {
            for (auto col = 0; col < width; ++col)
            {
                const auto sourceIndex = static_cast<size_t>(row) * static_cast<size_t>(std::abs(bitmap.pitch))
                    + static_cast<size_t>(col);
                const auto targetIndex = static_cast<size_t>(penY + row) * static_cast<size_t>(textAtlasWidth)
                    + static_cast<size_t>(penX + col);
                atlas[targetIndex] = bitmap.buffer[sourceIndex];
            }
        }

        penX += width + 1;
        rowHeight = std::max(rowHeight, height);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    glGenTextures(1, &textGpu.texture);
    glBindTexture(GL_TEXTURE_2D, textGpu.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R8,
        textAtlasWidth,
        textAtlasHeight,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        atlas.data());
    return textGpu.texture != 0;
}

uint32_t RenderResourceManager::compileShader(const uint32_t type, const char *source)
{
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        std::array<char, 2048> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::fprintf(stderr, "[RenderResourceManager] Shader compilation failed: %s\n", log.data());
    }
    return shader;
}

uint32_t RenderResourceManager::linkProgram(const uint32_t vertexShader, const uint32_t fragmentShader)
{
    const auto program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        std::array<char, 2048> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::fprintf(stderr, "[RenderResourceManager] Program link failed: %s\n", log.data());
    }
    return program;
}

std::array<float, 16> RenderResourceManager::viewportTransform(const Interval &xRange, const Interval &yRange)
{
    const auto sx = static_cast<float>(2.0 / xRange.size());
    const auto sy = static_cast<float>(2.0 / yRange.size());
    const auto tx = static_cast<float>(-xRange.mid() * sx);
    const auto ty = static_cast<float>(-yRange.mid() * sy);

    return {
        sx, 0.0f, 0.0f, 0.0f,
        0.0f, sy, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        tx, ty, 0.0f, 1.0f
    };
}
}
