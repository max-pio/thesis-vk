// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "log.hpp"

#include <cstdint>
#include <string>

class Results;

/// All options that can be set on the command line. main() creates it once and passes it as const reference to the
/// parts of the program that need it. To add an option, add a field here and an entry in the option table in args.cpp.
struct Config
{
    bool showHelp = false;

    // Window
    int windowWidth = 1280;
    int windowHeight = 720;
    bool vsync = true;

    // Logging and validation
    LogLevel logLevel = LogLevel::Info;
    std::string logFile; ///< additionally write the log to this file, "": console only
    // Vulkan validation layers, and synchronization validation (checks for missing barriers): both on by default in
    // Debug builds
#ifdef NDEBUG
    bool validation = false;
    bool syncValidation = false;
#else
    bool validation = true;
    bool syncValidation = true;
#endif

    // Data
    /// Input data file (--data). The renderer decides what it contains, e.g. a raw volume (see utils::loadVolume()) or
    /// a mesh. "": the renderer's default.
    std::string dataFile;

    // Reproducible frames, e.g. for screenshots in the thesis or for comparing two versions of the code
    /// Saves the frame with this number (counting from 0) as PNG to captureFile and exits. The default UINT32_MAX is
    /// never reached, so only --capture enables capturing.
    uint32_t captureFrame = UINT32_MAX;
    std::string captureFile = "capture.png";
    /// Fixed time step in seconds for animations and camera movement, so that every run renders the same frames.
    /// 0 uses the measured frame time.
    float fixedTimeStep = 0.0f;

    // Benchmark: measures the times of benchmarkFrames frames after benchmarkWarmupFrames frames, appends them with all
    // options as one row to resultsFile, and exits. 0 frames disables the benchmark.
    uint32_t benchmarkFrames = 0;
    uint32_t benchmarkWarmupFrames = 100;
    std::string resultsFile = "results.csv";
    std::string runName; ///< e.g. a name for the variant that is measured, written to the results

    bool isCapturing() const { return captureFrame != UINT32_MAX; }
    bool isBenchmarking() const { return benchmarkFrames > 0; }
    /// Captures and benchmarks run without user input: the camera ignores mouse and keyboard, and animations use a
    /// fixed time step, so that every run renders the same frames.
    bool isAutomatedRun() const { return isCapturing() || isBenchmarking(); }
};

/// Parses the command line arguments into a Config. Run the program with --help to see all options.
class Args
{
public:
    /// Parses the arguments of main(). Throws std::invalid_argument for unknown options and invalid values.
    static Config parse(int argc, const char* const* argv);
    /// Prints all options and their descriptions.
    static void printUsage(const char* programName);
    /// Adds one column per option with its value in config, e.g. for the results of a benchmark.
    static void addToResults(const Config& config, Results& results);
};
