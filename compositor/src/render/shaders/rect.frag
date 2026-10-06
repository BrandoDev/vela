#version 450
// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#extension GL_GOOGLE_include_directive : require

// Tinta unita (anteprima dello snap, sfondo, catture): il colore arriva già
// in spazio lineare e premoltiplicato.

#include "push.glsl"
#include "shape.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = colorFilter(pc.color) * shapeClip();
}
