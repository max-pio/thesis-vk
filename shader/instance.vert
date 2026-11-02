// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460
// buffer pointer types and push constants
#include "instance_common.glsl"

// the output location of the vertex shader must match the input location of the fragment shader
layout(location = 0) out vec3 vertex_color;
layout(location = 1) out vec3 object_pos;

void main()
{
   // gl_VertexIndex is the index of the current vertex (0..35 for the cube)
   vec3 in_vertex_pos = pc.vertex_buffer.vertices[gl_VertexIndex];
   // gl_InstanceIndex returns the instance index from the vkCmdDraw(..) host call
   vec3 offset = pc.positions_buffer.instance_pos[gl_InstanceIndex];
   // move model to the current instance position: move all vertices by offset
   vec3 pos = in_vertex_pos + offset;
   // the position in the cube, used for the texture coordinates and the normal
   object_pos = in_vertex_pos;

   // gl_Position is the output vertex position in homogeneous coordinates in clip space
   gl_Position = pc.mvp * vec4(pos, 1.f);

   // optional: vary the color per instance
   float v = float((gl_InstanceIndex ^ 23582361) % 1024) / 1024.f;
   vertex_color = pc.color.rgb * (0.5f + 0.5f * v);
}
