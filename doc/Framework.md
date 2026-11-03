# Framework

Overview of the code in `src/` and `core/`, the frame loop, and the Vulkan features and conventions the framework uses. Back to the [README](../README.md).

## Contents

* [Project Structure](#project-structure)
* [Frame Loop](#frame-loop)
* [Vulkan Concepts used in the Framework](#vulkan-concepts-used-in-the-framework)

## Project Structure

`src/` is the starting point for your code; `core/` is the framework library (`framework`), shared by the main program and the samples.

* `src/main.cpp`: parses options, creates the GLFW window, initializes Vulkan and ImGui, runs the main loop and defines the GUI elements
* `src/renderer.cpp`: initializes (before the main loop), records the rendering commands (during it) and cleans up (after it)
* `core/args.cpp`: parses command line options into `Config`
* `core/log.cpp`: console and optional file logging
* `core/cpu_timer.hpp`, `core/gpu/gpu_timer.cpp`: CPU and GPU timers
* `core/results.cpp`: benchmark results as CSV
* `core/gpu/vulkan_context.cpp`:
  * creates the instance, device, swapchain and memory allocator
  * per-frame synchronization (`beginFrame()` / `endFrame()`)
  * helpers for buffers, images and textures (1D, 2D, 3D, with mip levels)
  * frame capture
* `core/gpu/gpu_utils.cpp`: Vulkan helpers such as `VK_CHECK(..)`, shader compilation and pipeline barriers
* `core/gpu/vma_implementation.cpp`: compiles the Vulkan Memory Allocator
* `core/gui.cpp`: sets up Dear ImGui and draws the GUI over the frame
* `core/camera.cpp`: orbit camera; left mouse rotates, wheel zooms, WASD / QE move the center of rotation (horizontal / vertical)
* `core/utils.cpp`: general helpers, e.g. saving PNGs and creating the cube texture on the CPU (OpenMP)
* `core/volume.cpp`: loads raw 3D scalar volumes from disk (`utils::loadVolume()`, see [VolumeRendering.md](VolumeRendering.md))

## Frame Loop

`src/main.cpp` is the same for the main program and all samples and calls the `Renderer` interface (`src/renderer.hpp`):

1. `Renderer::configureInstance/configureGpu/configureDevice` (static): add extensions and features while `VulkanContext::init()` creates the instance and device.
2. `init()`: create buffers, textures and pipelines.
3. Each frame:
   * on a window size change: `VulkanContext::recreateSwapchain()` and `resize()`
   * `processEvents()` and `drawGui()`
   * `VulkanContext::beginFrame()` waits for the previous use of this frame slot to finish, acquires a swapchain image and returns the command buffer
   * `runFrame(cmd, target)` records the rendering commands
   * the GUI is drawn over the result and `endFrame()` submits and presents
4. `shutdown()`: destroy everything in reverse order of creation.

`runFrame()` renders either directly into the swapchain image (`sample-fullscreen-quad`) or into its own images that are blitted to it afterwards (`sample-gbuffer`).

## Vulkan Concepts used in the Framework

* **Dynamic rendering**: no `VkRenderPass` / `VkFramebuffer`; start rendering with `vkCmdBeginRendering(..)`.
* **Unified image layouts**: all images stay in `VK_IMAGE_LAYOUT_GENERAL`.
  * Images go from `UNDEFINED` to `GENERAL` before their first use. Render targets that are overwritten every frame (e.g. depth buffers) do this every frame, which tells the GPU that the old content can be discarded.
  * Swapchain images go to `PRESENT_SRC_KHR` for presentation (`VulkanContext::endFrame()`).
  * You still need barriers whenever a command reads data that a previous command wrote, or overwrites data that a previous command read: `utils::imageBarrier(..)` with `GENERAL` -> `GENERAL` for images, `utils::memoryBarrier(..)` for buffers.
* **Buffer device addresses**: shaders read buffers through pointers (`GL_EXT_buffer_reference`) passed in push constants, so buffers need no descriptor sets. With the scalar block layout (`GL_EXT_scalar_block_layout`) shader structs match C++ structs, e.g. `vec3` is 12 bytes.
* **Y-axis points down**: Vulkan's clip space y points down, OpenGL's and glm's up.
  * `Camera::getViewToProjectionSpace()` flips y (`projection[1][1] *= -1`), so NDC, `gl_FragCoord`, texture coordinates and images all have their origin top left with y down. This is also the convention of screen space techniques like TAA motion vectors.
  * Counter-clockwise triangles are still front faces.
  * Flip y in your own glm projection matrices too.
* **Textures and descriptor sets**: textures cannot use buffer addresses; they are bound with a descriptor set (`layout(set = 0, binding = ..)`), see `Renderer::createPatternTexture()`. `VulkanContext::createTexture(..)` creates 1D / 2D / 3D textures and their mip levels.
* **Per-frame data**: up to two frames run at once (`VulkanContext::FRAMES_IN_FLIGHT`).
  * Data the CPU writes every frame (e.g. the animated cube positions) needs one buffer per frame in flight, or the CPU overwrites data the GPU still reads.
  * Small data can go into push constants, which are copied into the command buffer.
* **One-time GPU work**: `VulkanContext::immediateSubmit(..)` records commands into its own command buffer, submits it and **waits until the GPU has finished** (`vkQueueSubmit` + fence wait). Use it for setup only, e.g. uploads and mip level generation in `init()`. Do not call it every frame: the CPU stalls until the GPU is idle, so CPU and GPU no longer work in parallel. Per-frame work belongs into the command buffer that `runFrame(..)` receives, which is submitted once per frame by `VulkanContext::endFrame()`.
* **Extensions and features**: the renderer adds its own via three static hooks, `Renderer::configureInstance`, `configureGpu` and `configureDevice` (`ContextHooks`), which receive the [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap/blob/main/docs/getting_started.md) builders. Anything added in `configureGpu` is required (GPUs without it are rejected), anything enabled in `configureDevice` is optional; check afterwards with `VulkanContext::isDeviceExtensionEnabled(..)`. This way `VulkanContext` stays the same for all renderers and samples.
* **Vertex pulling**: the vertex shader reads vertices from a buffer with `gl_VertexIndex`, no vertex attributes.
* **Compute shaders**: create a compute pipeline with `vkCreateComputePipelines(..)` and the same pipeline layout approach.
