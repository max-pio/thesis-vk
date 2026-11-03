// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "volume.hpp"
#include "log.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <stdexcept>
#include <type_traits>
#include <utility>

// The raw files are little-endian and are copied to the GPU as they are
static_assert(std::endian::native == std::endian::little, "loadVolume() assumes a little-endian CPU");

const char* voxelTypeName(VoxelType type)
{
    switch (type)
    {
        case VoxelType::UInt8: return "uint8";
        case VoxelType::UInt16: return "uint16";
        case VoxelType::Int16: return "int16";
        case VoxelType::Float32: return "float32";
        case VoxelType::Float64: return "float64";
        default:
            LOG_ERROR("Unknown VoxelType %i", static_cast<uint>(type));
            break;
    }
    return "unknown";
}

uint32_t voxelTypeSize(VoxelType type)
{
    switch (type)
    {
        case VoxelType::UInt8: return 1;
        case VoxelType::UInt16:
        case VoxelType::Int16: return 2;
        case VoxelType::Float32: return 4;
        case VoxelType::Float64: return 8;
        default:
            LOG_ERROR("Unknown VoxelType %i", static_cast<uint>(type));
            break;
    }
    return 0;
}

namespace {

/// Smallest and largest value of the voxels, which are of type T. NaN and infinity in float data are ignored.
template <typename T>
std::pair<double, double> findRange(const std::vector<uint8_t>& data)
{
    const size_t count = data.size() / sizeof(T);
    T lo = std::numeric_limits<T>::max(), hi = std::numeric_limits<T>::lowest();
    for (size_t i = 0; i < count; i++)
    {
        // the bytes of the vector have no alignment guarantee for T, so values are copied out instead of cast
        T value;
        std::memcpy(&value, data.data() + i * sizeof(T), sizeof(T));
        if constexpr (std::is_floating_point_v<T>)
            if (!std::isfinite(value))
                continue;
        lo = std::min(lo, value);
        hi = std::max(hi, value);
    }
    if (lo > hi) // no (finite) value at all
        return {0.0, 0.0};
    return {static_cast<double>(lo), static_cast<double>(hi)};
}

} // namespace

Volume utils::loadVolume(const std::string& path)
{
    const std::filesystem::path filePath(path);

    // name_WxHxD or name_WxHxD_type
    static const std::regex NAME_PATTERN(R"(^(.*)_(\d+)x(\d+)x(\d+)(?:_(uint8|uint16|int16|float32|float64))?$)");
    const std::string stem = filePath.stem().string();
    std::smatch match;
    if (!std::regex_match(stem, match, NAME_PATTERN))
        throw std::runtime_error("loadVolume: cannot read the size from the file name '" + filePath.filename().string()
                                 + "', expected name_WxHxD_type.raw, e.g. bonsai_256x256x256_uint8.raw");

    Volume volume;
    volume.name = match[1].str();
    // stoull instead of stoi: a name with a huge number should be reported as error, not wrap around
    for (int axis = 0; axis < 3; axis++)
    {
        const unsigned long long extent = std::stoull(match[2 + axis].str());
        if (extent == 0 || extent > 65535)
            throw std::runtime_error("loadVolume: invalid size in the file name '" + filePath.filename().string()
                                     + "'");
        volume.size[axis] = static_cast<uint32_t>(extent);
    }

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("loadVolume: cannot open '" + path + "'");
    const uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    const uint64_t voxelCount = volume.getVoxelCount();

    if (match[5].matched)
    {
        const std::string typeName = match[5].str();
        for (VoxelType type :
             {VoxelType::UInt8, VoxelType::UInt16, VoxelType::Int16, VoxelType::Float32, VoxelType::Float64})
            if (typeName == voxelTypeName(type))
                volume.type = type;
        if (fileSize != voxelCount * voxelTypeSize(volume.type))
            throw std::runtime_error("loadVolume: '" + filePath.filename().string() + "' has "
                                     + std::to_string(fileSize) + " bytes, but " + std::to_string(voxelCount)
                                     + " voxels of type " + typeName + " need "
                                     + std::to_string(voxelCount * voxelTypeSize(volume.type)));
    }
    else
    {
        // infer the type from the number of bytes per voxel
        if (fileSize % voxelCount != 0)
            throw std::runtime_error("loadVolume: the size of '" + filePath.filename().string() + "' ("
                                     + std::to_string(fileSize) + " bytes) is not a multiple of the "
                                     + std::to_string(voxelCount) + " voxels");
        switch (fileSize / voxelCount)
        {
            case 1: volume.type = VoxelType::UInt8; break;
            case 2: volume.type = VoxelType::UInt16; break;
            case 4: volume.type = VoxelType::Float32; break;
            case 8: volume.type = VoxelType::Float64; break;
            default:
                throw std::runtime_error("loadVolume: '" + filePath.filename().string() + "' has "
                                         + std::to_string(fileSize / voxelCount)
                                         + " bytes per voxel, only 1, 2, 4 and 8 are supported");
        }
    }

    file.seekg(0);
    if (volume.type == VoxelType::Float64)
    {
        // No GPU filters 64 bit float textures, so the voxels are converted to float32. This is done in chunks while
        // reading, so that the large double data is never completely in memory. Values beyond the float range become
        // infinity and are ignored for the value range below.
        LOG_WARN("'%s' has float64 voxels: converting them to float32 while loading (precision is reduced).",
                 filePath.filename().string().c_str());
        volume.data.resize(voxelCount * sizeof(float));
        std::vector<double> chunk(1 << 20);
        for (uint64_t done = 0; done < voxelCount;)
        {
            const uint64_t count = std::min<uint64_t>(chunk.size(), voxelCount - done);
            if (!file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(count * sizeof(double))))
                throw std::runtime_error("loadVolume: failed to read '" + path + "'");
            for (uint64_t i = 0; i < count; i++)
            {
                const float value = static_cast<float>(chunk[i]);
                std::memcpy(volume.data.data() + (done + i) * sizeof(float), &value, sizeof(float));
            }
            done += count;
        }
        volume.type = VoxelType::Float32;
    }
    else
    {
        volume.data.resize(fileSize);
        if (!file.read(reinterpret_cast<char*>(volume.data.data()), static_cast<std::streamsize>(fileSize)))
            throw std::runtime_error("loadVolume: failed to read '" + path + "'");
    }

    // Range as the shader sees it: UNORM / SNORM textures map the integers to [0, 1] / [-1, 1]. SNORM maps both -32768
    // and -32767 to -1.
    std::pair<double, double> range;
    double scale = 1.0;
    switch (volume.type)
    {
        case VoxelType::UInt8:
            range = findRange<uint8_t>(volume.data);
            scale = 1.0 / 255.0;
            break;
        case VoxelType::UInt16:
            range = findRange<uint16_t>(volume.data);
            scale = 1.0 / 65535.0;
            break;
        case VoxelType::Int16:
            range = findRange<int16_t>(volume.data);
            scale = 1.0 / 32767.0;
            break;
        case VoxelType::Float32: range = findRange<float>(volume.data); break;
        case VoxelType::Float64: break; // converted to Float32 above
    }
    double lowest = range.first * scale;
    if (volume.type == VoxelType::Int16)
        lowest = std::max(lowest, -1.0);
    volume.minValue = static_cast<float>(lowest);
    volume.maxValue = static_cast<float>(range.second * scale);

    LOG_INFO("Loaded volume '%s': %ux%ux%u %s, %.1f MB, value range [%g, %g].", volume.name.c_str(), volume.size.x,
             volume.size.y, volume.size.z, voxelTypeName(volume.type), static_cast<double>(fileSize) / 1.0e6,
             static_cast<double>(volume.minValue), static_cast<double>(volume.maxValue));
    return volume;
}
