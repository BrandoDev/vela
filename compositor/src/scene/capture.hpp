// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Catturare l'immagine di una finestra (anteprime di Alt+Tab, e in futuro
// la condivisione di una finestra): il nostro renderer la disegna da sola,
// senza ciò che le sta sopra o sotto, anche se è coperta o ridotta a icona.

#include "render/renderer.hpp"
#include "scene/scene.hpp"

#include <functional>

namespace vela::scene {

class WindowCapture {
public:
    // `frameBox`: il riquadro della finestra in coordinate globali (logiche).
    WindowCapture(Node* source, std::function<wlr_box()> frameBox, render::Renderer& renderer,
        wlr_allocator* allocator);
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    // La sorgente, con dimensioni e formati aggiornati alla finestra.
    wlr_ext_image_capture_source_v1* source();

    // Disegna la finestra e la offre a chi aspetta un frame.
    void renderFrame();

private:
    bool updateConstraints();

    struct Shim {
        wlr_ext_image_capture_source_v1 base;
        WindowCapture* self;
    };
    Shim m_shim {};

    Node* m_node;
    std::function<wlr_box()> m_frameBox;
    render::Renderer& m_renderer;
    wlr_allocator* m_allocator;
    wlr_swapchain* m_swapchain = nullptr;

public:
    static WindowCapture* from(wlr_ext_image_capture_source_v1* source)
    {
        return reinterpret_cast<Shim*>(source)->self;
    }
};

} // namespace vela::scene
