// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decoration.hpp"

#include "icons.hpp"
#include "render/pixelbuffer.hpp"
#include "server.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>

namespace vela {

namespace {

constexpr int glyphSize = 10; // logici, come i simboli di Windows 11
constexpr int titleMargin = 12; // senza icona
constexpr int titleAfterIcon = Decoration::iconX + Decoration::iconSize + 8;

// Colori come Windows 11, in tema scuro e chiaro (la modalità delle app).
struct Palette {
    wlr_render_color activeBackground;
    wlr_render_color inactiveBackground;
    wlr_render_color hover; // premoltiplicato
    uint32_t activeText;
    uint32_t inactiveText;
};
constexpr Palette darkPalette {
    { 0.125f, 0.125f, 0.125f, 1.0f }, // #202020
    { 0.169f, 0.169f, 0.169f, 1.0f }, // #2b2b2b
    { 0.08f, 0.08f, 0.08f, 0.08f }, // bianco all'8%
    0xffffffff,
    0xff9a9a9a,
};
constexpr Palette lightPalette {
    { 0.953f, 0.953f, 0.953f, 1.0f }, // #f3f3f3
    { 0.976f, 0.976f, 0.976f, 1.0f }, // #f9f9f9
    { 0.0f, 0.0f, 0.0f, 0.06f }, // nero al 6%
    0xff1a1a1a,
    0xff8f8f8f,
};
constexpr wlr_render_color closeHoverColor { 0.769f, 0.169f, 0.110f, 1.0f }; // #c42b1c
constexpr uint32_t closeHoverText = 0xffffffff;

const Palette& palette(const Server& server)
{
    return server.lightApps ? lightPalette : darkPalette;
}

struct Segment {
    double x1, y1, x2, y2;
};

// Un simbolo fatto di segmenti nel quadrato unitario, disegnato a `size`
// pixel con un tratto di `stroke` pixel e bordi antialiasati.
std::vector<uint32_t> drawSegments(int size, double stroke, uint32_t color, const std::vector<Segment>& segments)
{
    std::vector<uint32_t> pixels(size_t(size) * size_t(size), 0);
    const double span = size - stroke; // il tratto resta dentro l'immagine
    const double offset = stroke / 2.0;
    const double alpha = ((color >> 24) & 0xff) / 255.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            double distance = 1e9;
            for (const Segment& s : segments) {
                const double ax = offset + s.x1 * span;
                const double ay = offset + s.y1 * span;
                const double bx = offset + s.x2 * span;
                const double by = offset + s.y2 * span;
                const double dx = bx - ax;
                const double dy = by - ay;
                const double length = dx * dx + dy * dy;
                const double t = length > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / length, 0.0, 1.0) : 0.0;
                distance = std::min(distance, std::hypot(px - (ax + t * dx), py - (ay + t * dy)));
            }
            const double coverage = std::clamp(stroke / 2.0 + 0.5 - distance, 0.0, 1.0) * alpha;
            if (coverage > 0.0) {
                auto channel = [&](int shift) {
                    return uint32_t(std::lround(((color >> shift) & 0xff) * coverage)) << shift;
                };
                pixels[size_t(y) * size_t(size) + size_t(x)]
                    = uint32_t(std::lround(coverage * 255.0)) << 24 | channel(16) | channel(8) | channel(0);
            }
        }
    }
    return pixels;
}

std::vector<Segment> glyph(Decoration::Part part, bool maximized)
{
    switch (part) {
    case Decoration::Part::Minimize:
        return { { 0.0, 0.5, 1.0, 0.5 } };
    case Decoration::Part::Maximize:
        if (maximized) {
            // "Ripristina": due quadrati sovrapposti.
            return {
                { 0.0, 0.2, 0.8, 0.2 }, { 0.8, 0.2, 0.8, 1.0 }, { 0.8, 1.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0, 0.2 },
                { 0.2, 0.2, 0.2, 0.0 }, { 0.2, 0.0, 1.0, 0.0 }, { 1.0, 0.0, 1.0, 0.8 }, { 1.0, 0.8, 0.8, 0.8 },
            };
        }
        return { { 0.0, 0.0, 1.0, 0.0 }, { 1.0, 0.0, 1.0, 1.0 }, { 1.0, 1.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0, 0.0 } };
    case Decoration::Part::Close:
        return { { 0.0, 0.0, 1.0, 1.0 }, { 1.0, 0.0, 0.0, 1.0 } };
    default:
        return {};
    }
}

constexpr Decoration::Part buttonParts[3] { Decoration::Part::Minimize, Decoration::Part::Maximize,
    Decoration::Part::Close };

} // namespace

Decoration::Decoration(Toplevel& toplevel)
    : m_toplevel(toplevel)
    , m_tree(std::make_unique<scene::Tree>(toplevel.tree.get()))
    , m_background(std::make_unique<scene::RectNode>(m_tree.get(), 0, height, darkPalette.activeBackground))
{
    // Sopra la superficie dell'app, fuori dalla sua area.
    m_tree->setPosition(0, -height);
    m_background->hittable = true;
    for (auto& rect : m_hoverRects) {
        rect = std::make_unique<scene::RectNode>(m_tree.get(), buttonWidth, height, wlr_render_color {});
        rect->hittable = true;
    }
    update();
}

// I colori della barra con la tinta dello sfondo (Mica, docs/renderer.md
// §9.4): la tinta resa "sicura" per il testo (meno satura, luminosità
// nella fascia scura del tema, o in quella chiara) e mescolata al grigio
// di Windows 11; da inattiva pesa di più. Lo stesso calcolo è in
// shell/src/mica.h, per le app di Vela.
wlr_render_color micaColor(const wlr_render_color& base, const Server& server, float weight)
{
    if (!server.hasWallpaperTint) {
        return base;
    }
    const float* t = server.wallpaperTint;
    const float luma = 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2];
    float safe[3];
    for (int i = 0; i < 3; ++i) {
        const float desaturated = luma + (t[i] - luma) * 0.5f;
        safe[i] = server.lightApps
            ? std::clamp(1.0f - (1.0f - desaturated) * (0.10f / std::max(1.0f - luma, 0.02f)), 0.82f, 1.0f)
            : std::clamp(desaturated * (0.16f / std::max(luma, 0.02f)), 0.0f, 0.32f);
    }
    return { base.r + (safe[0] - base.r) * weight, base.g + (safe[1] - base.g) * weight,
        base.b + (safe[2] - base.b) * weight, 1.0f };
}

Decoration::~Decoration()
{
    clearImage(m_icon);
    clearImage(m_title);
    for (Image& image : m_glyphs) {
        clearImage(image);
    }
}

void Decoration::clearImage(Image& image)
{
    image.node.reset(); // prima il nodo: sblocca il buffer
    if (image.texture) {
        wlr_texture_destroy(image.texture);
        image.texture = nullptr;
    }
}

void Decoration::setImage(Image& image, int width, int height, std::vector<uint32_t> pixels, double x, double y,
    double logicalWidth, double logicalHeight)
{
    clearImage(image);
    wlr_buffer* buffer = render::createPixelBuffer(width, height, std::move(pixels));
    image.texture = wlr_texture_from_buffer(m_toplevel.server.renderer, buffer);
    if (image.texture) {
        image.node = std::make_unique<scene::BufferNode>(m_tree.get(), buffer, image.texture,
            wlr_fbox { 0, 0, double(width), double(height) }, WL_OUTPUT_TRANSFORM_NORMAL, logicalWidth, logicalHeight);
        image.node->setPosition(x, y);
        image.node->hittable = true;
    }
    wlr_buffer_drop(buffer);
}

void Decoration::layoutButtons()
{
    for (int i = 0; i < 3; ++i) {
        const double x = m_width - (3 - i) * buttonWidth;
        m_hoverRects[i]->setPosition(x, 0);
        if (m_glyphs[i].node) {
            m_glyphs[i].node->setPosition(x + (buttonWidth - glyphSize) / 2.0, (height - glyphSize) / 2.0);
        }
    }
}

void Decoration::update()
{
    const Toplevel& t = m_toplevel;
    m_tree->setEnabled(!t.fullscreen);
    // In cima al riquadro della finestra (le app Wayland possono avere la
    // geometria spostata rispetto all'origine della superficie).
    const wlr_box geometry = t.geometry();
    m_tree->setPosition(geometry.x, geometry.y);
    const int width = geometry.width;
    const Output* out = t.output();
    const float scale = out ? out->wlr->scale : 1.0f;
    const std::string title = t.title();
    const bool active = t.activated;
    const bool maximized = t.maximized;

    const std::string appId = t.appId();
    const bool resized = width != m_width;
    const bool restyled = !m_drawn || scale != m_scale || active != m_active
        || m_tintVersion != t.server.wallpaperTintVersion;
    m_width = width;
    if (resized) {
        m_background->setSize(width, height);
    }
    const Palette& colors = palette(t.server);
    const uint32_t textColor = active ? colors.activeText : colors.inactiveText;
    if (restyled) {
        m_background->setColor(active ? micaColor(colors.activeBackground, t.server, 0.30f)
                                      : micaColor(colors.inactiveBackground, t.server, 0.45f));
    }

    // L'icona dell'app, disegnata alla dimensione fisica esatta (§9.6).
    if (restyled || appId != m_appId) {
        const int pixels = std::max(1, int(std::lround(iconSize * scale)));
        const IconLoader::Image& icon = IconLoader::instance().appIcon(appId, pixels);
        m_hasIcon = !icon.pixels.empty();
        if (m_hasIcon) {
            setImage(m_icon, icon.width, icon.height, icon.pixels, iconX, (height - iconSize) / 2.0, iconSize,
                iconSize);
            m_icon.node->setOpacity(active ? 1.0f : 0.6f);
        } else {
            clearImage(m_icon);
        }
    }
    const int titleX = m_hasIcon ? titleAfterIcon : titleMargin;

    // Il titolo, rasterizzato ai pixel fisici dello schermo.
    TextRenderer* text = TextRenderer::instance();
    if (text && (restyled || resized || title != m_titleText || appId != m_appId)) {
        const int maxWidth = int((width - titleX - 3 * buttonWidth - 8) * scale);
        if (maxWidth > 0 && !title.empty()) {
            TextRenderer::Image image
                = text->render(title, text->defaultPixelSize() * scale, textColor, maxWidth);
            const double logicalHeight = image.height / double(scale);
            setImage(m_title, image.width, image.height, std::move(image.pixels), titleX,
                std::round((height - logicalHeight) / 2.0), image.width / double(scale), logicalHeight);
        } else {
            clearImage(m_title);
        }
    }

    // I simboli dei pulsanti.
    if (restyled || maximized != m_maximized) {
        const int size = std::max(1, int(std::lround(glyphSize * scale)));
        const double stroke = std::max(1.0, double(scale));
        for (int i = 0; i < 3; ++i) {
            // Sul rosso di Chiudi il simbolo è sempre bianco.
            const bool closeHovered = buttonParts[i] == Part::Close && m_hover == Part::Close;
            setImage(m_glyphs[i], size, size,
                drawSegments(size, stroke, closeHovered ? closeHoverText : textColor, glyph(buttonParts[i], maximized)),
                0, 0, glyphSize, glyphSize);
        }
    }
    if (resized || restyled || maximized != m_maximized) {
        layoutButtons();
    }

    m_scale = scale;
    m_active = active;
    m_maximized = maximized;
    m_titleText = title;
    m_appId = appId;
    m_tintVersion = t.server.wallpaperTintVersion;
    m_drawn = true;
}

Decoration::Part Decoration::partAt(double lx, double ly) const
{
    double ox = 0.0;
    double oy = 0.0;
    m_tree->coords(ox, oy);
    const double x = lx - ox;
    const double y = ly - oy;
    if (y < 0 || y >= height || x < 0 || x >= m_width) {
        return Part::None;
    }
    if (x >= m_width - buttonWidth) {
        return Part::Close;
    }
    if (x >= m_width - 2 * buttonWidth) {
        return Part::Maximize;
    }
    if (x >= m_width - 3 * buttonWidth) {
        return Part::Minimize;
    }
    if (m_hasIcon && x >= iconX - 6 && x < iconX + iconSize + 6) {
        return Part::Icon;
    }
    return Part::Title;
}

wlr_box Decoration::maximizeBox() const
{
    double ox = 0.0;
    double oy = 0.0;
    m_tree->coords(ox, oy);
    return { int(std::lround(ox)) + m_width - 2 * buttonWidth, int(std::lround(oy)), buttonWidth, height };
}

void Decoration::setHover(Part part)
{
    if (part == m_hover) {
        return;
    }
    const bool closeChanged = (part == Part::Close) != (m_hover == Part::Close);
    m_hover = part;
    const Palette& colors = palette(m_toplevel.server);
    for (int i = 0; i < 3; ++i) {
        const bool hovered = buttonParts[i] == part;
        const bool close = buttonParts[i] == Part::Close;
        m_hoverRects[i]->setColor(hovered ? (close ? closeHoverColor : colors.hover) : wlr_render_color {});
    }
    // Sul rosso il simbolo di Chiudi diventa bianco (in tema scuro lo è già, da attiva).
    if (closeChanged && m_drawn && (m_toplevel.server.lightApps || !m_active)) {
        const float scale = m_scale > 0.0f ? m_scale : 1.0f;
        const int size = std::max(1, int(std::lround(glyphSize * scale)));
        const double stroke = std::max(1.0, double(scale));
        const uint32_t color = part == Part::Close ? closeHoverText
            : m_active                             ? colors.activeText
                                                   : colors.inactiveText;
        setImage(m_glyphs[2], size, size, drawSegments(size, stroke, color, glyph(Part::Close, m_maximized)), 0, 0,
            glyphSize, glyphSize);
        layoutButtons();
    }
}

} // namespace vela
