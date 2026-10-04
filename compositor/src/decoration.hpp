#pragma once

// La barra del titolo che Vela disegna per le finestre che non ne hanno una
// propria (per ora le app X11 che la chiedono al gestore di finestre).
// Prima versione della barra di docs/renderer.md §9: misure di Windows 11
// (alta 32, pulsanti 46×32), titolo nel font di KDE, simboli disegnati alla
// dimensione fisica esatta. Sfocatura e tinta dallo sfondo arriveranno con
// la tappa S6.

#include "scene/scene.hpp"

#include <array>
#include <memory>
#include <string>

namespace vela {

struct Toplevel;

class Decoration {
public:
    static constexpr int height = 32; // logici
    static constexpr int buttonWidth = 46;

    enum class Part { None, Icon, Title, Minimize, Maximize, Close };
    static constexpr int iconSize = 16; // logici
    static constexpr int iconX = 12;

    explicit Decoration(Toplevel& toplevel);
    ~Decoration();
    Decoration(const Decoration&) = delete;
    Decoration& operator=(const Decoration&) = delete;

    // Riallinea la barra alla finestra: larghezza, titolo, attiva o no,
    // massimizzata, scala dello schermo. Ridisegna solo ciò che è cambiato.
    void update();

    // Che cosa c'è in un punto globale della barra.
    Part partAt(double lx, double ly) const;
    void setHover(Part part);
    // Il pulsante Ingrandisci in coordinate globali (lì sotto si aprono i layout di snap).
    wlr_box maximizeBox() const;

private:
    // Un'immagine della CPU (testo, simbolo) come nodo della scena.
    struct Image {
        std::unique_ptr<scene::BufferNode> node;
        wlr_texture* texture = nullptr;
    };
    void setImage(Image& image, int width, int height, std::vector<uint32_t> pixels, double x, double y,
        double logicalWidth, double logicalHeight);
    void clearImage(Image& image);
    void layoutButtons();

    Toplevel& m_toplevel;
    std::unique_ptr<scene::Tree> m_tree;
    std::unique_ptr<scene::RectNode> m_background;
    std::array<std::unique_ptr<scene::RectNode>, 3> m_hoverRects; // riduci, massimizza, chiudi
    std::array<Image, 3> m_glyphs;
    Image m_title;
    Image m_icon;

    // Ciò che è disegnato adesso.
    int m_width = -1;
    float m_scale = 0.0f;
    std::string m_titleText;
    std::string m_appId;
    bool m_hasIcon = false;
    uint32_t m_tintVersion = 0; // la tinta dello sfondo con cui è disegnata
    bool m_active = false;
    bool m_maximized = false;
    bool m_drawn = false;
    Part m_hover = Part::None;
};

} // namespace vela
