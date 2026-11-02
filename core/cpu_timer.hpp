// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/// A measured time, e.g. {"gpu_scene", 1.23}
struct Timing
{
    std::string name;
    double ms;
};

/// Measures CPU wall-clock times of several named parts of a frame, e.g. {"animation", "upload"}. Each frame, call
/// beginFrame(), then begin(i) / end(i) around the measured code. See GpuTimer for GPU times.
class CpuTimer
{
public:
    explicit CpuTimer(std::vector<std::string> names)
        : mNames(std::move(names)), mStarts(mNames.size()), mResultsMs(mNames.size(), -1.0)
    {}

    /// Forgets the results of the previous frame
    void beginFrame() { std::fill(mResultsMs.begin(), mResultsMs.end(), -1.0); }

    void begin(uint32_t timer)
    {
        assert(timer < mNames.size());
        mStarts[timer] = std::chrono::steady_clock::now();
    }

    void end(uint32_t timer)
    {
        assert(timer < mNames.size());
        mResultsMs[timer] =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mStarts[timer]).count();
    }

    /// Appends the times measured in this frame as "cpu_<name>"
    void appendTimings(std::vector<Timing>& timings) const
    {
        for (size_t i = 0; i < mNames.size(); i++)
            if (mResultsMs[i] >= 0.0)
                timings.push_back({"cpu_" + mNames[i], mResultsMs[i]});
    }

private:
    std::vector<std::string> mNames;
    std::vector<std::chrono::steady_clock::time_point> mStarts;
    std::vector<double> mResultsMs; ///< negative if not measured in this frame
};

/// Mean, minimum and maximum of a series of times
struct Statistics
{
    uint32_t count = 0;
    double sumMs = 0.0;
    double minMs = std::numeric_limits<double>::max();
    double maxMs = 0.0;

    void add(double ms)
    {
        count++;
        sumMs += ms;
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
    }
    double meanMs() const { return count > 0 ? sumMs / count : 0.0; }
};

/// Statistics of several named times over many frames, in the order in which the names were first added
class TimingStatistics
{
public:
    /// Adds the times of one frame
    void add(const std::vector<Timing>& timings)
    {
        for (const Timing& timing : timings)
        {
            auto entry =
                std::find_if(mEntries.begin(), mEntries.end(), [&](const auto& e) { return e.first == timing.name; });
            if (entry == mEntries.end())
            {
                mEntries.push_back({timing.name, {}});
                entry = mEntries.end() - 1;
            }
            entry->second.add(timing.ms);
        }
    }

    void clear() { mEntries.clear(); }

    const std::vector<std::pair<std::string, Statistics>>& getEntries() const { return mEntries; }

private:
    std::vector<std::pair<std::string, Statistics>> mEntries;
};
