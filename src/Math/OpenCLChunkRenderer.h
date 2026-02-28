//
// Created by Codex on 2/27/2026.
//

#ifndef OPENCLCHUNKRENDERER_H
#define OPENCLCHUNKRENDERER_H

#include <cstdint>
#include <memory>
#include <vector>

#include "ChunkRenderer.h"

class Formula;

class OpenCLChunkRenderer final : public ChunkRenderer
{
public:
    OpenCLChunkRenderer();
    ~OpenCLChunkRenderer();

    OpenCLChunkRenderer(const OpenCLChunkRenderer &) = delete;
    OpenCLChunkRenderer &operator=(const OpenCLChunkRenderer &) = delete;
    OpenCLChunkRenderer(OpenCLChunkRenderer &&) = delete;
    OpenCLChunkRenderer &operator=(OpenCLChunkRenderer &&) = delete;

    bool isAvailable() const;

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

    bool rasterize(const std::vector<int> &chunkStates,
                   int chunkWidth,
                   int chunkHeight,
                   float xLower,
                   float yLower,
                   float deltaX,
                   float deltaY,
                   int targetLevel,
                   int64_t minChunkX,
                   int64_t minChunkY,
                   int windowWidth,
                   int windowHeight,
                   std::vector<int> &outputImage) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif //OPENCLCHUNKRENDERER_H
