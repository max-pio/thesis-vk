// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
   vec4 tint; // color from the GUI
};

void main()
{
   outColor = vec4(uv, 0.f, 1.f) * tint;
}
