// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

/// Data type of the voxels of a Volume. All of them are stored in little-endian byte order, like on x86 and ARM.
enum class VoxelType
{
    UInt8,
    UInt16,
    Int16,
    Float32,
    /// Only exists in files: GPUs cannot filter 64 bit float textures, so loadVolume() converts the voxels to Float32
    /// and a loaded Volume never has this type.
    Float64,
};

/// "uint8", "uint16", "int16", "float32" or "float64": the spelling used in file names such as
/// bonsai_256x256x256_uint8.raw
const char* voxelTypeName(VoxelType type);
/// Size of one voxel in bytes.
uint32_t voxelTypeSize(VoxelType type);

/// A 3D scalar field on a regular grid (one value per voxel), loaded into CPU memory.
struct Volume
{
    std::string name;   ///< file name without the size and type, e.g. "bonsai"
    glm::uvec3 size{0}; ///< number of voxels in x (width), y (height) and z (depth)
    VoxelType type = VoxelType::UInt8;
    /// The voxels, tightly packed: x varies fastest, then y, then z. This is the layout that
    /// VulkanContext::createTexture(VK_IMAGE_TYPE_3D, ..) expects.
    std::vector<uint8_t> data;
    /// Smallest and largest voxel value as a shader sees it when it samples the texture (see utils::getVkFormat()):
    /// integer types are normalized (uint8 / 255, uint16 / 65535, int16 / 32767), float32 values are unchanged.
    /// Useful for the default range of a transfer function.
    float minValue = 0.0f;
    float maxValue = 1.0f;
    /// Distance between neighboring voxels in each direction. The raw format does not store it, so this is 1 after
    /// loading. E.g. the Open SciVis data sets list the spacing of each volume on their web page.
    glm::vec3 spacing{1.0f};

    uint64_t getVoxelCount() const { return uint64_t(size.x) * size.y * size.z; }
};

namespace utils {
/// Loads a raw volume that store the voxel data directly without any header (as used in Open SciVis data).
/// Size (and optional type) are part of the file name:
///  - `name_WxHxD_type.raw`, e.g. `bonsai_256x256x256_uint8.raw`, with type uint8, uint16, int16, float32 or
///    float64. float64 voxels are converted to float32 while loading (with a warning), the Volume has type float32.
///  - `name_WxHxD.raw`: the type is inferred from the file size: 1 byte per voxel is uint8, 2 bytes uint16 (not
///    int16, give the type if the data is signed), 4 bytes float32, 8 bytes float64
/// File extension is not checked. Throws std::runtime_error if the file cannot be read, the name does not match or the
/// file size does not fit to the size and type.
Volume loadVolume(const std::string& path);

} // namespace utils
