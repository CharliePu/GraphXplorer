//
// Created by Codex on 2/27/2026.
//

#include "CpuChunkRenderer.h"

#include "../Formula/Formula.h"

bool CpuChunkRenderer::rasterizeMixedChunkTexture(const std::shared_ptr<Formula> &formula,
                                                  const Interval &xRange,
                                                  const Interval &yRange,
                                                  const int textureSize,
                                                  std::vector<int> &outputPixels)
{
    if (!formula || textureSize <= 0)
    {
        outputPixels.clear();
        return false;
    }

    outputPixels.assign(static_cast<size_t>(textureSize) * static_cast<size_t>(textureSize), 0);

    const auto deltaX = xRange.size() / static_cast<double>(textureSize);
    const auto deltaY = yRange.size() / static_cast<double>(textureSize);

    for (int y = 0; y < textureSize; ++y)
    {
        const auto sampleY = yRange.lower + (static_cast<double>(y) + 0.5) * deltaY;
        for (int x = 0; x < textureSize; ++x)
        {
            const auto sampleX = xRange.lower + (static_cast<double>(x) + 0.5) * deltaX;
            const auto idx = y * textureSize + x;

            try
            {
                const auto value = formula->evaluate({{"x", sampleX}, {"y", sampleY}});
                outputPixels[static_cast<size_t>(idx)] = value > 0.0 ? 1 : 0;
            }
            catch (...)
            {
                outputPixels[static_cast<size_t>(idx)] = 0;
            }
        }
    }

    return true;
}
