#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "../src/Compute/InequalityTileRefiner.h"
#include "../src/Formula/FormulaCompiler.h"

namespace
{
struct BenchCase
{
    std::string name;
    std::string formula;
    gx::TileKey key;
    gx::Rect bounds;
    uint32_t pixelsPerAxis{128};
    int extraDepth{gx::DefaultRasterProofExtraDepth};
};

struct CaseResult
{
    bool ok{false};
    std::chrono::microseconds elapsed{0};
    size_t visitedNodes{0};
    size_t unknownPixels{0};
    size_t falsePixels{0};
    size_t estimatedPixels{0};
    size_t truePixels{0};
    size_t proofNodes{0};
    size_t intervalEvaluations{0};
    size_t pointEvaluations{0};
    uint64_t pixelChecksum{0};
    uint64_t proofChecksum{0};
    gx::TextureCertainty certainty{gx::TextureCertainty::Imprecise};
    gx::TileExistenceState existence{gx::TileExistenceState::Unknown};
    std::string message;
};

uint64_t mix(const uint64_t hash, const uint64_t value)
{
    auto mixed = hash ^ value;
    mixed *= 1099511628211ull;
    return mixed;
}

uint64_t pixelChecksumFor(const gx::RegionOutput &region)
{
    auto hash = 1469598103934665603ull;
    hash = mix(hash, region.width);
    hash = mix(hash, region.height);
    hash = mix(hash, static_cast<uint64_t>(region.certainty));
    hash = mix(hash, static_cast<uint64_t>(region.existence));
    for (const auto pixel : region.pixels)
    {
        hash = mix(hash, pixel);
    }
    return hash;
}

uint64_t proofChecksumFor(const gx::RegionOutput &region)
{
    auto hash = 1469598103934665603ull;
    hash = mix(hash, region.proofTree.nodes.size());
    for (const auto &node : region.proofTree.nodes)
    {
        hash = mix(hash, static_cast<uint64_t>(node.key.x));
        hash = mix(hash, static_cast<uint64_t>(node.key.y));
        hash = mix(hash, static_cast<uint64_t>(node.key.level));
        hash = mix(hash, static_cast<uint64_t>(node.classification));
        hash = mix(hash, static_cast<uint64_t>(node.existence));
    }
    return hash;
}

std::array<size_t, 3> pixelCountsFor(const gx::RegionOutput &region)
{
    std::array<size_t, 3> counts{};
    for (const auto pixel : region.pixels)
    {
        if (pixel == uint8_t{0})
        {
            ++counts[0];
        }
        else if (pixel == uint8_t{255})
        {
            ++counts[2];
        }
        else
        {
            ++counts[1];
        }
    }
    return counts;
}

int intArg(const int argc, char **argv, const std::string_view name, const int fallback)
{
    for (auto i = 1; i + 1 < argc; ++i)
    {
        if (argv[i] == name)
        {
            return std::max(1, std::atoi(argv[i + 1]));
        }
    }
    return fallback;
}

CaseResult runCase(const BenchCase &bench)
{
    const auto formula = gx::FormulaCompiler{}.compile(bench.formula);
    if (!formula.diagnostics.ok)
    {
        return {.message = formula.diagnostics.message};
    }

    const auto started = std::chrono::steady_clock::now();
    auto result = gx::refineInequalityTile(
        formula,
        bench.key,
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = bench.pixelsPerAxis,
            .subpixelExtraDepth = bench.extraDepth,
            .rootBounds = bench.bounds
        });
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);

    if (!result.ok)
    {
        return {
            .ok = false,
            .elapsed = elapsed,
            .visitedNodes = result.visitedNodes,
            .unknownPixels = result.unknownPixels,
            .intervalEvaluations = result.intervalEvaluations,
            .pointEvaluations = result.pointEvaluations,
            .message = result.message
        };
    }

    const auto pixelCounts = pixelCountsFor(result.region);
    return {
        .ok = true,
        .elapsed = elapsed,
        .visitedNodes = result.visitedNodes,
        .unknownPixels = result.unknownPixels,
        .falsePixels = pixelCounts[0],
        .estimatedPixels = pixelCounts[1],
        .truePixels = pixelCounts[2],
        .proofNodes = result.region.proofTree.nodes.size(),
        .intervalEvaluations = result.intervalEvaluations,
        .pointEvaluations = result.pointEvaluations,
        .pixelChecksum = pixelChecksumFor(result.region),
        .proofChecksum = proofChecksumFor(result.region),
        .certainty = result.region.certainty,
        .existence = result.region.existence
    };
}
}

int main(const int argc, char **argv)
{
    const auto iterations = intArg(argc, argv, "--iterations", 3);
    const auto pixels = static_cast<uint32_t>(intArg(argc, argv, "--pixels", 128));

    std::vector<BenchCase> cases{
        {
            .name = "uniform_true",
            .formula = "x<2",
            .key = {0, 0, 0},
            .bounds = {0.0, 1.0, 0.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "diagonal_threshold",
            .formula = "x<=y",
            .key = {0, 0, 0},
            .bounds = {0.0, 1.0, 0.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "vertical_threshold",
            .formula = "x>4",
            .key = {0, 0, 0},
            .bounds = {3.0, 5.0, 0.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "nested_trig",
            .formula = "sin(x*y)<sin(sin(y))",
            .key = {0, 0, 0},
            .bounds = {-8.0, 8.0, -8.0, 8.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "tan_pole",
            .formula = "y<=tan(x*y)",
            .key = {0, 0, 0},
            .bounds = {1.5, 1.6, 1.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "wide_tan",
            .formula = "tan(x)>y",
            .key = {0, 0, 0},
            .bounds = {-1000.0, 1000.0, -1.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "log_domain",
            .formula = "y<log(x)",
            .key = {0, 0, 0},
            .bounds = {-2.0, 2.0, -2.0, 2.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "log_tiny_domain",
            .formula = "y<log(x)",
            .key = {0, 0, 0},
            .bounds = {-1.0e-6, 1.0e-3, -12.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "circle_quadratic",
            .formula = "x^2+y^2<16",
            .key = {0, 0, 0},
            .bounds = {-5.0, 5.0, -5.0, 5.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "outside_circle",
            .formula = "x^2+y^2>16",
            .key = {0, 0, 0},
            .bounds = {-5.0, 5.0, -5.0, 5.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "rational_asymptote",
            .formula = "y<1/(x-0.001)",
            .key = {0, 0, 0},
            .bounds = {-2.0, 2.0, -5.0, 5.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "sqrt_domain",
            .formula = "y<sqrt(x)",
            .key = {0, 0, 0},
            .bounds = {-1.0, 4.0, -1.0, 3.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "exp_wall",
            .formula = "exp(x)-y>0",
            .key = {0, 0, 0},
            .bounds = {-4.0, 4.0, 0.0, 20.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "steep_exp_wall",
            .formula = "exp(x)-y>0",
            .key = {0, 0, 0},
            .bounds = {-10.0, 10.0, 0.0, 1000.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "high_freq_trig",
            .formula = "sin(20*x*y)<sin(sin(10*y))",
            .key = {0, 0, 0},
            .bounds = {-2.0, 2.0, -2.0, 2.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "thin_band",
            .formula = "(x-y)^2<0.0001",
            .key = {0, 0, 0},
            .bounds = {-1.0, 1.0, -1.0, 1.0},
            .pixelsPerAxis = pixels
        },
        {
            .name = "outside_band",
            .formula = "(x-y)^2>0.0001",
            .key = {0, 0, 0},
            .bounds = {-1.0, 1.0, -1.0, 1.0},
            .pixelsPerAxis = pixels
        }
    };

    std::cout << "case,ok,pixels,iterations,best_us,avg_us,visited,unknown,false_px,estimate_px,true_px,proof_nodes,interval_evals,point_evals,pixel_checksum,proof_checksum,certainty,existence\n";
    for (const auto &bench : cases)
    {
        std::vector<CaseResult> results;
        results.reserve(static_cast<size_t>(iterations));
        for (auto i = 0; i < iterations; ++i)
        {
            results.push_back(runCase(bench));
        }

        const auto best = std::ranges::min_element(results, [](const CaseResult &lhs, const CaseResult &rhs)
        {
            return lhs.elapsed < rhs.elapsed;
        });
        const auto total = std::ranges::fold_left(results, int64_t{0}, [](const int64_t sum, const CaseResult &result)
        {
            return sum + result.elapsed.count();
        });
        const auto &last = results.back();
        std::cout << bench.name
            << "," << (last.ok ? "true" : "false")
            << "," << bench.pixelsPerAxis
            << "," << iterations
            << "," << best->elapsed.count()
            << "," << std::fixed << std::setprecision(1)
            << static_cast<double>(total) / static_cast<double>(iterations)
            << "," << last.visitedNodes
            << "," << last.unknownPixels
            << "," << last.falsePixels
            << "," << last.estimatedPixels
            << "," << last.truePixels
            << "," << last.proofNodes
            << "," << last.intervalEvaluations
            << "," << last.pointEvaluations
            << "," << last.pixelChecksum
            << "," << last.proofChecksum
            << "," << static_cast<int>(last.certainty)
            << "," << static_cast<int>(last.existence)
            << "\n";

        if (!last.ok)
        {
            std::cerr << bench.name << ": " << last.message << "\n";
            return 1;
        }
    }

    return 0;
}
