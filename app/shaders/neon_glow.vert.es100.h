// SPDX-License-Identifier: GPL-3.0-or-later
// Written by scripts/build-shaders.sh from app/shaders/neon_glow.vert. Do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr char kNeonGlowVertEs100[] = R"shader(#version 100

struct View
{
    vec4 view;
};

uniform View vw;

varying vec4 v_color;
attribute vec4 a_color;
varying vec2 v_uv;
attribute vec2 a_uv;
attribute vec2 a_pos;

void main()
{
    v_color = a_color;
    v_uv = a_uv;
    vec2 ndc = (a_pos * vw.view.xy) - vec2(1.0);
    gl_Position = vec4(ndc.x, ndc.y * vw.view.z, 0.0, 1.0);
}
)shader";
