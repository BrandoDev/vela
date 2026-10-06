// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Dalla scena ai pixel di uno schermo (docs/renderer.md §6): la scena
// appiattita in una lista di quad, le parti coperte scartate, il danno
// calcolato confrontando con il frame precedente, e solo quello ridisegnato.

#include "render/pass.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"

#include <functional>
#include <unordered_map>
#include <vector>

namespace vela::scene {

// Un quad da disegnare, in pixel della destinazione.
struct Element {
    const void* key; // chi è: la superficie o il nodo nostro
    wlr_surface* surface; // superfici delle app, altrimenti null
    wlr_texture* texture; // null: tinta unita
    wlr_render_color color;
    wlr_fbox src;
    wl_output_transform transform;
    wlr_box box;
    float opacity;
    bool linear; // filtro bilineare; false: copia 1:1
    // Dalla superficie ai pixel: pixel = origin + scale * coordinata logica.
    double originX, originY;
    double scaleX, scaleY;
    // La forma (§8.1): ritaglio arrotondato in pixel; raggio 0, nessuno.
    wlr_box shapeRect;
    float shapeRadius;
    // Ombra (§8.2) se sigma > 0: proiettata da shapeRect, non sotto shadowWindow.
    wlr_box shadowWindow;
    float shadowSigma;
    // Sfocatura dietro la superficie (ext-background-effect, §8.3): dove,
    // in pixel (vuoto: niente).
    wlr_box blurBox;
    bool visible; // non del tutto coperto
    int64_t visibleArea; // pixel non coperti
    size_t order; // posizione nella lista, dal basso

    bool sameLook(const Element& other) const;
};

// Appiattisce `root` (dal basso verso l'alto). Il punto logico (originX,
// originY) finisce nel pixel (0, 0); `bounds`: i pixel della destinazione.
struct BuildParams {
    double originX = 0.0;
    double originY = 0.0;
    double scale = 1.0;
    wlr_box bounds {};
    // Catture: la radice si disegna anche se nascosta (finestra ridotta a
    // icona) e senza la sua opacità (animazioni).
    bool captureRoot = false;
};
void buildElements(Node* root, const BuildParams& params, std::vector<Element>& out);

// Scarta ciò che è coperto da superfici opache (dall'alto verso il basso).
void cullOccluded(std::vector<Element>& elements);

// Le zone sfocate leggono ciò che sta loro attorno: se il danno ne tocca una
// (col raggio della sfocatura) si ridisegna tutta, raggio compreso.
void expandDamageForBlur(const std::vector<Element>& elements, pixman_region32_t* damage);

// Disegna gli elementi visibili dentro `clip` (in pixel del buffer). Gli
// elementi sono nello spazio dello schermo ruotato (width x height); il
// buffer può essere ruotato rispetto a esso (outputTransform).
void drawElements(render::Pass& pass, const std::vector<Element>& elements, const pixman_region32_t* clip,
    wl_output_transform outputTransform, int width, int height);

// Dopo un disegno che ha letto `elements`: le app con sincronizzazione
// esplicita riavranno i loro buffer quando scatta `syncPoint` della timeline
// del renderer (la GPU ha finito).
void addReleasePoints(const std::vector<Element>& elements, render::Renderer& renderer, uint64_t syncPoint);

class OutputFrame {
public:
    OutputFrame(Scene& scene, render::Renderer& renderer, wlr_output* output);
    ~OutputFrame();
    OutputFrame(const OutputFrame&) = delete;
    OutputFrame& operator=(const OutputFrame&) = delete;

    wlr_output* output() const { return m_output; }

    // Chiesto un frame (danno, cursore, cattura): chi lo programma.
    std::function<void()> scheduleFrame;

    // Costruisce il frame per lo schermo che nel layout sta in (lx, ly) e fa
    // il commit se c'è qualcosa da mostrare. false: niente da fare.
    // `pending`: uno stato da applicare nello stesso commit (nuova modalità
    // o scala): il frame si disegna già alla nuova dimensione, senza il
    // buffer nero che wlroots metterebbe altrimenti.
    bool render(double lx, double ly, wlr_output_state* pending = nullptr);

    // Qualcuno (wlroots) ha scritto nei buffer dello schermo: nessuno di
    // loro contiene più ciò che crediamo.
    void resetDamage();

    // Dopo il frame: i frame callback alle superfici visibili scandite da
    // questo schermo.
    void sendFrameDone(const timespec& when);

    void damage(const pixman_region32_t* region);
    void damageWhole();

    // L'ultimo frame consegnato da render(): per misurarne il costo (§4.3)
    // e, col vblank virtuale, sapere quando è pronto.
    struct Delivered {
        uint64_t point = 0; // punto della timeline del renderer; 0: nessun disegno della GPU
        int timingSlot = -1;
        bool scanout = false; // il buffer di un'app direttamente sullo schermo
        bool tearing = false; // e mostrato subito, senza aspettare il vblank
    };
    const Delivered& delivered() const { return m_delivered; }

    // Lente di ingrandimento (Accessibilità): lo schermo mostra la zona del
    // layout che comincia nel punto logico (x, y), ingrandita `zoom` volte.
    // zoom 1: lo schermo com'è.
    void setMagnifier(double zoom, double x, double y);
    double zoom() const { return m_zoom; }

    // Dalla scena.
    void surfaceCommitted(wlr_surface* surface);
    void surfaceDestroyed(wlr_surface* surface);
    bool shows(wlr_surface* surface) const { return m_last.contains(surface); }

private:
    void updateSurfaces(const std::vector<Element>& elements);
    // Scanout diretto (§5.3): una sola superficie opaca copre lo schermo,
    // 1:1, e il suo buffer va sul piano primario senza disegnare nulla.
    const Element* scanoutCandidate(const std::vector<Element>& elements, wl_output_transform transform);
    const char* m_scanoutReason = nullptr; // perché non c'è scanout (VELA_DEBUG_SCANOUT)
    bool tryScanout(const Element& element, wlr_output_state& state);
    // Feedback dmabuf: all'app candidata allo scanout i formati del piano
    // primario, dopo qualche frame di conferma; poi di nuovo quelli normali.
    void updateFeedback(const Element* candidate);
    void sendFeedback(wlr_surface* surface, bool scanout);

    Scene& m_scene;
    render::Renderer& m_renderer;
    wlr_output* m_output;
    wlr_damage_ring m_ring {};
    int m_width = 0;
    int m_height = 0;
    float m_scale = 0.0f;

    // Il frame precedente, per chiave: da qui il danno.
    std::unordered_map<const void*, Element> m_last;
    std::vector<wlr_surface*> m_visibleSurfaces;

    Delivered m_delivered;
    bool m_scanout = false; // l'ultimo frame era uno scanout diretto
    bool m_tearing = false; // l'ultimo scanout era con tearing
    double m_zoom = 1.0;
    double m_zoomX = 0.0;
    double m_zoomY = 0.0;
    // La Luce notturna nella gamma del monitor: la versione applicata
    // (quella della scena), se il monitor la accetta.
    // Prima di ogni frame: se la Luce notturna è cambiata, la prova sulla
    // gamma; se il monitor la accetta va nel prossimo commit.
    void prepareNightLight();
    wlr_color_transform* m_nightTransform = nullptr;
    bool m_nightCommitPending = false;
    uint32_t m_nightVersion = 0;
    bool m_nightInGamma = false; // la gamma la sta mostrando
    bool m_gammaRefused = false; // questo schermo non la accetta: nel disegno
    bool m_cursorLocked = false; // col filtro nel disegno, il cursore lo disegniamo noi
    // Sincronizzazione esplicita dello scanout: il backend fa scattare qui
    // il rilascio del buffer di un'app quando smette di mostrarlo.
    wlr_drm_syncobj_timeline* m_scanoutTimeline = nullptr;
    uint64_t m_scanoutPoint = 0;
    int m_feedbackDebounce = 0;
    wlr_surface* m_feedbackSurface = nullptr; // chi ha il feedback di scanout da noi

    wl_listener m_damage {};
    wl_listener m_needsFrame {};
};

} // namespace vela::scene
