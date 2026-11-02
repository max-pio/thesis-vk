// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

#include <cstdint>
#include <string>
#include <vector>

/// General helper functions that do not depend on Vulkan
namespace utils {
/// Prints whether OpenMP is available for CPU parallelization, and its version.
void printOpenMPVersion();

/// Saves an image with 4 bytes per pixel (RGBA, rows from top to bottom) as PNG file. Returns false on failure.
bool savePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba);

/// Creates a size x size RGBA image of bright tiles with dark borders, e.g. as texture. The pixels are computed in
/// parallel on the CPU with OpenMP.
std::vector<glm::u8vec4> createTilePattern(uint32_t size, uint32_t tileSize);
} // namespace utils
