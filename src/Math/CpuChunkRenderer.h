//
// Created by Codex on 2/27/2026.
//

#ifndef CPUCHUNKRENDERER_H
#define CPUCHUNKRENDERER_H

#include "ChunkRenderer.h"

class CpuChunkRenderer final : public ChunkRenderer
{
public:
    bool rasterizeMixedChunkTexture(const std::shared_ptr<Formula> &formula,
                                    const Interval &xRange,
                                    const Interval &yRange,
                                    int textureSize,
                                    std::vector<int> &outputPixels) override;

    bool rasterizeChunkContourSegments(const RPN &residualRpn,
                                       const Interval &xRange,
                                       const Interval &yRange,
                                       int cellsPerAxis,
                                       std::vector<RasterContourSegment> &outputSegments) override;
};

#endif // CPUCHUNKRENDERER_H
