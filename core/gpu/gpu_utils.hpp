// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "volume.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

/// Vulkan helper functions: error checking, shader compilation, pipeline barriers and texture formats
namespace utils {
/// Implementation of VK_CHECK, use the macro instead.
void vkCheck_(VkResult result, const char* call, const char* file, int line);
/// Checks the VkResult of a Vulkan call. Prints the error and aborts the program if the call failed.
#define VK_CHECK(call) utils::vkCheck_((call), #call, __FILE__, __LINE__)

/// Returns the name of a VkResult, e.g. "VK_ERROR_DEVICE_LOST".
const char* vkResultToString(VkResult result);

/// Compiles a GLSL shader file to SPIR-V with glslc. The shader stage is derived from the file extension
/// (.vert, .frag, .comp, ...). Shaders can #include "file.glsl" from their own directory and from the shared shader
/// directory (FRAMEWORK_SHADERS_PATH). The defines ("NAME" or "NAME=VALUE") are set like #define NAME VALUE at the
/// top of the shader, e.g. to compile variants of a shader. On failure, returns an empty vector and sets errorMessage
/// to the compiler output.
std::vector<uint32_t> compileShader(const std::string& shaderPath, std::string& errorMessage,
                                    const std::vector<std::string>& defines = {});

/// Compiles a GLSL shader file (see compileShader()) and creates a shader module from it. On failure, returns
/// VK_NULL_HANDLE and sets errorMessage to the compiler output.
VkShaderModule createShaderModuleFromFile(VkDevice device, const std::string& shaderPath, std::string& errorMessage,
                                          const std::vector<std::string>& defines = {});

/// Records a pipeline barrier for a single image. With VK_KHR_unified_image_layouts, images stay in
/// VK_IMAGE_LAYOUT_GENERAL and oldLayout == newLayout == GENERAL is just a memory dependency without transition.
void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                  VkImageLayout newLayout, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

/// Records a global memory barrier, e.g. between a compute shader writing a buffer and a draw call reading it.
void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

/// The texture format for the voxels of a Volume, e.g. for VulkanContext::createTexture(VK_IMAGE_TYPE_3D, ..). The
/// integer types are normalized, so a shader that samples the texture gets floats: uint8 -> R8_UNORM [0, 1],
/// uint16 -> R16_UNORM [0, 1], int16 -> R16_SNORM [-1, 1], float32 -> R32_SFLOAT. float64 has no
/// filterable format and returns VK_FORMAT_UNDEFINED; loadVolume() converts it to float32. (Like in OpenGL, texelFetch
/// and texture() return the normalized value. Linear filtering of R32_SFLOAT is not required by Vulkan, but supported
/// by all desktop GPUs.)
VkFormat getVkFormat(VoxelType type);

} // namespace utils
