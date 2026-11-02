// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460

// texture coordinates of the image: (0, 0) top left, (1, 1) bottom right
layout(location = 0) out vec2 uv;

// A quad as triangle strip without a vertex buffer: vkCmdDraw(cmd, 4, ..) creates the vertices
// uv = (0, 0), (1, 0), (0, 1), (1, 1), i.e. NDC (-1, -1), (1, -1), (-1, 1), (1, 1). Y points down in NDC.
void main()
{
   uv = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
   gl_Position = vec4(uv * 2.f - 1.f, 0.f, 1.f);
}
