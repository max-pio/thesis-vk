// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460
// scalar block layout: the push constants are tightly packed like the C++ struct PushConstants in renderer.hpp
#extension GL_EXT_scalar_block_layout : require

// (0, 0) is the top left and (1, 1) the bottom right of the image, see volume.vert
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// The volume as 3D texture. Texture coordinates are in [0, 1]^3, (0, 0, 0) is the corner of the first voxel. The
// sampler uses linear filtering, so texture() interpolates trilinearly between 8 voxels. The integer formats are
// normalized (uint8 255 -> 1.0), float32 values are unchanged.
layout(set = 0, binding = 0) uniform sampler3D volume;

// must match PushConstants in renderer.hpp
layout(push_constant, scalar) uniform PushConstants
{
   mat4 clip_to_world;
   vec3 camera_pos;
   float step_size;
   vec3 box_half_size;
   float window_min;
   vec3 voxel_size;
   float window_max;
   float density;
   float iso_value;
   int mode;
   int use_colormap;
} pc;

// must match RenderMode in renderer.hpp
const int MODE_DVR = 0;
const int MODE_ISOSURFACE = 1;

const vec3 BACKGROUND = vec3(0.08, 0.09, 0.11);

// The volume fills the box [-box_half_size, box_half_size] in world space, with the x, y and z axes of the file along
// the world axes. World +y is up: the bonsai and other upright data sets are only upright because their file stores
// "up" as +y. Others (e.g. CT scans that stack slices along z) have to be rotated by the renderer.
float sampleVolume(vec3 p)
{
   vec3 tc = p / (2.f * pc.box_half_size) + 0.5f;
   return texture(volume, tc).r;
}

// raw value -> [0, 1] with the window of the GUI
float windowed(float value)
{
   return clamp((value - pc.window_min) / (pc.window_max - pc.window_min), 0.f, 1.f);
}

// Transfer function for sampled value v in [0, 1]. Returns a vec4 with:
// .rgb: a simple color map: dark blue -> cyan -> yellow -> red, or gray
// .a: opacity in the alpha channel where 0.f is fully transparent
// The simple implementation has alpha = v.
vec4 transferFunction(float v)
{
   if (pc.use_colormap == 0)
      return vec4(v);

   const vec4 c0 = vec4(0.10, 0.12, 0.55, v);
   const vec4 c1 = vec4(0.10, 0.75, 0.85, v);
   const vec4 c2 = vec4(0.98, 0.90, 0.25, v);
   const vec4 c3 = vec4(0.85, 0.15, 0.10, v);
   if (v < 1.f / 3.f)
      return mix(c0, c1, v * 3.f);
   if (v < 2.f / 3.f)
      return mix(c1, c2, v * 3.f - 1.f);
   return mix(c2, c3, v * 3.f - 2.f);
}

// Gradient of the volume with central differences, one voxel to each side. Points to higher values.
vec3 gradient(vec3 p)
{
   const vec3 d = pc.voxel_size;
   return vec3(sampleVolume(p + vec3(d.x, 0, 0)) - sampleVolume(p - vec3(d.x, 0, 0)),
               sampleVolume(p + vec3(0, d.y, 0)) - sampleVolume(p - vec3(0, d.y, 0)),
               sampleVolume(p + vec3(0, 0, d.z)) - sampleVolume(p - vec3(0, 0, d.z))) / (2.f * d);
}

// Pseudo random number in [0, 1) per pixel (interleaved gradient noise). A random start offset along the ray turns
// the stripes of the fixed step size into noise.
float jitter()
{
   return fract(52.9829189f * fract(dot(gl_FragCoord.xy, vec2(0.06711056f, 0.00583715f))));
}

void main()
{
   // Ray through this pixel: unproject the point on the far plane (NDC z = 1) of the pixel. The matrix contains the y
   // flip of the camera, so NDC y points down like uv.
   const vec2 ndc = uv * 2.f - 1.f;
   vec4 far_point = pc.clip_to_world * vec4(ndc, 1.f, 1.f);
   const vec3 origin = pc.camera_pos;
   const vec3 dir = normalize(far_point.xyz / far_point.w - origin);

   float t_enter, t_exit;
   // Slab test: intersect the ray with the three pairs of planes of the box. The ray is inside the box between the
   // largest entry and the smallest exit distance.
   {
      const vec3 inv_dir = 1.f / dir;
      const vec3 t_a = (-pc.box_half_size - origin) * inv_dir;
      const vec3 t_b = (pc.box_half_size - origin) * inv_dir;
      const vec3 t_min = min(t_a, t_b);
      const vec3 t_max = max(t_a, t_b);
      t_enter = max(max(t_min.x, t_min.y), max(t_min.z, 0.f));
      t_exit = min(min(t_max.x, t_max.y), t_max.z);
   }
   
   if (t_enter >= t_exit)
   {
      outColor = vec4(BACKGROUND, 1.f);
      return;
   }

   vec3 color = vec3(0.f); // emission accumluated along the ray
   float alpha = 0.f;      // opacity accumulated along the ray

   // isosurface rendering
   if (pc.mode == MODE_ISOSURFACE)
   {
      float t = t_enter + jitter() * pc.step_size;
      float prev_value = sampleVolume(origin + t * dir);
      for (t += pc.step_size; t < t_exit; t += pc.step_size)
      {
         const float value = sampleVolume(origin + t * dir);
         
         // the ray crossed the iso value between the last and this sample (in either direction)
         if ((prev_value - pc.iso_value) * (value - pc.iso_value) <= 0.f && prev_value != value)
         {
            // linear interpolation of the crossing between the two samples
            const float t_hit = t - pc.step_size * (value - pc.iso_value) / (value - prev_value);
            const vec3 hit = origin + t_hit * dir;

            // The gradient points to higher values, i.e. into the object if it is dense. Flip the normal so that it
            // faces the camera (two-sided lighting).
            vec3 normal = normalize(gradient(hit) + vec3(1e-8));
            if (dot(normal, dir) > 0.f)
               normal = -normal;

            // headlight: diffuse shading with a light at the camera, plus ambient light
            const vec4 base = transferFunction(windowed(pc.iso_value));
            color = base.rgb * (0.15f + 0.85f * dot(normal, -dir));
            alpha = 1.f;

            break;
         }

         prev_value = value;
      }
   }
   // semi-transparent volume rendering
   else
   {
      for (float t = t_enter + jitter() * pc.step_size; t < t_exit; t += pc.step_size)
      {
         const float value = windowed(sampleVolume(origin + t * dir));
         const vec4 step_tf = transferFunction(value);

         // Direct volume rendering with the emission-absorption model. The opacity is used as extinction:
         // 1 - exp(-extinction * distance) is the opacity of this step (independent of the step size).
         const float step_alpha = 1.f - exp(-pc.density * step_tf.a * pc.step_size);
         color += (1.f - alpha) * step_alpha * step_tf.rgb;
         alpha += (1.f - alpha) * step_alpha;

         // early ray termination: nothing behind an (almost) opaque point is visible
         if (alpha > 0.99f)
            break;
      }
   }

   outColor = vec4(color + (1.f - alpha) * BACKGROUND, 1.f);
}
