// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "gpu/vulkan_context.hpp"

struct GLFWwindow;

/// Sets up Dear ImGui with its GLFW and Vulkan backends and draws the GUI on top of the rendered frame.
/// The GUI elements themselves are defined in main.cpp between newFrame() and endFrame().
class Gui
{
public:
    /// Creates the ImGui context and initializes the GLFW and Vulkan backends.
    void init(GLFWwindow* window, const VulkanContext& context);
    /// Destroys the ImGui context and backends. The GPU must be idle when calling this.
    void shutdown();

    /// Starts a new ImGui frame. Call ImGui:: functions to create GUI elements after this.
    void newFrame();
    /// Finalizes the GUI elements of this frame.
    void endFrame();
    /// Records the commands to draw the GUI on top of the frame target.
    void recordCommands(VkCommandBuffer cmd, const FrameTarget& target);

private:
    VkFormat mColorFormat = VK_FORMAT_UNDEFINED;
};
