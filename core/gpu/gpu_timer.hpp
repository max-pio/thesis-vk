// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "cpu_timer.hpp"
#include "gpu/vulkan_context.hpp"

#include <string>
#include <vector>

/// Measures the GPU time of several named parts of a frame, e.g. {"scene", "lighting"}, with timestamp queries. Each
/// frame, call beginFrame() first, then begin(cmd, i) / end(cmd, i) around the measured commands.
///
/// The GPU executes a frame after the CPU recorded it, so the times are only known later: beginFrame() reads the times
/// of the frame that used the same frame-in-flight slot before (FRAMES_IN_FLIGHT frames ago), which finished because
/// VulkanContext::beginFrame() waited for its fence. Reading the results never stalls the CPU.
class GpuTimer
{
public:
    /// Creates the query pool for one timer per name. Logs a warning if the GPU does not support timestamps on the
    /// queue; then all times are missing.
    void init(const VulkanContext& context, std::vector<std::string> names);
    void shutdown();

    /// Reads the times of the finished frame in this frame slot and resets its queries. Call it after
    /// VulkanContext::beginFrame() and outside of vkCmdBeginRendering() / vkCmdEndRendering().
    void beginFrame(VkCommandBuffer cmd);
    /// Records a timestamp when all previous commands finished. Each timer can be used once per frame.
    void begin(VkCommandBuffer cmd, uint32_t timer);
    /// Records a timestamp when all previous commands, including the measured ones, finished.
    void end(VkCommandBuffer cmd, uint32_t timer);

    /// Appends the times read in the last beginFrame() as "gpu_<name>"
    void appendTimings(std::vector<Timing>& timings) const;

private:
    uint32_t getQuery(uint32_t frame, uint32_t timer, uint32_t isEnd) const;

    const VulkanContext* mContext = nullptr;
    std::vector<std::string> mNames;
    VkQueryPool mQueryPool = VK_NULL_HANDLE;
    /// nanoseconds per timestamp tick
    double mTimestampPeriod = 0.0;
    /// timestamps have only timestampValidBits valid bits, the difference of two timestamps is masked with this
    uint64_t mValidBitsMask = 0;
    /// for each frame slot and timer: whether begin() and end() were recorded since the queries were reset
    std::vector<bool> mRecorded;
    std::vector<double> mResultsMs; ///< negative if not available
};
