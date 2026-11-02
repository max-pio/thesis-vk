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
#include <span>
#include <string>
#include <vector>

struct GLFWwindow;

/// Sample: deferred shading with a G-buffer, rendered at a resolution independent of the window size.
///
/// Each frame has three passes:
///  1. G-buffer pass: renders the instanced cubes into several images at the render resolution (multiple render
///     targets): albedo (surface color), normal and depth.
///  2. Lighting pass: draws one triangle that covers the whole image. For each pixel, the fragment shader reads the
///     G-buffer, reconstructs the world position from the depth, and adds up the light of many point lights.
///  3. Blit: copies the lit image to the swapchain image and scales it to the window size.
///
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

    /// Creates all buffers, images and pipelines. Called once before the main loop.
    void init();
    /// Recompiles the shaders from their source files and recreates the pipelines.
    /// If compilation fails, the old pipelines are kept.
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

private:
    /// What the lighting pass shows, see shader/lighting.frag
    enum ViewMode : uint32_t
    {
        VIEW_LIT,
        VIEW_ALBEDO,
        VIEW_NORMAL,
        VIEW_DEPTH,
        VIEW_POSITION,
    };

    /// A point light, the same struct as in shader/lighting.frag (scalar layout)
    struct Light
    {
        glm::vec3 position;
        float radius; ///< the light has no effect beyond this distance
        glm::vec3 color;
        float intensity;
    };

    /// Push constants of the G-buffer pass, must match shader/gbuffer_scene.glsl
    struct ScenePushConstants
    {
        glm::mat4 viewProjection;        ///< world space to projection space
        glm::vec4 color;                 ///< color of the cubes
        glm::vec3 cameraPosition;        ///< to orient the normals towards the camera
        uint32_t padding;                ///< the next member needs 8 byte alignment, in C++ as in GLSL
        VkDeviceAddress vertexBuffer;    ///< GPU address of the vertex buffer
        VkDeviceAddress positionsBuffer; ///< GPU address of the instance positions buffer of this frame
    };

    /// Push constants of the lighting pass, must match shader/lighting.frag
    struct LightingPushConstants
    {
        glm::mat4 inverseViewProjection; ///< projection space to world space, to reconstruct positions from depth
        glm::vec3 cameraPosition;
        uint32_t viewMode;           ///< a ViewMode
        VkDeviceAddress lightBuffer; ///< GPU address of the lights of this frame
        uint32_t lightCount;
    };

    float getAspectRatio() const;
    /// Creates the descriptor set layouts, pipeline layouts, descriptor pool and the two descriptor sets
    void createDescriptorsAndLayouts();
    /// Creates the texture of the cube faces and writes it into its descriptor set
    void createPatternTexture();
    /// (Re)creates the G-buffer and the lit image at the render resolution (window size * render scale), and writes
    /// the G-buffer into the descriptor set of the lighting pass. The GPU must not use the old images anymore.
    void createRenderTargets();
    void destroyRenderTargets();
    /// Creates a graphics pipeline with dynamic rendering into the given attachment formats, with a depth test if
    /// depthFormat is not VK_FORMAT_UNDEFINED. Returns VK_NULL_HANDLE and sets errorMessage if a shader does not
    /// compile.
    VkPipeline createPipeline(const std::string& vertexShader, const std::string& fragmentShader,
                              VkPipelineLayout layout, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
                              VkCullModeFlags cullMode, std::string& errorMessage);

    VulkanContext& mContext;
    const Config& mConfig;

    // Settings, changed in the GUI
    glm::vec4 mColor = {1.0f, 0.55f, 0.0f, 1.0f};
    bool mAnimate = true;
    /// render resolution = window size * render scale
    float mRenderScale = 1.0f;
    ViewMode mViewMode = VIEW_LIT;
    uint32_t mLightCount = 64;

    // Pipelines and descriptors
    VkDescriptorSetLayout mPatternSetLayout = VK_NULL_HANDLE; ///< G-buffer pass: the texture of the cube faces
    VkDescriptorSetLayout mGBufferSetLayout = VK_NULL_HANDLE; ///< lighting pass: albedo, normal and depth
    VkPipelineLayout mScenePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout mLightingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mScenePipeline = VK_NULL_HANDLE;
    VkPipeline mLightingPipeline = VK_NULL_HANDLE;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet mPatternSet = VK_NULL_HANDLE;
    VkDescriptorSet mGBufferSet = VK_NULL_HANDLE;
    /// compiler output of the last failed shader reload, shown in the GUI, "" after a successful reload
    std::string mShaderError;

    // Texture of the cube faces
    Image mPattern;
    VkSampler mPatternSampler = VK_NULL_HANDLE; ///< trilinear filtering, repeats texture coordinates
    VkSampler mNearestSampler = VK_NULL_HANDLE; ///< reads single texels of the G-buffer

    // Render targets at the render resolution
    static constexpr VkFormat ALBEDO_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat NORMAL_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT; ///< signed, normals have negative values
    static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat LIT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    Image mAlbedo;
    Image mNormal;
    Image mDepth;
    Image mLit; ///< output of the lighting pass, copied to the swapchain image
    VkExtent2D mRenderSize = {0, 0};
    /// the render scale of the current render targets, they are recreated if mRenderScale changes
    float mRenderTargetScale = 0.0f;

    // Scene
    Camera mCamera;
    glm::ivec2 mFramebufferSize;
    /// glfwGetTime() at the previous frame, for the time step of the animation; set in init()
    double mLastTime = 0.0;
    float mTime = 0.0f;
    Buffer mVertexBuffer;
    std::vector<glm::vec3> mBasePositions;
    std::array<Buffer, VulkanContext::FRAMES_IN_FLIGHT> mPositionsBuffers;
    static constexpr uint32_t MAX_LIGHTS = 256;
    std::vector<Light> mBaseLights; ///< lights without animation
    /// the lights are animated on the CPU, so each frame in flight needs its own buffer (see mPositionsBuffers)
    std::array<Buffer, VulkanContext::FRAMES_IN_FLIGHT> mLightBuffers;

    /// True while F5 is held down, so that the shaders are reloaded only once per key press
    bool mF5Pressed = false;

    // Time measurements
    static constexpr uint32_t GPU_TIMER_GBUFFER = 0;
    static constexpr uint32_t GPU_TIMER_LIGHTING = 1;
    static constexpr uint32_t GPU_TIMER_BLIT = 2;
    static constexpr uint32_t CPU_TIMER_ANIMATION = 0;
    GpuTimer mGpuTimer;
    CpuTimer mCpuTimer{{"animation"}};

    /// Vertex positions of a cube, 12 triangles with counter-clockwise winding (as in the cube demo)
    // clang-format off
    static constexpr float CUBE_VERTICES[] = {
        -1,-1,-1,  -1,-1, 1,  -1, 1, 1,     1, 1,-1,  -1,-1,-1,  -1, 1,-1,
         1,-1, 1,  -1,-1,-1,   1,-1,-1,     1, 1,-1,   1,-1,-1,  -1,-1,-1,
        -1,-1,-1,  -1, 1, 1,  -1, 1,-1,     1,-1, 1,  -1,-1, 1,  -1,-1,-1,
        -1, 1, 1,  -1,-1, 1,   1,-1, 1,     1, 1, 1,   1,-1,-1,   1, 1,-1,
         1,-1,-1,   1, 1, 1,   1,-1, 1,     1, 1, 1,   1, 1,-1,  -1, 1,-1,
         1, 1, 1,  -1, 1,-1,  -1, 1, 1,     1, 1, 1,  -1, 1, 1,   1,-1, 1,
    };
    // clang-format on
};
