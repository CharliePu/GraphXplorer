#include "UploadPlanner.h"

#include <algorithm>

namespace gx
{
UploadPlan UploadPlanner::plan(std::span<const TileRecord> records, const UploadBudget &budget) const
{
    UploadPlan result;
    auto textureSlices = 0;
    auto instanceUpdates = 0;

    for (const auto &record : records)
    {
        if (record.workState != TileWorkState::UploadQueued
            && record.workState != TileWorkState::RegionReady
            && record.workState != TileWorkState::ContourReady)
        {
            continue;
        }

        if (record.regionPixels && textureSlices < budget.maxTextureSlicesPerFrame)
        {
            const auto textureBytes = static_cast<size_t>(record.regionPixels->width)
                * static_cast<size_t>(record.regionPixels->height);
            if (result.textureBytes + textureBytes <= budget.maxTextureBytesPerFrame)
            {
                result.textureUploads.push_back(record.key);
                result.textureBytes += textureBytes;
                ++textureSlices;
            }
            else
            {
                result.budgetExhausted = true;
            }
        }

        if (record.contourSegments && instanceUpdates < budget.maxTileInstanceUpdatesPerFrame)
        {
            const auto bufferBytes = static_cast<size_t>(record.contourSegments->count) * sizeof(float) * 4u;
            if (result.bufferBytes + bufferBytes <= budget.maxBufferBytesPerFrame)
            {
                result.bufferUploads.push_back(record.key);
                result.bufferBytes += bufferBytes;
                ++instanceUpdates;
            }
            else
            {
                result.budgetExhausted = true;
            }
        }
    }

    return result;
}

UploadPlan UploadPlanner::planVisible(std::span<const DisplayTile> tiles, const UploadBudget &budget) const
{
    UploadPlan result;
    auto textureSlices = 0;
    for (const auto &tile : tiles)
    {
        if (!tile.cpuRegion || tile.gpuSlice.textureId != 0)
        {
            continue;
        }
        if (std::ranges::find(result.textureUploads, tile.sourceKey) != result.textureUploads.end())
        {
            continue;
        }
        if (textureSlices >= budget.maxTextureSlicesPerFrame)
        {
            result.budgetExhausted = true;
            continue;
        }

        const auto textureBytes = static_cast<size_t>(tile.cpuRegion->width)
            * static_cast<size_t>(tile.cpuRegion->height);
        if (result.textureBytes + textureBytes > budget.maxTextureBytesPerFrame)
        {
            result.budgetExhausted = true;
            continue;
        }

        result.textureUploads.push_back(tile.sourceKey);
        result.textureBytes += textureBytes;
        ++textureSlices;
    }
    return result;
}
}
