// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Le costanti di ogni disegno: vedi QuadPush in render/renderer.hpp.
layout(push_constant) uniform Push {
    vec4 dst; // x, y, larghezza, altezza in pixel della destinazione
    vec2 target; // dimensioni della destinazione
    float alpha;
    uint flags;
    vec2 uvOrigin; // coordinate texture dell'angolo in alto a sinistra
    vec2 uvX; // spostamento lungo il bordo superiore
    vec2 uvY; // spostamento lungo il bordo sinistro
    vec2 pad;
    vec4 color; // rettangoli e ombre: colore lineare premoltiplicato
    // La forma (docs/renderer.md §8): rettangolo arrotondato in pixel della
    // destinazione. Ritaglio (flags bit 1) o, per le ombre, chi la proietta.
    vec4 shapeRect;
    vec4 shape; // x: raggio degli angoli; y: sigma dell'ombra (pixel)
    // Il filtro colore dello schermo (flags bit 2): Luce notturna e filtri
    // colore, una matrice 3x3 in spazio lineare (una riga per vec4).
    vec4 colorMatrix[3];
} pc;

// Il filtro è lineare: vale anche sui colori premoltiplicati, e dare il
// filtro a ogni disegno equivale a darlo all'immagine finita (le fusioni
// sono combinazioni lineari in spazio lineare). Il colore resta dentro
// l'alfa.
vec4 colorFilter(vec4 c)
{
    if ((pc.flags & 4u) == 0u) {
        return c;
    }
    const vec3 f = vec3(dot(pc.colorMatrix[0].xyz, c.rgb), dot(pc.colorMatrix[1].xyz, c.rgb), dot(pc.colorMatrix[2].xyz, c.rgb));
    return vec4(clamp(f, 0.0, c.a), c.a);
}
