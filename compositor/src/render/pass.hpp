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
    };
    void addTexture(const TextureDraw& draw);

    // Colore come in wlroots: sRGB, premoltiplicato.
    void addRect(const wlr_box& box, const wlr_render_color& color, const pixman_region32_t* clip, bool blend);

    // Invia il disegno. Il Pass non si può più usare dopo.
    bool submit();

private:
    struct Draw {
        Renderer::PipelineKind kind;
        bool blend;
        bool linear;
        Texture* texture;
        QuadPush push;
        VkRect2D scissor;
    };
    void addDraw(const Draw& draw, const wlr_box& dst, const pixman_region32_t* clip);

    struct Shim {
        wlr_render_pass base;
        Pass* self;
    };
    Shim m_shim {};

    Renderer& m_renderer;
    RenderTarget* m_target;
    wlr_buffer* m_buffer; // bloccato finché il Pass esiste
    std::vector<Draw> m_draws;
    bool m_submitted = false;
};

} // namespace vela::render
