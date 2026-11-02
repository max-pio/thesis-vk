// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

// Declarations shared by gbuffer.vert and gbuffer.frag (the G-buffer pass)

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
   vec3 vertices[];
};
layout(buffer_reference, scalar) readonly buffer PositionsBuffer {
   vec3 instance_pos[];
};

// push constants must match ScenePushConstants in renderer.hpp
layout(push_constant, scalar) uniform PushConstants {
   mat4 view_projection;
   vec4 color;
   vec3 camera_position;
   uint padding;
   VertexBuffer vertex_buffer;
   PositionsBuffer positions_buffer;
} pc;
