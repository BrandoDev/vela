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
    vec4 color; // rettangoli: colore lineare premoltiplicato
} pc;
