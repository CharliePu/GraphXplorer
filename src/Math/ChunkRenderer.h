//
// Created by Codex on 2/27/2026.
//

#ifndef CHUNKRENDERER_H
#define CHUNKRENDERER_H

#include <memory>
#include <vector>

#include "Interval.h"
#include "RasterizedPlot.h"
#include "../Formula/Parser.h"

class Formula;

class ChunkRenderer
{
public:
    virtual ~ChunkRenderer() = default;

    virtual bool rasterizeMixedChunkTexture(const std::shared_ptr<Formula> &formula,
                                            const Interval &xRange,
                                            const Interval &yRange,
                                            int textureSize,
                                            std::vector<int> &outputPixels) = 0;

    virtual bool rasterizeChunkContourSegments(const RPN &residualRpn,
                                               const Interval &xRange,
                                               const Interval &yRange,
                                               int cellsPerAxis,
                                               std::vector<RasterContourSegment> &outputSegments) = 0;
};

#endif // CHUNKRENDERER_H
