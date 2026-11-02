// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

// Declarations shared by instance.vert and instance_color.frag, included with #include "instance_common.glsl".
// glslc finds included files in the shader directory (see utils::compileShader()).

// buffer_reference allows to access buffers through their GPU address (a pointer)
#extension GL_EXT_buffer_reference : require
// scalar block layout: buffer data is tightly packed like in C++, e.g. a vec3 has a size of 12 bytes
#extension GL_EXT_scalar_block_layout : require

// pointer types to buffers in GPU memory
layout(buffer_reference, scalar) readonly buffer VertexBuffer {
   vec3 vertices[];
};
layout(buffer_reference, scalar) readonly buffer PositionsBuffer {
   // (offset-)positions of all instances, animated on the CPU every frame
   vec3 instance_pos[];    // the last variable in a buffer can have dynamic size
};

// push constants must match the PushConstants struct in renderer.hpp
layout(push_constant) uniform PushConstants {
   mat4 mvp;               // Model-View-Projection matrix
   vec4 color;
   VertexBuffer vertex_buffer;
   PositionsBuffer positions_buffer;
} pc;
