# Framework TODO

Working packages to extend the general framework (`main`), independent of a specific thesis topic. Topic-specific
work belongs on the thesis branches. Move a WP out of this file once it is done.

## WP: Compute sample (`samples/compute`)

The README and `CLAUDE.md` mention compute shaders and storage images, but no code shows them.

- [ ] Compute pipeline (`vkCreateComputePipelines`) that writes a storage image, e.g. an animated pattern or a simple
      image filter.
- [ ] Descriptor binding for the storage image (`VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`), next to the existing combined
      image samplers.
- [ ] Barriers, with comments that name the hazards: compute write -> blit (or sample), and the write-after-read of
      the next frame's dispatch.
- [ ] Blit of the result to the swapchain, as in `samples/gbuffer`.
- [ ] GPU timer for the dispatch, `--capture` and `--benchmark` work as in the other samples.
- [ ] README "Samples" section and `CLAUDE.md` updated.

## WP: Size window-sized resources by the swapchain extent (from the 2026-11-02 review)

`main.cpp` passes `glfwGetFramebufferSize()` to the renderer (constructor and `resize()`), but the swapchain extent can
differ: vk-bootstrap uses the surface's `currentExtent` whenever the platform defines it (X11, Windows), and the window
may be resized between the two queries. The cube demo then renders with `renderArea = target.extent` into a depth image
of the framebuffer size, which is a validation error if the depth image is smaller (only during interactive resizing,
not reproducible with `--capture`).

- [ ] Add `VulkanContext::getSwapchainExtent()` and pass it to `Renderer(..)` and `Renderer::resize(..)` in `main.cpp`
      (keep the renderer interface unchanged).
- [ ] Test by resizing the window quickly on X11 and Wayland with a Debug build: no validation messages.

## WP: Host visible buffers for GPU -> CPU readback (from the 2026-11-02 review)

`createBuffer(.., hostVisible = true)` always uses `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`, i.e. memory
for CPU writes, which may be uncached for CPU reads (VMA: "reading is allowed but may be very slow"). The capture buffer
in `recordCapture()` is read by the CPU, and students will need readback buffers too (e.g. results of a compute pass).

- [ ] Replace `bool hostVisible` by a small enum (e.g. `HostAccess::None / Write / Read`) that selects
      `HOST_ACCESS_SEQUENTIAL_WRITE` or `HOST_ACCESS_RANDOM`; update all callers and the README.
- [ ] Use `Read` for the capture buffer. Document the barrier for readback (destination `HOST` / `HOST_READ`, as in
      `recordCapture()`) next to the new option.

## WP: Compiler warnings as errors in CI (from the 2026-11-02 review)

Debug builds enable `-Wall -Wextra -Wpedantic -Wshadow` (GCC/Clang) and `/W4` (MSVC) on the framework and the
executables (`enable_warnings()` in `CMakeLists.txt`); the dependencies are included as system headers.

- [ ] Optional: `-Werror` (or `CMAKE_COMPILE_WARNING_AS_ERROR`) in the CI Debug job only.
- [ ] `/W4` has not been tried with MSVC yet; check it together with the Windows CI job.

## WP: Small robustness items (from the 2026-11-02 review)

None of these causes problems today; fix them when touching the code.

- [ ] `recreateSwapchain()` destroys the render-finished semaphores after `vkDeviceWaitIdle()`, but without
      `VK_EXT_swapchain_maintenance1` there is no guarantee that the present operation finished waiting on them. Use
      the extension's present fences where available (newer validation layers may warn about this).
- [ ] `Gui::init()` sets ImGui's `ImageCount` to the swapchain image count, which ImGui uses as the ring size of its
      per-frame vertex buffers. It is never updated after swapchain recreation; safe as long as it is
      `>= FRAMES_IN_FLIGHT`. Consider `std::max(FRAMES_IN_FLIGHT, imageCount)` and a comment.
- [ ] All executables write SPIR-V to the same `<build>/shader_spirv/<file name>.spv`: shaders with the same file name
      in different directories, or two programs running at the same time, overwrite each other's output. Use one
      subdirectory per executable (e.g. a per-target `SHADER_BINARY_PATH`) or let glslc write to stdout.
- [ ] `GpuTimer::end()` marks a timer as recorded even if `begin()` was not called in this frame, which yields a
      meaningless time. Assert or track `begin()` as well.
