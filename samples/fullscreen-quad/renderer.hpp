// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "args.hpp"
#include "cpu_timer.hpp"
#include "gpu/vulkan_context.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

/// Sample: the smallest possible renderer. Draws a fullscreen quad that shows its screen space uv coordinates as
/// color (red = u, green = v), multiplied by a tint color from the GUI. It has the same functions as the Renderer of
/// the cube demo (src/renderer.hpp), so that both use the same main loop (src/main.cpp).
class Renderer
{
public:
    Renderer(VulkanContext& context, const Config& config, int framebufferWidth, int framebufferHeight);

    /// Hooks into the creation of the Vulkan instance and device, see ContextHooks. Static, because they are called
    /// by VulkanContext::init() before the renderer exists. Add the extensions and features your renderer needs.
    static void configureInstance(vkb::InstanceBuilder& builder, const vkb::SystemInfo& systemInfo);
    static void configureGpu(vkb::PhysicalDeviceSelector& selector);
    static void configureDevice(vkb::PhysicalDevice& gpu);

    /// Creates the pipeline. Called once before the main loop.
    void init();
    /// Recompiles the shaders and recreates the pipeline. If compilation fails, the old pipeline is kept.
    void reloadShaders();
    /// Records the commands to render a frame into the command buffer. Called once per frame.
    void runFrame(VkCommandBuffer cmd, const FrameTarget& target);
    /// Destroys all resources in inverse order of their creation. Called once after the main loop.
    void shutdown();

    /// Reloads the shaders when F5 is pressed. Called once per frame before runFrame().
    void processEvents(GLFWwindow* window);
    /// Nothing to recreate: the quad is drawn at the size of the swapchain image.
    void resize(int framebufferWidth, int framebufferHeight);
    /// Draws the GUI elements of the renderer. Called between ImGui::Begin() and ImGui::End() in main().
    void drawGui();
    /// This sample has no timers, but main() calls this.
    void appendTimings(std::vector<Timing>& timings) const;

private:
    VulkanContext& mContext;

    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mPipeline = VK_NULL_HANDLE;
    /// compiler output of the last failed shader reload, shown in the GUI, "" after a successful reload
    std::string mShaderError;
    /// True while F5 is held down, so that the shaders are reloaded only once per key press
    bool mF5Pressed = false;

    /// Tint of the gradient, can be changed in the GUI
    glm::vec4 mTint{1.0f, 1.0f, 1.0f, 1.0f};
};
