// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
// The vertex stage of the Neon glow for the OpenGL path, which draws the mesh itself (the GPU
// path keeps SDL's own vertex stage). Positions come in as pixels of the render target, top left
// first; `view` turns them into clip space.

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;

layout(set = 1, binding = 0) uniform View {
    // x, y: 2 / width and 2 / height of the target in pixels. z: 1 when the image is stored
    // bottom up (an offscreen target), -1 when it is top down (the window).
    vec4 view;
} vw;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main() {
    v_color = a_color;
    v_uv = a_uv;
    vec2 ndc = a_pos * vw.view.xy - vec2(1.0);
    gl_Position = vec4(ndc.x, ndc.y * vw.view.z, 0.0, 1.0);
}
