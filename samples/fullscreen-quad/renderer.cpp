// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "renderer.hpp"
#include "gpu/gpu_utils.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

Renderer::Renderer(VulkanContext& context, const Config& /*config*/, int /*framebufferWidth*/,
                   int /*framebufferHeight*/)
    : mContext(context)
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
    // The only shader input is the tint color, passed as push constants (a small block of data that is copied into
    // the command buffer with vkCmdPushConstants). No descriptor sets, no vertex buffer.
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(glm::vec4),
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    VK_CHECK(vkCreatePipelineLayout(mContext.getDevice(), &layoutInfo, nullptr, &mPipelineLayout));
    reloadShaders();
}

void Renderer::reloadShaders()
{
    const VkDevice device = mContext.getDevice();
    const std::string path = SHADERS_PATH;
    std::string vertexError, fragmentError;
    VkShaderModule vertexModule = utils::createShaderModuleFromFile(device, path + "quad.vert", vertexError);
    VkShaderModule fragmentModule = utils::createShaderModuleFromFile(device, path + "quad.frag", fragmentError);
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
        vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &mTint);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }

    vkCmdEndRendering(cmd);
}

void Renderer::shutdown()
{
    const VkDevice device = mContext.getDevice();
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, mPipeline, nullptr);
    vkDestroyPipelineLayout(device, mPipelineLayout, nullptr);
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
}

void Renderer::resize(int /*framebufferWidth*/, int /*framebufferHeight*/) {}

void Renderer::drawGui()
{
    ImGui::ColorEdit4("Tint", &mTint.r);
    if (!mShaderError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Shader error (fix it and press F5):\n%s",
                           mShaderError.c_str());
}

void Renderer::appendTimings(std::vector<Timing>& /*timings*/) const {}
