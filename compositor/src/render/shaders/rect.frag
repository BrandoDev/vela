#version 450
// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#extension GL_GOOGLE_include_directive : require

// Solid color (snap preview, backdrop, captures): the color arrives already
// linear and premultiplied.

#include "push.glsl"
#include "shape.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = colorFilter(pc.color) * shapeClip();
}
