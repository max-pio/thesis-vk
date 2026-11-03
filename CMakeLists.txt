cmake_minimum_required(VERSION 3.24)
project(thesis-framework-vk VERSION 1.0 DESCRIPTION "CG Thesis Framework (Vulkan)" LANGUAGES C CXX)

# Require out-of-source builds (this is good practice)
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()

# Use C++20 in all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json for clang-tidy (see .clang-tidy) and clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to a Debug build (enables the Vulkan validation layers) if nothing else is specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# source files -----------------------------------------------------------
# All files that will be compiled have to be listed here.
# Shaders are compiled at runtime (see utils::compileShader in core/gpu/gpu_utils.cpp) and only listed so that they show up in IDEs.

# the framework: everything except the main loop (main.cpp) and the renderer
set(FRAMEWORK_HEADERS
    core/args.hpp
    core/camera.hpp
    core/gui.hpp
    core/log.hpp
    core/results.hpp
    core/cpu_timer.hpp
    core/utils.hpp
    core/volume.hpp
    core/gpu/gpu_timer.hpp
    core/gpu/gpu_utils.hpp
    core/gpu/vulkan_context.hpp
)

set(FRAMEWORK_SOURCES
    core/args.cpp
    core/camera.cpp
    core/gui.cpp
    core/log.cpp
    core/results.cpp
    core/utils.cpp
    core/volume.cpp
    core/gpu/gpu_timer.cpp
    core/gpu/gpu_utils.cpp
    core/gpu/vulkan_context.cpp
    core/gpu/vma_implementation.cpp
)

# the main program: the main loop and the renderer of the cube demo
set(HEADERS
    src/renderer.hpp
)

set(SOURCES
    src/main.cpp
    src/renderer.cpp
)

set(SHADERS
    shader/instance_common.glsl
    shader/instance.vert
    shader/instance_color.frag
)

# define targets ---------------------------------------------------------

# Compiler warnings for the project's own targets (not the dependencies), only in Debug builds.
# -Wno-missing-field-initializers: designated initializers of Vulkan structs (e.g. {.sType = ..}) leave the other
# fields zero on purpose.
function(enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/W4>)
    else()
        target_compile_options(${target} PRIVATE
            "$<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wno-missing-field-initializers>")
    endif()
endfunction()

# The framework is a static library, which is shared by the main program and the samples
add_library(framework STATIC ${FRAMEWORK_HEADERS} ${FRAMEWORK_SOURCES})
# allows including all project headers relative to core/, e.g. #include "gpu/vulkan_context.hpp"
target_include_directories(framework PUBLIC core)
enable_warnings(framework)
# MSVC needs its standard conforming preprocessor for __VA_OPT__ (used by DLOG in log.hpp)
if(MSVC)
    target_compile_options(framework PUBLIC /Zc:preprocessor)
endif()

# git_version.hpp with the git commit and build type, written to the results of benchmarks
include(cmake/git_version.cmake)
git_version(framework)

# add executable
# this is the build target that we create in the end and that we link all libraries to
add_executable(thesis-framework-vk ${HEADERS} ${SOURCES} ${SHADERS})
# main.cpp includes <renderer.hpp> (see the samples below)
target_include_directories(thesis-framework-vk BEFORE PRIVATE src)
target_compile_definitions(thesis-framework-vk PRIVATE SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shader/")
target_link_libraries(thesis-framework-vk PRIVATE framework)
enable_warnings(thesis-framework-vk)

# Samples: each sample is an executable with the same main loop (src/main.cpp) and its own Renderer class.
# main.cpp includes <renderer.hpp>, which the compiler finds in the first include directory: the sample's directory.
option(BUILD_SAMPLES "Build the samples in samples/" ON)
if(BUILD_SAMPLES)
    # deferred shading with a G-buffer, at a render resolution independent of the window size
    set(GBUFFER_SHADERS
        samples/gbuffer/shader/gbuffer_scene.glsl
        samples/gbuffer/shader/gbuffer.vert
        samples/gbuffer/shader/gbuffer.frag
        samples/gbuffer/shader/fullscreen.vert
        samples/gbuffer/shader/lighting.frag
    )
    add_executable(sample-gbuffer
        src/main.cpp samples/gbuffer/renderer.hpp samples/gbuffer/renderer.cpp ${GBUFFER_SHADERS})
    target_include_directories(sample-gbuffer BEFORE PRIVATE samples/gbuffer)
    target_compile_definitions(sample-gbuffer PRIVATE
        SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/samples/gbuffer/shader/")
    target_link_libraries(sample-gbuffer PRIVATE framework)
    enable_warnings(sample-gbuffer)

    # the smallest possible renderer: a fullscreen quad that shows its uv coordinates, tinted with a GUI color
    set(FULLSCREEN_QUAD_SHADERS
        samples/fullscreen-quad/shader/quad.vert
        samples/fullscreen-quad/shader/quad.frag
    )
    add_executable(sample-fullscreen-quad
        src/main.cpp samples/fullscreen-quad/renderer.hpp samples/fullscreen-quad/renderer.cpp ${FULLSCREEN_QUAD_SHADERS})
    target_include_directories(sample-fullscreen-quad BEFORE PRIVATE samples/fullscreen-quad)
    target_compile_definitions(sample-fullscreen-quad PRIVATE
        SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/samples/fullscreen-quad/shader/")
    target_link_libraries(sample-fullscreen-quad PRIVATE framework)
    enable_warnings(sample-fullscreen-quad)

    # loads a raw 3D volume (see README) and renders it with a ray marcher
    set(VOLUME_SHADERS
        samples/volume/shader/volume.vert
        samples/volume/shader/volume.frag
    )
    add_executable(sample-volume
        src/main.cpp samples/volume/renderer.hpp samples/volume/renderer.cpp ${VOLUME_SHADERS})
    target_include_directories(sample-volume BEFORE PRIVATE samples/volume)
    target_compile_definitions(sample-volume PRIVATE
        SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/samples/volume/shader/"
        # the directory of the default volume files, see cmake/download_volumes.cmake
        VOLUMES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/volumes/")
    target_link_libraries(sample-volume PRIVATE framework)
    enable_warnings(sample-volume)

    # cmake --build --preset debug --target download-volumes: downloads the default volume into data/volumes/. More
    # volumes: cmake -DVOLUMES="neghip;foot" -P cmake/download_volumes.cmake
    add_custom_target(download-volumes
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/download_volumes.cmake
        COMMENT "Downloading the default volume into data/volumes/")
endif()

# external dependencies and libraries ------------------------------------

# The Vulkan SDK (https://vulkan.lunarg.com/) provides the Vulkan headers and loader, the validation layers,
# and the glslc shader compiler that we use to compile GLSL shaders to SPIR-V at runtime.
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)
# find optional packages
find_package(OpenMP)

# All other libraries are downloaded and built automatically with FetchContent (internet connection required on
# the first CMake configure). The sources end up in <build-dir>/_deps.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# GLFW creates windows and rendering surfaces
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# glm provides vector and matrix math
FetchContent_Declare(glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# vk-bootstrap simplifies instance, device and swapchain creation
set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vk-bootstrap
    URL https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v1.4.365.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Vulkan Memory Allocator (VMA) handles GPU memory allocations for buffers and images
set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vma
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Dear ImGui is a lightweight GUI library. It has no CMake project, so we define the library target ourselves below.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# stb_image_write saves images as PNG files, e.g. captured frames. stb has no releases, so we use a fixed commit.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # stb is a collection of headers without a CMake project: only download it
    SOURCE_SUBDIR no-cmake-project
)

FetchContent_MakeAvailable(glfw glm vk-bootstrap vma imgui stb)

# Include the headers of the dependencies as system headers, so that the warnings of our targets (enable_warnings())
# do not report warnings in the dependencies' code
foreach(dep glfw glm vk-bootstrap VulkanMemoryAllocator)
    get_target_property(dep_include_dirs ${dep} INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(${dep} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dep_include_dirs}")
endforeach()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
# GLFW would include the OpenGL header GL/gl.h otherwise, which is not installed everywhere (e.g. CI) and not needed
# for Vulkan. PUBLIC: the framework and all programs linking it inherit this definition.
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)

# set defines that can be used in the project code ----------------------

# SHADERS_PATH, the directory of a renderer's own shaders, is defined per executable above, so that a renderer copied
# from a sample into src/ finds its shaders without changes. The framework only needs the directory of the shared
# #include files.
target_compile_definitions(framework PRIVATE
    # FRAMEWORK_SHADERS_PATH points to the shared shader files (#include "file.glsl")
    FRAMEWORK_SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shader/"
)
target_compile_definitions(framework PUBLIC
    # SHADER_BINARY_PATH is the directory the compiled SPIR-V files are written to
    SHADER_BINARY_PATH="${CMAKE_CURRENT_BINARY_DIR}/shader_spirv/"
    # GLSLC_PATH is the GLSL to SPIR-V compiler from the Vulkan SDK
    GLSLC_PATH="${Vulkan_GLSLC_EXECUTABLE}"
    # Vulkan uses a [0, 1] depth range instead of OpenGL's [-1, 1]
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
)

# link required libraries, PUBLIC: also for the programs that link the framework
target_link_libraries(framework PUBLIC
    Vulkan::Vulkan
    glfw
    glm::glm
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    imgui
)
# stb is header only
target_include_directories(framework SYSTEM PRIVATE ${stb_SOURCE_DIR})

# link optional libraries
if(OpenMP_CXX_FOUND)
    target_link_libraries(framework PUBLIC OpenMP::OpenMP_CXX)
endif()
