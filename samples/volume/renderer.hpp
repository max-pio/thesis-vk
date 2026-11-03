// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include "args.hpp"
#include "camera.hpp"
#include "cpu_timer.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/vulkan_context.hpp"
#include "volume.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

/// Sample: loads a raw 3D scalar volume from disk (utils::loadVolume(), see README), uploads it as 3D texture and
/// renders it with a ray marcher in the fragment shader of a fullscreen quad. For each pixel, a ray from the camera
/// is intersected with the bounding box of the volume and then sampled in small steps, in one of three ways:
///  - DVR (direct volume rendering): emission-absorption model, the samples are mapped to color and opacity with a
///    simple transfer function and blended front to back
///  - Isosurface: shows the first point where the volume crosses an iso value, shaded with the gradient as normal
/// The class has the same functions as the Renderer of the cube demo (src/renderer.hpp), so that both use the same
/// main loop (src/main.cpp).
class Renderer
{
public:
    Renderer(VulkanContext& context, const Config& config, int framebufferWidth, int framebufferHeight);

    /// Hooks into the creation of the Vulkan instance and device, see ContextHooks. Static, because they are called
    /// by VulkanContext::init() before the renderer exists. Add the extensions and features your renderer needs.
    static void configureInstance(vkb::InstanceBuilder& builder, const vkb::SystemInfo& systemInfo);
    static void configureGpu(vkb::PhysicalDeviceSelector& selector);
    static void configureDevice(vkb::PhysicalDevice& gpu);

    /// Loads the volume, creates the texture and the pipeline. Called once before the main loop.
    void init();
    /// Recompiles the shaders and recreates the pipeline. If compilation fails, the old pipeline is kept.
    void reloadShaders();
    /// Records the commands to render a frame into the command buffer. Called once per frame.
    void runFrame(VkCommandBuffer cmd, const FrameTarget& target);
    /// Destroys all resources in inverse order of their creation. Called once after the main loop.
    void shutdown();

    /// Moves the camera and reloads the shaders when F5 is pressed. Called once per frame before runFrame().
    void processEvents(GLFWwindow* window);
    /// Nothing to recreate: the aspect ratio is taken from the swapchain image in runFrame().
    void resize(int framebufferWidth, int framebufferHeight);
    /// Draws the GUI elements of the renderer. Called between ImGui::Begin() and ImGui::End() in main().
    void drawGui();
    /// Appends the CPU and GPU times of the newest measured frame, e.g. to show them in the GUI or for benchmarks.
    void appendTimings(std::vector<Timing>& timings) const;

private:
    /// Rendering modes, the same values as MODE_* in shader/volume.frag
    enum RenderMode : int
    {
        MODE_DVR,
        MODE_ISOSURFACE,
    };

    /// Push constants, the same layout as in shader/volume.frag (scalar layout: a vec3 has 12 bytes)
    struct PushConstants
    {
        glm::mat4 clipToWorld; ///< inverse of the camera's world -> clip space matrix, to create rays
        glm::vec3 cameraPos;
        float stepSize;        ///< distance between two samples along a ray in world space
        glm::vec3 boxHalfSize; ///< half size of the bounding box of the volume in world space, centered at the origin
        float windowMin;       ///< voxel values <= windowMin are transparent (DVR) or black
        glm::vec3 voxelSize;   ///< in world space, to scale the gradient
        float windowMax;       ///< voxel values >= windowMax are fully opaque (DVR) or white
        float density;         ///< opacity scale of the DVR, larger = more opaque
        float isoValue;
        int mode;
        int useColormap; ///< 0: grayscale, 1: color map
    };

    VulkanContext& mContext;
    const Config& mConfig;

    Camera mCamera;
    double mLastTime = 0.0;

    // The volume on the CPU is only needed for its properties after the upload
    Volume mVolume;
    Image mVolumeTexture{};
    VkSampler mSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mPipeline = VK_NULL_HANDLE;

    GpuTimer mGpuTimer;

    /// compiler output of the last failed shader reload, shown in the GUI, "" after a successful reload
    std::string mShaderError;
    /// True while F5 is held down, so that the shaders are reloaded only once per key press
    bool mF5Pressed = false;

    // Settings of the GUI
    int mMode = MODE_DVR;
    glm::vec3 mSpacing{1.0f};      ///< voxel spacing, the raw file does not contain it
    glm::vec2 mWindow{0.0f, 1.0f}; ///< value range that is mapped to opacity / brightness, initially the data range
    float mDensity = 8.0f;
    float mIsoValue = 0.5f;
    float mSamplesPerVoxel = 2.0f;
    bool mUseColormap = true;
};
