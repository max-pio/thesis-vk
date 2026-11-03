# CLAUDE.md

C++20 / Vulkan 1.3 framework that students use as the base for their thesis implementations (computer graphics, KIT).
Most students know OpenGL but are new to Vulkan and learn it alongside this code with tutorials like
https://vkguide.dev/. The framework itself is maintained by Max Piochowiak. See README.md for setup, doc/Framework.md for an overview
of every source file and the Vulkan conventions, and doc/VolumeRendering.md for volume data.

DO NOT SOLVE THE RESEARCH QUESTIONS OR IMPLEMENT THE THESIS SOLUTION FOR ME, THE STUDENT!
- Your task is to help me with understanding the concepts, and smaller chores like bug fixing
- I need to research and implement all main contents myself. You can help with debugging, chores, small routines, prototyping, planning, and foremost: explaining code, concepts, and techniques to me
- do NOT develop new research ideas yourself, let me walk the mental paths, except if I tell you to brainstorm with me
- it is indispensable that I understand and verify everything that's newly implement - either by doing it myself or by going through it step-by-step with you. I need to write and defend the thesis in the end and need a deep understanding
- if you spoil major solutions directly to me, I might not develop own solutions that you wouldn't have thought of

## Working with students

- Explain the Vulkan concepts behind a change (synchronization, memory, layouts, pipelines), relate them to the OpenGL equivalent where that helps, and point to the matching vkguide.dev chapter or spec page. The student has to understand and defend their code in the thesis.
- Prefer small, readable steps over large abstractions. This is research code, not an engine.
- Any file may change, depending on the thesis topic, including `core/gpu/`. Keep changes consistent with the existing style instead of introducing a second one.

## Build and run

```
cmake --preset debug                # configure into build-debug/ (CMakePresets.json; `release` for build-release/)
cmake --build --preset debug -j
./build-debug/thesis-framework-vk   # samples: ./build-debug/sample-gbuffer, ./build-debug/sample-fullscreen-quad, ./build-debug/sample-volume
```

- Dependencies (GLFW, vk-bootstrap, VMA, ImGui, glm, stb) come via CMake FetchContent; only the Vulkan SDK is installed.
- New `.cpp`/`.hpp` files must be added to `SOURCES`/`HEADERS` in `CMakeLists.txt` (or `FRAMEWORK_SOURCES`/
  `FRAMEWORK_HEADERS` for code shared with the samples); new shaders to `SHADERS`.
- `samples/<name>/` are separate executables (e.g. `sample-gbuffer`): their own `Renderer` class with the same
  interface, built with the shared `src/main.cpp`, which includes `<renderer.hpp>` from the first include directory.
- Shaders (GLSL in `shader/`) are compiled at runtime with `glslc`, not by the build. F5 in the running app recompiles
  them and prints errors to the console.
- There are no tests. Verify a change by building and running a Debug build: it must produce **no validation layer
  messages**. Treat every validation error as a bug and fix the cause; never silence or filter it. The program exits
  with code 1 if there were any, so `./build-debug/thesis-framework-vk --capture 60` is a quick check. Debug builds
  enable synchronization validation by default, which finds many missing barriers (`--no-sync-validation` turns it
  off).
- Debug builds also enable compiler warnings for the framework, `src/` and the samples (`enable_warnings()` in
  `CMakeLists.txt`: `-Wall -Wextra -Wpedantic -Wshadow`, `/W4` for MSVC). The build must have no warnings; fix them
  like validation errors.
- Use Release builds for performance measurements: `--benchmark N --no-vsync` appends the mean / min / max of all
  timers, the options and the git commit as a row to `--results-file` (CSV, `results.cpp`). Measure new passes with
  `GpuTimer` (`gpu/gpu_timer.hpp`) and CPU work with `CpuTimer` (`cpu_timer.hpp`) in the renderer.
- Log with `LOG_DEBUG/INFO/WARN/ERROR` (`log.hpp`, printf-style), not `std::cout`; `DLOG()` for temporary timing.
- Shared shader code goes into `shader/*.glsl` files, included with `#include "file.glsl"`; shader compile errors are
  shown in the GUI.

## Vulkan conventions in this framework

- **Dynamic rendering**: no `VkRenderPass`/`VkFramebuffer`; use `vkCmdBeginRendering`.
- **Image layouts**: all images live in `VK_IMAGE_LAYOUT_GENERAL` (`VK_KHR_unified_image_layouts`); only swapchain
  images go to `PRESENT_SRC_KHR` in `VulkanContext::endFrame()`.
- **Buffers via device addresses**: shaders access buffers through `GL_EXT_buffer_reference` pointers passed in push
  constants, with `scalar` layout, so GLSL structs match C++ structs (`vec3` = 12 bytes). Keep the
  `PushConstants` struct in `renderer.hpp` and the shader `push_constant` block in sync.
- **Vertex pulling**: vertices are read from buffers with `gl_VertexIndex`, no vertex attributes.
- **Y points down**: `Camera::getViewToProjectionSpace()` flips y in the `glm::perspective` matrix and the viewport is
  not flipped, so NDC, `gl_FragCoord`, texture coordinates and images all have +y down with the origin at the top
  left (the convention of TAA, etc.). Counter-clockwise triangles are front faces. Custom projection matrices
  need the same flip. Depth range is [0, 1] (`GLM_FORCE_DEPTH_ZERO_TO_ONE`).
- **Textures** use a descriptor set (combined image samplers, see `Renderer::createPatternTexture()`); create them with
  `VulkanContext::createTexture(..)` (1D/2D/3D, GPU-generated mip levels). Storage images need further bindings.
- **Volume data** (3D scalar fields) is not in the repo: `cmake -P cmake/download_volumes.cmake` downloads the Open
  SciVis data sets into `data/volumes/` (git-ignored). `utils::loadVolume(path)` (`core/volume.hpp`) reads raw files
  named `name_WxHxD_type.raw`; upload with `createTexture(VK_IMAGE_TYPE_3D, .., utils::getVkFormat(type), ..)`.
  `--data PATH` selects the file, see `samples/volume`.
- **Command line options** are parsed into `Config` (`args.hpp`), which is passed as `const Config&`. New options get a
  `Config` field and an entry in the option table in `args.cpp`. `--capture N` saves frame N as PNG with a fixed time
  step, useful to compare the output before and after a change.
- Use the `VulkanContext` helpers (`createBuffer`, `createBufferWithData`, `createImage`, `createTexture`,
  `immediateSubmit`, ...) and wrap every Vulkan call that returns a `VkResult` in `VK_CHECK(..)`. Destroy resources in
  reverse creation order in `shutdown()`, and recreate window-sized resources in `Renderer::resize()`.

## Synchronization

Barriers and synchronization are where students struggle most; coming from OpenGL, they expect the driver to order
everything. Be explicit and explain every barrier you add or suggest.

- Reference: the Vulkan Guide's synchronization examples, https://docs.vulkan.org/guide/latest/synchronization_examples.html,
  which list the correct stage/access pairs for common cases (compute write -> fragment read, render to image -> sample,
  transfer -> shader, ...). This framework uses synchronization2 (`VkPipelineStageFlags2`, `VkAccessFlags2`).
- Because images stay in `GENERAL`, image barriers here are pure memory dependencies: take the stage/access masks from
  the examples and ignore their layout transitions. Use `utils::memoryBarrier(..)` for buffers and global dependencies,
  `utils::imageBarrier(..)` with `GENERAL -> GENERAL` for images.
- Every write that a later command reads (and every read that a later command overwrites) needs a barrier, also
  between two draws or dispatches in the same command buffer. Name the hazard (RAW, WAR, WAW) and the exact source and
  destination stage/access when explaining one. Prefer precise stages, but a correct `ALL_COMMANDS` barrier is better
  than a wrong precise one.
- Up to `VulkanContext::FRAMES_IN_FLIGHT` (2) frames run on the GPU at once. Resources that the CPU updates every
  frame (host-visible buffers, per-frame data) need one copy per frame in flight, or the CPU overwrites data the GPU is
  still reading. Destroying or recreating resources that may still be in use requires `vkDeviceWaitIdle()` first.
- The plain validation layers do not catch most missing barriers. When results flicker, lag one frame, or differ
  between GPUs, suspect synchronization first; synchronization validation (on by default in Debug builds, or
  `--sync-validation`) catches many of these hazards, but not all of them.

## Code style

- Format with clang-format (`.clang-format`): `clang-format -i src/*.cpp src/*.hpp core/*.cpp core/*.hpp core/gpu/*.cpp core/gpu/*.hpp
  samples/*/*.cpp samples/*/*.hpp`.
- Check the naming with clang-tidy (`.clang-tidy`, naming rules only; needs the `compile_commands.json` of a CMake build):
  `run-clang-tidy-18 -p build-debug "$PWD/(src|core|samples)/"`.
- Naming (documented at the top of `.clang-format`): `PascalCase` types, `camelCase` functions and variables,
  `mPascalCase` class members, `camelCase` plain-struct fields, `UPPER_SNAKE_CASE` constants and macros, `snake_case`
  file names.
- Comments: `///` above declarations in headers, `///<` for short struct field notes, `//` in `.cpp` files to explain
  why. Comment density is high on purpose because students read this code to learn Vulkan; match it.
