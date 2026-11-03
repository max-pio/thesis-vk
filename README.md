# Vulkan Thesis Framework

A lightweight C++20 / Vulkan 1.3 project setup. Questions or suggestions: [max.piochowiak@kit.edu](mailto:max.piochowiak@kit.edu)

## Contents

* [Dependencies](#dependencies)
* [Installation](#installation)
* [Build and Run](#build-and-run)
* [Development](#development)
* [Project Structure](#project-structure)
* [Samples](#samples)
* [Vulkan Concepts](#vulkan-concepts-used-in-the-framework)
* [Debugging](#debugging)
* [License](#license)

## Dependencies

Built with [CMake](https://cmake.org/). Libraries used:

* [Vulkan](https://www.vulkan.org/) 1.3 with [dynamic rendering](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_dynamic_rendering.html), [synchronization2](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_synchronization2.html), [buffer device addresses](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_buffer_device_address.html) and [VK_KHR_unified_image_layouts](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_unified_image_layouts.html)
* [GLFW](https://www.glfw.org/): windows and surfaces (zlib license)
* [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap): Vulkan instance, device and swapchain (MIT)
* [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator): memory for buffers and images (MIT)
* [Dear ImGui](https://github.com/ocornut/imgui): GUI (MIT)
* [glm](https://github.com/g-truc/glm): vector and matrix math (MIT or Happy Bunny license)
* [stb_image_write](https://github.com/nothings/stb): saving frames as PNG (MIT or public domain)

All of them have permissive licenses that are compatible with the MIT license of this framework (see [License](#license)).

## Installation

CMake downloads and builds GLFW, vk-bootstrap, VMA, ImGui, glm and stb itself ([FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)), so the first configure needs an internet connection. You only install:

* a C++20 compiler (GCC 10, Clang 10, Visual Studio 2019 16.11 or newer)
* CMake 3.24 or newer
* the **Vulkan SDK**: Vulkan headers and loader, validation layers and the `glslc` shader compiler
* a GPU and driver with Vulkan 1.3 (all reasonably recent NVIDIA, AMD and Intel GPUs)

### Ubuntu / Debian / Unix

1. Install compiler, CMake and the libraries GLFW needs:

   `sudo apt install build-essential cmake git libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev wayland-protocols`

   Optional, for a CMake GUI: `sudo apt install cmake-qt-gui`

2. Install the latest [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#linux) and check that it finds your GPU:

   ```sh
   mkdir -p ~/vulkansdk && cd ~/vulkansdk
   wget https://sdk.lunarg.com/sdk/download/latest/linux/vulkan-sdk.tar.xz && tar xf vulkan-sdk.tar.xz
   echo "source $(echo ~/vulkansdk/1.*/setup-env.sh)" >> ~/.bashrc && source ~/.bashrc
   vulkaninfo --summary   # should list your GPU with Vulkan API version >= 1.3
   ```

Notes:

* The `source` line sets `VULKAN_SDK`, which CMake uses to find the headers.
* Use the LunarG SDK, not distribution packages (`libvulkan-dev`, ...): vk-bootstrap needs Vulkan headers 1.4, which most distributions do not ship yet.
* If you configured a build with old headers, delete the build directory first.

### Windows

1. Install [Visual Studio](https://visualstudio.microsoft.com/de/free-developer-offers/) with the "Desktop development with C++" workload (includes CMake).
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows). The installer sets `VULKAN_SDK`.

### GPU and Driver

* The program prints which GPU it uses and whether `VK_KHR_unified_image_layouts` is available.
* Without that extension (e.g. old drivers) everything still works, but `VK_IMAGE_LAYOUT_GENERAL` everywhere may be slower on some GPUs. Update your driver if possible.

## Build and Run

### From the Console

`CMakePresets.json` defines the presets `debug` and `release`, which build out of source into `build-debug/` and `build-release/`.

1. Configure (the first time this downloads the dependencies): `cmake --preset debug`
2. Build: `cmake --build --preset debug -j`
3. Run: `./build-debug/thesis-framework-vk`

* Use `release` instead of `debug` for a Release build, which you need for performance measurements.
* Run the configure step again after adding files to `CMakeLists.txt`.

Debug builds enable validation:

* **Validation layers** print an error whenever you use the API incorrectly. Always fix these!
* **Synchronization validation** finds many missing or wrong pipeline barriers (slower; `--no-sync-validation` turns it off).
* On exit the program reports the number of validation messages and returns an error code if there were any.

### Using an IDE

Import `CMakeLists.txt` from the base directory in an IDE with CMake support, e.g. [CLion](https://www.jetbrains.com/de-de/clion/), [Visual Studio](https://visualstudio.microsoft.com/de/free-developer-offers/) or [VS Code](https://code.visualstudio.com/) with the CMake Tools extension.

### Command Line Options

`./thesis-framework-vk --help` lists all options. Common ones:

* `--size 1920x1080`: window / render size
* `--capture N`: save frame N (counting from 0, without GUI) as PNG and exit; `--capture-file figure.png` sets the file name
* `--log-level`, `--log-file`: console and file logging
* `--validation`, `--no-validation`, `--sync-validation`, `--no-sync-validation`: on by default in Debug builds

Captured frames are reproducible: animations advance by a fixed time step (1/60 s, or `--fixed-timestep`) and the camera ignores input. Use this for thesis figures and to compare two versions of your code.

Example: `./thesis-framework-vk --size 1920x1080 --capture 300 --capture-file figure.png`

To add an option, add a field to the `Config` struct (`args.hpp`) and an entry to the option table in `args.cpp`.

### Time Measurements and Benchmarks

* The GUI shows the CPU and GPU times of the last second.
* Measure GPU times with `GpuTimer` (timestamp queries, `core/gpu/gpu_timer.hpp`) and CPU times with `CpuTimer` (`core/cpu_timer.hpp`). Add a timer for each part of your method that you want to evaluate.
* For the thesis, benchmark from the command line with a Release build:

  `./thesis-framework-vk --no-vsync --benchmark 1000 --results-file results.csv --run-name baseline`

What this does:

* renders 100 warmup frames (`--benchmark-warmup`), then measures 1000 frames
* appends one CSV row with mean, minimum and maximum of every timer, then exits
* the row also holds all options, the git commit, the build type, the GPU and `timing_caveats` (`debug_build`, `validation`, `vsync`, ... mean the times are not representative)

Tips:

* Run the program several times with different options (shell or Python script) and evaluate the CSV with pandas, a spreadsheet or pgfplots.
* Adding an option or a timer changes the columns: start a new results file.

## Development

The project defines a simple render loop with an example GUI. Extend it with your own files and shaders.

* **New files**: add new `.cpp` / `.hpp` files to `SOURCES` / `HEADERS` in `CMakeLists.txt` and run CMake again. CMake builds `core/` as the static library `framework`, shared by the main program and the samples.
* **Shaders**: GLSL, compiled to SPIR-V at runtime with `glslc`, so they are not part of the C++ build.
  * **F5** recompiles them while the program runs; errors show in the GUI and the console.
  * Shared code goes into the shader directory and is included with `#include "file.glsl"` (e.g. `instance_common.glsl`).
  * `utils::compileShader(..)` can set preprocessor defines to compile shader variants.
* **Logging**: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` (`log.hpp`, printf-style). `DLOG("..")` prints the time since program start and since the last `DLOG`, handy for finding slow code.
* **Renaming**: the executable name is set in `add_executable(..)` in `CMakeLists.txt`, the window title in `src/main.cpp`.

### Code Style

* [clang-format](https://clang.llvm.org/docs/ClangFormat.html) with `.clang-format`, which also documents the naming conventions and comment style:
  * `PascalCase` types, `camelCase` functions and variables, `mPascalCase` class members
  * `///` documentation comments above declarations in headers, `//` comments in implementations
* Most IDEs format automatically. From the console: `clang-format -i src/*.cpp src/*.hpp core/*.cpp core/*.hpp core/gpu/*.cpp core/gpu/*.hpp samples/*/*.cpp samples/*/*.hpp`
* [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) checks the naming with `.clang-tidy` and needs the `compile_commands.json` from the CMake build directory:
  `run-clang-tidy-18 -p build-debug "$PWD/(src|core|samples)/"` (the suffix depends on your LLVM version)
* CLion and clangd in VS Code show these warnings in the editor.
* Debug builds enable compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow`, `/W4` for MSVC) for the framework, `src/` and the samples, but not for the dependencies (`enable_warnings()` in `CMakeLists.txt`). Keep the build free of warnings.

## Project Structure

The default project renders instanced cubes with a GUI color and a texture on their faces. `src/` is your starting point (main loop and renderer); `core/` is the framework library that you usually only extend.

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
* `core/volume.cpp`: loads raw 3D scalar volumes from disk (`utils::loadVolume()`, see [Volume Data](#volume-data))

## Samples

`samples/` holds programs that show common techniques. Each sample is its own executable with its own `Renderer` class (same interface as `src/renderer.hpp`), built with the shared main loop `src/main.cpp`. To start from a sample, copy its `renderer.hpp`/`renderer.cpp` over the ones in `src/` and its shaders into `shader/`, and list the shaders in `SHADERS` in `CMakeLists.txt`. Nothing else changes: every renderer finds its shaders through `SHADERS_PATH`, which CMake defines per executable (the sample's `shader/` folder for samples, `shader/` for the main program). Shaders that `#include` shared code from `shader/` find it via the framework's `-I` path.

* `sample-gbuffer` (`samples/gbuffer/`): deferred shading at a render resolution independent of the window size (render scale in the GUI).
  * G-buffer pass: albedo, normals and depth into several images (multiple render targets)
  * lighting pass: one full-screen triangle reconstructs world positions from depth and shades them with many point lights
  * blit: scales the result to the window
  * the GUI shows the G-buffer contents and the GPU time of each pass
* `sample-fullscreen-quad` (`samples/fullscreen-quad/`): the smallest possible renderer, a fullscreen quad that shows its uv coordinates as color, tinted with a GUI color.
  * one pipeline that renders directly into the swapchain image, with a push constant and without buffers, descriptor sets or barriers: a minimal starting point for image-space techniques

* `sample-volume` (`samples/volume/`): loads a raw 3D scalar volume from disk and renders it with a ray marcher in a fragment shader (see [Volume Data](#volume-data)).
  * the volume is a 3D texture (`VK_IMAGE_TYPE_3D`) with a linear sampler; the ray is intersected with the bounding box and sampled in steps
  * modes: direct volume rendering (emission-absorption, front-to-back compositing with early termination), maximum intensity projection and isosurface with gradient shading
  * the GUI sets the value window, density, iso value, sampling rate and voxel spacing, and shows the GPU time

## Volume Data

Thesis topics in scientific visualization need real 3D scalar fields (CT, MRI, simulations). The framework reads **raw files** (the voxels without header, little-endian, x varies fastest) with `utils::loadVolume(path)` (`core/volume.hpp`). The grid size and the voxel type are part of the file name:

* `name_WxHxD_type.raw`, e.g. `bonsai_256x256x256_uint8.raw`, with type `uint8`, `uint16`, `int16`, `float32` or `float64` (GPUs cannot filter 64 bit floats, so the loader converts `float64` to `float32` and logs a warning)
* `name_WxHxD.raw`: the type is inferred from the file size (1 byte per voxel is `uint8`, 2 bytes `uint16`, 4 bytes `float32`, 8 bytes `float64`)

The loader throws `std::runtime_error` with a readable message if the name or the file size does not fit. The result is a `Volume` with the voxels and the value range, which you upload with `VulkanContext::createTexture(VK_IMAGE_TYPE_3D, .., utils::getVkFormat(volume.type), ..)`. Integer voxels are normalized by the texture format (`uint8` to [0, 1], `int16` to [-1, 1], ..), so shaders sample floats; `Volume::minValue` / `maxValue` give the range in these units. The format has no voxel spacing: it is 1 after loading, set it yourself (see the table). `sample-volume` shows the whole path.

**The volumes are not part of the repository.** Download them with the script that verifies the SHA-512 hash of each file from the web page of the collection, into `data/volumes/` (ignored by git):

```
cmake -P cmake/download_volumes.cmake                         # default: bonsai (16 MB)
cmake -DVOLUMES="neghip;foot;carp" -P cmake/download_volumes.cmake
cmake -DVOLUMES=list -P cmake/download_volumes.cmake          # lists all names
./build-debug/sample-volume                                   # shows data/volumes/bonsai_256x256x256_uint8.raw
./build-debug/sample-volume --volume data/volumes/carp_256x256x512_uint16.raw
```

The data sets come from the [Open SciVis Datasets](https://klacansky.com/open-scivis-datasets/) collection by Pavol Klacansky, which many recent papers use for their evaluation, so your results stay comparable. The collection has no single license; the data sets have different origins (e.g. volvis.org, hospitals, simulation groups). Check the acknowledgement of the data set on the web page and **cite it** (BibTeX is provided there) if you use it in your thesis. Volumes in the table can be downloaded by name with the script; all dimensions are `width x height x depth`.

| Name | Size | Type | Spacing | File size | Used in |
|---|---|---|---|---|---|
| `neghip` | 64x64x64 | uint8 | 1x1x1 | 256 kB | tiny, for tests |
| `bonsai` (default) | 256x256x256 | uint8 | 1x1x1 | 16 MB | classic CT, thin structures |
| `foot`, `skull`, `aneurism` | 256x256x256 | uint8 | 1x1x1 | 16 MB each | Foot in the visibility optimization paper below |
| `csafe_heptane` | 302x302x302 | uint8 | 1x1x1 | 26 MB | simulation, Heptane in the visibility optimization paper |
| `mri_woman` | 256x256x109 | uint16 | 1x1x1.5 | 14 MB | MRI, only 12 bit of the 16 bit range are used |
| `tacc_turbulence` | 256x256x256 | float32 | 1x1x1 | 64 MB | simulation, heavy-tailed values: narrow the window |
| `carp` | 256x256x512 | uint16 | 0.78x0.39x1 | 64 MB | CT, anisotropic voxels |
| `duct` | 193x194x1000 | float32 | 1x1x1 | 143 MB | simulation |
| `bunny` | 512x512x361 | uint16 | 0.34x0.34x0.5 | 181 MB | CT |
| `prone` | 512x512x463 | uint16 | 0.625x0.625x1 | 232 MB | CT (colon) |
| `magnetic_reconnection` | 512x512x512 | float32 | 1x1x1 | 512 MB | simulation, in the visibility optimization paper |
| `chameleon` | 1024x1024x1080 | uint16 | 0.092x0.092x0.105 | 2.1 GB | CT, in both papers below; the GPU needs the memory for the 3D texture |

Volumes from the literature that you can compare with: Chameleon, Heptane, Foot and Magnetic Reconnection (Open SciVis names) are evaluation data sets of [View-Dependent Visibility Optimization for Monte Carlo Volume Visualization (Lerzer and Dachsbacher, Eurographics 2025)](https://cg.ivd.kit.edu/publications/2025/mc-visibility-opt/paper_mc-vis-opt.pdf); Chameleon is also used by [Real-Time Denoising of Volumetric Path Tracing for Direct Volume Rendering (Iglesias-Guitian et al.)](https://arxiv.org/pdf/2106.08034), which additionally uses Manix, Heloderma and Dragon (not part of the collection). `sample-volume` maps the x, y and z axes of the file to the world axes with +y up. Upright data sets like the bonsai look right, but CT and MRI scans are usually stored with the patient's long axis along z, so they appear rotated (`mri_woman` is seen from above): rotate the volume in your own renderer.

Larger volumes of the collection (up to 256 GB) are listed on its web page; they need a streaming or bricked approach and are beyond what the loader (reads the whole file into memory) is meant for.

## Vulkan Concepts used in the Framework

Modern Vulkan features remove a lot of boilerplate:

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

## Debugging

* [RenderDoc](https://renderdoc.org/) and [Nsight Graphics](https://developer.nvidia.com/nsight-graphics) capture and inspect frames. Shaders carry debug info, so you see the GLSL source.
* Flickering, results one frame behind, or results that differ between GPUs usually mean a **missing barrier**: run a Debug build (synchronization validation is on by default there, or pass `--sync-validation`).

## License

The framework is licensed under the [MIT license](LICENSE), `Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology`. Every source and shader file starts with the short header `// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology` + `// SPDX-License-Identifier: MIT`; use the same header in new files. You can build your thesis code on it and publish it under a license of your choice, as long as the copyright and license notice of the framework are kept. The dependencies keep their own licenses (listed under [Dependencies](#dependencies)); CMake downloads them, they are not part of this repository.
