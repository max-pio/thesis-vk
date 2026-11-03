// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "renderer.hpp"
#include "gpu/gpu_utils.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <exception>

Renderer::Renderer(VulkanContext& context, const Config& config, int /*framebufferWidth*/, int /*framebufferHeight*/)
    : mContext(context), mConfig(config)
{
    // The volume fits into a box of size 2 around the origin (see init()). Look at it from outside, from a distance
    // where it fills the window. The near and far plane only have to include the volume, the ray marcher does not
    // use them for depth testing.
    mCamera.mTarget = glm::vec3(0.0f);
    mCamera.mDistance = 3.5f;
    mCamera.mNear = 0.05f;
    mCamera.mFar = 100.0f;
}

// Nothing special is needed here. Examples for what goes into the hooks (ContextHooks in gpu/vulkan_context.hpp):
// - configureInstance: builder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
// - configureGpu (GPUs without it are rejected): selector.add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
// - configureDevice (optional): if (!gpu.enable_extension_if_present(VK_EXT_MESH_SHADER_EXTENSION_NAME)) {..}
void Renderer::configureInstance(vkb::InstanceBuilder&, const vkb::SystemInfo&) {}

void Renderer::configureGpu(vkb::PhysicalDeviceSelector&) {}

void Renderer::configureDevice(vkb::PhysicalDevice&) {}

void Renderer::init()
{
    mLastTime = glfwGetTime();
    const VkDevice device = mContext.getDevice();

    // Load the volume from disk -------------------------------------------------------------------------------------
    const std::string path =
        mConfig.dataFile.empty() ? std::string(VOLUMES_PATH) + "bonsai_256x256x256_uint8.raw" : mConfig.dataFile;
    try
    {
        mVolume = utils::loadVolume(path);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("%s", e.what());
        LOG_ERROR("Download the default volume with 'cmake -P cmake/download_volumes.cmake' or pass a raw file with "
                  "--data, see the README.");
        std::exit(EXIT_FAILURE);
    }

    // Upload it as 3D texture ----------------------------------------------------------------------------------------
    // createTexture() copies the voxels into a staging buffer, then into the image on the GPU. Mip levels are not
    // needed: the ray marcher samples the full resolution (and large float32 volumes may not support the blits).
    mVolumeTexture =
        mContext.createTexture(VK_IMAGE_TYPE_3D, {mVolume.size.x, mVolume.size.y, mVolume.size.z},
                               utils::getVkFormat(mVolume.type), mVolume.data.data(), mVolume.data.size(), false);
    // The CPU copy is not needed anymore, free it (a large volume can have gigabytes).
    mVolume.data.clear();
    mVolume.data.shrink_to_fit();

    // The GUI starts with the whole value range of the volume
    mWindow = {mVolume.minValue, mVolume.maxValue};
    mIsoValue = 0.5f * (mVolume.minValue + mVolume.maxValue);

    // Sampler: trilinear interpolation between the voxels. Outside of [0, 1], the border voxels are repeated (the
    // ray marcher stays inside the box anyway, but the gradient looks one voxel to each side).
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &mSampler));

    // Descriptor set with the volume (combined image sampler, as for the 2D texture of the cube demo)
    // ------------------
    const VkDescriptorSetLayoutBinding textureBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo setLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &textureBinding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &mDescriptorSetLayout));
    const VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1};
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &mDescriptorPool));
    const VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = mDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &mDescriptorSetLayout,
    };
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &mDescriptorSet));
    const VkDescriptorImageInfo imageInfo{
        .sampler = mSampler,
        .imageView = mVolumeTexture.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Pipeline layout: push constants for the camera and settings (small data, copied into the command buffer), and
    // the descriptor set with the volume.
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &mDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &mPipelineLayout));
    reloadShaders();

    mGpuTimer.init(mContext, {"volume"});
}

void Renderer::reloadShaders()
{
    const VkDevice device = mContext.getDevice();
    const std::string path = SHADERS_PATH;
    std::string vertexError, fragmentError;
    VkShaderModule vertexModule = utils::createShaderModuleFromFile(device, path + "volume.vert", vertexError);
    VkShaderModule fragmentModule = utils::createShaderModuleFromFile(device, path + "volume.frag", fragmentError);
    mShaderError = vertexError + fragmentError;
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, vertexModule, nullptr);
        vkDestroyShaderModule(device, fragmentModule, nullptr);
        return;
    }

    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertexModule,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragmentModule,
         .pName = "main"},
    };
    // no vertex attributes: the vertex shader creates the quad from gl_VertexIndex (vertex pulling without a buffer)
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    // a triangle strip of 4 vertices is a quad
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    // no depth attachment, blending disabled: the output replaces the color
    const VkPipelineColorBlendAttachmentState blendAttachment{
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };
    // dynamic rendering: the pipeline only needs the format of the swapchain image it renders to
    const VkFormat colorFormat = mContext.getSwapchainFormat();
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = mPipelineLayout,
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    vkDestroyShaderModule(device, vertexModule, nullptr);
    vkDestroyShaderModule(device, fragmentModule, nullptr);

    // the old pipeline may still be in use by the GPU
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, mPipeline, nullptr);
    mPipeline = pipeline;
    LOG_INFO("Shaders loaded.");
}

void Renderer::runFrame(VkCommandBuffer cmd, const FrameTarget& target)
{
    mGpuTimer.beginFrame(cmd);
    mGpuTimer.begin(cmd, 0);

    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, // every pixel is overwritten by the quad
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = target.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };
    vkCmdBeginRendering(cmd, &renderingInfo);

    // without a pipeline (the shaders did not compile, see the GUI or the log), there is nothing to draw
    if (mPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
        const VkViewport viewport{.x = 0.0f,
                                  .y = 0.0f,
                                  .width = static_cast<float>(target.extent.width),
                                  .height = static_cast<float>(target.extent.height),
                                  .minDepth = 0.0f,
                                  .maxDepth = 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        const VkRect2D scissor{.offset = {0, 0}, .extent = target.extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // The volume is scaled to fit into a box with a side length of 2 around the origin, whatever its size and
        // spacing. The volume texture is never written after the upload in createTexture(), which already made it
        // visible to the fragment shader, so no barrier is needed here.
        const glm::vec3 worldSize = glm::vec3(mVolume.size) * mSpacing;
        const glm::vec3 voxelSize = mSpacing * (2.0f / std::max({worldSize.x, worldSize.y, worldSize.z}));
        const float aspectRatio = static_cast<float>(target.extent.width) / static_cast<float>(target.extent.height);
        const PushConstants pushConstants{
            .clipToWorld = glm::inverse(mCamera.getWorldToProjectionSpace(aspectRatio)),
            .cameraPos = mCamera.getPosition(),
            .stepSize = std::min({voxelSize.x, voxelSize.y, voxelSize.z}) / mSamplesPerVoxel,
            .boxHalfSize = glm::vec3(mVolume.size) * voxelSize * 0.5f,
            .windowMin = mWindow.x,
            .voxelSize = voxelSize,
            .windowMax = std::max(mWindow.y, mWindow.x + 1e-6f), // the shader divides by the window size
            .density = mDensity,
            .isoValue = mIsoValue,
            .mode = mMode,
            .useColormap = mUseColormap ? 1 : 0,
        };
        vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants),
                           &pushConstants);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &mDescriptorSet, 0,
                                nullptr);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }

    vkCmdEndRendering(cmd);
    mGpuTimer.end(cmd, 0);
}

void Renderer::shutdown()
{
    const VkDevice device = mContext.getDevice();
    vkDeviceWaitIdle(device);
    mGpuTimer.shutdown();
    vkDestroyPipeline(device, mPipeline, nullptr);
    vkDestroyPipelineLayout(device, mPipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, mDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, mDescriptorSetLayout, nullptr);
    vkDestroySampler(device, mSampler, nullptr);
    mContext.destroyImage(mVolumeTexture);
}

void Renderer::processEvents(GLFWwindow* window)
{
    // F5 to reload shaders
    if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_RELEASE)
    {
        mF5Pressed = false;
    }
    else
    {
        if (!mF5Pressed)
            reloadShaders();
        mF5Pressed = true;
    }

    // Camera, ignores input while capturing a frame or benchmarking, so that the frames are reproducible
    const double now = glfwGetTime();
    const float timeDelta = mConfig.fixedTimeStep > 0.0f ? mConfig.fixedTimeStep : static_cast<float>(now - mLastTime);
    mLastTime = now;
    if (!mConfig.isAutomatedRun())
        mCamera.update(window, timeDelta);
}

void Renderer::resize(int /*framebufferWidth*/, int /*framebufferHeight*/) {}

void Renderer::drawGui()
{
    ImGui::Text("Volume '%s': %ux%ux%u %s", mVolume.name.c_str(), mVolume.size.x, mVolume.size.y, mVolume.size.z,
                voxelTypeName(mVolume.type));
    ImGui::Text("Camera: left mouse to rotate, mouse wheel to zoom, WASD+QE to move.");
    ImGui::Combo("Mode", &mMode, "DVR\0Isosurface\0");
    ImGui::Checkbox("Color map", &mUseColormap);
    // The value range of the data as the shader sees it (normalized for integer types)
    ImGui::DragFloatRange2("Window", &mWindow.x, &mWindow.y, (mVolume.maxValue - mVolume.minValue) * 0.001f,
                           mVolume.minValue, mVolume.maxValue);
    if (mMode == MODE_DVR)
        ImGui::SliderFloat("Density", &mDensity, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    if (mMode == MODE_ISOSURFACE)
        ImGui::SliderFloat("Iso value", &mIsoValue, mVolume.minValue, mVolume.maxValue);
    ImGui::SliderFloat("Samples per voxel", &mSamplesPerVoxel, 0.25f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    // The raw file has no spacing: stretch the volume here if its voxels are not cubes (see README for the values)
    ImGui::DragFloat3("Voxel spacing", &mSpacing.x, 0.01f, 0.01f, 100.0f);
    if (!mShaderError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Shader error (fix it and press F5):\n%s",
                           mShaderError.c_str());
}

void Renderer::appendTimings(std::vector<Timing>& timings) const
{
    mGpuTimer.appendTimings(timings);
}
