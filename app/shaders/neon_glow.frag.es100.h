// SPDX-License-Identifier: GPL-3.0-or-later
// Written by scripts/build-shaders.sh from app/shaders/neon_glow.frag. Do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr char kNeonGlowFragEs100[] = R"shader(#version 100
precision mediump float;
precision highp int;

struct Params
{
    highp vec4 shape;
};

uniform Params fx;

uniform highp sampler2D u_glow;

varying highp vec2 v_uv;
varying highp vec4 v_color;

void main()
{
    highp vec2 q = abs(v_uv - vec2(0.5)) * 2.0;
    highp float d = min(length(q), 1.0);
    highp float edge = 1.0 - ((d * d) * (3.0 - (2.0 * d)));
    highp float core = exp(((-fx.shape.w) * d) * d) * edge;
    highp float halo = texture2D(u_glow, v_uv).w;
    highp float a = (fx.shape.x * core) + (fx.shape.y * halo);
    highp vec3 colour = mix(v_color.xyz, vec3(1.0), vec3(clamp(fx.shape.z * core, 0.0, 1.0)));
    gl_FragData[0] = vec4(colour, v_color.w * a);
}
)shader";
