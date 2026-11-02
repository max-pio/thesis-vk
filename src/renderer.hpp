// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "args.hpp"
#include "camera.hpp"
#include "cpu_timer.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/vulkan_context.hpp"

#include <array>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

/// Renders the scene: a cloud of instanced cubes. This is the place to add your own rendering code.
/// The faces of the cubes show how to use a texture.
class Renderer
{
public:
    Renderer(VulkanContext& context, const Config& config, int framebufferWidth, int framebufferHeight);

    /// Hooks into the creation of the Vulkan instance and device, see ContextHooks. Static, because they are called
    /// by VulkanContext::init() before the renderer exists. Add the extensions and features your renderer needs.
    static void configureInstance(vkb::InstanceBuilder& builder, const vkb::SystemInfo& systemInfo);
    static void configureGpu(vkb::PhysicalDeviceSelector& selector);
    static void configureDevice(vkb::PhysicalDevice& gpu);

    /// Creates all buffers and pipelines. Called once before the main loop.
    void init();
    /// Recompiles the shaders from their source files and recreates the graphics pipeline.
    /// If compilation fails, the old pipeline is kept.
    void reloadShaders();
    /// Records the commands to render a frame into the command buffer. Called once per frame.
    void runFrame(VkCommandBuffer cmd, const FrameTarget& target);
    /// Destroys all resources in inverse order of their creation. Called once after the main loop.
    void shutdown();

    /// Processes keyboard and mouse input. Called once per frame before runFrame().
    void processEvents(GLFWwindow* window);
    /// Recreates all framebuffer sized resources. Called after the swapchain was recreated.
    void resize(int framebufferWidth, int framebufferHeight);
    /// Draws the GUI elements of the renderer. Called between ImGui::Begin() and ImGui::End() in main().
    void drawGui();
    /// Appends the CPU and GPU times of the newest measured frame, e.g. to show them in the GUI or for benchmarks.
    void appendTimings(std::vector<Timing>& timings) const;

    /// Color of the cubes, can be changed in the GUI
    glm::vec4 mColor;
    /// Let the cubes move up and down
    bool mAnimate = true;

private:
    float getAspectRatio() const;
    void createDepthImage();
    /// Creates the texture on the cube faces, its sampler and the descriptor set to access it in the shaders
    void createPatternTexture();

    VulkanContext& mContext;
    const Config& mConfig;

    // Shaders and pipeline
    std::string mVertexShaderPath;
    std::string mFragmentShaderPath;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkPipelineLayout mPipelineLayout;
    VkPipeline mPipeline;
    /// compiler output of the last failed shader reload, shown in the GUI, "" after a successful reload
    std::string mShaderError;

    // Texture. Shaders access it through a descriptor set: textures can't be passed as buffer addresses.
    Image mPattern;     ///< pattern on the faces of the cubes
    VkSampler mSampler; ///< trilinear filtering, repeats texture coordinates outside of [0, 1]
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSet mDescriptorSet;

    /// Push constants are a small block of data that is passed to the shaders with each draw call. Every GPU supports
    /// at least 128 bytes (VkPhysicalDeviceLimits::maxPushConstantsSize). The layout must match the push_constant block
    /// in the shaders.
    struct PushConstants
    {
        glm::mat4 mvp;                   ///< model-view-projection matrix
        glm::vec4 color;                 ///< color of the cubes
        VkDeviceAddress vertexBuffer;    ///< GPU address of the vertex buffer, accessed via buffer_reference
        VkDeviceAddress positionsBuffer; ///< GPU address of the instance positions buffer of this frame
    };

    /// Depth buffer for correct ordering of overlapping objects
    static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    Image mDepthImage;

    Camera mCamera;
    glm::ivec2 mFramebufferSize;

    /// True while F5 is held down, so that the shaders are reloaded only once per key press
    bool mF5Pressed;

    /// glfwGetTime() at the previous frame, for the time step of the animation; set in init()
    double mLastTime = 0.0;

    // Time measurements. The indices of the timers are given by these constants, their names in init().
    static constexpr uint32_t GPU_TIMER_SCENE = 0;
    static constexpr uint32_t CPU_TIMER_ANIMATION = 0;
    GpuTimer mGpuTimer;
    CpuTimer mCpuTimer{{"animation"}};

    /// Vertex buffer of the cube
    Buffer mVertexBuffer;
    /// Positions of the instanced cubes without animation
    std::vector<glm::vec3> mBasePositions;
    /// Animated positions of the cubes, written by the CPU every frame. Up to FRAMES_IN_FLIGHT frames are rendered at
    /// the same time, so each frame in flight needs its own buffer: otherwise the CPU would overwrite positions that
    /// the GPU still reads for the previous frame. Data that fits into the push constants (like the matrix and color
    /// above) does not need this, because push constants are copied into the command buffer when they are recorded.
    std::array<Buffer, VulkanContext::FRAMES_IN_FLIGHT> mPositionsBuffers;
    /// Time of the animation in seconds
    float mTime = 0.0f;

    /// Vertex positions of a cube, 12 triangles with counter-clockwise winding
    // clang-format off
    static constexpr float CUBE_VERTICES[] = {
            -1.0f,-1.0f,-1.0f,
            -1.0f,-1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            1.0f, 1.0f,-1.0f,
            -1.0f,-1.0f,-1.0f,
            -1.0f, 1.0f,-1.0f,
            1.0f,-1.0f, 1.0f,
            -1.0f,-1.0f,-1.0f,
            1.0f,-1.0f,-1.0f,
            1.0f, 1.0f,-1.0f,
            1.0f,-1.0f,-1.0f,
            -1.0f,-1.0f,-1.0f,
            -1.0f,-1.0f,-1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f,-1.0f,
            1.0f,-1.0f, 1.0f,
            -1.0f,-1.0f, 1.0f,
            -1.0f,-1.0f,-1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f,-1.0f, 1.0f,
            1.0f,-1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f,-1.0f,-1.0f,
            1.0f, 1.0f,-1.0f,
            1.0f,-1.0f,-1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f,-1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f,-1.0f,
            -1.0f, 1.0f,-1.0f,
            1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f,-1.0f,
            -1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            1.0f,-1.0f, 1.0f
    };
    // clang-format on
};
