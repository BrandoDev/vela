// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Un disegno su un buffer: si raccolgono i quad (texture o tinta unita),
// poi submit() registra un unico command buffer e lo invia alla GPU, con la
// sincronizzazione implicita verso chi usa gli stessi buffer (kernel, app,
// compositor ospite).
//
// Verso wlroots è anche un wlr_render_pass.

#include "render/renderer.hpp"
#include "render/texture.hpp"

#include <vector>

namespace vela::render {

class Pass {
public:
    Pass(Renderer& renderer, RenderTarget* target, wlr_buffer* buffer);
    ~Pass();
    Pass(const Pass&) = delete;
    Pass& operator=(const Pass&) = delete;

    wlr_render_pass* wlr() { return &m_shim.base; }
    static Pass* from(wlr_render_pass* pass);

    int width() const { return int(m_target->width); }
    int height() const { return int(m_target->height); }

    struct TextureDraw {
        Texture* texture;
        wlr_fbox src; // in pixel della texture; vuoto: tutta
        wlr_box dst; // in pixel della destinazione
        wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL; // applicata alla texture
        float alpha = 1.0f;
        bool linear = true; // filtro bilineare; false: nearest (copia 1:1)
        bool blend = true;
        const pixman_region32_t* clip = nullptr; // in pixel della destinazione
        // Sincronizzazione esplicita (linux-drm-syncobj-v1): il punto da
        // aspettare prima di leggere la texture, al posto della fence
        // implicita del dmabuf.
        wlr_drm_syncobj_timeline* waitTimeline = nullptr;
        uint64_t waitPoint = 0;
        // Ritaglio arrotondato (docs/renderer.md §8.1), in pixel della
        // destinazione: raggio 0 o rettangolo vuoto, niente ritaglio.
        wlr_box shapeRect {};
        float shapeRadius = 0.0f;
    };
    void addTexture(const TextureDraw& draw);

    // Colore come in wlroots: sRGB, premoltiplicato.
    void addRect(const wlr_box& box, const wlr_render_color& color, const pixman_region32_t* clip, bool blend,
        const wlr_box& shapeRect = {}, float shapeRadius = 0.0f);

    // Ombra di un rettangolo arrotondato (§8.2) dentro `box`: proiettata da
    // `caster`, non disegnata sotto `window`. Colore sRGB premoltiplicato.
    void addShadow(const wlr_box& box, const wlr_box& caster, const wlr_box& window, float radius, float sigma,
        const wlr_render_color& color, const pixman_region32_t* clip);

    // Sfocatura dal vivo (§8.3) sotto un pannello: legge ciò che è già stato
    // disegnato dietro `region` (pixel della destinazione, clip compreso), lo
    // sfoca (dual Kawase) e lo disegna con la ricetta acrylic. La forma la dà
    // l'alfa di `panel`, la superficie che va sopra (da aggiungere dopo,
    // come al solito). `strength`: l'ampiezza dei passaggi, in pixel.
    // Niente, se la destinazione non si può leggere.
    // `tint`: la tinta acrylic (sRGB premoltiplicato), scura o chiara secondo il tema.
    void addBlur(const TextureDraw& panel, const pixman_region32_t* region, float strength,
        const wlr_render_color& tint);
    // Fin dove legge la sfocatura attorno a una zona, in pixel.
    static int blurReach(float strength);

    // Il filtro colore dello schermo (Luce notturna, filtri colore): una
    // matrice 3x3 per righe, in spazio lineare, applicata a tutto ciò che si
    // disegna. null: nessuno. Solo per i frame degli schermi, non per le
    // catture.
    void setColorFilter(const float* matrix);

    // Da chiamare prima di submit(): misura i tempi della GPU di questo
    // disegno (Renderer::readTiming con timingSlot() e point()).
    void measure();
    // A fine lavoro fa scattare anche questo punto (wlroots, per esempio
    // una cattura con sincronizzazione esplicita).
    void signalOnDone(wlr_drm_syncobj_timeline* timeline, uint64_t point);

    // Invia il disegno. Il Pass non si può più usare dopo.
    bool submit();

    // Dopo submit(): il punto della timeline del renderer, lo slot della
    // misura (-1 se non misurato) e il punto della timeline syncobj del
    // renderer che scatta a fine lavoro (0 se non c'è): il rilascio dei
    // buffer delle app lette da questo disegno.
    uint64_t point() const { return m_point; }
    int timingSlot() const { return m_timingSlot; }
    uint64_t syncPoint() const { return m_syncPoint; }

private:
    struct Draw {
        Renderer::PipelineKind kind;
        bool blend;
        bool linear;
        Texture* texture;
        QuadPush push;
        VkRect2D scissor;
        int blurOp = -1; // prima di disegnarlo, la sfocatura m_blurOps[blurOp]
    };
    // Una sfocatura da calcolare: la zona dello schermo da leggere.
    struct BlurOp {
        wlr_box source;
        float strength;
    };
    void addDraw(const Draw& draw, const wlr_box& dst, const pixman_region32_t* clip);
    bool prepareTexture(const TextureDraw& in, Draw& draw);
    void runBlur(VkCommandBuffer cmd, const BlurOp& op);
    std::vector<BlurOp> m_blurOps;

    struct Shim {
        wlr_render_pass base;
        Pass* self;
    };
    Shim m_shim {};

    Renderer& m_renderer;
    RenderTarget* m_target;
    wlr_buffer* m_buffer; // bloccato finché il Pass esiste
    std::vector<Draw> m_draws;
    struct Wait {
        Texture* texture;
        wlr_drm_syncobj_timeline* timeline; // un riferimento nostro
        uint64_t point;
    };
    std::vector<Wait> m_waits;
    wlr_drm_syncobj_timeline* m_signalTimeline = nullptr;
    uint64_t m_signalPoint = 0;
    int m_timingSlot = -1;
    bool m_filtered = false;
    float m_filter[12] {};
    uint64_t m_point = 0;
    uint64_t m_syncPoint = 0;
    bool m_submitted = false;
};

} // namespace vela::render
