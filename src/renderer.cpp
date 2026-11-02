// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "renderer.hpp"
#include "gpu/gpu_utils.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

Renderer::Renderer(VulkanContext& context, const Config& config, int framebufferWidth, int framebufferHeight)
    : mColor{1.f, 0.55f, 0.f, 1.0f},
      mContext(context),
      mConfig(config),
      mVertexShaderPath(std::string(SHADERS_PATH) + std::string("instance.vert")),
      mFragmentShaderPath(std::string(SHADERS_PATH) + std::string("instance_color.frag")),
      mDescriptorSetLayout(VK_NULL_HANDLE),
      mPipelineLayout(VK_NULL_HANDLE),
      mPipeline(VK_NULL_HANDLE),
      mSampler(VK_NULL_HANDLE),
      mDescriptorPool(VK_NULL_HANDLE),
      mDescriptorSet(VK_NULL_HANDLE),
      mCamera(),
      mFramebufferSize(framebufferWidth, framebufferHeight),
      mF5Pressed(false)
{}

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

    // Descriptor Set Layout -------------------------------------------------------------------------------------------
    // Buffers are passed to the shaders as addresses in the push constants, but textures need descriptors. A descriptor
    // set layout lists the bindings that the shaders declare with layout(set = 0, binding = ..), here one texture with
    // a sampler (combined image sampler), used in the fragment shader.
    //
    // Vulkan structs are filled with designated initializers (C++20): all fields that are not listed are zero. The
    // fields have to be listed in the order in which they are declared in the struct. Most Vulkan structs need their
    // type in sType.
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
    VK_CHECK(vkCreateDescriptorSetLayout(mContext.getDevice(), &setLayoutInfo, nullptr, &mDescriptorSetLayout));

    // Pipeline Layout -------------------------------------------------------------------------------------------------
    // The pipeline layout describes all resources the shaders can access: the push constant block and the descriptor
    // set with the texture.
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
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
    VK_CHECK(vkCreatePipelineLayout(mContext.getDevice(), &layoutInfo, nullptr, &mPipelineLayout));

    // Load the shader files and create the graphics pipeline
    reloadShaders();

    // Vertex Data -----------------------------------------------------------------------------------------------------
    // Instead of a vertex input state, the vertex shader reads the vertices directly from the buffer ("vertex pulling")
    mVertexBuffer =
        mContext.createBufferWithData(sizeof(CUBE_VERTICES), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, CUBE_VERTICES);

    // Setup Instance Positions ----------------------------------------------------------------------------------------
    constexpr size_t INSTANCE_COUNT = 1024;
    // initialize with "random" positions in [-50, 50)^3. std::rand() is deliberately not seeded: every run gets the
    // same positions, so that captures and benchmarks of different runs and code versions can be compared.
    mBasePositions.clear();
    for (size_t i = 0; i < INSTANCE_COUNT; i++)
        mBasePositions.emplace_back(std::rand() % 100 - 50, std::rand() % 100 - 50, std::rand() % 100 - 50);

    // The animated positions are written every frame: use host visible storage buffers, one per frame in flight
    for (Buffer& buffer : mPositionsBuffers)
        buffer =
            mContext.createBuffer(mBasePositions.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

    // Texture ---------------------------------------------------------------------------------------------------------
    createPatternTexture();

    // Depth Buffer ----------------------------------------------------------------------------------------------------
    createDepthImage();

    // GPU timer with one timer per measured pass, in the order of the GPU_TIMER_* indices
    mGpuTimer.init(mContext, {"scene"});
}

void Renderer::createPatternTexture()
{
    const VkDevice device = mContext.getDevice();

    // 2D pattern on the cube faces: bright tiles with dark borders. Mip levels avoid aliasing when the cubes are far
    // away. VulkanContext::createTexture(..) also creates 1D and 3D textures, e.g. for volume rendering.
    constexpr uint32_t PATTERN_SIZE = 128;
    const std::vector<glm::u8vec4> pattern = utils::createTilePattern(PATTERN_SIZE, 32);
    mPattern = mContext.createTexture(VK_IMAGE_TYPE_2D, {PATTERN_SIZE, PATTERN_SIZE, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                      pattern.data(), pattern.size() * sizeof(glm::u8vec4));

    // A sampler defines how a texture is filtered and what happens outside of [0, 1]: linear filtering between texels
    // and mip levels (trilinear filtering), and texture coordinates outside of [0, 1] repeat the texture.
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
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &mSampler));

    // Descriptor set: descriptors are allocated from a pool, then written with the texture and sampler
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

    // binding 0 matches layout(set = 0, binding = 0) in shader/instance_color.frag
    const VkDescriptorImageInfo imageInfo{
        .sampler = mSampler,
        .imageView = mPattern.view,
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
}

void Renderer::createDepthImage()
{
    mContext.destroyImage(mDepthImage);
    mDepthImage =
        mContext.createImage({static_cast<uint32_t>(mFramebufferSize.x), static_cast<uint32_t>(mFramebufferSize.y), 1},
                             DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Renderer::reloadShaders()
{
    const VkDevice device = mContext.getDevice();

    // Compile shaders, keep the old pipeline if compilation fails and show the error in the GUI
    std::string vertexError, fragmentError;
    VkShaderModule vertexShader = utils::createShaderModuleFromFile(device, mVertexShaderPath, vertexError);
    VkShaderModule fragmentShader = utils::createShaderModuleFromFile(device, mFragmentShaderPath, fragmentError);
    mShaderError = vertexError + fragmentError;
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, vertexShader, nullptr);
        vkDestroyShaderModule(device, fragmentShader, nullptr);
        return;
    }

    // In Vulkan, all render state is baked into an immutable pipeline object. Only the states listed in
    // dynamicStates below can be set while recording the command buffer.
    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };

    // no vertex attributes: vertices are read from a buffer in the vertex shader
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
        // skip triangles facing away from the camera. Front faces are counter-clockwise as seen on screen
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    // Enable the depth buffer for correct ordering of overlapping objects
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };

    const VkPipelineColorBlendAttachmentState blendAttachment{
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };

    // viewport and scissor are set in runFrame(), so that the pipeline does not depend on the window size
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };

    // dynamic rendering: specify the attachment formats instead of a render pass
    const VkFormat colorFormat = mContext.getSwapchainFormat();
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = DEPTH_FORMAT,
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
        .layout = mPipelineLayout,
    };

    VkPipeline newPipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline));

    // shader modules are not needed anymore once the pipeline is created
    vkDestroyShaderModule(device, vertexShader, nullptr);
    vkDestroyShaderModule(device, fragmentShader, nullptr);

    // the old pipeline may still be in use by the GPU: wait until all work finished before destroying it
    if (mPipeline != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
        vkDestroyPipeline(device, mPipeline, nullptr);
    }
    mPipeline = newPipeline;
    LOG_INFO("Shaders loaded.");
}

void Renderer::runFrame(VkCommandBuffer cmd, const FrameTarget& target)
{
    // without a pipeline (the shaders did not compile, see the GUI or the log), there is nothing to draw
    if (mPipeline == VK_NULL_HANDLE)
        return;

    // Read the times of an earlier frame, then measure the GPU time of the commands until mGpuTimer.end()
    mGpuTimer.beginFrame(cmd);
    mCpuTimer.beginFrame();
    mGpuTimer.begin(cmd, GPU_TIMER_SCENE);

    // Make the depth image ready for use as attachment. Its old content is discarded (UNDEFINED), and the barrier
    // waits for the previous frame to finish its depth writes.
    utils::imageBarrier(cmd, mDepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // Begin rendering: clear the color and depth attachment
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // the color of the background
        .clearValue = {.color = {{0.9f, 0.94f, 0.98f, 1.0f}}},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = mDepthImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}},
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = target.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &renderingInfo);

    // Tell Vulkan which pipeline and descriptor set (texture) we want to use
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);

    // The viewport maps NDC to pixels: (-1, -1) is the top left corner. The projection matrix flips y (see
    // Camera::getViewToProjectionSpace()), so that y points down in NDC, as in Vulkan and in the image.
    const VkViewport viewport{.x = 0.0f,
                              .y = 0.0f,
                              .width = static_cast<float>(target.extent.width),
                              .height = static_cast<float>(target.extent.height),
                              .minDepth = 0.0f,
                              .maxDepth = 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    const VkRect2D scissor{.offset = {0, 0}, .extent = target.extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Animate the cubes and write their positions into the buffer of this frame in flight. The GPU may still read the
    // buffer of the previous frame, but this one was last used FRAMES_IN_FLIGHT frames ago, and beginFrame() waited
    // for that frame to finish.
    mCpuTimer.begin(CPU_TIMER_ANIMATION);
    const Buffer& positionsBuffer = mPositionsBuffers[mContext.getFrameIndex()];
    glm::vec3* positions = static_cast<glm::vec3*>(positionsBuffer.mapped);
    for (size_t i = 0; i < mBasePositions.size(); i++)
        positions[i] = mBasePositions[i] + glm::vec3(0.0f, 3.0f * std::sin(mTime + static_cast<float>(i)), 0.0f);
    mCpuTimer.end(CPU_TIMER_ANIMATION);
    // make the CPU writes visible to the GPU, required if the memory is not host coherent
    VK_CHECK(vmaFlushAllocation(mContext.getAllocator(), positionsBuffer.allocation, 0, VK_WHOLE_SIZE));

    // Set push constants (the Vulkan equivalent of small uniforms)
    const PushConstants pushConstants{
        .mvp = mCamera.getWorldToProjectionSpace(getAspectRatio()),
        .color = mColor,
        .vertexBuffer = mVertexBuffer.address,
        .positionsBuffer = positionsBuffer.address,
    };
    vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstants), &pushConstants);

    // Schedule the draw command
    // The model vertices are the same for all instances: an object centered around the origin
    vkCmdDraw(cmd, 36, static_cast<uint32_t>(mBasePositions.size()), 0, 0);

    vkCmdEndRendering(cmd);
    mGpuTimer.end(cmd, GPU_TIMER_SCENE);
}

void Renderer::shutdown()
{
    const VkDevice device = mContext.getDevice();
    vkDeviceWaitIdle(device);

    mGpuTimer.shutdown();
    mContext.destroyImage(mDepthImage);
    vkDestroyDescriptorPool(device, mDescriptorPool, nullptr);
    vkDestroySampler(device, mSampler, nullptr);
    mContext.destroyImage(mPattern);
    for (Buffer& buffer : mPositionsBuffers)
        mContext.destroyBuffer(buffer);
    mContext.destroyBuffer(mVertexBuffer);
    vkDestroyPipeline(device, mPipeline, nullptr);
    vkDestroyPipelineLayout(device, mPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, mDescriptorSetLayout, nullptr);
}

void Renderer::processEvents(GLFWwindow* window)
{
    // alternatively: use GLFW's glfwSetKeyCallback

    // F5 to reload shaders
    if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_RELEASE)
    {
        mF5Pressed = false;
    }
    else
    {
        if (!mF5Pressed)
        {
            reloadShaders();
        }
        mF5Pressed = true;
    }

    // Time since the last frame. A fixed time step (--fixed-timestep, --capture, --benchmark) renders the same frames
    // in every run.
    const double now = glfwGetTime();
    const float timeDelta = mConfig.fixedTimeStep > 0.0f ? mConfig.fixedTimeStep : static_cast<float>(now - mLastTime);
    mLastTime = now;

    if (mAnimate)
        mTime += timeDelta;

    // Camera, ignores input while capturing a frame or benchmarking, so that the frames are reproducible
    if (!mConfig.isAutomatedRun())
        mCamera.update(window, timeDelta);
}

void Renderer::resize(int framebufferWidth, int framebufferHeight)
{
    mFramebufferSize.x = framebufferWidth;
    mFramebufferSize.y = framebufferHeight;
    // the depth image may still be in use by the GPU
    vkDeviceWaitIdle(mContext.getDevice());
    createDepthImage();
}

void Renderer::drawGui()
{
    ImGui::Text("Camera: left mouse to rotate, mouse wheel to zoom, WASD+QE to move.\n");
    ImGui::Text("Add your GUI elements here!");
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
