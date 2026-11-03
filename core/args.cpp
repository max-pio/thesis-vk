// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "args.hpp"
#include "results.hpp"

#include <cstdio>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {

/// A command line option. Options with a valueName take a value (--name value), the others are flags (--name).
struct Option
{
    const char* name;
    const char* valueName; ///< nullptr for flags
    const char* description;
    std::function<void(Config&, const std::string&)> apply;
    /// Column name in the results of a benchmark, nullptr if the option is not written to the results (e.g. if another
    /// option already writes the same Config field)
    const char* column = nullptr;
    /// Returns the value of the option in a Config as text for the results
    std::function<std::string(const Config&)> value;
};

int parseInt(const std::string& value, const std::string& option)
{
    try
    {
        size_t end;
        const int result = std::stoi(value, &end);
        if (end == value.size())
            return result;
    }
    catch (const std::exception&)
    {}
    throw std::invalid_argument(option + ": expected an integer, got '" + value + "'");
}

uint32_t parseUint(const std::string& value, const std::string& option)
{
    const int result = parseInt(value, option);
    if (result < 0)
        throw std::invalid_argument(option + ": the value must not be negative");
    return static_cast<uint32_t>(result);
}

float parseFloat(const std::string& value, const std::string& option)
{
    try
    {
        size_t end;
        const float result = std::stof(value, &end);
        if (end == value.size())
            return result;
    }
    catch (const std::exception&)
    {}
    throw std::invalid_argument(option + ": expected a number, got '" + value + "'");
}

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

/// All options. Add new options here and the corresponding field to Config.
const std::vector<Option>& getOptions()
{
    static const std::vector<Option> OPTIONS = {
        {"--help", nullptr, "Show this help and exit.",
         [](Config& c, const std::string&) {
             c.showHelp = true;
         }},

        // Window
        {"--size", "WxH", "Window size in pixels, e.g. 1920x1080.",
         [](Config& c, const std::string& v) {
             int width, height;
             char x;
             if (std::sscanf(v.c_str(), "%d%c%d", &width, &x, &height) != 3 || x != 'x' || width <= 0 || height <= 0)
                 throw std::invalid_argument("--size: expected WxH, e.g. 1920x1080, got '" + v + "'");
             c.windowWidth = width;
             c.windowHeight = height;
         },
         "window_size",
         [](const Config& c) {
             return std::to_string(c.windowWidth) + "x" + std::to_string(c.windowHeight);
         }},
        {"--no-vsync", nullptr, "Disable VSync, e.g. to measure the frame rate.",
         [](Config& c, const std::string&) { c.vsync = false; }, "vsync",
         [](const Config& c) {
             return boolText(c.vsync);
         }},

        // Logging and validation
        {"--log-level", "LEVEL", "Minimum level of log messages: debug, info, warn or error (default: info).",
         [](Config& c, const std::string& v) {
             for (size_t i = 0; i < std::size(LOG_LEVEL_NAMES); i++)
                 if (v == LOG_LEVEL_NAMES[i])
                     c.logLevel = static_cast<LogLevel>(i);
             if (v != LOG_LEVEL_NAMES[static_cast<size_t>(c.logLevel)])
                 throw std::invalid_argument("--log-level: expected debug, info, warn or error, got '" + v + "'");
         }},
        {"--log-file", "PATH", "Additionally write the log to this file.",
         [](Config& c, const std::string& v) {
             c.logFile = v;
         }},
        {"--validation", nullptr, "Enable the Vulkan validation layers (default in Debug builds).",
         [](Config& c, const std::string&) { c.validation = true; }, "validation",
         [](const Config& c) {
             return boolText(c.validation);
         }},
        {"--no-validation", nullptr, "Disable the Vulkan validation layers.",
         [](Config& c, const std::string&) {
             c.validation = false;
         }},
        {"--sync-validation", nullptr,
         "Enable the validation layers with synchronization validation, which finds many missing barriers (slower, "
         "default in Debug builds).",
         [](Config& c, const std::string&) {
             c.validation = true;
             c.syncValidation = true;
         },
         "sync_validation",
         [](const Config& c) {
             return boolText(c.syncValidation);
         }},
        {"--no-sync-validation", nullptr, "Disable synchronization validation, keep the other validation layers.",
         [](Config& c, const std::string&) {
             c.syncValidation = false;
         }},

        // Data
        {"--data", "PATH",
         "Input data file.",
         [](Config& c, const std::string& v) { c.dataFile = v; }, "data",
         [](const Config& c) {
             return c.dataFile;
         }},

        // Reproducible frames
        {"--capture", "N",
         "Save frame N (counting from 0) as PNG and exit. Uses a fixed time step of 1/60 s unless --fixed-timestep is "
         "given, and ignores camera input.",
         [](Config& c, const std::string& v) {
             c.captureFrame = parseUint(v, "--capture");
         }},
        {"--capture-file", "PATH", "File name of the captured frame (default: capture.png).",
         [](Config& c, const std::string& v) {
             c.captureFile = v;
         }},
        {"--fixed-timestep", "SECONDS", "Advance animations by a fixed time per frame instead of the measured time.",
         [](Config& c, const std::string& v) {
             c.fixedTimeStep = parseFloat(v, "--fixed-timestep");
             if (c.fixedTimeStep <= 0.0f)
                 throw std::invalid_argument("--fixed-timestep: the time step must be positive");
         },
         "fixed_timestep",
         [](const Config& c) {
             return std::to_string(c.fixedTimeStep);
         }},

        // Benchmark
        {"--benchmark", "N",
         "Measure the CPU and GPU times of N frames, append them with all options to the results file, and exit. Uses "
         "a fixed time step like --capture.",
         [](Config& c, const std::string& v) { c.benchmarkFrames = parseUint(v, "--benchmark"); }, "benchmark_frames",
         [](const Config& c) {
             return std::to_string(c.benchmarkFrames);
         }},
        {"--benchmark-warmup", "N", "Frames rendered before the benchmark measures (default: 100).",
         [](Config& c, const std::string& v) { c.benchmarkWarmupFrames = parseUint(v, "--benchmark-warmup"); },
         "benchmark_warmup_frames",
         [](const Config& c) {
             return std::to_string(c.benchmarkWarmupFrames);
         }},
        {"--results-file", "PATH", "CSV file the benchmark results are appended to (default: results.csv).",
         [](Config& c, const std::string& v) {
             c.resultsFile = v;
         }},
        {"--run-name", "NAME", "Name of the run in the benchmark results, e.g. the variant that is measured.",
         [](Config& c, const std::string& v) {
             c.runName = v;
         }},
    };
    return OPTIONS;
}

} // namespace

Config Args::parse(int argc, const char* const* argv)
{
    Config config;
    bool hasFixedTimeStep = false;
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        const std::string name = arg == "-h" ? "--help" : arg;

        const Option* option = nullptr;
        for (const Option& o : getOptions())
            if (name == o.name)
                option = &o;
        if (option == nullptr)
            throw std::invalid_argument("Unknown option '" + arg + "'");

        std::string value;
        if (option->valueName != nullptr)
        {
            if (i + 1 >= argc)
                throw std::invalid_argument(name + " requires a value " + option->valueName);
            value = argv[++i];
        }
        option->apply(config, value);
        hasFixedTimeStep |= name == "--fixed-timestep";
    }

    // synchronization validation is part of the validation layers: without them (--no-validation, or the Release
    // default) it is off too, also in the results of a benchmark
    if (!config.validation)
        config.syncValidation = false;

    // captured frames and benchmarks should be reproducible
    if (config.isAutomatedRun() && !hasFixedTimeStep)
        config.fixedTimeStep = 1.0f / 60.0f;
    return config;
}

void Args::printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]\n\nOptions:\n";
    for (const Option& option : getOptions())
    {
        std::string left = std::string("  ") + option.name;
        if (option.valueName != nullptr)
            left += std::string(" ") + option.valueName;
        std::cout << left;
        // align the descriptions in a column
        for (size_t i = left.size(); i < 28; i++)
            std::cout << ' ';
        std::cout << " " << option.description << "\n";
    }
    std::cout << std::flush;
}

void Args::addToResults(const Config& config, Results& results)
{
    for (const Option& option : getOptions())
        if (option.column != nullptr)
            results.add(option.column, option.value(config));
}
