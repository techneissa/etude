// SPDX-License-Identifier: GPL-3.0-or-later
// Written by scripts/build-shaders.sh from app/shaders/neon_glow.frag. Do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr char kNeonGlowFragGl120[] = R"shader(#version 120
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif

struct Params
{
    vec4 shape;
};

uniform Params fx;

uniform sampler2D u_glow;

varying vec2 v_uv;
varying vec4 v_color;

void main()
{
    vec2 q = abs(v_uv - vec2(0.5)) * 2.0;
    float d = min(length(q), 1.0);
    float edge = 1.0 - ((d * d) * (3.0 - (2.0 * d)));
    float core = exp(((-fx.shape.w) * d) * d) * edge;
    float halo = texture2D(u_glow, v_uv).w;
    float a = (fx.shape.x * core) + (fx.shape.y * halo);
    vec3 colour = mix(v_color.xyz, vec3(1.0), vec3(clamp(fx.shape.z * core, 0.0, 1.0)));
    gl_FragData[0] = vec4(colour, v_color.w * a);
}
)shader";
