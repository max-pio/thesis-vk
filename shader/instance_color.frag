// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460

// buffer pointer types and push constants
#include "instance_common.glsl"

// texture from the descriptor set, see Renderer::createPatternTexture()
layout(set = 0, binding = 0) uniform sampler2D pattern;

layout(location = 0) in vec3 vertex_color;
layout(location = 1) in vec3 object_pos;

layout(location = 0) out vec4 out_color;

void main()
{
   // flat shading: compute the face normal from the screen space derivatives of the position. The cubes are only
   // moved, not rotated, so the normal in the cube is the same as in the world.
   vec3 normal = normalize(cross(dFdx(object_pos), dFdy(object_pos)));
   float light = 0.4f + 0.6f * abs(dot(normal, normalize(vec3(0.3f, 1.f, 0.5f))));

   // texture coordinates on the cube faces: the object space coordinates in [-1, 1] that vary across the face
   vec3 n = abs(normal);
   vec2 face_uv = n.x > 0.5f ? object_pos.yz : n.y > 0.5f ? object_pos.xz : object_pos.xy;
   vec3 color = vertex_color * texture(pattern, 0.5f * face_uv + 0.5f).rgb;

   out_color = vec4(color * light, pc.color.a);
}
