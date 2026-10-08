// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// The constants of every draw: see struct vela_quad_push in render/render.h.
layout(push_constant) uniform Push {
    vec4 dst; // x, y, width, height in target pixels
    vec2 target; // target size
    float alpha;
    uint flags;
    vec2 uvOrigin; // texture coordinates of the top left corner
    vec2 uvX; // step along the top edge
    vec2 uvY; // step along the left edge
    vec2 pad;
    vec4 color; // rectangles and shadows: premultiplied linear color
    // The shape (docs/renderer.md §8): a rounded rectangle in target pixels.
    // The clip (flags bit 1) or, for shadows, what casts it.
    vec4 shapeRect;
    vec4 shape; // x: corner radius; y: shadow sigma (pixels)
    // The output's color filter (flags bit 2): night light and color filters,
    // a 3x3 matrix in linear space (one row per vec4).
    vec4 colorMatrix[3];
} pc;

// The filter is linear: it holds for premultiplied colors too, and applying it
// to every draw equals applying it to the finished image (blends are linear
// combinations in linear space). The color stays within the alpha.
vec4 colorFilter(vec4 c)
{
    if ((pc.flags & 4u) == 0u) {
        return c;
    }
    const vec3 f = vec3(dot(pc.colorMatrix[0].xyz, c.rgb), dot(pc.colorMatrix[1].xyz, c.rgb), dot(pc.colorMatrix[2].xyz, c.rgb));
    return vec4(clamp(f, 0.0, c.a), c.a);
}
