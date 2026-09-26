#version 450
#extension GL_GOOGLE_include_directive : require

// Tinta unita (anteprima dello snap, sfondo, catture): il colore arriva già
// in spazio lineare e premoltiplicato.

#include "push.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = pc.color;
}
