// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "gpu_timer.hpp"
#include "gpu_utils.hpp"
#include "log.hpp"

#include <cassert>

void GpuTimer::init(const VulkanContext& context, std::vector<std::string> names)
{
    mContext = &context;
    mNames = std::move(names);
    const uint32_t timerCount = static_cast<uint32_t>(mNames.size());
    mRecorded.assign(VulkanContext::FRAMES_IN_FLIGHT * timerCount, false);
    mResultsMs.assign(timerCount, -1.0);

    // Timestamps count GPU clock ticks: timestampPeriod is the length of one tick in nanoseconds, and only the lower
    // timestampValidBits bits are valid (0: the queue does not support timestamps)
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(context.getPhysicalDevice(), &properties);
    mTimestampPeriod = properties.limits.timestampPeriod;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.getPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(context.getPhysicalDevice(), &familyCount, families.data());
    const uint32_t validBits = families[context.getQueueFamily()].timestampValidBits;
    if (validBits == 0)
    {
        LOG_WARN("GpuTimer: the GPU does not support timestamps, GPU times are not measured.");
        return;
    }
    mValidBitsMask = validBits >= 64 ? ~0ull : (1ull << validBits) - 1;

    // two queries (begin, end) per timer and frame slot
    const VkQueryPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = VulkanContext::FRAMES_IN_FLIGHT * timerCount * 2,
    };
    VK_CHECK(vkCreateQueryPool(context.getDevice(), &poolInfo, nullptr, &mQueryPool));
}

void GpuTimer::shutdown()
{
    if (mContext != nullptr)
        vkDestroyQueryPool(mContext->getDevice(), mQueryPool, nullptr);
    mQueryPool = VK_NULL_HANDLE;
}

uint32_t GpuTimer::getQuery(uint32_t frame, uint32_t timer, uint32_t isEnd) const
{
    return (frame * static_cast<uint32_t>(mNames.size()) + timer) * 2 + isEnd;
}

void GpuTimer::beginFrame(VkCommandBuffer cmd)
{
    std::fill(mResultsMs.begin(), mResultsMs.end(), -1.0);
    if (mQueryPool == VK_NULL_HANDLE)
        return;

    // Read the timestamps of the frame that used this slot before. It finished, so the results are available without
    // waiting. The availability value after each timestamp is 0 if the query was not written.
    const uint32_t frame = mContext->getFrameIndex();
    const uint32_t timerCount = static_cast<uint32_t>(mNames.size());
    for (uint32_t timer = 0; timer < timerCount; timer++)
    {
        if (!mRecorded[frame * timerCount + timer])
            continue;
        uint64_t data[4]; // begin timestamp, availability, end timestamp, availability
        const VkResult result =
            vkGetQueryPoolResults(mContext->getDevice(), mQueryPool, getQuery(frame, timer, 0), 2, sizeof(data), data,
                                  2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result == VK_SUCCESS && data[1] != 0 && data[3] != 0)
            mResultsMs[timer] = static_cast<double>((data[2] - data[0]) & mValidBitsMask) * mTimestampPeriod * 1e-6;
        else if (result != VK_NOT_READY)
            VK_CHECK(result);
        mRecorded[frame * timerCount + timer] = false;
    }

    // Queries have to be reset before they are written again. vkCmdResetQueryPool must not be called inside of
    // vkCmdBeginRendering() / vkCmdEndRendering().
    vkCmdResetQueryPool(cmd, mQueryPool, getQuery(frame, 0, 0), timerCount * 2);
}

void GpuTimer::begin(VkCommandBuffer cmd, uint32_t timer)
{
    assert(timer < mNames.size());
    if (mQueryPool == VK_NULL_HANDLE)
        return;
    // ALL_COMMANDS: the timestamp is written once all previously submitted commands completed all their stages. A
    // timestamp is not a barrier: without one, the GPU may already start the measured commands while earlier ones
    // still run, so the times of passes that overlap on the GPU are approximate.
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, mQueryPool,
                         getQuery(mContext->getFrameIndex(), timer, 0));
}

void GpuTimer::end(VkCommandBuffer cmd, uint32_t timer)
{
    assert(timer < mNames.size());
    if (mQueryPool == VK_NULL_HANDLE)
        return;
    const uint32_t frame = mContext->getFrameIndex();
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, mQueryPool, getQuery(frame, timer, 1));
    mRecorded[frame * mNames.size() + timer] = true;
}

void GpuTimer::appendTimings(std::vector<Timing>& timings) const
{
    for (size_t i = 0; i < mNames.size(); i++)
        if (mResultsMs[i] >= 0.0)
            timings.push_back({"gpu_" + mNames[i], mResultsMs[i]});
}
