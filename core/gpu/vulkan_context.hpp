// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;
struct Config;

/// A buffer and its memory allocation
struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize size = 0;
    VkDeviceAddress address = 0; ///< GPU address for buffer_reference access in shaders
    void* mapped = nullptr;      ///< CPU pointer, only set for host visible buffers
};

/// An image, its memory allocation, and a view to the whole image
struct Image
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent = {0, 0, 0};
    uint32_t mipLevels = 1;
    VkImageType type = VK_IMAGE_TYPE_2D;
};

/// The swapchain image that is rendered to in the current frame
struct FrameTarget
{
    VkImage image;
    VkImageView view;
    VkFormat format;
    VkExtent2D extent;
};

/// Hooks with which a renderer adds the instance extensions, device extensions and features that it needs, without
/// changing the VulkanContext, which is shared by all renderers. Every hook is optional. The renderer provides them
/// as static functions (Renderer::configureInstance, ...), because it is created after the context. The builders
/// are from vk-bootstrap, see https://github.com/charles-lunarg/vk-bootstrap/blob/main/docs/getting_started.md
struct ContextHooks
{
    /// Called before the instance is created, e.g. builder.enable_extension(..) for extensions that must exist
    /// (the instance creation fails otherwise). Use systemInfo.is_extension_available(..) for optional ones.
    std::function<void(vkb::InstanceBuilder& builder, const vkb::SystemInfo& systemInfo)> configureInstance;
    /// Called before the GPU is selected: everything added here is required, GPUs without it are not selected.
    /// E.g. selector.add_required_extension(..) and selector.add_required_extension_features(featureStruct).
    std::function<void(vkb::PhysicalDeviceSelector& selector)> configureGpu;
    /// Called after the GPU was selected and before the device is created: everything added here is optional, e.g.
    /// gpu.enable_extension_if_present(..). Check with VulkanContext::isDeviceExtensionEnabled(..) afterwards.
    std::function<void(vkb::PhysicalDevice& gpu)> configureDevice;
};

/// Owns the core Vulkan objects (instance, device, queue, swapchain, memory allocator) and the per-frame
/// synchronization. All images are used in VK_IMAGE_LAYOUT_GENERAL (VK_KHR_unified_image_layouts),
/// swapchain images are only transitioned to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presentation.
class VulkanContext
{
public:
    /// Number of frames the CPU may record ahead of the GPU
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

    /// Creates the Vulkan instance, selects a GPU, and creates the logical device, the memory allocator, and the
    /// swapchain. Uses config.vsync and the validation options. Throws a std::runtime_error if no suitable GPU is
    /// found.
    void init(GLFWwindow* window, const Config& config, const ContextHooks& hooks = {});
    /// Destroys all Vulkan objects in inverse order of their creation. The GPU must be idle when calling this.
    void shutdown();

    /// Waits for the next frame slot, acquires a swapchain image, and begins recording its command buffer. The
    /// swapchain image is transitioned to VK_IMAGE_LAYOUT_GENERAL. Returns VK_NULL_HANDLE if the swapchain is out of
    /// date; call recreateSwapchain() and skip this frame in this case.
    VkCommandBuffer beginFrame();
    /// Transitions the swapchain image for presentation, submits the command buffer, and presents the image.
    void endFrame();
    /// The swapchain image of the current frame
    FrameTarget getFrameTarget() const;

    /// True if the swapchain is out of date or VSync was changed, e.g. after the window was resized.
    bool swapchainNeedsRecreation() const { return mSwapchainOutdated; }
    /// Recreates the swapchain with the given size. The old swapchain is destroyed.
    void recreateSwapchain(int framebufferWidth, int framebufferHeight);
    /// Enables or disables VSync. The swapchain is recreated before the next frame.
    void setVsync(bool vsync);
    bool getVsync() const { return mVsync; }

    /// Records commands with the given function into a one-time command buffer, submits it, and waits for completion.
    /// Use this for uploads and other setup work outside of the frame loop.
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

    /// Creates a buffer. Device local buffers are fastest for GPU access. Host visible buffers are persistently mapped
    /// (Buffer::mapped) and can be written from the CPU every frame. All buffers have a device address.
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible = false);
    /// Creates a device local buffer and uploads the data to it through a staging buffer.
    Buffer createBufferWithData(VkDeviceSize size, VkBufferUsageFlags usage, const void* data);
    void destroyBuffer(Buffer& buffer);

    /// Creates an image and a view to all of its mip levels. The image is in VK_IMAGE_LAYOUT_UNDEFINED, transition it
    /// once to VK_IMAGE_LAYOUT_GENERAL before using it. The type is VK_IMAGE_TYPE_1D, _2D or _3D; images with
    /// extent.depth > 1 are always 3D. 1D images need extent.height == 1.
    Image createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                      uint32_t mipLevels = 1, VkImageType type = VK_IMAGE_TYPE_2D);
    /// Creates a texture (sampled image) of the given type and uploads the data of mip level 0 through a staging
    /// buffer. The data is tightly packed: x varies fastest, then y, then z, e.g. width * height * depth * 4 bytes for
    /// VK_FORMAT_R8G8B8A8_UNORM. With generateMipmaps, all smaller mip levels are computed on the GPU. The texture is
    /// in VK_IMAGE_LAYOUT_GENERAL and ready to be sampled in any shader stage when this function returns.
    Image createTexture(VkImageType type, VkExtent3D extent, VkFormat format, const void* data, VkDeviceSize dataSize,
                        bool generateMipmaps = true);
    void destroyImage(Image& image);

    /// Records a copy of the current swapchain image into a host visible buffer, e.g. to save a screenshot. Call it
    /// between beginFrame() and endFrame(), after everything that should be visible was rendered.
    void recordCapture(VkCommandBuffer cmd);
    /// Waits until the GPU finished the captured frame and saves it as PNG file. Returns false on failure.
    bool saveCapture(const std::string& path);

    VkInstance getInstance() const { return mInstance.instance; }
    VkPhysicalDevice getPhysicalDevice() const { return mPhysicalDevice.physical_device; }
    VkDevice getDevice() const { return mDevice.device; }
    VkQueue getQueue() const { return mQueue; }
    uint32_t getQueueFamily() const { return mQueueFamily; }
    VmaAllocator getAllocator() const { return mAllocator; }
    VkFormat getSwapchainFormat() const { return mSwapchain.image_format; }
    uint32_t getSwapchainImageCount() const { return mSwapchain.image_count; }
    uint32_t getFrameIndex() const { return mFrameIndex; }
    const std::string& getDeviceName() const { return mPhysicalDevice.name; }
    bool hasUnifiedImageLayouts() const { return mUnifiedImageLayouts; }
    /// True if the validation layers are enabled
    bool isValidationEnabled() const { return mValidationEnabled; }
    /// Number of messages (errors and warnings) the validation layers reported so far. A correct program has none.
    uint32_t getValidationMessageCount() const { return mValidationMessageCount; }
    /// True if the given device extension was enabled
    bool isDeviceExtensionEnabled(const std::string& extension) const;

private:
    void createSwapchain(int framebufferWidth, int framebufferHeight);
    void destroySwapchain();

    vkb::Instance mInstance;
    vkb::PhysicalDevice mPhysicalDevice;
    vkb::Device mDevice;
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkQueue mQueue = VK_NULL_HANDLE;
    uint32_t mQueueFamily = 0;
    VmaAllocator mAllocator = nullptr;
    bool mUnifiedImageLayouts = false;
    bool mValidationEnabled = false;
    /// incremented by the debug callback of the validation layers, which may be called from any thread
    std::atomic<uint32_t> mValidationMessageCount = 0;

    // Swapchain
    vkb::Swapchain mSwapchain;
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    /// Signaled when rendering to a swapchain image finished, one per swapchain image
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    bool mSwapchainOutdated = false;
    bool mVsync = true;
    uint32_t mImageIndex = 0;

    /// Per frame in flight resources
    struct Frame
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;              ///< signaled when the GPU finished the frame
        VkSemaphore imageAvailable = VK_NULL_HANDLE; ///< signaled when the swapchain image is ready
    };
    std::array<Frame, FRAMES_IN_FLIGHT> mFrames;
    uint32_t mFrameIndex = 0;

    // Resources for immediateSubmit
    VkCommandPool mImmediatePool = VK_NULL_HANDLE;
    VkFence mImmediateFence = VK_NULL_HANDLE;

    /// Copy of a swapchain image, see recordCapture()
    Buffer mCaptureBuffer;
    VkExtent2D mCaptureExtent = {0, 0};
};
