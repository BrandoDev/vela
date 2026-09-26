#pragma once

#include "listener.hpp"
#include "motion.hpp"
#include "render/frame_clock.hpp"
#include "render/output_renderer.hpp"
#include "render/vulkan.hpp"
#include "wlr.hpp"

#include <functional>
#include <list>
#include <memory>
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

// -------------------------------------------------------------- Snapshot --

// L'aspetto di una finestra "congelato": copie dei suoi buffer, che restano
// valide anche se l'app li cambia o si chiude. Le animazioni di chiusura e di
// riduzione a icona muovono e scalano l'istantanea, non la finestra vera
// (che è fatta di superfici annidate e non si può scalare).
class Snapshot {
public:
    // Copia i buffer sotto `source`, anche se è disabilitato (succede alla
    // chiusura e con la finestra ridotta a icona). `frame` è il riquadro della
    // finestra in coordinate globali: le trasformazioni si fanno sul suo centro.
    Snapshot(wlr_scene_tree* parent, wlr_scene_node* source, wlr_box frame);
    ~Snapshot();
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    // Disegna l'istantanea col centro del riquadro in (cx, cy).
    void apply(double cx, double cy, double scale, float opacity);

    wlr_scene_tree* tree() const { return m_tree; }
    const wlr_box& frame() const { return m_frame; }

private:
    struct Piece {
        wlr_scene_buffer* buffer;
        double x, y; // rispetto al centro del riquadro
        int width, height;
    };
    void collect(wlr_scene_node* node, int lx, int ly);

    wlr_scene_tree* m_tree;
    wlr_box m_frame;
    std::vector<Piece> m_pieces;
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
    Listener present;
    Listener destroy;

    // Renderer di Vela (VELA_RENDERER=vela, tappa S0 di docs/renderer.md).
    std::unique_ptr<render::OutputRenderer> renderer;
    render::FrameClock clock;

private:
    void onFrame();
    void onFrameVela();

    // Schermo headless con il renderer di Vela: un vblank virtuale esatto
    // al nanosecondo, per provare qualunque frequenza (il timer del backend
    // headless di wlroots lavora al millisecondo e non ha vblank).
    void startVirtualVblank();
    void onVirtualVblank();
    int m_vblankFd = -1;
    wl_event_source* m_vblankSource = nullptr;
    int64_t m_nextVblankNs = 0;
    bool m_awaitingPresent = false; // un frame è stato consegnato e aspetta il vblank
    uint32_t m_awaitingSeq = 0;
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

// Metà dello schermo a cui è agganciata una finestra (snap).
enum class Snap { None, Left, Right };

// Una finestra applicativa (xdg-shell).
struct Toplevel : SceneOwner {
    Toplevel(Server& server, wlr_xdg_toplevel* toplevel);
    ~Toplevel() override;

    Output* output() const; // lo schermo su cui sta il centro della finestra
    wlr_box frameBox() const; // geometria visibile, coordinate globali
    wlr_box minimizeTarget() const; // dove "va" quando si riduce a icona

    void setMaximized(bool on);
    void setFullscreen(bool on);
    void setMinimized(bool on);
    void setSnap(Snap side, Output* out = nullptr); // out: lo schermo, se non quello attuale
    void applyMaximized();
    void applySnap(Output* out);
    void setActivated(bool on); // per l'app e per la taskbar
    void finishRestore(); // fine dell'animazione di ripristino

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
    bool minimized = false;
    Snap snap = Snap::None;
    wlr_box restore {}; // posizione e dimensione prima di massimizzare o agganciare
    wlr_box taskbarRect {}; // il suo pulsante nella taskbar (globali), se noto

    // Come la taskbar vede e comanda questa finestra (foreign-toplevel).
    // Esiste solo mentre la finestra è mappata.
    wlr_foreign_toplevel_handle_v1* handle = nullptr;
    // La stessa finestra nel protocollo ext-foreign-toplevel-list: ha un
    // identificativo univoco e serve per catturarne l'immagine (Alt+Tab).
    wlr_ext_foreign_toplevel_handle_v1* extHandle = nullptr;

    // Per catturarne l'immagine: una scena tutta sua, con dentro solo
    // un'istantanea della finestra aggiornata a ogni richiesta. La sorgente
    // di wlroots disegna tutto ciò che la scena ha in quella zona: nella
    // scena principale comparirebbero anche le altre finestre.
    wlr_scene* captureScene = nullptr;
    wlr_scene_tree* captureTree = nullptr;
    wlr_ext_image_capture_source_v1* captureSource = nullptr; // vive quanto captureTree
    std::unique_ptr<Snapshot> captureSnapshot;
    wlr_ext_image_capture_source_v1* prepareCapture();

    Listener map;
    Listener unmap;
    Listener commit;
    Listener clientCommit;
    Listener destroy;
    Listener requestMove;
    Listener requestResize;
    Listener requestMaximize;
    Listener requestFullscreen;
    Listener requestMinimize;
    Listener setTitle;
    Listener setAppId;
    Listener setParent;
    Listener newPopup;

    // Richieste che arrivano dalla taskbar attraverso la maniglia.
    struct {
        Listener activate;
        Listener close;
        Listener maximize;
        Listener minimize;
        Listener fullscreen;
        Listener rectangle;
    } handleRequests;

private:
    void createHandle();
    void destroyHandle();
    void updateExtHandle();
    void updateHandleParent();
    void onMap();
    void onUnmap();
    void onCommit();
    void keepInPlace();
    void applyOpenFrame(double progress);
    void setOpacity(float opacity);

    bool m_animating = false;
    bool m_closeAnimated = false; // istantanea di chiusura già scattata
    Tween m_openTween;
    int m_openFrames = 0;
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

// Dove si aggancerà la finestra trascinata, se la rilasci ora.
enum class SnapZone { None, Left, Right, Maximize };

// L'area utile di uno schermo divisa a metà.
wlr_box snapArea(const Output* out, Snap side);

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
    void updateSnapZone(); // durante il trascinamento: anteprima dello snap
    void endSnapZone(bool apply);

    // Animazioni: chiamate dal frame di ogni schermo
    void addAnimation(Toplevel* toplevel);
    void tickAnimations(const timespec& now);
    void scheduleFrames();

    // Animazioni su un'istantanea della finestra
    enum class SnapshotKind { Close, Minimize, Restore };
    bool animateSnapshot(Toplevel* toplevel, SnapshotKind kind); // false se non parte
    void cancelSnapshotAnimations(Toplevel* toplevel);

    // Alt+Tab: il compositor decide l'ordine e la selezione, la shell
    // disegna il pannello con le anteprime.
    void switcherStep(int direction);
    void switcherFinish(bool activate);
    bool switcherActive() const { return m_switcher.active; }

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
    wlr_foreign_toplevel_manager_v1* foreignToplevels = nullptr;

    // Renderer di Vela, se attivo (VELA_RENDERER=vela): device Vulkan e
    // allocatore dei buffer degli schermi.
    std::unique_ptr<render::VulkanDevice> vulkan;
    wlr_allocator* velaAllocator = nullptr;
    wlr_ext_foreign_toplevel_list_v1* extToplevels = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* cursorManager = nullptr;
    wlr_seat* seat = nullptr;
    std::string socketName;
    bool nested = false; // dentro un'altra sessione (finestra Wayland o X11)

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

    // Il comando di avvio (la shell) viene rilanciato se si chiude male.
    void tickSnapshotAnimations(double nowMs);

    void supervise(const std::string& command);
    void onSupervisedExit();
    void stopSupervising();

    std::list<Listener> m_listeners; // listener globali, scollegati in shutdown()
    std::vector<Toplevel*> m_animating;

    struct SnapshotAnimation {
        SnapshotKind kind;
        Toplevel* owner; // la finestra, finché esiste (non per la chiusura)
        std::unique_ptr<Snapshot> snapshot;
        Tween tween;
        double fromX, fromY, toX, toY; // centro
        double fromScale, toScale;
        float fromOpacity, toOpacity;
    };
    std::list<SnapshotAnimation> m_snapshotAnimations;

    // Anteprima dello snap mentre trascini una finestra verso un bordo.
    struct {
        SnapZone zone = SnapZone::None;
        Output* output = nullptr;
        wlr_scene_rect* rect = nullptr;
        wlr_box target {};
        Tween tween;
    } m_snapPreview;
    void tickSnapPreview(double nowMs);

    struct {
        bool active = false;
        std::vector<Toplevel*> windows; // in ordine di uso recente
        size_t selected = 0;
    } m_switcher;
    void sendSwitcher(const char* command);

    struct {
        std::string command;
        pid_t pid = -1;
        int pidfd = -1;
        wl_event_source* source = nullptr;
        timespec startedAt {};
        int quickCrashes = 0; // chiusure anomale di fila poco dopo l'avvio
    } m_supervised;
};

} // namespace vela
