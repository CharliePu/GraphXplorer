#include "UploadPlanner.h"

namespace gx
{
UploadPlan UploadPlanner::plan(std::span<const TileRecord> records, const UploadBudget &budget) const
{
    UploadPlan result;
    auto textureSlices = 0;
    auto instanceUpdates = 0;

    for (const auto &record : records)
    {
        if (record.stage != TileStage::UploadQueued && record.stage != TileStage::RegionReady && record.stage != TileStage::ContourReady)
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
}
