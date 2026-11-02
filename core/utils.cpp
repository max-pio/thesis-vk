// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "utils.hpp"
#include "log.hpp"

// stb_image_write is a single header library: the implementation is compiled in exactly one .cpp file
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <unordered_map>

using namespace utils;

void utils::printOpenMPVersion()
{
#ifdef _OPENMP
    {
        std::unordered_map<unsigned, std::string> ver{{200505, "2.5"}, {200805, "3.0"}, {201107, "3.1"},
                                                      {201307, "4.0"}, {201511, "4.5"}, {201811, "5.0"},
                                                      {202011, "5.1"}};
        if (ver.find(_OPENMP) != ver.end())
            LOG_INFO("OpenMP %s available.", ver.at(_OPENMP).c_str());
        else
            LOG_INFO("OpenMP %d available.", _OPENMP);
    }
#else
    LOG_INFO("OpenMP not available.");
#endif
}

bool utils::savePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba)
{
    const int stride = static_cast<int>(width) * 4;
    return stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba, stride) != 0;
}

std::vector<glm::u8vec4> utils::createTilePattern(uint32_t size, uint32_t tileSize)
{
    std::vector<glm::u8vec4> pixels(static_cast<size_t>(size) * size);
    const uint32_t borderWidth = std::max(tileSize / 10, 1u);

    // OpenMP distributes the iterations of the following loop (the rows) over all CPU cores. This only works because
    // every iteration writes different pixels and does not depend on other iterations. Without OpenMP (see
    // CMakeLists.txt), the pragma is ignored and the loop runs sequentially. The loop variable is a signed int,
    // because MSVC only supports OpenMP 2.0.
#pragma omp parallel for
    for (int y = 0; y < static_cast<int>(size); y++)
    {
        for (uint32_t x = 0; x < size; x++)
        {
            const uint32_t ty = static_cast<uint32_t>(y);
            const bool border = x % tileSize < borderWidth || ty % tileSize < borderWidth;
            // checkerboard of two bright gray values
            const uint8_t value = border ? 90 : (((x / tileSize + ty / tileSize) & 1) ? 255 : 215);
            pixels[ty * size + x] = glm::u8vec4(value, value, value, 255);
        }
    }
    return pixels;
}
