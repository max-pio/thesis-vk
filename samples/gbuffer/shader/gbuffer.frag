// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460
#include "gbuffer_scene.glsl"

layout(set = 0, binding = 0) uniform sampler2D pattern;

layout(location = 0) in vec3 vertex_color;
layout(location = 1) in vec3 object_pos;
layout(location = 2) in vec3 world_pos;

// Multiple render targets: each output location is written to the color attachment with the same index
layout(location = 0) out vec4 out_albedo; // surface color
layout(location = 1) out vec4 out_normal; // world space normal

void main()
{
   // flat shading: the face normal from the screen space derivatives of the position, pointing towards the camera
   vec3 normal = normalize(cross(dFdx(world_pos), dFdy(world_pos)));
   if (dot(normal, pc.camera_position - world_pos) < 0.f)
      normal = -normal;

   // texture coordinates on the cube faces, as in the cube demo
   vec3 n = abs(normal);
   vec2 face_uv = n.x > 0.5f ? object_pos.yz : n.y > 0.5f ? object_pos.xz : object_pos.xy;

   out_albedo = vec4(vertex_color * texture(pattern, 0.5f * face_uv + 0.5f).rgb, 1.f);
   out_normal = vec4(normal, 0.f);
}
