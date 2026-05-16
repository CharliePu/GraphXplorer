#include "RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <glad/glad.h>
#include <limits>
#include <utility>

namespace gx
{
namespace
{
constexpr const char *PlotVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 iBounds;
layout(location = 2) in vec4 iColor;
layout(location = 3) in vec4 iMeta;

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
    vUv = aCorner;
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
    if (vMode == 3 && uHasRegionTexture && vSlice >= 0)
    {
        float sampleValue = texture(uRegionTexture, vec3(vUv.x, 1.0 - vUv.y, float(vSlice))).r;
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

std::array<float, 4> colorForVisualState(const TileVisualState visualState)
{
    switch (visualState)
    {
    case TileVisualState::UniformTrue:
        return {0.0f, 0.47f, 0.95f, 1.0f};
    case TileVisualState::UniformFalse:
        return {0.0f, 0.0f, 0.0f, 0.0f};
    case TileVisualState::MixedRegion:
        return {1.0f, 0.72f, 0.24f, 0.55f};
    case TileVisualState::ContourOnly:
        return {1.0f, 0.72f, 0.24f, 1.0f};
    case TileVisualState::DebugOverlay:
        return {0.58f, 0.62f, 0.72f, 0.65f};
    case TileVisualState::Missing:
    default:
        return {0.20f, 0.20f, 0.24f, 0.35f};
    }
}
}

RenderResourceManager::RenderResourceManager()
{
    plotTransform = viewportTransform(Interval{-20.0, 20.0}, Interval{-20.0, 20.0});
}

RenderResourceManager::~RenderResourceManager()
{
    destroyPlotResources();
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

void RenderResourceManager::setPlotViewport(const Interval &xRange, const Interval &yRange)
{
    plotTransform = viewportTransform(xRange, yRange);
}

void RenderResourceManager::setPlotInstances(std::vector<RenderTileInstance> instances)
{
    plotInstances = std::move(instances);
    plotInstanceFloats.clear();
    plotInstanceFloats.reserve(plotInstances.size() * 12);

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
    }

    plotInstancesDirty = true;
}

TextureSlice RenderResourceManager::registerRegionImage(const RegionImageRef &ref, const std::span<const uint8_t> pixels)
{
    constexpr uint32_t maxRegionSlices = 512;
    if (ref.id == 0 || ref.width <= 0 || ref.height <= 0 || pixels.empty())
    {
        return {};
    }

    auto it = regionTextures.slices.find(ref.id);
    if (it == regionTextures.slices.end())
    {
        if (regionTextures.nextSlice >= maxRegionSlices)
        {
            return {};
        }

        const auto slice = regionTextures.nextSlice++;
        it = regionTextures.slices.emplace(ref.id, slice).first;
        pendingRegionUploads.push_back({
            ref,
            TextureSlice{RegionTextureSet.id, slice},
            std::vector<uint8_t>{pixels.begin(), pixels.end()}
        });
    }

    return TextureSlice{RegionTextureSet.id, it->second};
}

void RenderResourceManager::draw(const DrawCommand &command)
{
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
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(plotInstances.size()));
    glBindVertexArray(0);
}

size_t RenderResourceManager::plotInstanceCount() const
{
    return plotInstances.size();
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
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(8 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    plotGpu.initialized = true;
    plotInstancesDirty = true;
}

void RenderResourceManager::ensureRegionTextureArray(const int width, const int height)
{
    if (regionTextures.initialized)
    {
        return;
    }

    constexpr uint32_t maxRegionSlices = 512;
    regionTextures.width = width;
    regionTextures.height = height;
    regionTextures.capacity = maxRegionSlices;

    glGenTextures(1, &regionTextures.texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, regionTextures.texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
}

void RenderResourceManager::uploadPlotInstancesIfDirty()
{
    if (!plotInstancesDirty)
    {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, plotGpu.instanceVbo);
    const auto requiredBytes = plotInstanceFloats.size() * sizeof(float);
    if (requiredBytes > plotGpu.instanceCapacity)
    {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(requiredBytes), plotInstanceFloats.data(), GL_DYNAMIC_DRAW);
        plotGpu.instanceCapacity = requiredBytes;
    }
    else if (requiredBytes > 0)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(requiredBytes), plotInstanceFloats.data());
    }
    plotInstancesDirty = false;
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
    for (const auto &upload : pendingRegionUploads)
    {
        if (upload.ref.width != regionTextures.width || upload.ref.height != regionTextures.height)
        {
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

void RenderResourceManager::destroyRegionTextures()
{
    if (!regionTextures.initialized)
    {
        return;
    }
    glDeleteTextures(1, &regionTextures.texture);
    regionTextures = {};
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
