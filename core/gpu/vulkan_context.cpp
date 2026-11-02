// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "vulkan_context.hpp"
#include "args.hpp"
#include "gpu_utils.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

// Returns the value of a vk-bootstrap result or throws an exception with its error message.
template <typename T>
static T unwrap(vkb::Result<T> result, const std::string& what)
{
    if (!result)
        throw std::runtime_error(what + ": " + result.error().message());
    return result.value();
}

// Called by the validation layers for each message. Writes the message to the log and counts it.
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                    VkDebugUtilsMessageTypeFlagsEXT type,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                    void* userData)
{
    const char* typeName = (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "performance" : "validation";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("[%s] %s", typeName, callbackData->pMessage);
    else
        LOG_WARN("[%s] %s", typeName, callbackData->pMessage);
    (*static_cast<std::atomic<uint32_t>*>(userData))++;
    // returning VK_FALSE continues the Vulkan call that caused the message
    return VK_FALSE;
}

void VulkanContext::init(GLFWwindow* window, const Config& config, const ContextHooks& hooks)
{
    mVsync = config.vsync;

    // Instance --------------------------------------------------------------------------------------------------------
    // GLFW tells us which instance extensions are required to create a surface for the window
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr)
        throw std::runtime_error("GLFW: Vulkan is not supported on this system");

    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Thesis Framework")
        .require_api_version(1, 3, 0)
        .enable_extensions(glfwExtensionCount, glfwExtensions);
    const vkb::SystemInfo systemInfo = unwrap(vkb::SystemInfo::get_system_info(), "Failed to query Vulkan system info");
    if (hooks.configureInstance)
        hooks.configureInstance(instanceBuilder, systemInfo);
    // Validation layers check for API misuse and report errors and warnings to debugCallback(). They are enabled by
    // default in Debug builds (see Config::validation), since they cost performance. They are part of the Vulkan SDK.
    if (config.validation)
    {
        if (systemInfo.validation_layers_available)
        {
            mValidationEnabled = true;
            instanceBuilder.enable_validation_layers()
                .set_debug_callback(debugCallback)
                .set_debug_callback_user_data_pointer(&mValidationMessageCount);
            // synchronization validation finds many missing or wrong barriers (read-after-write hazards etc.)
            if (config.syncValidation)
                instanceBuilder.add_validation_feature_enable(
                    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
        }
        else
        {
            LOG_WARN("Validation layers not available, install the Vulkan SDK.");
        }
    }
    mInstance = unwrap(instanceBuilder.build(), "Failed to create Vulkan instance");

    VK_CHECK(glfwCreateWindowSurface(mInstance.instance, window, nullptr, &mSurface));

    // Physical device -------------------------------------------------------------------------------------------------
    // Required core features. Renderer-specific features and extensions are added in Renderer::configureGpu(..) and
    // Renderer::configureDevice(..) instead, so that this code stays the same for all renderers.
    const VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE, // simpler pipeline barriers and queue submits
        .dynamicRendering = VK_TRUE, // render without VkRenderPass and VkFramebuffer objects
    };
    const VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .scalarBlockLayout = VK_TRUE,   // C-like memory layout of buffers in shaders, e.g. vec3 is 12 bytes
        .bufferDeviceAddress = VK_TRUE, // access buffers through pointers (buffer_reference) in shaders
    };

    vkb::PhysicalDeviceSelector selector(mInstance);
    selector.set_surface(mSurface)
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12);
    // the renderer can require more extensions and features, GPUs without them are not selected
    if (hooks.configureGpu)
        hooks.configureGpu(selector);
    mPhysicalDevice = unwrap(selector.select(), "Failed to find a suitable GPU");

    // VK_KHR_unified_image_layouts allows us to use VK_IMAGE_LAYOUT_GENERAL for (almost) everything without
    // performance penalty, so that we don't have to track image layouts. If the GPU or driver does not support the
    // extension, the GENERAL layout is still valid, but may be slower on some GPUs.
#ifdef VK_KHR_unified_image_layouts
    const VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unifiedImageLayouts{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
        .unifiedImageLayouts = VK_TRUE,
    };
    mUnifiedImageLayouts = mPhysicalDevice.enable_extension_if_present(VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME)
                           && mPhysicalDevice.enable_extension_features_if_present(unifiedImageLayouts);
#endif
    // the renderer can enable optional extensions and features here
    if (hooks.configureDevice)
        hooks.configureDevice(mPhysicalDevice);
    LOG_INFO("GPU: %s", mPhysicalDevice.name.c_str());
    LOG_INFO("VK_KHR_unified_image_layouts %s", mUnifiedImageLayouts ? "enabled." : "not available.");
    LOG_INFO("Validation layers %s", !mValidationEnabled ? "off." : config.syncValidation ? "on (with sync)." : "on.");

    // Logical device and queue ----------------------------------------------------------------------------------------
    vkb::DeviceBuilder deviceBuilder(mPhysicalDevice);
    mDevice = unwrap(deviceBuilder.build(), "Failed to create Vulkan device");
    // For simplicity, we use a single queue for graphics, compute, transfer, and presentation
    mQueue = unwrap(mDevice.get_queue(vkb::QueueType::graphics), "Failed to get graphics queue");
    mQueueFamily = unwrap(mDevice.get_queue_index(vkb::QueueType::graphics), "Failed to get graphics queue");

    // Memory allocator ------------------------------------------------------------------------------------------------
    const VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = mPhysicalDevice.physical_device,
        .device = mDevice.device,
        .instance = mInstance.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &mAllocator));

    // Per frame command buffers and synchronization -------------------------------------------------------------------
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = mQueueFamily,
    };
    const VkFenceCreateInfo signaledFenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT, // signaled, so that waiting for the first frame returns
    };
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (Frame& frame : mFrames)
    {
        VK_CHECK(vkCreateCommandPool(mDevice.device, &poolInfo, nullptr, &frame.commandPool));
        const VkCommandBufferAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VK_CHECK(vkAllocateCommandBuffers(mDevice.device, &allocInfo, &frame.commandBuffer));
        VK_CHECK(vkCreateFence(mDevice.device, &signaledFenceInfo, nullptr, &frame.fence));
        VK_CHECK(vkCreateSemaphore(mDevice.device, &semaphoreInfo, nullptr, &frame.imageAvailable));
    }

    const VkCommandPoolCreateInfo immediatePoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = mQueueFamily,
    };
    VK_CHECK(vkCreateCommandPool(mDevice.device, &immediatePoolInfo, nullptr, &mImmediatePool));
    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(mDevice.device, &fenceInfo, nullptr, &mImmediateFence));

    // Swapchain -------------------------------------------------------------------------------------------------------
    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    createSwapchain(framebufferWidth, framebufferHeight);
}

void VulkanContext::shutdown()
{
    vkDeviceWaitIdle(mDevice.device);

    destroyBuffer(mCaptureBuffer);
    destroySwapchain();
    vkb::destroy_swapchain(mSwapchain);

    vkDestroyFence(mDevice.device, mImmediateFence, nullptr);
    vkDestroyCommandPool(mDevice.device, mImmediatePool, nullptr);
    for (Frame& frame : mFrames)
    {
        vkDestroySemaphore(mDevice.device, frame.imageAvailable, nullptr);
        vkDestroyFence(mDevice.device, frame.fence, nullptr);
        vkDestroyCommandPool(mDevice.device, frame.commandPool, nullptr);
    }

    vmaDestroyAllocator(mAllocator);
    vkb::destroy_device(mDevice);
    vkb::destroy_surface(mInstance, mSurface);
    vkb::destroy_instance(mInstance);
}

bool VulkanContext::isDeviceExtensionEnabled(const std::string& extension) const
{
    const std::vector<std::string> enabled = mPhysicalDevice.get_extensions();
    return std::find(enabled.begin(), enabled.end(), extension) != enabled.end();
}

// Swapchain -----------------------------------------------------------------------------------------------------------

void VulkanContext::createSwapchain(int framebufferWidth, int framebufferHeight)
{
    vkb::SwapchainBuilder builder(mDevice, mSurface);
    // A UNORM format matches the colors of the OpenGL framework. Use an _SRGB format for gamma correct output.
    builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_extent(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight))
        // TRANSFER_DST to blit images into the swapchain, TRANSFER_SRC to save screenshots
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        .set_old_swapchain(mSwapchain);
    if (mVsync)
    {
        // FIFO waits for the vertical blank of the monitor and is always supported
        builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
    }
    else
    {
        // MAILBOX and IMMEDIATE do not limit the frame rate, the first call has the lowest priority
        builder.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
            .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
    }
    vkb::Swapchain newSwapchain = unwrap(builder.build(), "Failed to create swapchain");
    vkb::destroy_swapchain(mSwapchain);
    mSwapchain = newSwapchain;

    mSwapchainImages = unwrap(mSwapchain.get_images(), "Failed to get swapchain images");
    mSwapchainImageViews = unwrap(mSwapchain.get_image_views(), "Failed to get swapchain image views");

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    mRenderFinishedSemaphores.resize(mSwapchainImages.size());
    for (VkSemaphore& semaphore : mRenderFinishedSemaphores)
        VK_CHECK(vkCreateSemaphore(mDevice.device, &semaphoreInfo, nullptr, &semaphore));

    mSwapchainOutdated = false;
}

void VulkanContext::destroySwapchain()
{
    for (VkSemaphore semaphore : mRenderFinishedSemaphores)
        vkDestroySemaphore(mDevice.device, semaphore, nullptr);
    mRenderFinishedSemaphores.clear();
    mSwapchain.destroy_image_views(mSwapchainImageViews);
    mSwapchainImageViews.clear();
    mSwapchainImages.clear();
}

void VulkanContext::recreateSwapchain(int framebufferWidth, int framebufferHeight)
{
    vkDeviceWaitIdle(mDevice.device);
    destroySwapchain();
    createSwapchain(framebufferWidth, framebufferHeight);
}

void VulkanContext::setVsync(bool vsync)
{
    if (vsync != mVsync)
    {
        mVsync = vsync;
        mSwapchainOutdated = true;
    }
}

// Frame loop ----------------------------------------------------------------------------------------------------------

VkCommandBuffer VulkanContext::beginFrame()
{
    Frame& frame = mFrames[mFrameIndex];

    // Wait until the GPU finished the last frame that used this frame slot, so that we can reuse its command buffer
    VK_CHECK(vkWaitForFences(mDevice.device, 1, &frame.fence, VK_TRUE, UINT64_MAX));

    VkResult result = vkAcquireNextImageKHR(mDevice.device, mSwapchain.swapchain, UINT64_MAX, frame.imageAvailable,
                                            VK_NULL_HANDLE, &mImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        mSwapchainOutdated = true;
        return VK_NULL_HANDLE;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        mSwapchainOutdated = true; // the image can still be used, recreate the swapchain after this frame
    else
        VK_CHECK(result);

    VK_CHECK(vkResetFences(mDevice.device, 1, &frame.fence));
    VK_CHECK(vkResetCommandPool(mDevice.device, frame.commandPool, 0));

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    // Transition the swapchain image to the GENERAL layout. Its previous content is discarded (UNDEFINED).
    // The source stage matches the wait stage of the imageAvailable semaphore in endFrame().
    utils::imageBarrier(
        frame.commandBuffer, mSwapchainImages[mImageIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

    return frame.commandBuffer;
}

void VulkanContext::endFrame()
{
    Frame& frame = mFrames[mFrameIndex];

    // Swapchain images have to be in the PRESENT_SRC layout for presentation, even with unified image layouts
    utils::imageBarrier(frame.commandBuffer, mSwapchainImages[mImageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    // Submit: wait until the swapchain image is available, signal when rendering finished
    const VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = mRenderFinishedSemaphores[mImageIndex],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo,
    };
    VK_CHECK(vkQueueSubmit2(mQueue, 1, &submitInfo, frame.fence));

    // Present the image once rendering finished
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &mRenderFinishedSemaphores[mImageIndex],
        .swapchainCount = 1,
        .pSwapchains = &mSwapchain.swapchain,
        .pImageIndices = &mImageIndex,
    };
    VkResult result = vkQueuePresentKHR(mQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        mSwapchainOutdated = true;
    else
        VK_CHECK(result);

    mFrameIndex = (mFrameIndex + 1) % FRAMES_IN_FLIGHT;
}

FrameTarget VulkanContext::getFrameTarget() const
{
    return {
        .image = mSwapchainImages[mImageIndex],
        .view = mSwapchainImageViews[mImageIndex],
        .format = mSwapchain.image_format,
        .extent = mSwapchain.extent,
    };
}

void VulkanContext::recordCapture(VkCommandBuffer cmd)
{
    // the conversion in saveCapture() and the buffer size below assume 4 bytes per pixel in a UNORM/SRGB RGBA or BGRA
    // format; vk-bootstrap may fall back to another format if the GPU supports neither of the desired ones
    const VkFormat format = mSwapchain.image_format;
    if (format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_SRGB
        && format != VK_FORMAT_R8G8B8A8_SRGB)
        throw std::runtime_error("--capture supports only 8 bit RGBA/BGRA swapchain formats, got VkFormat "
                                 + std::to_string(format));

    const FrameTarget target = getFrameTarget();
    destroyBuffer(mCaptureBuffer);
    mCaptureExtent = target.extent;
    // swapchain images have 4 bytes per pixel (B8G8R8A8 or R8G8B8A8)
    mCaptureBuffer = createBuffer(static_cast<VkDeviceSize>(target.extent.width) * target.extent.height * 4,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

    // wait until everything that rendered into the swapchain image finished (render passes, blits, compute, ...)
    utils::imageBarrier(cmd, target.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferImageCopy region{
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {target.extent.width, target.extent.height, 1},
    };
    vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_GENERAL, mCaptureBuffer.buffer, 1, &region);
    // The CPU reads the buffer in saveCapture() (read-after-write). Waiting there for the GPU makes sure that the copy
    // finished, but it does not make the copied data available to the host: waiting for a fence or vkDeviceWaitIdle()
    // only orders device memory accesses. This barrier with the destination HOST / HOST_READ does that, similar to
    // glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT) for persistently mapped buffers in OpenGL. Without it, the
    // CPU may read stale data on some GPUs.
    utils::memoryBarrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
}

bool VulkanContext::saveCapture(const std::string& path)
{
    if (mCaptureBuffer.buffer == VK_NULL_HANDLE)
        return false;
    vkDeviceWaitIdle(mDevice.device);
    // make the GPU writes visible to the CPU, required if the memory is not host coherent
    VK_CHECK(vmaInvalidateAllocation(mAllocator, mCaptureBuffer.allocation, 0, VK_WHOLE_SIZE));

    // convert to RGBA with opaque alpha, the swapchain may use BGRA
    const VkFormat format = mSwapchain.image_format;
    const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    const size_t pixelCount = static_cast<size_t>(mCaptureExtent.width) * mCaptureExtent.height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    const uint8_t* pixels = static_cast<const uint8_t*>(mCaptureBuffer.mapped);
    for (size_t i = 0; i < pixelCount; i++)
    {
        rgba[4 * i + 0] = pixels[4 * i + (bgra ? 2 : 0)];
        rgba[4 * i + 1] = pixels[4 * i + 1];
        rgba[4 * i + 2] = pixels[4 * i + (bgra ? 0 : 2)];
        rgba[4 * i + 3] = 255;
    }
    destroyBuffer(mCaptureBuffer);

    const bool saved = utils::savePng(path, mCaptureExtent.width, mCaptureExtent.height, rgba.data());
    if (saved)
        LOG_INFO("Frame saved to %s", path.c_str());
    else
        LOG_ERROR("Failed to save the frame to %s", path.c_str());
    return saved;
}

void VulkanContext::immediateSubmit(const std::function<void(VkCommandBuffer)>& record)
{
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = mImmediatePool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(mDevice.device, &allocInfo, &cmd));

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    record(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
    };
    VK_CHECK(vkQueueSubmit2(mQueue, 1, &submitInfo, mImmediateFence));
    VK_CHECK(vkWaitForFences(mDevice.device, 1, &mImmediateFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(mDevice.device, 1, &mImmediateFence));
    vkFreeCommandBuffers(mDevice.device, mImmediatePool, 1, &cmd);
}

// Resources -----------------------------------------------------------------------------------------------------------

Buffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible)
{
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    const VmaAllocationCreateInfo allocInfo{
        // host visible buffers are mapped and written sequentially by the CPU (e.g. memcpy), but not read
        .flags = hostVisible ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
                             : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    Buffer buffer{.size = size};
    VmaAllocationInfo allocationInfo;
    VK_CHECK(vmaCreateBuffer(mAllocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, &allocationInfo));
    buffer.mapped = allocationInfo.pMappedData;

    const VkBufferDeviceAddressInfo addressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer.buffer,
    };
    buffer.address = vkGetBufferDeviceAddress(mDevice.device, &addressInfo);
    return buffer;
}

Buffer VulkanContext::createBufferWithData(VkDeviceSize size, VkBufferUsageFlags usage, const void* data)
{
    Buffer buffer = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    // copy the data to a host visible staging buffer, then let the GPU copy it to the device local buffer
    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, data, size);
    VK_CHECK(vmaFlushAllocation(mAllocator, staging.allocation, 0, VK_WHOLE_SIZE));
    immediateSubmit([&](VkCommandBuffer cmd) {
        const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = size};
        vkCmdCopyBuffer(cmd, staging.buffer, buffer.buffer, 1, &region);
    });
    destroyBuffer(staging);
    return buffer;
}

void VulkanContext::destroyBuffer(Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(mAllocator, buffer.buffer, buffer.allocation);
    buffer = Buffer{};
}

Image VulkanContext::createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                 uint32_t mipLevels, VkImageType type)
{
    if (extent.depth > 1)
        type = VK_IMAGE_TYPE_3D;
    const VkImageViewType viewType = type == VK_IMAGE_TYPE_1D   ? VK_IMAGE_VIEW_TYPE_1D
                                     : type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D
                                                                : VK_IMAGE_VIEW_TYPE_2D;

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = format,
        .extent = extent,
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

    Image image{.format = format, .extent = extent, .mipLevels = mipLevels, .type = type};
    VK_CHECK(vmaCreateImage(mAllocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr));

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = viewType,
        .format = format,
        .subresourceRange =
            {.aspectMask = aspect, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 1},
    };
    VK_CHECK(vkCreateImageView(mDevice.device, &viewInfo, nullptr, &image.view));
    return image;
}

Image VulkanContext::createTexture(VkImageType type, VkExtent3D extent, VkFormat format, const void* data,
                                   VkDeviceSize dataSize, bool generateMipmaps)
{
    // The mip levels are generated by downscaling each level to the next with a linearly filtered blit, which the
    // format has to support. Common color formats (RGBA8, R8, R16F, ...) support it on all desktop GPUs.
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice.physical_device, format, &formatProperties);
    const VkFormatFeatureFlags blitFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT
                                              | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (generateMipmaps && (formatProperties.optimalTilingFeatures & blitFeatures) != blitFeatures)
    {
        LOG_WARN("createTexture: the format does not support linear blits, no mip levels are generated.");
        generateMipmaps = false;
    }

    // a full mip chain halves the size until all dimensions are 1
    uint32_t mipLevels = 1;
    if (generateMipmaps)
    {
        const uint32_t maxSize = std::max({extent.width, extent.height, extent.depth});
        while ((maxSize >> mipLevels) > 0)
            mipLevels++;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mipLevels > 1)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // the blits read from the texture
    Image image = createImage(extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, type);

    // copy the data to a host visible staging buffer, then let the GPU copy it into the image
    Buffer staging = createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, data, dataSize);
    VK_CHECK(vmaFlushAllocation(mAllocator, staging.allocation, 0, VK_WHOLE_SIZE));

    immediateSubmit([&](VkCommandBuffer cmd) {
        // the new image is UNDEFINED, transition it to GENERAL before the copy writes to it
        utils::imageBarrier(cmd, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBufferImageCopy region{
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .layerCount = 1},
            .imageExtent = extent,
        };
        vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);

        // each mip level is a linearly filtered, half sized copy of the previous level
        auto levelSize = [&](uint32_t level) {
            return VkOffset3D{static_cast<int32_t>(std::max(1u, extent.width >> level)),
                              static_cast<int32_t>(std::max(1u, extent.height >> level)),
                              static_cast<int32_t>(std::max(1u, extent.depth >> level))};
        };
        for (uint32_t level = 1; level < mipLevels; level++)
        {
            // the previous level has to be written (by the copy or the last blit) before the blit reads it
            utils::imageBarrier(cmd, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
            const VkImageBlit blit{
                .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level - 1, .layerCount = 1},
                .srcOffsets = {{0, 0, 0}, levelSize(level - 1)},
                .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .layerCount = 1},
                .dstOffsets = {{0, 0, 0}, levelSize(level)},
            };
            vkCmdBlitImage(cmd, image.image, VK_IMAGE_LAYOUT_GENERAL, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                           VK_FILTER_LINEAR);
        }

        // make the texture visible to all shaders that sample it later
        utils::imageBarrier(cmd, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    destroyBuffer(staging);
    return image;
}

void VulkanContext::destroyImage(Image& image)
{
    if (image.view != VK_NULL_HANDLE)
        vkDestroyImageView(mDevice.device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE)
        vmaDestroyImage(mAllocator, image.image, image.allocation);
    image = Image{};
}
