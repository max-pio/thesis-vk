// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "gpu_utils.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

const char* utils::vkResultToString(VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "unknown VkResult";
    }
}

void utils::vkCheck_(VkResult result, const char* call, const char* file, int line)
{
    if (result == VK_SUCCESS)
        return;
    LOG_ERROR("Vulkan error %s (%d) in %s | %s (%d)", vkResultToString(result), static_cast<int>(result), call, file,
              line);
    // Aborting stops a debugger right here, so that you can inspect the call stack.
    std::abort();
}

std::vector<uint32_t> utils::compileShader(const std::string& shaderPath, std::string& errorMessage,
                                           const std::vector<std::string>& defines)
{
    namespace fs = std::filesystem;
    errorMessage.clear();
    if (!fs::exists(shaderPath))
    {
        errorMessage = "Shader file not found: " + shaderPath;
        LOG_ERROR("%s", errorMessage.c_str());
        return {};
    }

    // compile the shader with glslc into the build directory, e.g. instance.vert -> instance.vert.spv
    fs::create_directories(SHADER_BINARY_PATH);
    const fs::path spirvPath = fs::path(SHADER_BINARY_PATH) / (fs::path(shaderPath).filename().string() + ".spv");
    fs::remove(spirvPath);

    // -g keeps debug information (variable names, source) for debuggers like RenderDoc or Nsight Graphics, -I is the
    // directory of #include files, -D sets a define
    std::string command =
        std::string("\"") + GLSLC_PATH + "\" --target-env=vulkan1.3 -g -I \"" + FRAMEWORK_SHADERS_PATH + "\"";
    for (const std::string& define : defines)
        command += " \"-D" + define + "\"";
    // 2>&1 redirects the error output of glslc, so that we can read the compile errors
    command += " -o \"" + spirvPath.string() + "\" \"" + shaderPath + "\" 2>&1";
#ifdef _WIN32
    // cmd.exe strips the first and last quote of the command, so wrap it in an additional pair
    command = "\"" + command + "\"";
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    // read the output of glslc, which is empty if the shader compiled without errors or warnings
    std::string output;
    if (pipe != nullptr)
    {
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
            output += buffer;
    }
#ifdef _WIN32
    const int exitCode = pipe != nullptr ? _pclose(pipe) : -1;
#else
    const int exitCode = pipe != nullptr ? pclose(pipe) : -1;
#endif
    // shorten the file paths in the messages to the file names
    const std::string shaderDirectory = fs::path(shaderPath).parent_path().string() + "/";
    for (size_t pos = output.find(shaderDirectory); pos != std::string::npos; pos = output.find(shaderDirectory, pos))
        output.erase(pos, shaderDirectory.size());
    if (exitCode != 0 || !fs::exists(spirvPath))
    {
        errorMessage = output.empty() ? "Shader compilation failed: " + shaderPath : output;
        LOG_ERROR("Shader compilation failed:\n%s", errorMessage.c_str());
        return {};
    }
    if (!output.empty())
        LOG_WARN("%s", output.c_str()); // e.g. warnings

    // read the SPIR-V binary
    std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
    const size_t byteSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> spirv(byteSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(byteSize));
    return spirv;
}

VkShaderModule utils::createShaderModuleFromFile(VkDevice device, const std::string& shaderPath,
                                                 std::string& errorMessage, const std::vector<std::string>& defines)
{
    const std::vector<uint32_t> spirv = compileShader(shaderPath, errorMessage, defines);
    if (spirv.empty())
        return VK_NULL_HANDLE;

    const VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data(),
    };
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

void utils::imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                         VkImageLayout newLayout, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                         VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = aspect,
                             .baseMipLevel = 0,
                             .levelCount = VK_REMAINING_MIP_LEVELS,
                             .baseArrayLayer = 0,
                             .layerCount = VK_REMAINING_ARRAY_LAYERS},
    };
    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void utils::memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                          VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
    };
    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
