// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#version 460

// texture coordinates of the image: (0, 0) top left, (1, 1) bottom right
layout(location = 0) out vec2 uv;

// One triangle that covers the whole image, without a vertex buffer: vkCmdDraw(cmd, 3, ..) creates the vertices
// uv = (0, 0), (2, 0) and (0, 2), i.e. NDC (-1, -1), (3, -1) and (-1, 3). The parts outside of the image are clipped.
// One triangle is a bit faster than two triangles of a quad, which would shade the pixels on their diagonal twice.
void main()
{
   uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
   gl_Position = vec4(uv * 2.f - 1.f, 0.f, 1.f);
}
