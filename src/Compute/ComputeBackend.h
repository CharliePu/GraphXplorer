#ifndef COMPUTEBACKEND_H
#define COMPUTEBACKEND_H

#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "../Formula/FormulaCompiler.h"
#include "../Tile/TileMath.h"
#include "../Util/Contracts.h"

namespace gx
{
struct BackendCapabilities
{
    bool supportsIntervalClassification{true};
    bool supportsRegionRaster{true};
    bool supportsContourExtraction{false};
    bool supportsOpenCl{false};
};

struct BatchResult
{
    bool ok{true};
    size_t completed{0};
    std::string message{};
};

struct IntervalBatchView
{
    const CompiledFormula *formula{nullptr};
    std::span<const TileKey> keys{};
    std::span<const double> xMin{};
    std::span<const double> xMax{};
    std::span<const double> yMin{};
    std::span<const double> yMax{};
    std::function<bool()> cancelled{};
};

struct RasterBatchView
{
    const CompiledFormula *formula{nullptr};
    std::span<const TileKey> keys{};
    std::span<const double> xMin{};
    std::span<const double> xMax{};
    std::span<const double> yMin{};
    std::span<const double> yMax{};
    std::span<const uint32_t> outputOffsets{};
    uint32_t pixelsPerAxis{RasterTexturePixels};
    bool allowGpu{true};
    std::function<bool()> cancelled{};
};

struct TileClassificationResult
{
    TileKey key{};
    TileClassification classification{TileClassification::Unknown};
    Interval interval{};
};

struct RegionOutput
{
    TileKey key{};
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> pixels{};
};

struct ContourOutput
{
    TileKey key{};
    std::vector<ContourSegmentRange> ranges{};
};

class ComputeBackend
{
public:
    virtual ~ComputeBackend() = default;

    [[nodiscard]] virtual BackendCapabilities capabilities() const = 0;

    virtual BatchResult classifyIntervals(
        const IntervalBatchView &batch,
        std::span<TileClassificationResult> out) = 0;

    virtual BatchResult rasterizeRegions(
        const RasterBatchView &batch,
        std::span<RegionOutput> out) = 0;
};

class CpuComputeBackend final : public ComputeBackend
{
public:
    [[nodiscard]] BackendCapabilities capabilities() const override;

    BatchResult classifyIntervals(
        const IntervalBatchView &batch,
        std::span<TileClassificationResult> out) override;

    BatchResult rasterizeRegions(
        const RasterBatchView &batch,
        std::span<RegionOutput> out) override;
};

[[nodiscard]] std::unique_ptr<ComputeBackend> makeDefaultComputeBackend();
}

#endif // COMPUTEBACKEND_H
