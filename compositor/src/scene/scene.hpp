#pragma once

// La scena di Vela (docs/renderer.md §5): cosa c'è sullo schermo, dal basso
// verso l'alto. Prende il posto di wlr_scene.
//
// - Le superfici delle app si leggono dal vivo (§5.2): un SurfaceNode indica
//   solo la superficie radice; superficie, sottosuperfici, buffer e stato
//   sincronizzato si leggono da wlroots al momento di disegnare.
// - I nodi nostri (alberi, rettangoli, istantanee) tengono solo ciò che è
//   nostro: posizione, visibilità, opacità.
// - Il danno non si calcola a ogni modifica: a ogni frame lo schermo
//   confronta ciò che disegna con il frame precedente (scene/frame.cpp).
//   Qui ogni modifica si limita a chiedere un nuovo frame.
//
// Coordinate logiche (double): un'unità = un pixel a scala 100% (§3.1).

#include "wlr.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace vela::scene {

class Tree;
class OutputFrame;

class Node {
public:
    enum class Type { Tree, Surface, Rect, Buffer };

    virtual ~Node();
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    Type type() const { return m_type; }
    Tree* parent() const { return m_parent; }

    double x() const { return m_x; }
    double y() const { return m_y; }
    void setPosition(double x, double y);
    // Posizione assoluta (logica).
    void coords(double& lx, double& ly) const;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    // Abilitato lui e tutti gli antenati, e appeso alla radice.
    bool visibleInTree() const;

    // Si moltiplica con quella degli antenati.
    float opacity() const { return m_opacity; }
    void setOpacity(float opacity);

    void raiseToTop();
    void placeAbove(Node* sibling);
    void placeBelow(Node* sibling);
    void reparent(Tree* parent);

    // Chi lo possiede (per sapere cosa c'è sotto il cursore).
    void* data = nullptr;
    // Rettangoli e immagini di solito lasciano passare i clic (anteprime,
    // istantanee); quelli della barra del titolo no.
    bool hittable = false;
    // Si vede ma non prende input, lui e i figli (es. l'icona trascinata,
    // che sta sotto il cursore e non deve coprire dove la si lascia).
    bool ignoresInput = false;

protected:
    Node(Type type, Tree* parent);

private:
    friend class Tree;
    void detach();

    Type m_type;
    Tree* m_parent = nullptr;
    double m_x = 0.0;
    double m_y = 0.0;
    bool m_enabled = true;
    float m_opacity = 1.0f;
};

class Tree : public Node {
public:
    explicit Tree(Tree* parent);
    ~Tree() override; // i figli restano orfani (non si disegnano)

    // Dal basso verso l'alto.
    const std::vector<Node*>& children() const { return m_children; }

private:
    friend class Node;
    std::vector<Node*> m_children;
};

// Una superficie di un'app con le sue sottosuperfici, lette dal vivo.
class SurfaceNode : public Node {
public:
    SurfaceNode(Tree* parent, wlr_surface* surface);
    wlr_surface* surface() const { return m_surface; }

private:
    wlr_surface* m_surface;
};

// Un rettangolo a tinta unita (colore sRGB premoltiplicato, come wlroots).
class RectNode : public Node {
public:
    RectNode(Tree* parent, double width, double height, const wlr_render_color& color);
    void setSize(double width, double height);
    void setColor(const wlr_render_color& color);
    double width() const { return m_width; }
    double height() const { return m_height; }
    const wlr_render_color& color() const { return m_color; }

private:
    double m_width;
    double m_height;
    wlr_render_color m_color;
};

// Un buffer "congelato" (pezzo di un'istantanea): resta valido anche se
// l'app lo cambia o si chiude, perché è bloccato finché il nodo esiste.
class BufferNode : public Node {
public:
    // `buffer` viene bloccato; `texture` deve vivere quanto lui.
    BufferNode(Tree* parent, wlr_buffer* buffer, wlr_texture* texture, const wlr_fbox& src,
        wl_output_transform transform, double width, double height);
    ~BufferNode() override;

    void setSize(double width, double height);
    wlr_texture* texture() const { return m_texture; }
    const wlr_fbox& src() const { return m_src; }
    wl_output_transform transform() const { return m_transform; }
    double width() const { return m_width; }
    double height() const { return m_height; }

private:
    wlr_buffer* m_buffer;
    wlr_texture* m_texture;
    wlr_fbox m_src;
    wl_output_transform m_transform;
    double m_width;
    double m_height;
};

// Ogni superficie di un albero di superfici (radice e sottosuperfici
// mappate), nell'ordine di disegno, con la posizione relativa alla radice.
void forEachSurface(wlr_surface* root, const std::function<void(wlr_surface*, int, int)>& fn);

// Posizione di un popup rispetto all'origine della superficie genitore
// (anche se il genitore è un pannello della shell).
void popupPosition(wlr_xdg_popup* popup, double& x, double& y);

class Scene {
public:
    Scene();
    ~Scene();

    Tree& root() { return *m_root; }

    // Da chiamare a ogni cambiamento: chiede un frame a tutti gli schermi.
    static void changed();

    // Cosa c'è in un punto del layout (solo superfici che accettano input).
    struct Hit {
        wlr_surface* surface = nullptr;
        double sx = 0.0;
        double sy = 0.0;
        void* owner = nullptr; // il `data` più vicino risalendo l'albero
    };
    Hit at(double lx, double ly) const;

    // Le superfici che si impegnano ad arrivare su schermo passano da qui:
    // il danno dei loro commit va agli schermi che le mostrano.
    void watch(wlr_compositor* compositor);

    // Schermi attivi (li registra OutputFrame).
    std::vector<OutputFrame*> frames;
    // Per il feedback dmabuf per superficie (§5.3); può mancare.
    wlr_linux_dmabuf_v1* linuxDmabuf = nullptr;
    // Per i punti di rilascio della sincronizzazione esplicita.
    wl_event_loop* eventLoop = nullptr;
    static Scene* instance() { return s_instance; }

private:
    static Scene* s_instance;
    std::unique_ptr<Tree> m_root;
    wl_listener m_newSurface {};
};

} // namespace vela::scene
