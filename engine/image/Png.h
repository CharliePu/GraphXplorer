#ifndef GXR_IMAGE_PNG_H
#define GXR_IMAGE_PNG_H

#include <cstdint>
#include <span>
#include <string>

namespace gxr
{
// Dependency-free PNG writer (8-bit RGBA, top-down rows, stored zlib blocks).
[[nodiscard]] bool writePng(const std::string &path, int width, int height,
                            std::span<const uint8_t> rgbaTopDown);
}

#endif // GXR_IMAGE_PNG_H
