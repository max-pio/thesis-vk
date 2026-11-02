// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// a point light, must match Renderer::Light in renderer.hpp
struct Light {
   vec3 position;
   float radius;
   vec3 color;
   float intensity;
};
layout(buffer_reference, scalar) readonly buffer LightBuffer {
   Light lights[];
};

// push constants must match LightingPushConstants in renderer.hpp
layout(push_constant, scalar) uniform PushConstants {
   mat4 inverse_view_projection;
   vec3 camera_position;
   uint view_mode;
   LightBuffer light_buffer;
   uint light_count;
} pc;

// the G-buffer, written by the G-buffer pass
layout(set = 0, binding = 0) uniform sampler2D albedo_texture;
layout(set = 0, binding = 1) uniform sampler2D normal_texture;
layout(set = 0, binding = 2) uniform sampler2D depth_texture;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

// view modes, see Renderer::ViewMode
const uint VIEW_LIT = 0;
const uint VIEW_ALBEDO = 1;
const uint VIEW_NORMAL = 2;
const uint VIEW_DEPTH = 3;
const uint VIEW_POSITION = 4;

const vec3 BACKGROUND = vec3(0.9f, 0.94f, 0.98f);

void main()
{
   // The G-buffer has the resolution of this pass: read exactly the texel of this pixel, without filtering
   ivec2 pixel = ivec2(gl_FragCoord.xy);
   vec3 albedo = texelFetch(albedo_texture, pixel, 0).rgb;
   vec3 normal = texelFetch(normal_texture, pixel, 0).xyz;
   float depth = texelFetch(depth_texture, pixel, 0).r;

   // Reconstruct the world position from the depth: the pixel's position in normalized device coordinates (NDC) is
   // (uv * 2 - 1, depth). In this framework y points down in NDC and in uv, so no flip is needed. The inverse
   // view-projection matrix transforms it back, followed by the perspective division.
   vec4 world = pc.inverse_view_projection * vec4(uv * 2.f - 1.f, depth, 1.f);
   vec3 position = world.xyz / world.w;

   // depth 1 (the clear value) means that no cube covers this pixel
   bool background = depth == 1.f;

   if (pc.view_mode == VIEW_ALBEDO) {
      out_color = vec4(albedo, 1.f);
      return;
   }
   if (pc.view_mode == VIEW_NORMAL) {
      out_color = vec4(background ? vec3(0.f) : normal * 0.5f + 0.5f, 1.f);
      return;
   }
   if (pc.view_mode == VIEW_DEPTH) {
      // the depth is not linear: most of the range is close to the camera. Show the distance instead.
      out_color = vec4(vec3(background ? 1.f : distance(position, pc.camera_position) / 300.f), 1.f);
      return;
   }
   if (pc.view_mode == VIEW_POSITION) {
      out_color = vec4(background ? vec3(0.f) : fract(position / 10.f), 1.f);
      return;
   }

   if (background) {
      out_color = vec4(BACKGROUND, 1.f);
      return;
   }

   // Lighting: a weak directional light (sun), and the point lights with Blinn-Phong shading
   vec3 view_dir = normalize(pc.camera_position - position);
   vec3 sun_dir = normalize(vec3(0.3f, 1.f, 0.5f));
   vec3 color = albedo * (0.3f + 0.4f * max(dot(normal, sun_dir), 0.f));
   for (uint i = 0; i < pc.light_count; i++) {
      Light light = pc.light_buffer.lights[i];
      vec3 to_light = light.position - position;
      float light_distance = length(to_light);
      if (light_distance >= light.radius)
         continue;
      vec3 light_dir = to_light / light_distance;
      // smooth falloff that reaches 0 at the radius
      float falloff = pow(clamp(1.f - pow(light_distance / light.radius, 2.f), 0.f, 1.f), 2.f);
      float diffuse = max(dot(normal, light_dir), 0.f);
      float specular = pow(max(dot(normal, normalize(light_dir + view_dir)), 0.f), 32.f);
      color += light.intensity * falloff * light.color * (albedo * diffuse + 0.3f * specular);
   }
   out_color = vec4(color, 1.f);
}
