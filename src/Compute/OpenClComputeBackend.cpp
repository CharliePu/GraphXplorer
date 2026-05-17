#include "ComputeBackend.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../Util/PipelineLog.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gx
{
namespace
{
constexpr int32_t MaxGpuFormulaStack = 64;

struct GpuFormulaInstruction
{
    int32_t op{0};
    int32_t variableSlot{0};
    double constantValue{0.0};
};

static_assert(sizeof(GpuFormulaInstruction) == 16);

[[nodiscard]] std::optional<int32_t> maxStackDepthFor(const CompiledFormula &formula)
{
    auto depth = 0;
    auto maxDepth = 0;
    for (const auto &instruction : formula.evaluationIr)
    {
        switch (instruction.op)
        {
        case FormulaOp::PushConstant:
        case FormulaOp::PushVariable:
            ++depth;
            maxDepth = std::max(maxDepth, depth);
            break;
        case FormulaOp::Sin:
        case FormulaOp::Cos:
        case FormulaOp::Tan:
        case FormulaOp::Log:
        case FormulaOp::Exp:
        case FormulaOp::Sqrt:
            if (depth < 1)
            {
                return std::nullopt;
            }
            break;
        default:
            if (depth < 2)
            {
                return std::nullopt;
            }
            --depth;
            break;
        }
    }

    if (depth != 1)
    {
        return std::nullopt;
    }
    return maxDepth;
}

[[nodiscard]] std::vector<GpuFormulaInstruction> lowerFormulaForGpu(const CompiledFormula &formula)
{
    std::vector<GpuFormulaInstruction> instructions;
    instructions.reserve(formula.evaluationIr.size());
    for (const auto &instruction : formula.evaluationIr)
    {
        instructions.push_back({
            .op = static_cast<int32_t>(instruction.op),
            .variableSlot = static_cast<int32_t>(instruction.variableSlot),
            .constantValue = instruction.constant
        });
    }
    return instructions;
}

[[nodiscard]] BatchResult invalidRasterBatch()
{
    return {false, 0, "Invalid raster batch shape"};
}

struct RasterOutputStats
{
    size_t zeroTiles{0};
    size_t fullTiles{0};
    size_t mixedTiles{0};
};

[[nodiscard]] RasterOutputStats summarizeRasterOutputs(std::span<const RegionOutput> out,
                                                       const size_t count)
{
    RasterOutputStats stats;
    const auto limit = std::min(count, out.size());
    for (size_t index = 0; index < limit; ++index)
    {
        const auto &pixels = out[index].pixels;
        if (pixels.empty())
        {
            continue;
        }
        const auto zero = std::ranges::all_of(pixels, [](const uint8_t value)
        {
            return value == 0;
        });
        if (zero)
        {
            ++stats.zeroTiles;
            continue;
        }
        const auto full = std::ranges::all_of(pixels, [](const uint8_t value)
        {
            return value == 255;
        });
        if (full)
        {
            ++stats.fullTiles;
            continue;
        }
        ++stats.mixedTiles;
    }
    return stats;
}

#ifdef _WIN32
using cl_int = int32_t;
using cl_uint = uint32_t;
using cl_ulong = uint64_t;
using cl_bool = cl_uint;
using cl_bitfield = cl_ulong;
using cl_device_type = cl_bitfield;
using cl_platform_id = struct _cl_platform_id *;
using cl_device_id = struct _cl_device_id *;
using cl_context = struct _cl_context *;
using cl_command_queue = struct _cl_command_queue *;
using cl_program = struct _cl_program *;
using cl_kernel = struct _cl_kernel *;
using cl_mem = struct _cl_mem *;

constexpr cl_int CL_SUCCESS = 0;
constexpr cl_bool CL_TRUE = 1;
constexpr cl_device_type CL_DEVICE_TYPE_GPU = 1ull << 2;
constexpr cl_uint CL_DEVICE_DOUBLE_FP_CONFIG = 0x1032;
constexpr cl_bitfield CL_MEM_READ_ONLY = 1ull << 2;
constexpr cl_bitfield CL_MEM_WRITE_ONLY = 1ull << 1;
constexpr cl_bitfield CL_MEM_COPY_HOST_PTR = 1ull << 5;

#define GX_CL_CALL __stdcall

struct OpenClApi
{
    HMODULE library{nullptr};

    using clGetPlatformIDs_t = cl_int(GX_CL_CALL *)(cl_uint, cl_platform_id *, cl_uint *);
    using clGetDeviceIDs_t = cl_int(GX_CL_CALL *)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
    using clGetDeviceInfo_t = cl_int(GX_CL_CALL *)(cl_device_id, cl_uint, size_t, void *, size_t *);
    using clCreateContext_t = cl_context(GX_CL_CALL *)(const void *, cl_uint, const cl_device_id *, void *, void *, cl_int *);
    using clReleaseContext_t = cl_int(GX_CL_CALL *)(cl_context);
    using clCreateCommandQueue_t = cl_command_queue(GX_CL_CALL *)(cl_context, cl_device_id, cl_bitfield, cl_int *);
    using clReleaseCommandQueue_t = cl_int(GX_CL_CALL *)(cl_command_queue);
    using clCreateProgramWithSource_t = cl_program(GX_CL_CALL *)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
    using clBuildProgram_t = cl_int(GX_CL_CALL *)(cl_program, cl_uint, const cl_device_id *, const char *, void *, void *);
    using clGetProgramBuildInfo_t = cl_int(GX_CL_CALL *)(cl_program, cl_device_id, cl_uint, size_t, void *, size_t *);
    using clReleaseProgram_t = cl_int(GX_CL_CALL *)(cl_program);
    using clCreateKernel_t = cl_kernel(GX_CL_CALL *)(cl_program, const char *, cl_int *);
    using clReleaseKernel_t = cl_int(GX_CL_CALL *)(cl_kernel);
    using clCreateBuffer_t = cl_mem(GX_CL_CALL *)(cl_context, cl_bitfield, size_t, void *, cl_int *);
    using clReleaseMemObject_t = cl_int(GX_CL_CALL *)(cl_mem);
    using clSetKernelArg_t = cl_int(GX_CL_CALL *)(cl_kernel, cl_uint, size_t, const void *);
    using clEnqueueNDRangeKernel_t = cl_int(GX_CL_CALL *)(cl_command_queue, cl_kernel, cl_uint, const size_t *,
                                                           const size_t *, const size_t *, cl_uint, const void *, void *);
    using clEnqueueReadBuffer_t = cl_int(GX_CL_CALL *)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *,
                                                       cl_uint, const void *, void *);
    using clFinish_t = cl_int(GX_CL_CALL *)(cl_command_queue);

    clGetPlatformIDs_t clGetPlatformIDs{nullptr};
    clGetDeviceIDs_t clGetDeviceIDs{nullptr};
    clGetDeviceInfo_t clGetDeviceInfo{nullptr};
    clCreateContext_t clCreateContext{nullptr};
    clReleaseContext_t clReleaseContext{nullptr};
    clCreateCommandQueue_t clCreateCommandQueue{nullptr};
    clReleaseCommandQueue_t clReleaseCommandQueue{nullptr};
    clCreateProgramWithSource_t clCreateProgramWithSource{nullptr};
    clBuildProgram_t clBuildProgram{nullptr};
    clGetProgramBuildInfo_t clGetProgramBuildInfo{nullptr};
    clReleaseProgram_t clReleaseProgram{nullptr};
    clCreateKernel_t clCreateKernel{nullptr};
    clReleaseKernel_t clReleaseKernel{nullptr};
    clCreateBuffer_t clCreateBuffer{nullptr};
    clReleaseMemObject_t clReleaseMemObject{nullptr};
    clSetKernelArg_t clSetKernelArg{nullptr};
    clEnqueueNDRangeKernel_t clEnqueueNDRangeKernel{nullptr};
    clEnqueueReadBuffer_t clEnqueueReadBuffer{nullptr};
    clFinish_t clFinish{nullptr};

    ~OpenClApi()
    {
        if (library)
        {
            FreeLibrary(library);
        }
    }

    template<typename T>
    [[nodiscard]] bool load(T &target, const char *name)
    {
        target = reinterpret_cast<T>(GetProcAddress(library, name));
        return target != nullptr;
    }

    [[nodiscard]] static std::shared_ptr<OpenClApi> load()
    {
        auto api = std::make_shared<OpenClApi>();
        api->library = LoadLibraryA("OpenCL.dll");
        if (!api->library)
        {
            return {};
        }

        if (!api->load(api->clGetPlatformIDs, "clGetPlatformIDs")
            || !api->load(api->clGetDeviceIDs, "clGetDeviceIDs")
            || !api->load(api->clGetDeviceInfo, "clGetDeviceInfo")
            || !api->load(api->clCreateContext, "clCreateContext")
            || !api->load(api->clReleaseContext, "clReleaseContext")
            || !api->load(api->clCreateCommandQueue, "clCreateCommandQueue")
            || !api->load(api->clReleaseCommandQueue, "clReleaseCommandQueue")
            || !api->load(api->clCreateProgramWithSource, "clCreateProgramWithSource")
            || !api->load(api->clBuildProgram, "clBuildProgram")
            || !api->load(api->clGetProgramBuildInfo, "clGetProgramBuildInfo")
            || !api->load(api->clReleaseProgram, "clReleaseProgram")
            || !api->load(api->clCreateKernel, "clCreateKernel")
            || !api->load(api->clReleaseKernel, "clReleaseKernel")
            || !api->load(api->clCreateBuffer, "clCreateBuffer")
            || !api->load(api->clReleaseMemObject, "clReleaseMemObject")
            || !api->load(api->clSetKernelArg, "clSetKernelArg")
            || !api->load(api->clEnqueueNDRangeKernel, "clEnqueueNDRangeKernel")
            || !api->load(api->clEnqueueReadBuffer, "clEnqueueReadBuffer")
            || !api->load(api->clFinish, "clFinish"))
        {
            return {};
        }

        return api;
    }
};

struct OpenClMem
{
    std::shared_ptr<OpenClApi> api;
    cl_mem value{nullptr};

    OpenClMem() = default;
    OpenClMem(std::shared_ptr<OpenClApi> nextApi, cl_mem nextValue): api{std::move(nextApi)}, value{nextValue} {}
    OpenClMem(const OpenClMem &) = delete;
    OpenClMem &operator=(const OpenClMem &) = delete;

    OpenClMem(OpenClMem &&other) noexcept
        : api{std::move(other.api)},
          value{std::exchange(other.value, nullptr)}
    {
    }

    OpenClMem &operator=(OpenClMem &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            api = std::move(other.api);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }

    ~OpenClMem()
    {
        reset();
    }

    void reset()
    {
        if (api && value)
        {
            api->clReleaseMemObject(value);
        }
        value = nullptr;
    }
};

[[nodiscard]] constexpr std::string_view openClRasterKernelSource()
{
    return R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

typedef struct GpuFormulaInstruction
{
    int op;
    int variableSlot;
    double constantValue;
} GpuFormulaInstruction;

double binaryValue(int op, double lhs, double rhs)
{
    switch (op)
    {
    case 2: return lhs + rhs;
    case 3: return lhs - rhs;
    case 4: return lhs * rhs;
    case 5: return lhs / rhs;
    case 6: return pow(lhs, rhs);
    case 7: return lhs > rhs ? 1.0 : 0.0;
    case 8: return lhs < rhs ? 1.0 : 0.0;
    case 9: return lhs >= rhs ? 1.0 : 0.0;
    case 10: return lhs <= rhs ? 1.0 : 0.0;
    case 11: return lhs == rhs ? 1.0 : 0.0;
    case 12: return lhs != rhs ? 1.0 : 0.0;
    case 13: return (lhs != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
    case 14: return (lhs != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
    default: return 0.0;
    }
}

double unaryValue(int op, double value)
{
    switch (op)
    {
    case 15: return sin(value);
    case 16: return cos(value);
    case 17: return tan(value);
    case 18: return log(value);
    case 19: return exp(value);
    case 20: return sqrt(value);
    default: return 0.0;
    }
}

__kernel void rasterizeFormula(
    __global const GpuFormulaInstruction *instructions,
    uint instructionCount,
    int xSlot,
    int ySlot,
    __global const double *xMin,
    __global const double *xMax,
    __global const double *yMin,
    __global const double *yMax,
    __global const uint *outputOffsets,
    uint pixelsPerAxis,
    uint tileCount,
    __global uchar *pixels)
{
    const uint pixelCount = pixelsPerAxis * pixelsPerAxis;
    const size_t globalIndex = get_global_id(0);
    const size_t totalPixels = (size_t)tileCount * (size_t)pixelCount;
    if (globalIndex >= totalPixels)
    {
        return;
    }

    const uint tileIndex = (uint)(globalIndex / pixelCount);
    const uint pixelIndex = (uint)(globalIndex - (size_t)tileIndex * pixelCount);
    const uint py = pixelIndex / pixelsPerAxis;
    const uint px = pixelIndex - py * pixelsPerAxis;
    const double dx = (xMax[tileIndex] - xMin[tileIndex]) / (double)pixelsPerAxis;
    const double dy = (yMax[tileIndex] - yMin[tileIndex]) / (double)pixelsPerAxis;
    const double sampleX = xMin[tileIndex] + ((double)px + 0.5) * dx;
    const double sampleY = yMin[tileIndex] + ((double)py + 0.5) * dy;

    double stack[64];
    int sp = 0;
    int valid = 1;
    for (uint i = 0; i < instructionCount && valid; ++i)
    {
        const GpuFormulaInstruction instruction = instructions[i];
        switch (instruction.op)
        {
        case 0:
            if (sp >= 64) { valid = 0; break; }
            stack[sp++] = instruction.constantValue;
            break;
        case 1:
            if (sp >= 64) { valid = 0; break; }
            stack[sp++] = instruction.variableSlot == xSlot
                ? sampleX
                : (instruction.variableSlot == ySlot ? sampleY : 0.0);
            break;
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
            if (sp < 1) { valid = 0; break; }
            stack[sp - 1] = unaryValue(instruction.op, stack[sp - 1]);
            break;
        default:
            if (sp < 2) { valid = 0; break; }
            {
                const double rhs = stack[--sp];
                const double lhs = stack[sp - 1];
                stack[sp - 1] = binaryValue(instruction.op, lhs, rhs);
            }
            break;
        }
    }

    const double result = valid && sp == 1 ? stack[0] : 0.0;
    pixels[outputOffsets[tileIndex] + pixelIndex] = result > 0.0 ? (uchar)255 : (uchar)0;
}
)CLC";
}

class OpenClRasterizer
{
public:
    ~OpenClRasterizer()
    {
        if (api && kernel)
        {
            api->clReleaseKernel(kernel);
        }
        if (api && program)
        {
            api->clReleaseProgram(program);
        }
        if (api && queue)
        {
            api->clReleaseCommandQueue(queue);
        }
        if (api && context)
        {
            api->clReleaseContext(context);
        }
    }

    [[nodiscard]] static std::unique_ptr<OpenClRasterizer> create()
    {
        auto api = OpenClApi::load();
        if (!api)
        {
            return {};
        }

        cl_uint platformCount = 0;
        if (api->clGetPlatformIDs(0, nullptr, &platformCount) != CL_SUCCESS || platformCount == 0)
        {
            return {};
        }
        std::vector<cl_platform_id> platforms(platformCount);
        if (api->clGetPlatformIDs(platformCount, platforms.data(), nullptr) != CL_SUCCESS)
        {
            return {};
        }

        for (const auto platform : platforms)
        {
            cl_uint deviceCount = 0;
            if (api->clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &deviceCount) != CL_SUCCESS
                || deviceCount == 0)
            {
                continue;
            }
            std::vector<cl_device_id> devices(deviceCount);
            if (api->clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, deviceCount, devices.data(), nullptr) != CL_SUCCESS)
            {
                continue;
            }
            for (const auto device : devices)
            {
                if (!deviceSupportsDouble(*api, device))
                {
                    continue;
                }
                if (auto rasterizer = createForDevice(api, device))
                {
                    return rasterizer;
                }
            }
        }
        return {};
    }

    [[nodiscard]] BatchResult rasterize(const RasterBatchView &batch, std::span<RegionOutput> out)
    {
        if (!batch.formula || batch.keys.size() != out.size()
            || batch.xMin.size() != batch.keys.size()
            || batch.xMax.size() != batch.keys.size()
            || batch.yMin.size() != batch.keys.size()
            || batch.yMax.size() != batch.keys.size()
            || batch.outputOffsets.size() != batch.keys.size()
            || batch.pixelsPerAxis == 0)
        {
            return invalidRasterBatch();
        }

        const auto depth = maxStackDepthFor(*batch.formula);
        if (!depth || *depth > MaxGpuFormulaStack)
        {
            return {false, 0, "Formula is too large for OpenCL raster stack"};
        }

        const auto tileCount = static_cast<uint32_t>(batch.keys.size());
        if (tileCount == 0)
        {
            return {true, 0, {}};
        }

        if (batch.cancelled && batch.cancelled())
        {
            return {false, 0, "Cancelled"};
        }

        const auto pixelsPerAxis = batch.pixelsPerAxis;
        const auto pixelsPerTile = pixelsPerAxis * pixelsPerAxis;
        auto outputPixelCount = size_t{0};
        for (size_t index = 0; index < batch.outputOffsets.size(); ++index)
        {
            outputPixelCount = std::max(
                outputPixelCount,
                static_cast<size_t>(batch.outputOffsets[index]) + static_cast<size_t>(pixelsPerTile));
        }

        std::vector<uint8_t> outputPixels(outputPixelCount, 0);
        auto instructions = lowerFormulaForGpu(*batch.formula);
        const auto xSlot = batch.formula->variableSlot("x")
            ? static_cast<int32_t>(*batch.formula->variableSlot("x"))
            : -1;
        const auto ySlot = batch.formula->variableSlot("y")
            ? static_cast<int32_t>(*batch.formula->variableSlot("y"))
            : -1;

        std::lock_guard lock(mutex);
        if (!kernel || !context || !queue)
        {
            return {false, 0, "OpenCL rasterizer is not initialized"};
        }

        auto error = CL_SUCCESS;
        auto makeReadOnlyBuffer = [&](const size_t bytes, const void *data) -> OpenClMem
        {
            auto nextError = CL_SUCCESS;
            auto buffer = api->clCreateBuffer(
                context,
                CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                bytes,
                const_cast<void *>(data),
                &nextError);
            if (nextError != CL_SUCCESS)
            {
                error = nextError;
                return {};
            }
            return OpenClMem{api, buffer};
        };

        auto instructionBuffer = makeReadOnlyBuffer(
            instructions.size() * sizeof(GpuFormulaInstruction),
            instructions.data());
        auto xMinBuffer = makeReadOnlyBuffer(batch.xMin.size_bytes(), batch.xMin.data());
        auto xMaxBuffer = makeReadOnlyBuffer(batch.xMax.size_bytes(), batch.xMax.data());
        auto yMinBuffer = makeReadOnlyBuffer(batch.yMin.size_bytes(), batch.yMin.data());
        auto yMaxBuffer = makeReadOnlyBuffer(batch.yMax.size_bytes(), batch.yMax.data());
        auto offsetBuffer = makeReadOnlyBuffer(batch.outputOffsets.size_bytes(), batch.outputOffsets.data());

        auto outputBufferError = CL_SUCCESS;
        OpenClMem outputBuffer{
            api,
            api->clCreateBuffer(context, CL_MEM_WRITE_ONLY, outputPixels.size(), nullptr, &outputBufferError)
        };
        if (outputBufferError != CL_SUCCESS)
        {
            error = outputBufferError;
        }

        if (error != CL_SUCCESS)
        {
            return {false, 0, "OpenCL buffer allocation failed"};
        }

        const auto instructionCount = static_cast<uint32_t>(instructions.size());
        const std::array args{
            KernelArg{sizeof(cl_mem), &instructionBuffer.value},
            KernelArg{sizeof(uint32_t), &instructionCount},
            KernelArg{sizeof(int32_t), &xSlot},
            KernelArg{sizeof(int32_t), &ySlot},
            KernelArg{sizeof(cl_mem), &xMinBuffer.value},
            KernelArg{sizeof(cl_mem), &xMaxBuffer.value},
            KernelArg{sizeof(cl_mem), &yMinBuffer.value},
            KernelArg{sizeof(cl_mem), &yMaxBuffer.value},
            KernelArg{sizeof(cl_mem), &offsetBuffer.value},
            KernelArg{sizeof(uint32_t), &pixelsPerAxis},
            KernelArg{sizeof(uint32_t), &tileCount},
            KernelArg{sizeof(cl_mem), &outputBuffer.value}
        };

        for (cl_uint index = 0; index < args.size(); ++index)
        {
            if (api->clSetKernelArg(kernel, index, args[index].size, args[index].value) != CL_SUCCESS)
            {
                return {false, 0, "OpenCL kernel argument setup failed"};
            }
        }

        const auto globalWorkItems = static_cast<size_t>(tileCount) * static_cast<size_t>(pixelsPerTile);
        if (api->clEnqueueNDRangeKernel(
                queue,
                kernel,
                1,
                nullptr,
                &globalWorkItems,
                nullptr,
                0,
                nullptr,
                nullptr) != CL_SUCCESS)
        {
            return {false, 0, "OpenCL raster kernel enqueue failed"};
        }

        if (api->clEnqueueReadBuffer(
                queue,
                outputBuffer.value,
                CL_TRUE,
                0,
                outputPixels.size(),
                outputPixels.data(),
                0,
                nullptr,
                nullptr) != CL_SUCCESS)
        {
            return {false, 0, "OpenCL raster readback failed"};
        }
        (void)api->clFinish(queue);

        for (size_t index = 0; index < batch.keys.size(); ++index)
        {
            auto &output = out[index];
            output.key = batch.keys[index];
            output.width = pixelsPerAxis;
            output.height = pixelsPerAxis;
            const auto offset = static_cast<size_t>(batch.outputOffsets[index]);
            output.pixels.assign(
                outputPixels.begin() + static_cast<std::ptrdiff_t>(offset),
                outputPixels.begin() + static_cast<std::ptrdiff_t>(offset + pixelsPerTile));
        }

        return {true, batch.keys.size(), {}};
    }

private:
    struct KernelArg
    {
        size_t size{0};
        const void *value{nullptr};
    };

    [[nodiscard]] static bool deviceSupportsDouble(OpenClApi &api, const cl_device_id device)
    {
        cl_ulong doubleConfig = 0;
        return api.clGetDeviceInfo(
                   device,
                   CL_DEVICE_DOUBLE_FP_CONFIG,
                   sizeof(doubleConfig),
                   &doubleConfig,
                   nullptr) == CL_SUCCESS
            && doubleConfig != 0;
    }

    [[nodiscard]] static std::unique_ptr<OpenClRasterizer> createForDevice(
        std::shared_ptr<OpenClApi> api,
        const cl_device_id device)
    {
        auto error = CL_SUCCESS;
        auto context = api->clCreateContext(nullptr, 1, &device, nullptr, nullptr, &error);
        if (error != CL_SUCCESS || !context)
        {
            return {};
        }

        auto queue = api->clCreateCommandQueue(context, device, 0, &error);
        if (error != CL_SUCCESS || !queue)
        {
            api->clReleaseContext(context);
            return {};
        }

        const auto source = openClRasterKernelSource();
        const char *sourceData = source.data();
        const auto sourceSize = source.size();
        auto program = api->clCreateProgramWithSource(context, 1, &sourceData, &sourceSize, &error);
        if (error != CL_SUCCESS || !program)
        {
            api->clReleaseCommandQueue(queue);
            api->clReleaseContext(context);
            return {};
        }

        error = api->clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
        if (error != CL_SUCCESS)
        {
            api->clReleaseProgram(program);
            api->clReleaseCommandQueue(queue);
            api->clReleaseContext(context);
            return {};
        }

        auto kernel = api->clCreateKernel(program, "rasterizeFormula", &error);
        if (error != CL_SUCCESS || !kernel)
        {
            api->clReleaseProgram(program);
            api->clReleaseCommandQueue(queue);
            api->clReleaseContext(context);
            return {};
        }

        auto rasterizer = std::unique_ptr<OpenClRasterizer>(new OpenClRasterizer);
        rasterizer->api = std::move(api);
        rasterizer->device = device;
        rasterizer->context = context;
        rasterizer->queue = queue;
        rasterizer->program = program;
        rasterizer->kernel = kernel;
        return rasterizer;
    }

    std::shared_ptr<OpenClApi> api;
    cl_device_id device{nullptr};
    cl_context context{nullptr};
    cl_command_queue queue{nullptr};
    cl_program program{nullptr};
    cl_kernel kernel{nullptr};
    std::mutex mutex;
};

class OpenClPreferredComputeBackend final : public ComputeBackend
{
public:
    explicit OpenClPreferredComputeBackend(std::unique_ptr<OpenClRasterizer> nextRasterizer)
        : rasterizer{std::move(nextRasterizer)}
    {
    }

    [[nodiscard]] BackendCapabilities capabilities() const override
    {
        return {
            .supportsIntervalClassification = true,
            .supportsRegionRaster = true,
            .supportsContourExtraction = false,
            .supportsOpenCl = rasterizer != nullptr
        };
    }

    BatchResult classifyIntervals(const IntervalBatchView &batch,
                                  std::span<TileClassificationResult> out) override
    {
        return cpu.classifyIntervals(batch, out);
    }

    BatchResult rasterizeRegions(const RasterBatchView &batch,
                                 std::span<RegionOutput> out) override
    {
        if (!loggedBackendMode)
        {
            PipelineLog::log("compute.backend opencl=%d", rasterizer ? 1 : 0);
            loggedBackendMode = true;
        }

        if (rasterizer)
        {
            const auto gpuResult = rasterizer->rasterize(batch, out);
            if (gpuResult.ok || gpuResult.message == "Invalid raster batch shape")
            {
                if (gpuResult.ok)
                {
                    const auto stats = summarizeRasterOutputs(out, gpuResult.completed);
                    if (loggedRasterBatches < 80 || stats.zeroTiles == gpuResult.completed)
                    {
                        PipelineLog::log(
                            "compute.raster backend=opencl batch=%zu completed=%zu pixels=%u zero=%zu full=%zu mixed=%zu formula=%llu firstTile=%s",
                            batch.keys.size(),
                            gpuResult.completed,
                            batch.pixelsPerAxis,
                            stats.zeroTiles,
                            stats.fullTiles,
                            stats.mixedTiles,
                            batch.formula ? static_cast<unsigned long long>(batch.formula->handle.semanticsHash.value) : 0ull,
                            batch.keys.empty() ? "(none)" : toDebugString(batch.keys.front()).c_str());
                        ++loggedRasterBatches;
                    }
                }
                return gpuResult;
            }
            PipelineLog::log(
                "compute.raster backend=opencl rejected batch=%zu reason=%s formula=%llu firstTile=%s",
                batch.keys.size(),
                gpuResult.message.c_str(),
                batch.formula ? static_cast<unsigned long long>(batch.formula->handle.semanticsHash.value) : 0ull,
                batch.keys.empty() ? "(none)" : toDebugString(batch.keys.front()).c_str());
        }
        const auto cpuResult = cpu.rasterizeRegions(batch, out);
        if (cpuResult.ok)
        {
            const auto stats = summarizeRasterOutputs(out, cpuResult.completed);
            if (loggedRasterBatches < 80 || stats.zeroTiles == cpuResult.completed)
            {
                PipelineLog::log(
                    "compute.raster backend=cpu batch=%zu completed=%zu pixels=%u zero=%zu full=%zu mixed=%zu formula=%llu firstTile=%s",
                    batch.keys.size(),
                    cpuResult.completed,
                    batch.pixelsPerAxis,
                    stats.zeroTiles,
                    stats.fullTiles,
                    stats.mixedTiles,
                    batch.formula ? static_cast<unsigned long long>(batch.formula->handle.semanticsHash.value) : 0ull,
                    batch.keys.empty() ? "(none)" : toDebugString(batch.keys.front()).c_str());
                ++loggedRasterBatches;
            }
        }
        return cpuResult;
    }

private:
    CpuComputeBackend cpu;
    std::unique_ptr<OpenClRasterizer> rasterizer;
    bool loggedBackendMode{false};
    size_t loggedRasterBatches{0};
};
#endif
}

std::unique_ptr<ComputeBackend> makeDefaultComputeBackend()
{
#ifdef _WIN32
    return std::make_unique<OpenClPreferredComputeBackend>(OpenClRasterizer::create());
#else
    return std::make_unique<CpuComputeBackend>();
#endif
}
}
