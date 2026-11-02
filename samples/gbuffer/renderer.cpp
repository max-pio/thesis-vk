// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "renderer.hpp"
#include "gpu/gpu_utils.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

Renderer::Renderer(VulkanContext& context, const Config& config, int framebufferWidth, int framebufferHeight)
    : mContext(context), mConfig(config), mFramebufferSize(framebufferWidth, framebufferHeight)
{
    // The positions in the lighting pass are reconstructed from the depth buffer. With a standard depth buffer, most
    // of the depth precision is close to the near plane: a larger near distance makes the positions far away more
    // precise (see e.g. https://developer.nvidia.com/content/depth-precision-visualized).
    mCamera.mNear = 0.5f;
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
    createDescriptorsAndLayouts();

    // Load the shader files and create the pipelines
    reloadShaders();

    // Scene: instanced cubes, as in the cube demo --------------------------------------------------------------------
    mVertexBuffer =
        mContext.createBufferWithData(sizeof(CUBE_VERTICES), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, CUBE_VERTICES);
    constexpr size_t INSTANCE_COUNT = 1024;
    for (size_t i = 0; i < INSTANCE_COUNT; i++)
        mBasePositions.emplace_back(std::rand() % 100 - 50, std::rand() % 100 - 50, std::rand() % 100 - 50);
    for (Buffer& buffer : mPositionsBuffers)
        buffer =
            mContext.createBuffer(mBasePositions.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

    // Lights: random positions in the cube cloud and random bright colors ---------------------------------------------
    for (uint32_t i = 0; i < MAX_LIGHTS; i++)
    {
        const glm::vec3 position(std::rand() % 100 - 50, std::rand() % 100 - 50, std::rand() % 100 - 50);
        const glm::vec3 color =
            glm::normalize(glm::vec3(std::rand() % 100, std::rand() % 100, std::rand() % 100) + glm::vec3(10.0f));
        mBaseLights.push_back({.position = position, .radius = 30.0f, .color = color, .intensity = 3.0f});
    }
    for (Buffer& buffer : mLightBuffers)
        buffer = mContext.createBuffer(MAX_LIGHTS * sizeof(Light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

    createPatternTexture();
    createRenderTargets();

    // one GPU timer per pass, in the order of the GPU_TIMER_* indices
    mGpuTimer.init(mContext, {"gbuffer", "lighting", "blit"});
}

void Renderer::createDescriptorsAndLayouts()
{
    const VkDevice device = mContext.getDevice();

    // Descriptor set layouts ------------------------------------------------------------------------------------------
    // G-buffer pass: one texture for the cube faces (binding 0 in gbuffer.frag)
    const VkDescriptorSetLayoutBinding patternBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo patternLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &patternBinding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &patternLayoutInfo, nullptr, &mPatternSetLayout));

    // lighting pass: the three G-buffer images albedo, normal and depth (bindings 0, 1, 2 in lighting.frag)
    VkDescriptorSetLayoutBinding gBufferBindings[3];
    for (uint32_t i = 0; i < 3; i++)
    {
        gBufferBindings[i] = {
            .binding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    const VkDescriptorSetLayoutCreateInfo gBufferLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(std::size(gBufferBindings)),
        .pBindings = gBufferBindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &gBufferLayoutInfo, nullptr, &mGBufferSetLayout));

    // Pipeline layouts: push constants and descriptor set of each pass ------------------------------------------------
    const VkPushConstantRange scenePushConstants{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(ScenePushConstants),
    };
    const VkPipelineLayoutCreateInfo sceneLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &mPatternSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &scenePushConstants,
    };
    VK_CHECK(vkCreatePipelineLayout(device, &sceneLayoutInfo, nullptr, &mScenePipelineLayout));

    const VkPushConstantRange lightingPushConstants{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(LightingPushConstants),
    };
    const VkPipelineLayoutCreateInfo lightingLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &mGBufferSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &lightingPushConstants,
    };
    VK_CHECK(vkCreatePipelineLayout(device, &lightingLayoutInfo, nullptr, &mLightingPipelineLayout));

    // Descriptor pool and sets ----------------------------------------------------------------------------------------
    // 1 texture in the pattern set + 3 in the G-buffer set
    const VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 4};
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &mDescriptorPool));
    const VkDescriptorSetLayout setLayouts[] = {mPatternSetLayout, mGBufferSetLayout};
    VkDescriptorSet sets[2];
    const VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = mDescriptorPool,
        .descriptorSetCount = 2,
        .pSetLayouts = setLayouts,
    };
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, sets));
    mPatternSet = sets[0];
    mGBufferSet = sets[1];

    // The G-buffer is read with texelFetch(), which ignores the filter, but a combined image sampler needs a sampler
    const VkSamplerCreateInfo nearestInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VK_CHECK(vkCreateSampler(device, &nearestInfo, nullptr, &mNearestSampler));
}

void Renderer::createPatternTexture()
{
    // the same texture as in the cube demo, see Renderer::createPatternTexture() in src/renderer.cpp
    constexpr uint32_t PATTERN_SIZE = 128;
    const std::vector<glm::u8vec4> pattern = utils::createTilePattern(PATTERN_SIZE, 32);
    mPattern = mContext.createTexture(VK_IMAGE_TYPE_2D, {PATTERN_SIZE, PATTERN_SIZE, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                      pattern.data(), pattern.size() * sizeof(glm::u8vec4));
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VK_CHECK(vkCreateSampler(mContext.getDevice(), &samplerInfo, nullptr, &mPatternSampler));

    const VkDescriptorImageInfo imageInfo{
        .sampler = mPatternSampler,
        .imageView = mPattern.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mPatternSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(mContext.getDevice(), 1, &write, 0, nullptr);
}

void Renderer::createRenderTargets()
{
    destroyRenderTargets();

    // the render resolution is independent of the window size, e.g. lower to render faster, or higher for
    // supersampling. The blit at the end of the frame scales the image to the window size.
    mRenderTargetScale = mRenderScale;
    mRenderSize = {std::max(1u, static_cast<uint32_t>(std::lround(mFramebufferSize.x * mRenderScale))),
                   std::max(1u, static_cast<uint32_t>(std::lround(mFramebufferSize.y * mRenderScale)))};
    const VkExtent3D extent = {mRenderSize.width, mRenderSize.height, 1};

    // G-buffer: rendered to as attachments in the G-buffer pass, then sampled in the lighting pass
    mAlbedo =
        mContext.createImage(extent, ALBEDO_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
    mNormal =
        mContext.createImage(extent, NORMAL_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
    mDepth = mContext.createImage(extent, DEPTH_FORMAT,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT);
    // lit image: rendered to in the lighting pass, then the source of the blit to the swapchain image
    mLit =
        mContext.createImage(extent, LIT_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);

    // The descriptor set of the lighting pass refers to the image views, so it has to be written again for the new
    // images. Updating a descriptor set is only allowed while no submitted command buffer uses it: the callers wait
    // with vkDeviceWaitIdle() before.
    const VkDescriptorImageInfo imageInfos[] = {
        {.sampler = mNearestSampler, .imageView = mAlbedo.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = mNearestSampler, .imageView = mNormal.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = mNearestSampler, .imageView = mDepth.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    };
    VkWriteDescriptorSet writes[std::size(imageInfos)];
    for (uint32_t i = 0; i < std::size(imageInfos); i++)
    {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mGBufferSet,
            .dstBinding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfos[i],
        };
    }
    vkUpdateDescriptorSets(mContext.getDevice(), static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
}

void Renderer::destroyRenderTargets()
{
    mContext.destroyImage(mLit);
    mContext.destroyImage(mDepth);
    mContext.destroyImage(mNormal);
    mContext.destroyImage(mAlbedo);
}

VkPipeline Renderer::createPipeline(const std::string& vertexShader, const std::string& fragmentShader,
                                    VkPipelineLayout layout, std::span<const VkFormat> colorFormats,
                                    VkFormat depthFormat, VkCullModeFlags cullMode, std::string& errorMessage)
{
    const VkDevice device = mContext.getDevice();
    std::string vertexError, fragmentError;
    VkShaderModule vertexModule = utils::createShaderModuleFromFile(device, vertexShader, vertexError);
    VkShaderModule fragmentModule = utils::createShaderModuleFromFile(device, fragmentShader, fragmentError);
    errorMessage = vertexError + fragmentError;
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, vertexModule, nullptr);
        vkDestroyShaderModule(device, fragmentModule, nullptr);
        return VK_NULL_HANDLE;
    }

    // The pipeline state as in the cube demo (see Renderer::reloadShaders() in src/renderer.cpp for explanations)
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
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cullMode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    // the depth test is only used if the pipeline renders into a depth attachment
    const bool hasDepth = depthFormat != VK_FORMAT_UNDEFINED;
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = hasDepth,
        .depthWriteEnable = hasDepth,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    // one blend state per color attachment (multiple render targets), blending disabled: the output replaces the color
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        colorFormats.size(), {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT});
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(blendAttachments.size()),
        .pAttachments = blendAttachments.data(),
    };
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
        .pColorAttachmentFormats = colorFormats.data(),
        .depthAttachmentFormat = depthFormat,
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = static_cast<uint32_t>(std::size(stages)),
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = layout,
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    vkDestroyShaderModule(device, vertexModule, nullptr);
    vkDestroyShaderModule(device, fragmentModule, nullptr);
    return pipeline;
}

void Renderer::reloadShaders()
{
    const VkDevice device = mContext.getDevice();
    const std::string path = SHADERS_PATH;

    // G-buffer pass: renders into two color attachments (albedo, normal) and the depth attachment
    const VkFormat gBufferFormats[] = {ALBEDO_FORMAT, NORMAL_FORMAT};
    std::string sceneError, lightingError;
    // (back faces of the cubes are culled)
    VkPipeline scenePipeline = createPipeline(path + "gbuffer.vert", path + "gbuffer.frag", mScenePipelineLayout,
                                              gBufferFormats, DEPTH_FORMAT, VK_CULL_MODE_BACK_BIT, sceneError);
    // lighting pass: renders into the lit image, without depth. No culling: the full-screen triangle is always drawn,
    // whatever the winding order of its vertices is.
    const VkFormat litFormats[] = {LIT_FORMAT};
    VkPipeline lightingPipeline =
        createPipeline(path + "fullscreen.vert", path + "lighting.frag", mLightingPipelineLayout, litFormats,
                       VK_FORMAT_UNDEFINED, VK_CULL_MODE_NONE, lightingError);

    // keep the old pipelines if one of the new ones failed
    mShaderError = sceneError + lightingError;
    if (scenePipeline == VK_NULL_HANDLE || lightingPipeline == VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, scenePipeline, nullptr);
        vkDestroyPipeline(device, lightingPipeline, nullptr);
        return;
    }

    // the old pipelines may still be in use by the GPU
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, mScenePipeline, nullptr);
    vkDestroyPipeline(device, mLightingPipeline, nullptr);
    mScenePipeline = scenePipeline;
    mLightingPipeline = lightingPipeline;
    LOG_INFO("Shaders loaded.");
}

void Renderer::runFrame(VkCommandBuffer cmd, const FrameTarget& target)
{
    // without pipelines (the shaders did not compile, see the GUI or the log), there is nothing to draw
    if (mScenePipeline == VK_NULL_HANDLE || mLightingPipeline == VK_NULL_HANDLE)
        return;

    mGpuTimer.beginFrame(cmd);
    mCpuTimer.beginFrame();

    // Animation: write the cube positions and lights into the buffers of this frame in flight -------------------------
    mCpuTimer.begin(CPU_TIMER_ANIMATION);
    const uint32_t frame = mContext.getFrameIndex();
    glm::vec3* positions = static_cast<glm::vec3*>(mPositionsBuffers[frame].mapped);
    for (size_t i = 0; i < mBasePositions.size(); i++)
        positions[i] = mBasePositions[i] + glm::vec3(0.0f, 3.0f * std::sin(mTime + static_cast<float>(i)), 0.0f);
    // the lights circle around the y-axis
    Light* lights = static_cast<Light*>(mLightBuffers[frame].mapped);
    for (uint32_t i = 0; i < mLightCount; i++)
    {
        const float angle = 0.3f * mTime * (i % 2 == 0 ? 1.0f : -1.0f);
        const glm::vec3 p = mBaseLights[i].position;
        lights[i] = mBaseLights[i];
        lights[i].position = {p.x * std::cos(angle) - p.z * std::sin(angle), p.y,
                              p.x * std::sin(angle) + p.z * std::cos(angle)};
    }
    VK_CHECK(vmaFlushAllocation(mContext.getAllocator(), mPositionsBuffers[frame].allocation, 0, VK_WHOLE_SIZE));
    VK_CHECK(vmaFlushAllocation(mContext.getAllocator(), mLightBuffers[frame].allocation, 0, VK_WHOLE_SIZE));
    mCpuTimer.end(CPU_TIMER_ANIMATION);

    const glm::mat4 viewProjection = mCamera.getWorldToProjectionSpace(getAspectRatio());
    const VkRect2D renderArea = {.offset = {0, 0}, .extent = mRenderSize};
    const VkViewport viewport = {.x = 0.0f,
                                 .y = 0.0f,
                                 .width = static_cast<float>(mRenderSize.width),
                                 .height = static_cast<float>(mRenderSize.height),
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f};

    // 1. G-buffer pass ------------------------------------------------------------------------------------------------
    // The G-buffer images are reused every frame. The lighting pass of the previous frame read them in its fragment
    // shader, and this pass overwrites them: a write-after-read hazard, for which an execution dependency is enough (no
    // access flags on the source side). The images are only used by one queue, and barriers order all commands
    // submitted before them on that queue, also those of earlier frames, so one set of images is enough although two
    // frames are in flight. UNDEFINED as old layout discards the old content.
    mGpuTimer.begin(cmd, GPU_TIMER_GBUFFER);
    for (const Image* image : {&mAlbedo, &mNormal})
        utils::imageBarrier(cmd, image->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    utils::imageBarrier(cmd, mDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // Multiple render targets: the fragment shader writes to layout(location = 0) and (location = 1), which are the
    // color attachments 0 and 1. The depth is stored for the lighting pass (STORE instead of DONT_CARE).
    const VkRenderingAttachmentInfo gBufferAttachments[] = {
        {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = mAlbedo.view,
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}}},
        {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = mNormal.view,
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}}},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mDepth.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}},
    };
    const VkRenderingInfo gBufferRendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderArea,
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(std::size(gBufferAttachments)),
        .pColorAttachments = gBufferAttachments,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &gBufferRendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mScenePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mScenePipelineLayout, 0, 1, &mPatternSet, 0, nullptr);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &renderArea);
    const ScenePushConstants scenePushConstants{
        .viewProjection = viewProjection,
        .color = mColor,
        .cameraPosition = mCamera.getPosition(),
        .vertexBuffer = mVertexBuffer.address,
        .positionsBuffer = mPositionsBuffers[frame].address,
    };
    vkCmdPushConstants(cmd, mScenePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(ScenePushConstants), &scenePushConstants);
    vkCmdDraw(cmd, 36, static_cast<uint32_t>(mBasePositions.size()), 0, 0);
    vkCmdEndRendering(cmd);
    mGpuTimer.end(cmd, GPU_TIMER_GBUFFER);

    // 2. Lighting pass -----------------------------------------------------------------------------------------------
    // The lighting pass reads the G-buffer that the G-buffer pass just wrote: a read-after-write hazard. The writes
    // have to finish and be made visible to the fragment shader reads.
    mGpuTimer.begin(cmd, GPU_TIMER_LIGHTING);
    for (const Image* image : {&mAlbedo, &mNormal})
        utils::imageBarrier(cmd, image->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    utils::imageBarrier(cmd, mDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // The lit image was read by the blit of the previous frame and is overwritten now (write-after-read)
    utils::imageBarrier(cmd, mLit.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // every pixel is written by the full-screen triangle, so the old content does not have to be cleared or loaded
    const VkRenderingAttachmentInfo litAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mLit.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo lightingRendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderArea,
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &litAttachment,
    };
    vkCmdBeginRendering(cmd, &lightingRendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mLightingPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mLightingPipelineLayout, 0, 1, &mGBufferSet, 0,
                            nullptr);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &renderArea);
    const LightingPushConstants lightingPushConstants{
        .inverseViewProjection = glm::inverse(viewProjection),
        .cameraPosition = mCamera.getPosition(),
        .viewMode = mViewMode,
        .lightBuffer = mLightBuffers[frame].address,
        .lightCount = mLightCount,
    };
    vkCmdPushConstants(cmd, mLightingPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LightingPushConstants),
                       &lightingPushConstants);
    // one triangle that covers the whole image, its vertices are computed from gl_VertexIndex in fullscreen.vert
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    mGpuTimer.end(cmd, GPU_TIMER_LIGHTING);

    // 3. Blit to the swapchain image ---------------------------------------------------------------------------------
    // The blit reads the lit image after the lighting pass wrote it (read-after-write). The swapchain image is ready
    // for writes: VulkanContext::beginFrame() transitioned it, and the GUI waits for all writes before it draws.
    mGpuTimer.begin(cmd, GPU_TIMER_BLIT);
    utils::imageBarrier(cmd, mLit.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    // A blit copies a rectangle and scales it to the size of the destination rectangle, filtering linearly if the
    // sizes differ. It also converts the format (16 bit float to the 8 bit swapchain format).
    const VkImageBlit blit{
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .srcOffsets = {{0, 0, 0},
                       {static_cast<int32_t>(mRenderSize.width), static_cast<int32_t>(mRenderSize.height), 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .dstOffsets = {{0, 0, 0},
                       {static_cast<int32_t>(target.extent.width), static_cast<int32_t>(target.extent.height), 1}},
    };
    const bool sameSize = mRenderSize.width == target.extent.width && mRenderSize.height == target.extent.height;
    vkCmdBlitImage(cmd, mLit.image, VK_IMAGE_LAYOUT_GENERAL, target.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                   sameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    mGpuTimer.end(cmd, GPU_TIMER_BLIT);
}

void Renderer::shutdown()
{
    const VkDevice device = mContext.getDevice();
    vkDeviceWaitIdle(device);

    mGpuTimer.shutdown();
    destroyRenderTargets();
    vkDestroySampler(device, mPatternSampler, nullptr);
    mContext.destroyImage(mPattern);
    for (Buffer& buffer : mLightBuffers)
        mContext.destroyBuffer(buffer);
    for (Buffer& buffer : mPositionsBuffers)
        mContext.destroyBuffer(buffer);
    mContext.destroyBuffer(mVertexBuffer);
    vkDestroyPipeline(device, mLightingPipeline, nullptr);
    vkDestroyPipeline(device, mScenePipeline, nullptr);
    vkDestroySampler(device, mNearestSampler, nullptr);
    vkDestroyDescriptorPool(device, mDescriptorPool, nullptr);
    vkDestroyPipelineLayout(device, mLightingPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, mScenePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, mGBufferSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, mPatternSetLayout, nullptr);
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

    // the render scale was changed in the GUI: recreate the render targets, which the GPU may still use
    if (mRenderScale != mRenderTargetScale)
    {
        vkDeviceWaitIdle(mContext.getDevice());
        createRenderTargets();
    }

    // Time since the last frame, a fixed time step for captures and benchmarks
    const double now = glfwGetTime();
    const float timeDelta = mConfig.fixedTimeStep > 0.0f ? mConfig.fixedTimeStep : static_cast<float>(now - mLastTime);
    mLastTime = now;
    if (mAnimate)
        mTime += timeDelta;

    if (!mConfig.isAutomatedRun())
        mCamera.update(window, timeDelta);
}

void Renderer::resize(int framebufferWidth, int framebufferHeight)
{
    mFramebufferSize = {framebufferWidth, framebufferHeight};
    // the render targets may still be in use by the GPU
    vkDeviceWaitIdle(mContext.getDevice());
    createRenderTargets();
}

void Renderer::drawGui()
{
    ImGui::Text("Camera: left mouse to rotate, mouse wheel to zoom, WASD+QE to move.\n");
    ImGui::SliderFloat("Render scale", &mRenderScale, 0.25f, 2.0f, "%.2f");
    ImGui::Text("Render resolution: %u x %u", mRenderSize.width, mRenderSize.height);
    const char* viewModes[] = {"Lit", "Albedo", "Normal", "Depth", "World position"};
    int viewMode = static_cast<int>(mViewMode);
    if (ImGui::Combo("View", &viewMode, viewModes, static_cast<int>(std::size(viewModes))))
        mViewMode = static_cast<ViewMode>(viewMode);
    int lightCount = static_cast<int>(mLightCount);
    if (ImGui::SliderInt("Lights", &lightCount, 0, static_cast<int>(MAX_LIGHTS)))
        mLightCount = static_cast<uint32_t>(lightCount);
    ImGui::ColorEdit4("Color", &mColor.r);
    ImGui::Checkbox("Animate", &mAnimate);
    if (!mShaderError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Shader error (fix it and press F5):\n%s",
                           mShaderError.c_str());
}

void Renderer::appendTimings(std::vector<Timing>& timings) const
{
    mGpuTimer.appendTimings(timings);
    mCpuTimer.appendTimings(timings);
}

float Renderer::getAspectRatio() const
{
    return static_cast<float>(mFramebufferSize.x) / static_cast<float>(mFramebufferSize.y);
}
