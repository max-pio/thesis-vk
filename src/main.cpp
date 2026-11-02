// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "imgui.h"

#include <GLFW/glfw3.h>
#include <cassert>
#include <exception>
#include <sstream>

#include "args.hpp"
#include "cpu_timer.hpp"
#include "gpu/vulkan_context.hpp"
#include "gui.hpp"
#include "log.hpp"
#include "results.hpp"
#include "utils.hpp"

// The renderer of this program. With angle brackets, the compiler searches the include directories in their order,
// so each program finds its own renderer.hpp: the main program src/renderer.hpp, a sample its own (see CMakeLists.txt).
#include <renderer.hpp>

// Reasons why the measured times of this run are not representative, for the results of a benchmark: e.g. Debug
// builds are slower, the validation layers cost time and VSync limits the frame rate. "" if there are none.
static std::string getTimingCaveats(const Config& config, const VulkanContext& context)
{
    std::string caveats;
    auto addCaveat = [&](const char* caveat) {
        caveats += (caveats.empty() ? "" : ";") + std::string(caveat);
    };
#ifndef NDEBUG
    addCaveat("debug_build");
#endif
    if (context.isValidationEnabled())
        addCaveat("validation");
    if (context.getVsync())
        addCaveat("vsync");
    if (config.logLevel == LogLevel::Debug)
        addCaveat("debug_log");
    return caveats;
}

// Appends the benchmark results to the results file: the run environment, all options, and mean, minimum and maximum
// of every measured time. Returns false if the file could not be written.
static bool writeBenchmarkResults(const Config& config, const VulkanContext& context, const TimingStatistics& times)
{
    Results results;
    results.addRunEnvironment(config.runName, context.getDeviceName(), getTimingCaveats(config, context));
    Args::addToResults(config, results);
    for (const auto& [name, statistics] : times.getEntries())
    {
        results.add(name + "_mean_ms", statistics.meanMs());
        results.add(name + "_min_ms", statistics.minMs);
        results.add(name + "_max_ms", statistics.maxMs);
    }
    results.add("validation_messages", context.getValidationMessageCount());

    LOG_INFO("Benchmark results: %s", results.toString().c_str());
    const std::string error = results.appendToFile(config.resultsFile);
    if (!error.empty())
    {
        LOG_ERROR("%s", error.c_str());
        return false;
    }
    LOG_INFO("Results appended to %s", config.resultsFile.c_str());
    return true;
}

int main(int argc, char** argv)
{
    // Parse the command line options, run with --help to see all of them
    Config config;
    try
    {
        config = Args::parse(argc, argv);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("%s", e.what());
        Args::printUsage(argv[0]);
        return -1;
    }
    if (config.showHelp)
    {
        Args::printUsage(argv[0]);
        return 0;
    }
    if (!logInit(config.logLevel, config.logFile))
        return -1;

    // If OpenMP is installed we can use it for CPU parallelization
    utils::printOpenMPVersion();

    // Initialize GLFW
    glfwInit();

    // Tell GLFW to not create an OpenGL context, we use Vulkan instead
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Create GLFW window
    std::string windowTitle = "Thesis Framework";
    GLFWwindow* window =
        glfwCreateWindow(config.windowWidth, config.windowHeight, windowTitle.c_str(), nullptr, nullptr);
    // Error check if the window fails to create
    if (window == nullptr)
    {
        LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }

    // Initialize Vulkan: instance, device, swapchain etc.
    VulkanContext context;
    try
    {
        // the renderer adds the extensions and features it needs; it does not exist yet, so the hooks are static
        context.init(window, config,
                     {.configureInstance = Renderer::configureInstance,
                      .configureGpu = Renderer::configureGpu,
                      .configureDevice = Renderer::configureDevice});
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("%s", e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Initialize ImGui
    Gui gui;
    gui.init(window, context);

    // Query the framebuffer size, this can differ from the window size on some systems
    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    // Create the renderer object
    Renderer renderer = Renderer(context, config, framebufferWidth, framebufferHeight);
    renderer.init();

    // Number of the current frame, counting from 0, used for --capture and --benchmark
    uint32_t frameNumber = 0;
    // false if saving the capture or the benchmark results failed, the program then exits with an error code
    bool success = true;

    if (config.isBenchmarking() && config.vsync)
        LOG_WARN("VSync limits the frame rate of the benchmark, use --no-vsync to measure the full speed.");

    // Time measurements: the CPU time of a whole frame (including waiting for the GPU and VSync), and the times of the
    // renderer. The GUI shows their statistics over the last second, a benchmark over its measured frames.
    CpuTimer frameTimer({"frame"});
    TimingStatistics currentSecondTimes, lastSecondTimes, benchmarkTimes;

    // FPS counter
    int frameCount = 0;
    double accumulatedTime = 0.0;
    bool vsync = context.getVsync();

    // Main while loop
    while (!glfwWindowShouldClose(window))
    {
        double startTime = glfwGetTime();
        frameTimer.beginFrame();
        frameTimer.begin(0);

        // Take care of all GLFW events
        glfwPollEvents();

        // Pause while the window is minimized
        int newFramebufferWidth, newFramebufferHeight;
        glfwGetFramebufferSize(window, &newFramebufferWidth, &newFramebufferHeight);
        if (newFramebufferWidth == 0 || newFramebufferHeight == 0)
        {
            glfwWaitEvents();
            continue;
        }

        // Recreate the swapchain and resize the renderer if the framebuffer / window size changed
        if (context.swapchainNeedsRecreation() || newFramebufferWidth != framebufferWidth
            || newFramebufferHeight != framebufferHeight)
        {
            framebufferWidth = newFramebufferWidth;
            framebufferHeight = newFramebufferHeight;
            context.recreateSwapchain(framebufferWidth, framebufferHeight);
            renderer.resize(framebufferWidth, framebufferHeight);
        }

        // Tell ImGui a new frame is about to begin
        gui.newFrame();

        // Process input after ImGui::NewFrame(): ImGui applies the input events of this frame there (e.g.
        // io.MouseWheel, which the camera reads, and io.WantCaptureMouse), and resets the mouse wheel in
        // ImGui::Render()
        renderer.processEvents(window);

        // ImGui elements
        ImGui::Begin("CG Thesis Framework GUI", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Hit F5 to reload shaders!");
        if (ImGui::Checkbox("VSync", &vsync))
            context.setVsync(vsync);
        renderer.drawGui();
        if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // the GPU executes frames later than the CPU records them, so the GPU times lag a few frames behind
            ImGui::Text("Last second: mean (min - max)");
            for (const auto& [name, statistics] : lastSecondTimes.getEntries())
                ImGui::Text("%-14s %7.3f ms (%.3f - %.3f)", name.c_str(), statistics.meanMs(), statistics.minMs,
                            statistics.maxMs);
        }
        ImGui::End();

        // Finalize the ImGui draw data
        gui.endFrame();

        // Begin recording the commands for this frame. If the swapchain is outdated, skip the frame.
        VkCommandBuffer cmd = context.beginFrame();
        if (cmd == VK_NULL_HANDLE)
            continue;
        FrameTarget target = context.getFrameTarget();

        // Draw frame from renderer
        renderer.runFrame(cmd, target);

        // --capture: copy the frame before the GUI is drawn on top of it
        const bool capture = frameNumber == config.captureFrame;
        if (capture)
            context.recordCapture(cmd);

        // Renders the ImGui elements
        gui.recordCommands(cmd, target);

        // Submit the commands and present the image on the screen
        context.endFrame();

        // Collect the times of this frame
        frameTimer.end(0);
        std::vector<Timing> timings;
        frameTimer.appendTimings(timings);
        renderer.appendTimings(timings);
        currentSecondTimes.add(timings);

        // --capture: save the frame once the GPU finished it
        if (capture)
            success &= context.saveCapture(config.captureFile);

        // --benchmark: measure the frames after the warmup, then write the results
        const uint32_t benchmarkEnd = config.benchmarkWarmupFrames + config.benchmarkFrames;
        if (config.isBenchmarking() && frameNumber >= config.benchmarkWarmupFrames && frameNumber < benchmarkEnd)
        {
            benchmarkTimes.add(timings);
            if (frameNumber + 1 == benchmarkEnd)
                success &= writeBenchmarkResults(config, context, benchmarkTimes);
        }

        // captures and benchmarks exit once they are done
        if (config.isAutomatedRun() && (!config.isCapturing() || frameNumber >= config.captureFrame)
            && (!config.isBenchmarking() || frameNumber + 1 >= benchmarkEnd))
            break;
        frameNumber++;

        // FPS counter
        double endTime = glfwGetTime();
        accumulatedTime += endTime - startTime;
        ++frameCount;
        if (1.0 < accumulatedTime)
        {
            assert(0 < frameCount);
            std::ostringstream oss;
            oss << windowTitle << " - " << frameCount / accumulatedTime << " fps";
            glfwSetWindowTitle(window, oss.str().c_str());
            accumulatedTime = 0.0;
            frameCount = 0;
            lastSecondTimes = currentSecondTimes;
            currentSecondTimes.clear();
        }
    }

    // Wait until the GPU finished all work before destroying anything
    vkDeviceWaitIdle(context.getDevice());

    // Destroy everything in reverse order of creation: renderer, GUI, Vulkan context
    renderer.shutdown();
    gui.shutdown();
    context.shutdown();

    // Delete window before ending the program
    glfwDestroyWindow(window);
    // Terminate GLFW before ending the program
    glfwTerminate();

    // A correct program does not cause validation messages. The error code lets scripts detect them, e.g. a test that
    // captures a frame.
    const uint32_t validationMessages = context.getValidationMessageCount();
    if (validationMessages > 0)
        LOG_ERROR("The validation layers reported %u messages, fix their causes!", validationMessages);
    logShutdown();
    return success && validationMessages == 0 ? 0 : 1;
}
