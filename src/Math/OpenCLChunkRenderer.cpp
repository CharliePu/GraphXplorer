//
// Created by Codex on 2/27/2026.
//

#include "OpenCLChunkRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "../Formula/Formula.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(_WIN32)
namespace
{
    using cl_int = int32_t;
    using cl_uint = uint32_t;
    using cl_ulong = uint64_t;
    using cl_long = int64_t;
    using cl_bitfield = cl_ulong;
    using cl_bool = cl_uint;
    using cl_device_type = cl_bitfield;
    using cl_mem_flags = cl_bitfield;
    using cl_context_properties = intptr_t;
    using cl_program_build_info = cl_uint;
    using cl_command_queue_properties = cl_bitfield;

    struct _cl_platform_id;
    struct _cl_device_id;
    struct _cl_context;
    struct _cl_command_queue;
    struct _cl_program;
    struct _cl_kernel;
    struct _cl_mem;

    using cl_platform_id = _cl_platform_id *;
    using cl_device_id = _cl_device_id *;
    using cl_context = _cl_context *;
    using cl_command_queue = _cl_command_queue *;
    using cl_program = _cl_program *;
    using cl_kernel = _cl_kernel *;
    using cl_mem = _cl_mem *;

    constexpr cl_int CL_SUCCESS = 0;
    constexpr cl_int CL_DEVICE_NOT_FOUND = -1;

    constexpr cl_uint CL_TRUE = 1;

    constexpr cl_device_type CL_DEVICE_TYPE_DEFAULT = 1u << 0;
    constexpr cl_device_type CL_DEVICE_TYPE_CPU = 1u << 1;
    constexpr cl_device_type CL_DEVICE_TYPE_GPU = 1u << 2;

    constexpr cl_mem_flags CL_MEM_WRITE_ONLY = 1u << 1;
    constexpr cl_mem_flags CL_MEM_READ_ONLY = 1u << 2;
    constexpr cl_mem_flags CL_MEM_COPY_HOST_PTR = 1u << 5;

    constexpr cl_program_build_info CL_PROGRAM_BUILD_LOG = 0x1183;
    constexpr cl_context_properties CL_CONTEXT_PLATFORM = 0x1084;

    using clGetPlatformIDs_fn = cl_int(WINAPI *)(cl_uint, cl_platform_id *, cl_uint *);
    using clGetDeviceIDs_fn = cl_int(WINAPI *)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
    using clCreateContext_fn = cl_context(WINAPI *)(
        const cl_context_properties *,
        cl_uint,
        const cl_device_id *,
        void (WINAPI *)(const char *, const void *, size_t, void *),
        void *,
        cl_int *);
    using clCreateCommandQueue_fn = cl_command_queue(WINAPI *)(
        cl_context,
        cl_device_id,
        cl_command_queue_properties,
        cl_int *);
    using clCreateProgramWithSource_fn = cl_program(WINAPI *)(
        cl_context,
        cl_uint,
        const char **,
        const size_t *,
        cl_int *);
    using clBuildProgram_fn = cl_int(WINAPI *)(
        cl_program,
        cl_uint,
        const cl_device_id *,
        const char *,
        void (WINAPI *)(cl_program, void *),
        void *);
    using clGetProgramBuildInfo_fn = cl_int(WINAPI *)(
        cl_program,
        cl_device_id,
        cl_program_build_info,
        size_t,
        void *,
        size_t *);
    using clCreateKernel_fn = cl_kernel(WINAPI *)(cl_program, const char *, cl_int *);
    using clCreateBuffer_fn = cl_mem(WINAPI *)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
    using clSetKernelArg_fn = cl_int(WINAPI *)(cl_kernel, cl_uint, size_t, const void *);
    using clEnqueueNDRangeKernel_fn = cl_int(WINAPI *)(
        cl_command_queue,
        cl_kernel,
        cl_uint,
        const size_t *,
        const size_t *,
        const size_t *,
        cl_uint,
        const void *,
        void *);
    using clEnqueueReadBuffer_fn = cl_int(WINAPI *)(
        cl_command_queue,
        cl_mem,
        cl_bool,
        size_t,
        size_t,
        void *,
        cl_uint,
        const void *,
        void *);
    using clFinish_fn = cl_int(WINAPI *)(cl_command_queue);
    using clReleaseMemObject_fn = cl_int(WINAPI *)(cl_mem);
    using clReleaseKernel_fn = cl_int(WINAPI *)(cl_kernel);
    using clReleaseProgram_fn = cl_int(WINAPI *)(cl_program);
    using clReleaseCommandQueue_fn = cl_int(WINAPI *)(cl_command_queue);
    using clReleaseContext_fn = cl_int(WINAPI *)(cl_context);

    struct OpenCLApi
    {
        clGetPlatformIDs_fn clGetPlatformIDs{nullptr};
        clGetDeviceIDs_fn clGetDeviceIDs{nullptr};
        clCreateContext_fn clCreateContext{nullptr};
        clCreateCommandQueue_fn clCreateCommandQueue{nullptr};
        clCreateProgramWithSource_fn clCreateProgramWithSource{nullptr};
        clBuildProgram_fn clBuildProgram{nullptr};
        clGetProgramBuildInfo_fn clGetProgramBuildInfo{nullptr};
        clCreateKernel_fn clCreateKernel{nullptr};
        clCreateBuffer_fn clCreateBuffer{nullptr};
        clSetKernelArg_fn clSetKernelArg{nullptr};
        clEnqueueNDRangeKernel_fn clEnqueueNDRangeKernel{nullptr};
        clEnqueueReadBuffer_fn clEnqueueReadBuffer{nullptr};
        clFinish_fn clFinish{nullptr};
        clReleaseMemObject_fn clReleaseMemObject{nullptr};
        clReleaseKernel_fn clReleaseKernel{nullptr};
        clReleaseProgram_fn clReleaseProgram{nullptr};
        clReleaseCommandQueue_fn clReleaseCommandQueue{nullptr};
        clReleaseContext_fn clReleaseContext{nullptr};
    };

    template <typename T>
    bool loadProc(const HMODULE module, const char *name, T &fn)
    {
        fn = reinterpret_cast<T>(GetProcAddress(module, name));
        return fn != nullptr;
    }

    constexpr const char *kKernelSource = R"CLC(
__kernel void rasterize_unresolved(
    __global const int* chunk_states,
    int chunk_width,
    int chunk_height,
    float x_lower,
    float y_lower,
    float delta_x,
    float delta_y,
    int target_level,
    long min_chunk_x,
    long min_chunk_y,
    int window_width,
    int window_height,
    __global int* output_image)
{
    int pixel_x = get_global_id(0);
    int pixel_y = get_global_id(1);

    if (pixel_x >= window_width || pixel_y >= window_height)
    {
        return;
    }

    float sample_x = x_lower + ((float)pixel_x + 0.5f) * delta_x;
    float sample_y = y_lower + ((float)pixel_y + 0.5f) * delta_y;
    float chunk_size = exp2((float)target_level);

    long chunk_x = (long)floor(sample_x / chunk_size);
    long chunk_y = (long)floor(sample_y / chunk_size);

    int out_index = pixel_y * window_width + pixel_x;

    long local_x = chunk_x - min_chunk_x;
    long local_y = chunk_y - min_chunk_y;
    if (local_x < 0 || local_x >= (long)chunk_width || local_y < 0 || local_y >= (long)chunk_height)
    {
        output_image[out_index] = 0;
        return;
    }

    int chunk_index = (int)(local_y * (long)chunk_width + local_x);
    int state = chunk_states[chunk_index];
    output_image[out_index] = state < 0 ? -1 : 0;
}
)CLC";

    std::string toOpenClFloatLiteral(const std::string &numberToken)
    {
        if (numberToken.empty())
        {
            return "0.0f";
        }

        auto literal = numberToken;
        if (literal.back() == 'f' || literal.back() == 'F')
        {
            return literal;
        }

        if (literal.find_first_of(".eE") == std::string::npos)
        {
            literal += ".0";
        }

        literal += "f";
        return literal;
    }

    bool buildOpenClExpressionFromRpn(const RPN &rpn, std::string &outputExpression)
    {
        std::vector<std::string> stack;
        stack.reserve(rpn.tokens.size());

        const auto popBinary = [&stack](std::string &lhs, std::string &rhs) -> bool
        {
            if (stack.size() < 2)
            {
                return false;
            }

            rhs = std::move(stack.back());
            stack.pop_back();
            lhs = std::move(stack.back());
            stack.pop_back();
            return true;
        };

        for (const auto &token : rpn.tokens)
        {
            switch (token.type)
            {
            case TokenType::NUMBER:
                stack.push_back(toOpenClFloatLiteral(token.value));
                break;
            case TokenType::VARIABLE:
                if (token.value == "x" || token.value == "y")
                {
                    stack.push_back(token.value);
                    break;
                }

                return false;
            case TokenType::OPERATOR:
            {
                std::string lhs;
                std::string rhs;
                if (!popBinary(lhs, rhs))
                {
                    return false;
                }

                if (token.value == "+")
                {
                    stack.push_back("(" + lhs + " + " + rhs + ")");
                }
                else if (token.value == "-")
                {
                    stack.push_back("(" + lhs + " - " + rhs + ")");
                }
                else if (token.value == "*")
                {
                    stack.push_back("(" + lhs + " * " + rhs + ")");
                }
                else if (token.value == "/")
                {
                    stack.push_back("(" + lhs + " / " + rhs + ")");
                }
                else if (token.value == "^")
                {
                    stack.push_back("pow(" + lhs + ", " + rhs + ")");
                }
                else if (token.value == ">")
                {
                    stack.push_back("((" + lhs + " > " + rhs + ") ? 1.0f : 0.0f)");
                }
                else if (token.value == "<")
                {
                    stack.push_back("((" + lhs + " < " + rhs + ") ? 1.0f : 0.0f)");
                }
                else if (token.value == ">=")
                {
                    stack.push_back("((" + lhs + " >= " + rhs + ") ? 1.0f : 0.0f)");
                }
                else if (token.value == "<=")
                {
                    stack.push_back("((" + lhs + " <= " + rhs + ") ? 1.0f : 0.0f)");
                }
                else if (token.value == "=")
                {
                    stack.push_back("((" + lhs + " == " + rhs + ") ? 1.0f : 0.0f)");
                }
                else if (token.value == "!=")
                {
                    stack.push_back("((" + lhs + " != " + rhs + ") ? 1.0f : 0.0f)");
                }
                else if (token.value == "&&")
                {
                    stack.push_back(
                        "((((" + lhs + ") > 0.0f) && ((" + rhs + ") > 0.0f)) ? 1.0f : 0.0f)");
                }
                else if (token.value == "||")
                {
                    stack.push_back(
                        "((((" + lhs + ") > 0.0f) || ((" + rhs + ") > 0.0f)) ? 1.0f : 0.0f)");
                }
                else
                {
                    return false;
                }
                break;
            }
            case TokenType::FUNCTION:
            {
                if (stack.empty())
                {
                    return false;
                }

                auto argument = std::move(stack.back());
                stack.pop_back();

                if (token.value == "sin" || token.value == "cos" || token.value == "tan"
                    || token.value == "log" || token.value == "exp" || token.value == "sqrt")
                {
                    stack.push_back(token.value + "(" + argument + ")");
                    break;
                }

                return false;
            }
            default:
                return false;
            }
        }

        if (stack.size() != 1)
        {
            return false;
        }

        outputExpression = std::move(stack.back());
        return true;
    }

    std::string buildMixedChunkKernelSource(const std::string &openClExpression)
    {
        return "__kernel void rasterize_mixed_chunk_texture(\n"
            "    float x_lower,\n"
            "    float y_lower,\n"
            "    float delta_x,\n"
            "    float delta_y,\n"
            "    int texture_size,\n"
            "    __global int* output_pixels)\n"
            "{\n"
            "    int px = get_global_id(0);\n"
            "    int py = get_global_id(1);\n"
            "    if (px >= texture_size || py >= texture_size)\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "\n"
            "    float x = x_lower + ((float)px + 0.5f) * delta_x;\n"
            "    float y = y_lower + ((float)py + 0.5f) * delta_y;\n"
            "    float value = " + openClExpression + ";\n"
            "    int idx = py * texture_size + px;\n"
            "    output_pixels[idx] = value > 0.0f ? 1 : 0;\n"
            "}\n";
    }

    std::string buildContourSegmentsKernelSource(const std::string &openClExpression)
    {
        return
            "inline float2 interpolate_zero(float v0, float v1, float2 p0, float2 p1)\n"
            "{\n"
            "    float t = 0.5f;\n"
            "    float denom = v0 - v1;\n"
            "    if (isfinite(denom) && fabs(denom) > 1.0e-12f)\n"
            "    {\n"
            "        t = clamp(v0 / denom, 0.0f, 1.0f);\n"
            "    }\n"
            "    return (float2)(p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t);\n"
            "}\n"
            "\n"
            "inline void write_segment(__global float* output_segments, int base, int segment_index, float2 p0, float2 p1)\n"
            "{\n"
            "    int offset = base + segment_index * 4;\n"
            "    output_segments[offset + 0] = p0.x;\n"
            "    output_segments[offset + 1] = p0.y;\n"
            "    output_segments[offset + 2] = p1.x;\n"
            "    output_segments[offset + 3] = p1.y;\n"
            "}\n"
            "\n"
            "__kernel void rasterize_contour_segments(\n"
            "    float x_lower,\n"
            "    float y_lower,\n"
            "    float cell_step_x,\n"
            "    float cell_step_y,\n"
            "    int cells_per_axis,\n"
            "    __global int* output_counts,\n"
            "    __global float* output_segments)\n"
            "{\n"
            "    int cell_x = get_global_id(0);\n"
            "    int cell_y = get_global_id(1);\n"
            "    if (cell_x >= cells_per_axis || cell_y >= cells_per_axis)\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "\n"
            "    int cell_index = cell_y * cells_per_axis + cell_x;\n"
            "    int base = cell_index * 8;\n"
            "    output_counts[cell_index] = 0;\n"
            "    for (int i = 0; i < 8; ++i)\n"
            "    {\n"
            "        output_segments[base + i] = 0.0f;\n"
            "    }\n"
            "\n"
            "    float cell_min_x = x_lower + (float)cell_x * cell_step_x;\n"
            "    float cell_max_x = cell_min_x + cell_step_x;\n"
            "    float cell_min_y = y_lower + (float)cell_y * cell_step_y;\n"
            "    float cell_max_y = cell_min_y + cell_step_y;\n"
            "\n"
            "    float x = cell_min_x;\n"
            "    float y = cell_min_y;\n"
            "    float v0 = " + openClExpression + ";\n"
            "    x = cell_max_x;\n"
            "    y = cell_min_y;\n"
            "    float v1 = " + openClExpression + ";\n"
            "    x = cell_max_x;\n"
            "    y = cell_max_y;\n"
            "    float v2 = " + openClExpression + ";\n"
            "    x = cell_min_x;\n"
            "    y = cell_max_y;\n"
            "    float v3 = " + openClExpression + ";\n"
            "\n"
            "    if (!isfinite(v0) || !isfinite(v1) || !isfinite(v2) || !isfinite(v3))\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "\n"
            "    int mask = (v0 > 0.0f ? 1 : 0)\n"
            "             | (v1 > 0.0f ? 2 : 0)\n"
            "             | (v2 > 0.0f ? 4 : 0)\n"
            "             | (v3 > 0.0f ? 8 : 0);\n"
            "    if (mask == 0 || mask == 15)\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "\n"
            "    float2 edge0 = interpolate_zero(v0, v1, (float2)(cell_min_x, cell_min_y), (float2)(cell_max_x, cell_min_y));\n"
            "    float2 edge1 = interpolate_zero(v1, v2, (float2)(cell_max_x, cell_min_y), (float2)(cell_max_x, cell_max_y));\n"
            "    float2 edge2 = interpolate_zero(v2, v3, (float2)(cell_max_x, cell_max_y), (float2)(cell_min_x, cell_max_y));\n"
            "    float2 edge3 = interpolate_zero(v3, v0, (float2)(cell_min_x, cell_max_y), (float2)(cell_min_x, cell_min_y));\n"
            "\n"
            "    int count = 0;\n"
            "    switch (mask)\n"
            "    {\n"
            "    case 1:\n"
            "    case 14:\n"
            "        write_segment(output_segments, base, count++, edge3, edge0);\n"
            "        break;\n"
            "    case 2:\n"
            "    case 13:\n"
            "        write_segment(output_segments, base, count++, edge0, edge1);\n"
            "        break;\n"
            "    case 3:\n"
            "    case 12:\n"
            "        write_segment(output_segments, base, count++, edge3, edge1);\n"
            "        break;\n"
            "    case 4:\n"
            "    case 11:\n"
            "        write_segment(output_segments, base, count++, edge1, edge2);\n"
            "        break;\n"
            "    case 5:\n"
            "        write_segment(output_segments, base, count++, edge3, edge2);\n"
            "        write_segment(output_segments, base, count++, edge0, edge1);\n"
            "        break;\n"
            "    case 6:\n"
            "    case 9:\n"
            "        write_segment(output_segments, base, count++, edge0, edge2);\n"
            "        break;\n"
            "    case 7:\n"
            "    case 8:\n"
            "        write_segment(output_segments, base, count++, edge3, edge2);\n"
            "        break;\n"
            "    case 10:\n"
            "        write_segment(output_segments, base, count++, edge0, edge1);\n"
            "        write_segment(output_segments, base, count++, edge2, edge3);\n"
            "        break;\n"
            "    default:\n"
            "        break;\n"
            "    }\n"
            "\n"
            "    output_counts[cell_index] = count;\n"
            "}\n";
    }
}
#endif

struct OpenCLChunkRenderer::Impl
{
    bool available{false};

#if defined(_WIN32)
    HMODULE module{nullptr};
    OpenCLApi api{};

    cl_context context{nullptr};
    cl_command_queue queue{nullptr};
    cl_program program{nullptr};
    cl_kernel kernel{nullptr};
    cl_program mixedTextureProgram{nullptr};
    cl_kernel mixedTextureKernel{nullptr};
    cl_program contourSegmentsProgram{nullptr};
    cl_kernel contourSegmentsKernel{nullptr};
    cl_device_id device{nullptr};
    cl_platform_id platform{nullptr};
    std::string compiledMixedFormula;
    std::string compiledContourFormula;

    bool initialize()
    {
        module = LoadLibraryA("OpenCL.dll");
        if (!module)
        {
            return false;
        }

        if (!loadApi())
        {
            shutdown();
            return false;
        }

        if (!createContextAndKernel())
        {
            shutdown();
            return false;
        }

        available = true;
        return true;
    }

    bool loadApi()
    {
        return loadProc(module, "clGetPlatformIDs", api.clGetPlatformIDs)
               && loadProc(module, "clGetDeviceIDs", api.clGetDeviceIDs)
               && loadProc(module, "clCreateContext", api.clCreateContext)
               && loadProc(module, "clCreateCommandQueue", api.clCreateCommandQueue)
               && loadProc(module, "clCreateProgramWithSource", api.clCreateProgramWithSource)
               && loadProc(module, "clBuildProgram", api.clBuildProgram)
               && loadProc(module, "clGetProgramBuildInfo", api.clGetProgramBuildInfo)
               && loadProc(module, "clCreateKernel", api.clCreateKernel)
               && loadProc(module, "clCreateBuffer", api.clCreateBuffer)
               && loadProc(module, "clSetKernelArg", api.clSetKernelArg)
               && loadProc(module, "clEnqueueNDRangeKernel", api.clEnqueueNDRangeKernel)
               && loadProc(module, "clEnqueueReadBuffer", api.clEnqueueReadBuffer)
               && loadProc(module, "clFinish", api.clFinish)
               && loadProc(module, "clReleaseMemObject", api.clReleaseMemObject)
               && loadProc(module, "clReleaseKernel", api.clReleaseKernel)
               && loadProc(module, "clReleaseProgram", api.clReleaseProgram)
               && loadProc(module, "clReleaseCommandQueue", api.clReleaseCommandQueue)
               && loadProc(module, "clReleaseContext", api.clReleaseContext);
    }

    bool createContextAndKernel()
    {
        cl_uint platformCount = 0;
        if (api.clGetPlatformIDs(0, nullptr, &platformCount) != CL_SUCCESS || platformCount == 0)
        {
            return false;
        }

        std::vector<cl_platform_id> platforms(platformCount);
        if (api.clGetPlatformIDs(platformCount, platforms.data(), nullptr) != CL_SUCCESS)
        {
            return false;
        }

        bool foundDevice = false;
        for (const auto candidatePlatform : platforms)
        {
            cl_int result = api.clGetDeviceIDs(candidatePlatform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
            if (result == CL_SUCCESS)
            {
                platform = candidatePlatform;
                foundDevice = true;
                break;
            }
            if (result != CL_DEVICE_NOT_FOUND)
            {
                continue;
            }
        }

        if (!foundDevice)
        {
            for (const auto candidatePlatform : platforms)
            {
                cl_int result = api.clGetDeviceIDs(candidatePlatform, CL_DEVICE_TYPE_CPU, 1, &device, nullptr);
                if (result == CL_SUCCESS)
                {
                    platform = candidatePlatform;
                    foundDevice = true;
                    break;
                }
                if (result != CL_DEVICE_NOT_FOUND)
                {
                    continue;
                }
            }
        }

        if (!foundDevice)
        {
            for (const auto candidatePlatform : platforms)
            {
                cl_int result = api.clGetDeviceIDs(candidatePlatform, CL_DEVICE_TYPE_DEFAULT, 1, &device, nullptr);
                if (result == CL_SUCCESS)
                {
                    platform = candidatePlatform;
                    foundDevice = true;
                    break;
                }
                if (result != CL_DEVICE_NOT_FOUND)
                {
                    continue;
                }
            }
        }

        if (!foundDevice)
        {
            return false;
        }

        cl_int error = CL_SUCCESS;
        std::array<cl_context_properties, 3> properties{
            CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platform), 0
        };
        context = api.clCreateContext(properties.data(), 1, &device, nullptr, nullptr, &error);
        if (error != CL_SUCCESS || !context)
        {
            return false;
        }

        queue = api.clCreateCommandQueue(context, device, 0, &error);
        if (error != CL_SUCCESS || !queue)
        {
            return false;
        }

        const char *source = kKernelSource;
        const size_t sourceLength = std::char_traits<char>::length(source);
        program = api.clCreateProgramWithSource(context, 1, &source, &sourceLength, &error);
        if (error != CL_SUCCESS || !program)
        {
            return false;
        }

        error = api.clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
        if (error != CL_SUCCESS)
        {
            return false;
        }

        kernel = api.clCreateKernel(program, "rasterize_unresolved", &error);
        return error == CL_SUCCESS && kernel != nullptr;
    }

    [[nodiscard]] std::string getProgramBuildLog(const cl_program targetProgram) const
    {
        if (!targetProgram)
        {
            return {};
        }

        size_t logSize = 0;
        if (api.clGetProgramBuildInfo(targetProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize)
            != CL_SUCCESS
            || logSize == 0)
        {
            return {};
        }

        std::string log(logSize, '\0');
        if (api.clGetProgramBuildInfo(targetProgram, device, CL_PROGRAM_BUILD_LOG, log.size(), log.data(), nullptr)
            != CL_SUCCESS)
        {
            return {};
        }

        while (!log.empty() && (log.back() == '\0' || log.back() == '\n' || log.back() == '\r'))
        {
            log.pop_back();
        }
        return log;
    }

    void releaseMixedTextureKernel()
    {
        compiledMixedFormula.clear();

        if (mixedTextureKernel && api.clReleaseKernel)
        {
            api.clReleaseKernel(mixedTextureKernel);
            mixedTextureKernel = nullptr;
        }

        if (mixedTextureProgram && api.clReleaseProgram)
        {
            api.clReleaseProgram(mixedTextureProgram);
            mixedTextureProgram = nullptr;
        }
    }

    void releaseContourSegmentsKernel()
    {
        compiledContourFormula.clear();

        if (contourSegmentsKernel && api.clReleaseKernel)
        {
            api.clReleaseKernel(contourSegmentsKernel);
            contourSegmentsKernel = nullptr;
        }

        if (contourSegmentsProgram && api.clReleaseProgram)
        {
            api.clReleaseProgram(contourSegmentsProgram);
            contourSegmentsProgram = nullptr;
        }
    }

    bool ensureMixedTextureKernel(const std::string &formulaSource)
    {
        if (formulaSource.empty())
        {
            return false;
        }

        if (mixedTextureKernel && compiledMixedFormula == formulaSource)
        {
            return true;
        }

        releaseMixedTextureKernel();

        const auto kernelSource = buildMixedChunkKernelSource(formulaSource);
        const char *source = kernelSource.c_str();
        const size_t sourceLength = kernelSource.size();

        cl_int error = CL_SUCCESS;
        mixedTextureProgram = api.clCreateProgramWithSource(context, 1, &source, &sourceLength, &error);
        if (error != CL_SUCCESS || !mixedTextureProgram)
        {
            return false;
        }

        error = api.clBuildProgram(mixedTextureProgram, 1, &device, nullptr, nullptr, nullptr);
        if (error != CL_SUCCESS)
        {
            std::cerr << "[OpenCLChunkRenderer] Failed to build mixed-texture kernel.\n"
                      << getProgramBuildLog(mixedTextureProgram) << std::endl;
            releaseMixedTextureKernel();
            return false;
        }

        mixedTextureKernel = api.clCreateKernel(mixedTextureProgram, "rasterize_mixed_chunk_texture", &error);
        if (error != CL_SUCCESS || !mixedTextureKernel)
        {
            releaseMixedTextureKernel();
            return false;
        }

        compiledMixedFormula = formulaSource;
        return true;
    }

    bool ensureContourSegmentsKernel(const std::string &formulaSource)
    {
        if (formulaSource.empty())
        {
            return false;
        }

        if (contourSegmentsKernel && compiledContourFormula == formulaSource)
        {
            return true;
        }

        releaseContourSegmentsKernel();

        const auto kernelSource = buildContourSegmentsKernelSource(formulaSource);
        const char *source = kernelSource.c_str();
        const size_t sourceLength = kernelSource.size();

        cl_int error = CL_SUCCESS;
        contourSegmentsProgram = api.clCreateProgramWithSource(context, 1, &source, &sourceLength, &error);
        if (error != CL_SUCCESS || !contourSegmentsProgram)
        {
            return false;
        }

        error = api.clBuildProgram(contourSegmentsProgram, 1, &device, nullptr, nullptr, nullptr);
        if (error != CL_SUCCESS)
        {
            std::cerr << "[OpenCLChunkRenderer] Failed to build contour-segments kernel.\n"
                      << getProgramBuildLog(contourSegmentsProgram) << std::endl;
            releaseContourSegmentsKernel();
            return false;
        }

        contourSegmentsKernel = api.clCreateKernel(contourSegmentsProgram, "rasterize_contour_segments", &error);
        if (error != CL_SUCCESS || !contourSegmentsKernel)
        {
            releaseContourSegmentsKernel();
            return false;
        }

        compiledContourFormula = formulaSource;
        return true;
    }

    void shutdown()
    {
        available = false;

        releaseMixedTextureKernel();
        releaseContourSegmentsKernel();

        if (kernel && api.clReleaseKernel)
        {
            api.clReleaseKernel(kernel);
            kernel = nullptr;
        }

        if (program && api.clReleaseProgram)
        {
            api.clReleaseProgram(program);
            program = nullptr;
        }

        if (queue && api.clReleaseCommandQueue)
        {
            api.clReleaseCommandQueue(queue);
            queue = nullptr;
        }

        if (context && api.clReleaseContext)
        {
            api.clReleaseContext(context);
            context = nullptr;
        }

        if (module)
        {
            FreeLibrary(module);
            module = nullptr;
        }
    }
#else
    bool initialize()
    {
        available = false;
        return false;
    }

    void shutdown()
    {
        available = false;
    }
#endif
};

OpenCLChunkRenderer::OpenCLChunkRenderer(): impl(std::make_unique<Impl>())
{
    impl->initialize();
}

OpenCLChunkRenderer::~OpenCLChunkRenderer()
{
    if (impl)
    {
        impl->shutdown();
    }
}

bool OpenCLChunkRenderer::isAvailable() const
{
    return impl && impl->available;
}

bool OpenCLChunkRenderer::rasterizeMixedChunkTexture(const std::shared_ptr<Formula> &formula,
                                                     const Interval &xRange,
                                                     const Interval &yRange,
                                                     const int textureSize,
                                                     std::vector<int> &outputPixels)
{
#if defined(_WIN32)
    if (!impl || !impl->available || !formula || textureSize <= 0)
    {
        return false;
    }

    std::string formulaExpression;
    if (!buildOpenClExpressionFromRpn(formula->getRPN(), formulaExpression))
    {
        return false;
    }

    if (!impl->ensureMixedTextureKernel(formulaExpression))
    {
        return false;
    }

    const auto pixelCount = static_cast<size_t>(textureSize) * static_cast<size_t>(textureSize);
    outputPixels.assign(pixelCount, 0);

    const auto xLower = static_cast<float>(xRange.lower);
    const auto yLower = static_cast<float>(yRange.lower);
    const auto deltaX = static_cast<float>(xRange.size() / static_cast<double>(textureSize));
    const auto deltaY = static_cast<float>(yRange.size() / static_cast<double>(textureSize));
    const cl_int textureSizeCL = textureSize;

    cl_int error = CL_SUCCESS;
    cl_mem outputBuffer = impl->api.clCreateBuffer(
        impl->context,
        CL_MEM_WRITE_ONLY,
        pixelCount * sizeof(int),
        nullptr,
        &error);

    if (error != CL_SUCCESS || !outputBuffer)
    {
        return false;
    }

    auto cleanup = [&]()
    {
        impl->api.clReleaseMemObject(outputBuffer);
    };

    error = impl->api.clSetKernelArg(impl->mixedTextureKernel, 0, sizeof(float), &xLower);
    error |= impl->api.clSetKernelArg(impl->mixedTextureKernel, 1, sizeof(float), &yLower);
    error |= impl->api.clSetKernelArg(impl->mixedTextureKernel, 2, sizeof(float), &deltaX);
    error |= impl->api.clSetKernelArg(impl->mixedTextureKernel, 3, sizeof(float), &deltaY);
    error |= impl->api.clSetKernelArg(impl->mixedTextureKernel, 4, sizeof(cl_int), &textureSizeCL);
    error |= impl->api.clSetKernelArg(impl->mixedTextureKernel, 5, sizeof(cl_mem), &outputBuffer);

    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    const std::array<size_t, 2> globalWorkSize{
        static_cast<size_t>(textureSize),
        static_cast<size_t>(textureSize)
    };

    error = impl->api.clEnqueueNDRangeKernel(
        impl->queue,
        impl->mixedTextureKernel,
        2,
        nullptr,
        globalWorkSize.data(),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clFinish(impl->queue);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clEnqueueReadBuffer(
        impl->queue,
        outputBuffer,
        CL_TRUE,
        0,
        pixelCount * sizeof(int),
        outputPixels.data(),
        0,
        nullptr,
        nullptr);

    cleanup();
    return error == CL_SUCCESS;
#else
    (void)formula;
    (void)xRange;
    (void)yRange;
    (void)textureSize;
    (void)outputPixels;
    return false;
#endif
}

bool OpenCLChunkRenderer::rasterizeChunkContourSegments(const RPN &residualRpn,
                                                        const Interval &xRange,
                                                        const Interval &yRange,
                                                        const int cellsPerAxis,
                                                        std::vector<RasterContourSegment> &outputSegments)
{
#if defined(_WIN32)
    outputSegments.clear();

    if (!impl || !impl->available || cellsPerAxis < 1)
    {
        return false;
    }

    std::string formulaExpression;
    if (!buildOpenClExpressionFromRpn(residualRpn, formulaExpression))
    {
        return false;
    }

    if (!impl->ensureContourSegmentsKernel(formulaExpression))
    {
        return false;
    }

    const auto cellCount = static_cast<size_t>(cellsPerAxis) * static_cast<size_t>(cellsPerAxis);
    std::vector<int> segmentCounts(cellCount, 0);
    std::vector<float> packedSegments(cellCount * 8, 0.0f);

    const auto xLower = static_cast<float>(xRange.lower);
    const auto yLower = static_cast<float>(yRange.lower);
    const auto cellStepX = static_cast<float>(xRange.size() / static_cast<double>(cellsPerAxis));
    const auto cellStepY = static_cast<float>(yRange.size() / static_cast<double>(cellsPerAxis));
    const cl_int cellsPerAxisCL = cellsPerAxis;

    cl_int error = CL_SUCCESS;
    cl_mem countBuffer = impl->api.clCreateBuffer(
        impl->context,
        CL_MEM_WRITE_ONLY,
        cellCount * sizeof(int),
        nullptr,
        &error);

    if (error != CL_SUCCESS || !countBuffer)
    {
        return false;
    }

    cl_mem segmentBuffer = impl->api.clCreateBuffer(
        impl->context,
        CL_MEM_WRITE_ONLY,
        packedSegments.size() * sizeof(float),
        nullptr,
        &error);

    if (error != CL_SUCCESS || !segmentBuffer)
    {
        impl->api.clReleaseMemObject(countBuffer);
        return false;
    }

    auto cleanup = [&]()
    {
        impl->api.clReleaseMemObject(segmentBuffer);
        impl->api.clReleaseMemObject(countBuffer);
    };

    error = impl->api.clSetKernelArg(impl->contourSegmentsKernel, 0, sizeof(float), &xLower);
    error |= impl->api.clSetKernelArg(impl->contourSegmentsKernel, 1, sizeof(float), &yLower);
    error |= impl->api.clSetKernelArg(impl->contourSegmentsKernel, 2, sizeof(float), &cellStepX);
    error |= impl->api.clSetKernelArg(impl->contourSegmentsKernel, 3, sizeof(float), &cellStepY);
    error |= impl->api.clSetKernelArg(impl->contourSegmentsKernel, 4, sizeof(cl_int), &cellsPerAxisCL);
    error |= impl->api.clSetKernelArg(impl->contourSegmentsKernel, 5, sizeof(cl_mem), &countBuffer);
    error |= impl->api.clSetKernelArg(impl->contourSegmentsKernel, 6, sizeof(cl_mem), &segmentBuffer);

    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    const std::array<size_t, 2> globalWorkSize{
        static_cast<size_t>(cellsPerAxis),
        static_cast<size_t>(cellsPerAxis)
    };

    error = impl->api.clEnqueueNDRangeKernel(
        impl->queue,
        impl->contourSegmentsKernel,
        2,
        nullptr,
        globalWorkSize.data(),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clFinish(impl->queue);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clEnqueueReadBuffer(
        impl->queue,
        countBuffer,
        CL_TRUE,
        0,
        cellCount * sizeof(int),
        segmentCounts.data(),
        0,
        nullptr,
        nullptr);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clEnqueueReadBuffer(
        impl->queue,
        segmentBuffer,
        CL_TRUE,
        0,
        packedSegments.size() * sizeof(float),
        packedSegments.data(),
        0,
        nullptr,
        nullptr);

    cleanup();
    if (error != CL_SUCCESS)
    {
        return false;
    }

    outputSegments.reserve(cellCount * 2);
    for (size_t cellIndex = 0; cellIndex < cellCount; ++cellIndex)
    {
        const auto count = std::clamp(segmentCounts[cellIndex], 0, 2);
        const auto base = cellIndex * 8;
        for (auto segmentIndex = 0; segmentIndex < count; ++segmentIndex)
        {
            const auto offset = base + static_cast<size_t>(segmentIndex) * 4;
            const auto x0 = static_cast<double>(packedSegments[offset + 0]);
            const auto y0 = static_cast<double>(packedSegments[offset + 1]);
            const auto x1 = static_cast<double>(packedSegments[offset + 2]);
            const auto y1 = static_cast<double>(packedSegments[offset + 3]);

            if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
            {
                continue;
            }

            if (std::hypot(x1 - x0, y1 - y0) <= 1e-12)
            {
                continue;
            }

            outputSegments.push_back({x0, y0, x1, y1});
        }
    }

    return true;
#else
    (void)residualRpn;
    (void)xRange;
    (void)yRange;
    (void)cellsPerAxis;
    (void)outputSegments;
    return false;
#endif
}

bool OpenCLChunkRenderer::rasterize(const std::vector<int> &chunkStates,
                                    const int chunkWidth,
                                    const int chunkHeight,
                                    const float xLower,
                                    const float yLower,
                                    const float deltaX,
                                    const float deltaY,
                                    const int targetLevel,
                                    const int64_t minChunkX,
                                    const int64_t minChunkY,
                                    const int windowWidth,
                                    const int windowHeight,
                                    std::vector<int> &outputImage) const
{
#if defined(_WIN32)
    if (!impl || !impl->available)
    {
        return false;
    }

    if (chunkWidth <= 0 || chunkHeight <= 0 || windowWidth <= 0 || windowHeight <= 0)
    {
        return false;
    }

    const size_t chunkCount = static_cast<size_t>(chunkWidth) * static_cast<size_t>(chunkHeight);
    if (chunkStates.size() != chunkCount)
    {
        return false;
    }

    const size_t imageCount = static_cast<size_t>(windowWidth) * static_cast<size_t>(windowHeight);
    if (outputImage.size() != imageCount)
    {
        outputImage.assign(imageCount, 0);
    }

    cl_int error = CL_SUCCESS;
    cl_mem chunkBuffer = impl->api.clCreateBuffer(
        impl->context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        chunkCount * sizeof(int),
        const_cast<int *>(chunkStates.data()),
        &error);
    if (error != CL_SUCCESS || !chunkBuffer)
    {
        return false;
    }

    cl_mem outputBuffer = impl->api.clCreateBuffer(
        impl->context,
        CL_MEM_WRITE_ONLY,
        imageCount * sizeof(int),
        nullptr,
        &error);
    if (error != CL_SUCCESS || !outputBuffer)
    {
        impl->api.clReleaseMemObject(chunkBuffer);
        return false;
    }

    auto cleanup = [&]()
    {
        impl->api.clReleaseMemObject(outputBuffer);
        impl->api.clReleaseMemObject(chunkBuffer);
    };

    const cl_int chunkWidthCL = chunkWidth;
    const cl_int chunkHeightCL = chunkHeight;
    const cl_int targetLevelCL = targetLevel;
    const cl_long minChunkXCL = minChunkX;
    const cl_long minChunkYCL = minChunkY;
    const cl_int windowWidthCL = windowWidth;
    const cl_int windowHeightCL = windowHeight;

    error = impl->api.clSetKernelArg(impl->kernel, 0, sizeof(cl_mem), &chunkBuffer);
    error |= impl->api.clSetKernelArg(impl->kernel, 1, sizeof(cl_int), &chunkWidthCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 2, sizeof(cl_int), &chunkHeightCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 3, sizeof(float), &xLower);
    error |= impl->api.clSetKernelArg(impl->kernel, 4, sizeof(float), &yLower);
    error |= impl->api.clSetKernelArg(impl->kernel, 5, sizeof(float), &deltaX);
    error |= impl->api.clSetKernelArg(impl->kernel, 6, sizeof(float), &deltaY);
    error |= impl->api.clSetKernelArg(impl->kernel, 7, sizeof(cl_int), &targetLevelCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 8, sizeof(cl_long), &minChunkXCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 9, sizeof(cl_long), &minChunkYCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 10, sizeof(cl_int), &windowWidthCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 11, sizeof(cl_int), &windowHeightCL);
    error |= impl->api.clSetKernelArg(impl->kernel, 12, sizeof(cl_mem), &outputBuffer);

    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    const std::array<size_t, 2> globalWorkSize{
        static_cast<size_t>(windowWidth),
        static_cast<size_t>(windowHeight)
    };

    error = impl->api.clEnqueueNDRangeKernel(
        impl->queue,
        impl->kernel,
        2,
        nullptr,
        globalWorkSize.data(),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clFinish(impl->queue);
    if (error != CL_SUCCESS)
    {
        cleanup();
        return false;
    }

    error = impl->api.clEnqueueReadBuffer(
        impl->queue,
        outputBuffer,
        CL_TRUE,
        0,
        imageCount * sizeof(int),
        outputImage.data(),
        0,
        nullptr,
        nullptr);

    cleanup();
    return error == CL_SUCCESS;
#else
    (void)chunkStates;
    (void)chunkWidth;
    (void)chunkHeight;
    (void)xLower;
    (void)yLower;
    (void)deltaX;
    (void)deltaY;
    (void)targetLevel;
    (void)minChunkX;
    (void)minChunkY;
    (void)windowWidth;
    (void)windowHeight;
    (void)outputImage;
    return false;
#endif
}
