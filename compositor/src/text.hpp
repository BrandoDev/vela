// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Il testo disegnato dal compositor (titoli delle finestre; più avanti
// l'overlay di debug). FreeType per i glifi, HarfBuzz per disporli,
// fontconfig per trovare il font scelto in KDE (docs/renderer.md §9.5).
// Il testo si rasterizza sempre alla dimensione fisica esatta: mai
// ingrandito, nitido a ogni scala.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vela {

class TextRenderer {
public:
    // Il motore condiviso; nullptr se non c'è nessun font utilizzabile.
    static TextRenderer* instance();
    ~TextRenderer();

    // La dimensione del font di KDE, in pixel logici (10 pt = 13,33 px).
    double defaultPixelSize() const { return m_pixelSize; }

    struct Image {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels; // ARGB8888 premoltiplicato, riga dopo riga
    };
    // Una riga di testo in `color` (0xAARRGGBB), alta `pixelSize` pixel
    // fisici, larga al massimo `maxWidth` (altrimenti tagliata con "…").
    Image render(const std::string& utf8, double pixelSize, uint32_t color, int maxWidth);

private:
    TextRenderer() = default;
    bool load();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    double m_pixelSize = 13.333;
};

} // namespace vela
