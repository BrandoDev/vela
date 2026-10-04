#version 450
#extension GL_GOOGLE_include_directive : require

// Sfocatura dal vivo (§8.3): lo sfondo sfocato sotto un pannello, con la
// ricetta acrylic di Windows 11: un po' più di saturazione, una tinta e un
// velo di rumore (niente bande nei gradienti). La forma la dà il pannello
// stesso: dove la sua superficie è trasparente (fuori dagli angoli
// arrotondati) non si sfoca. Poi sopra si disegna il pannello.
//
// binding 0: la superficie del pannello (uv); binding 1: lo sfondo sfocato,
// letto nel punto dello schermo: (pixel - pad) * shape.zw.
// color: la tinta (lineare, premoltiplicata: alfa = quanta).

#include "push.glsl"
#include "shape.glsl"

layout(set = 0, binding = 0) uniform sampler2D mask;
layout(set = 0, binding = 1) uniform sampler2D blurred;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float noise(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec3 c = texture(blurred, (gl_FragCoord.xy - pc.pad) * pc.shape.zw).rgb;
    const float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, 1.25); // saturazione
    c = c * (1.0 - pc.color.a) + pc.color.rgb; // tinta
    c += (noise(gl_FragCoord.xy) - 0.5) * (2.0 / 255.0);
    c = max(c, 0.0);
    const float coverage = smoothstep(0.0, 0.5, texture(mask, uv).a) * shapeClip() * pc.alpha;
    outColor = vec4(c, 1.0) * coverage;
}
