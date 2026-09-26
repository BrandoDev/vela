#pragma once

#include "listener.hpp"
#include "motion.hpp"
#include "wlr.hpp"

#include <functional>
#include <list>
#include <string>
#include <vector>

namespace vela {

class Server;

// Chi possiede un albero della scena: serve a capire cosa c'è sotto il
// cursore (una finestra o un pezzo della shell).
enum class SceneKind { Toplevel, Layer };

struct SceneOwner {
    explicit SceneOwner(SceneKind k)
        : kind(k)
    {
    }
    virtual ~SceneOwner() = default;
    SceneKind kind;
};

// ---------------------------------------------------------------- Output --

struct Output {
    Output(Server& server, wlr_output* output);
    ~Output();

    wlr_box box() const; // posizione e dimensioni nel layout globale
    void arrangeLayers(); // posiziona pannelli/sfondi e calcola l'area utile

    Server& server;
    wlr_output* wlr;
    wlr_scene_output* sceneOutput = nullptr;
    wlr_box usable {}; // area libera da pannelli (coordinate globali)

    Listener frame;
    Listener requestState;
    Listener destroy;

private:
    void onFrame();
};

// ----------------------------------------------------------------- Popup --

// Menu contestuali, tendine, tooltip. Si posizionano da soli rispetto al
// genitore; noi ci limitiamo a tenerli dentro lo schermo.
struct Popup {
    using BoxFn = std::function<wlr_box()>;

    Popup(wlr_xdg_popup* popup, wlr_scene_tree* parent, BoxFn constraintBox);

    void unconstrain();

    wlr_xdg_popup* xdg;
    wlr_scene_tree* tree;
    BoxFn constraintBox; // area consentita, relativa alla superficie radice

    Listener commit;
    Listener reposition;
    Listener newPopup;
    Listener destroy;
};

// -------------------------------------------------------------- Toplevel --

// Una finestra applicativa (xdg-shell).
struct Toplevel : SceneOwner {
    Toplevel(Server& server, wlr_xdg_toplevel* toplevel);
    ~Toplevel() override;

    Output* output() const; // lo schermo su cui sta il centro della finestra
    wlr_box frameBox() const; // geometria visibile, coordinate globali

    void setMaximized(bool on);
    void setFullscreen(bool on);
    void applyMaximized();

    // Animazione di apertura. tickOpen restituisce false quando ha finito.
    void startOpenAnimation();
    bool tickOpen(double nowMs);
    void finishOpenAnimation();

    Server& server;
    wlr_xdg_toplevel* xdg;
    wlr_scene_tree* tree;

    bool mapped = false;
    bool maximized = false;
    bool fullscreen = false;
    wlr_box restore {}; // posizione e dimensione prima di massimizzare

    Listener map;
    Listener unmap;
    Listener commit;
    Listener destroy;
    Listener requestMove;
    Listener requestResize;
    Listener requestMaximize;
    Listener requestFullscreen;
    Listener newPopup;

private:
    void onMap();
    void onUnmap();
    void onCommit();
    void keepInPlace();
    void applyOpenFrame(double progress);
    void setOpacity(float opacity);

    bool m_animating = false;
    Tween m_openTween;
    int m_targetX = 0;
    int m_targetY = 0;
};

// ---------------------------------------------------------- LayerSurface --

// Un pezzo della shell (taskbar, menu Start, sfondo, notifiche) che usa il
// protocollo wlr-layer-shell.
struct LayerSurface : SceneOwner {
    static void create(Server& server, wlr_layer_surface_v1* surface);
    ~LayerSurface() override;

    Output* output() const;
    bool wantsKeyboard() const;

    Server& server;
    wlr_layer_surface_v1* wlr;
    wlr_scene_layer_surface_v1* sceneLayer;
    bool mapped = false;

    Listener map;
    Listener unmap;
    Listener commit;
    Listener destroy;
    Listener newPopup;

private:
    LayerSurface(Server& server, wlr_layer_surface_v1* surface);
    void onCommit();
};

// -------------------------------------------------------------- Keyboard --

struct Keyboard {
    Keyboard(Server& server, wlr_keyboard* keyboard);
    ~Keyboard();

    Server& server;
    wlr_keyboard* wlr;

    Listener modifiers;
    Listener key;
    Listener destroy;

private:
    void onKey(wlr_keyboard_key_event* event);
};

// ---------------------------------------------------------------- Server --

enum class CursorMode { Passthrough, Move, Resize };

class Server {
public:
    bool init();
    bool start(const std::string& startupCommand);
    void run();
    void shutdown();

    // Focus e ordine delle finestre
    void focusToplevel(Toplevel* toplevel);
    void focusLayer(LayerSurface* layer);
    void refocus();
    void forget(Toplevel* toplevel);
    void forget(LayerSurface* layer);
    Toplevel* focusedToplevel() const;

    // Schermi
    Output* outputAt(double lx, double ly) const;
    Output* outputUnderCursor() const;
    wlr_scene_tree* layerTree(zwlr_layer_shell_v1_layer layer) const;

    // Interazione col puntatore
    void beginInteractive(Toplevel* toplevel, CursorMode mode, uint32_t edges);

    // Animazioni: chiamate dal frame di ogni schermo
    void addAnimation(Toplevel* toplevel);
    void tickAnimations(const timespec& now);
    void scheduleFrames();

    // Tastiera
    bool handleBinding(uint32_t modifiers, xkb_keysym_t sym);
    void spawn(const std::string& command);
    void sendShellCommand(const std::string& command);

    // --- stato wlroots ---
    wl_display* display = nullptr;
    wl_event_loop* loop = nullptr;
    wlr_backend* backend = nullptr;
    wlr_renderer* renderer = nullptr;
    wlr_allocator* allocator = nullptr;
    wlr_compositor* compositor = nullptr;
    wlr_linux_dmabuf_v1* dmabuf = nullptr;
    wlr_output_layout* outputLayout = nullptr;
    wlr_scene* scene = nullptr;
    wlr_scene_output_layout* sceneLayout = nullptr;
    wlr_xdg_shell* xdgShell = nullptr;
    wlr_layer_shell_v1* layerShell = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* cursorManager = nullptr;
    wlr_seat* seat = nullptr;
    std::string socketName;

    // Strati della scena, dal basso verso l'alto
    struct {
        wlr_scene_tree* background = nullptr;
        wlr_scene_tree* bottom = nullptr;
        wlr_scene_tree* windows = nullptr;
        wlr_scene_tree* top = nullptr;
        wlr_scene_tree* fullscreen = nullptr;
        wlr_scene_tree* overlay = nullptr;
    } layers;

    std::list<Output*> outputs;
    std::list<Toplevel*> toplevels; // ordine MRU: il primo è quello attivo
    std::list<LayerSurface*> layerSurfaces;
    std::list<Keyboard*> keyboards;
    LayerSurface* focusedLayerSurface = nullptr;

    // Trascinamento/ridimensionamento in corso
    CursorMode cursorMode = CursorMode::Passthrough;
    Toplevel* grabbed = nullptr;
    double grabX = 0.0;
    double grabY = 0.0;
    wlr_box grabBox {};
    uint32_t resizeEdges = 0;

    // Super premuto e rilasciato da solo = apri il menu Start
    bool superTap = false;

private:
    Listener& on(wl_signal* signal, Listener::Callback callback);
    void onNewInput(wlr_input_device* device);
    void onCursorMotion(uint32_t timeMsec);
    void onCursorButton(wlr_pointer_button_event* event);
    void keyboardEnter(wlr_surface* surface);

    std::list<Listener> m_listeners; // listener globali, scollegati in shutdown()
    std::vector<Toplevel*> m_animating;
};

} // namespace vela
