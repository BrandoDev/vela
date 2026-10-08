#version 450
// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#extension GL_GOOGLE_include_directive : require

// Live blur (§8.3), dual Kawase: up to double size with eight weighted
// bilinear reads.

#include "push.glsl"

layout(set = 0, binding = 0) uniform sampler2D source;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec4 tap(vec2 at)
{
    return texture(source, clamp(at, pc.shapeRect.xy, pc.shapeRect.zw));
}

void main()
{
    const vec2 h = pc.shape.xy;
    vec4 sum = tap(uv + vec2(-h.x * 2.0, 0.0));
    sum += tap(uv + vec2(-h.x, h.y)) * 2.0;
    sum += tap(uv + vec2(0.0, h.y * 2.0));
    sum += tap(uv + vec2(h.x, h.y)) * 2.0;
    sum += tap(uv + vec2(h.x * 2.0, 0.0));
    sum += tap(uv + vec2(h.x, -h.y)) * 2.0;
    sum += tap(uv + vec2(0.0, -h.y * 2.0));
    sum += tap(uv + vec2(-h.x, -h.y)) * 2.0;
    outColor = sum / 12.0;
}
