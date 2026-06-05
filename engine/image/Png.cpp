#include "Png.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace gxr
{
namespace
{
void be32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

uint32_t crc32(std::span<const uint8_t> data)
{
    uint32_t crc = 0xffffffffu;
    for (uint8_t b : data)
    {
        crc ^= b;
        for (int i = 0; i < 8; ++i) crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return crc ^ 0xffffffffu;
}

uint32_t adler32(std::span<const uint8_t> data)
{
    constexpr uint32_t mod = 65521;
    uint32_t a = 1, b = 0;
    for (uint8_t byte : data)
    {
        a = (a + byte) % mod;
        b = (b + a) % mod;
    }
    return (b << 16) | a;
}

void chunk(std::vector<uint8_t> &png, const std::array<char, 4> &type, std::span<const uint8_t> p)
{
    be32(png, static_cast<uint32_t>(p.size()));
    const size_t off = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), p.begin(), p.end());
    png.push_back(0);
    png.push_back(0);
    png.push_back(0);
    png.push_back(0);
    const uint32_t c = crc32(std::span<const uint8_t>{png.data() + off, type.size() + p.size()});
    png[png.size() - 4] = static_cast<uint8_t>(c >> 24);
    png[png.size() - 3] = static_cast<uint8_t>(c >> 16);
    png[png.size() - 2] = static_cast<uint8_t>(c >> 8);
    png[png.size() - 1] = static_cast<uint8_t>(c);
}

std::vector<uint8_t> zlibStored(std::span<const uint8_t> data)
{
    std::vector<uint8_t> out;
    out.push_back(0x78);
    out.push_back(0x01);
    size_t off = 0;
    while (off < data.size())
    {
        const size_t remaining = data.size() - off;
        const auto block = static_cast<uint16_t>(std::min<size_t>(remaining, 65535));
        const bool last = (off + block == data.size());
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<uint8_t>(block & 0xff));
        out.push_back(static_cast<uint8_t>(block >> 8));
        const auto inv = static_cast<uint16_t>(~block);
        out.push_back(static_cast<uint8_t>(inv & 0xff));
        out.push_back(static_cast<uint8_t>(inv >> 8));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(off),
                   data.begin() + static_cast<std::ptrdiff_t>(off + block));
        off += block;
    }
    be32(out, adler32(data));
    return out;
}
}

bool writePng(const std::string &path, int width, int height, std::span<const uint8_t> rgba)
{
    if (width <= 0 || height <= 0 || rgba.size() != static_cast<size_t>(width) * height * 4)
    {
        return false;
    }

    std::vector<uint8_t> filtered;
    filtered.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y)
    {
        filtered.push_back(0); // filter: none
        const auto row = rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * width * 4);
        filtered.insert(filtered.end(), row, row + static_cast<std::ptrdiff_t>(width * 4));
    }

    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(width));
    be32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);

    const auto idat = zlibStored(filtered);

    std::vector<uint8_t> png;
    constexpr std::array<uint8_t, 8> sig{137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig.begin(), sig.end());
    chunk(png, {'I', 'H', 'D', 'R'}, ihdr);
    chunk(png, {'I', 'D', 'A', 'T'}, idat);
    chunk(png, {'I', 'E', 'N', 'D'}, {});

    const std::filesystem::path fp{path};
    if (fp.has_parent_path()) std::filesystem::create_directories(fp.parent_path());
    std::ofstream f(fp, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return f.good();
}
}
