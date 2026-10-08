#version 450
// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#extension GL_GOOGLE_include_directive : require

// The shadow of a rounded rectangle (docs/renderer.md §8.2): Evan Wallace's
// closed form of a Gaussian integral
// (https://madebyevan.com/shaders/fast-rounded-rectangle-shadows/). One quad,
// no texture, no blur. It isn't drawn under the window: a semi-transparent
// window must not show its shadow through.

#include "push.glsl"
#include "shape.glsl"

layout(location = 0) out vec4 outColor;

float gaussian(float x, float sigma)
{
    const float pi = 3.141592653589793;
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * pi) * sigma);
}

vec2 erf(vec2 x)
{
    const vec2 s = sign(x);
    const vec2 a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

float boxShadowX(float x, float y, float sigma, float corner, vec2 halfSize)
{
    const float delta = min(halfSize.y - corner - abs(y), 0.0);
    const float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    const vec2 integral = 0.5 + 0.5 * erf((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

float boxShadow(vec2 lower, vec2 upper, vec2 point, float sigma, float corner)
{
    const vec2 center = (lower + upper) * 0.5;
    const vec2 halfSize = (upper - lower) * 0.5;
    point -= center;
    // Integrated along y with a few samples; along x the formula is exact.
    const float low = point.y - halfSize.y;
    const float high = point.y + halfSize.y;
    const float start = clamp(-3.0 * sigma, low, high);
    const float end = clamp(3.0 * sigma, low, high);
    const float step = (end - start) / 4.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; ++i) {
        value += boxShadowX(point.x, point.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

void main()
{
    const vec2 p = gl_FragCoord.xy;
    const vec4 caster = pc.shapeRect;
    const float radius = min(pc.shape.x, 0.5 * min(caster.z, caster.w));
    const float sigma = max(pc.shape.y, 0.5);
    float shadow = boxShadow(caster.xy, caster.xy + caster.zw, p, sigma, radius);
    // The window (in uvOrigin and uvX, which shadows don't use): nothing below
    // it.
    const vec4 window = vec4(pc.uvOrigin, pc.uvX);
    shadow *= 1.0 - roundedCoverage(p, window, pc.shape.x);
    outColor = colorFilter(pc.color) * shadow;
}
