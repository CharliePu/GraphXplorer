//
// Created by charl on 6/2/2024.
//

#include "Renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <span>
#include <vector>
#include <glad/glad.h>

#include "RenderResourceManager.h"

namespace
{
void appendBigEndian32(std::vector<uint8_t> &out, const uint32_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

uint32_t crc32For(std::span<const uint8_t> data)
{
    auto crc = 0xffffffffu;
    for (const auto byte : data)
    {
        crc ^= byte;
        for (auto bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
        }
    }
    return crc ^ 0xffffffffu;
}

uint32_t adler32For(std::span<const uint8_t> data)
{
    constexpr uint32_t mod = 65521;
    uint32_t a = 1;
    uint32_t b = 0;
    for (const auto byte : data)
    {
        a = (a + byte) % mod;
        b = (b + a) % mod;
    }
    return (b << 16) | a;
}

void appendChunk(std::vector<uint8_t> &png, const std::array<char, 4> &type, std::span<const uint8_t> payload)
{
    appendBigEndian32(png, static_cast<uint32_t>(payload.size()));
    const auto typeOffset = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), payload.begin(), payload.end());
    appendBigEndian32(png, crc32For(std::span<const uint8_t>{png.data() + typeOffset, type.size() + payload.size()}));
}

std::vector<uint8_t> deflateStoredZlib(std::span<const uint8_t> data)
{
    std::vector<uint8_t> out;
    out.reserve(data.size() + data.size() / 65535 * 5 + 8);
    out.push_back(0x78);
    out.push_back(0x01);

    size_t offset = 0;
    while (offset < data.size())
    {
        const auto remaining = data.size() - offset;
        const auto blockSize = static_cast<uint16_t>(std::min<size_t>(remaining, 65535));
        const auto finalBlock = offset + blockSize == data.size();
        out.push_back(finalBlock ? 0x01 : 0x00);
        out.push_back(static_cast<uint8_t>(blockSize & 0xff));
        out.push_back(static_cast<uint8_t>((blockSize >> 8) & 0xff));
        const auto inverse = static_cast<uint16_t>(~blockSize);
        out.push_back(static_cast<uint8_t>(inverse & 0xff));
        out.push_back(static_cast<uint8_t>((inverse >> 8) & 0xff));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                   data.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }

    appendBigEndian32(out, adler32For(data));
    return out;
}

bool writeRgbaPng(const std::filesystem::path &path,
                  const int width,
                  const int height,
                  std::span<const uint8_t> rgbaTopDown)
{
    if (width <= 0 || height <= 0 || rgbaTopDown.size() != static_cast<size_t>(width) * height * 4)
    {
        return false;
    }

    std::vector<uint8_t> filtered;
    filtered.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
    for (auto y = 0; y < height; ++y)
    {
        filtered.push_back(0);
        const auto rowBegin = rgbaTopDown.begin() + static_cast<std::ptrdiff_t>(y * width * 4);
        filtered.insert(filtered.end(), rowBegin, rowBegin + static_cast<std::ptrdiff_t>(width * 4));
    }

    std::vector<uint8_t> ihdr;
    ihdr.reserve(13);
    appendBigEndian32(ihdr, static_cast<uint32_t>(width));
    appendBigEndian32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);

    const auto idat = deflateStoredZlib(filtered);

    std::vector<uint8_t> png;
    constexpr std::array<uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), signature.begin(), signature.end());
    appendChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
    appendChunk(png, {'I', 'D', 'A', 'T'}, idat);
    appendChunk(png, {'I', 'E', 'N', 'D'}, {});

    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return file.good();
}
}

Renderer::Renderer(const GLADloadproc &gladLoader)
{
    // Load OpenGL function pointers
    if (!gladLoadGLLoader(gladLoader))
    {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.12, 0.12, 0.14, 1.0);
}

void Renderer::clear()
{
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw(const gx::FrameCommandBuffer &commands, const gx::UploadBudget &uploadBudget)
{
    draw(commands.commands(), uploadBudget);
}

void Renderer::draw(std::span<const gx::DrawCommand> commands, const gx::UploadBudget &uploadBudget)
{
    if (!resources)
    {
        return;
    }

    for (const auto &command : commands)
    {
        resources->draw(command, uploadBudget);
    }
}

void Renderer::setResourceManager(gx::RenderResourceManager *nextResources)
{
    resources = nextResources;
}

void Renderer::onWindowSizeChanged(int width, int height)
{
    viewportWidth = width;
    viewportHeight = height;
    glViewport(0, 0, width, height);
}

bool Renderer::saveBackbufferPng(const std::filesystem::path &path) const
{
    if (viewportWidth <= 0 || viewportHeight <= 0)
    {
        return false;
    }

    std::vector<uint8_t> bottomUp(static_cast<size_t>(viewportWidth) * viewportHeight * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, viewportWidth, viewportHeight, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());

    std::vector<uint8_t> topDown(bottomUp.size());
    const auto rowBytes = static_cast<size_t>(viewportWidth) * 4;
    for (auto y = 0; y < viewportHeight; ++y)
    {
        const auto srcRow = static_cast<size_t>(viewportHeight - 1 - y) * rowBytes;
        const auto dstRow = static_cast<size_t>(y) * rowBytes;
        std::copy_n(bottomUp.data() + srcRow, rowBytes, topDown.data() + dstRow);
    }

    return writeRgbaPng(path, viewportWidth, viewportHeight, topDown);
}
