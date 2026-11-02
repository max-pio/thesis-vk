// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460
#include "gbuffer_scene.glsl"

layout(location = 0) out vec3 vertex_color;
layout(location = 1) out vec3 object_pos;
layout(location = 2) out vec3 world_pos;

void main()
{
   // vertex pulling and instancing as in the cube demo (shader/instance.vert)
   vec3 in_vertex_pos = pc.vertex_buffer.vertices[gl_VertexIndex];
   object_pos = in_vertex_pos;
   world_pos = in_vertex_pos + pc.positions_buffer.instance_pos[gl_InstanceIndex];
   gl_Position = pc.view_projection * vec4(world_pos, 1.f);

   float v = float((gl_InstanceIndex ^ 23582361) % 1024) / 1024.f;
   vertex_color = pc.color.rgb * (0.5f + 0.5f * v);
}
