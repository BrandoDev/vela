#pragma once

#include "listener.hpp"
#include "motion.hpp"
#include "nested.hpp"
#include "render/frame_clock.hpp"
#include "render/renderer.hpp"
#include "render/vulkan.hpp"
#include "scene/capture.hpp"
#include "scene/frame.hpp"
#include "scene/scene.hpp"
#include "wlr.hpp"

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace vela {

class Server;

// Chi possiede un albero della scena (scene::Node::data): serve a capire
// cosa c'è sotto il cursore (una finestra o un pezzo della shell).
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
    Snapshot(scene::Tree* parent, scene::Node* source, wlr_box frame);
    ~Snapshot();
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    // Disegna l'istantanea col centro del riquadro in (cx, cy).
    void apply(double cx, double cy, double scale, float opacity);

    scene::Tree* tree() const { return m_tree.get(); }
    const wlr_box& frame() const { return m_frame; }

private:
    struct Piece {
        std::unique_ptr<scene::BufferNode> node;
        double x, y; // rispetto al centro del riquadro
        double width, height;
    };
    void collect(scene::Node* node, double lx, double ly);

    std::unique_ptr<scene::Tree> m_tree;
    wlr_box m_frame;
    std::vector<Piece> m_pieces;
};

// ---------------------------------------------------------------- Output --

// Un rettangolo logico i cui bordi cadono esattamente su pixel fisici di uno
// schermo (docs/renderer.md §3.5): la posizione può essere frazionaria.
struct Area {
    double x, y, width, height;
};

// Dove mettere una finestra perché copra un'area: posizione logica esatta e
// dimensione intera (quella che il client riceve nel configure).
struct Placement {
    double x, y;
    int width, height;
};

struct Output {
    Output(Server& server, wlr_output* output);
    ~Output();

    wlr_box box() const; // posizione e dimensioni nel layout globale
    void arrangeLayers(); // posiziona pannelli/sfondi e calcola l'area utile

    // Dove il compositor sistema le finestre, calcolato dai pixel fisici
    // (§3.5): tutto lo schermo, la parte libera da pannelli, e un'area in
    // pixel dello schermo (relativi al suo angolo) vista in logico.
    Area fullArea() const;
    Area usableArea() const;
    wlr_box physicalUsable() const;
    Area fromPhysical(const wlr_box& physical) const;
    // Posizione e dimensione intera il cui buffer, a questa scala, copre
    // esattamente `area`. Se i pixel esatti non si possono ottenere (a 150%
    // 2560 pixel sarebbero 1706,67 unità), il pixel in più finisce fuori
    // dallo schermo quando l'area ne tocca un bordo; altrimenti si resta
    // un pixel dentro, senza sovrapporsi ad altro.
    Placement place(const Area& area) const;

    Server& server;
    wlr_output* wlr;
    wlr_box usable {}; // area libera da pannelli (coordinate globali)

    // Chiede un frame: al prossimo vblank lo schermo ridisegna ciò che è
    // cambiato (e i frame callback partono).
    void scheduleFrame();
    // Applica un cambio di modalità o di scala, con il primo frame già
    // disegnato alla nuova dimensione.
    bool commitMode(wlr_output_state& state);
    // Spegne e riaccende lo schermo (inattività) restando nel layout: la
    // shell non perde i suoi pannelli.
    void setPowered(bool on);
    bool powered = true;

    // Solo se Vela gira in una finestra dentro un'altra sessione.
    std::unique_ptr<NestedWindow> nested;

    Listener frame;
    Listener requestState;
    Listener present;
    Listener destroy;

    // Dalla scena ai pixel di questo schermo (docs/renderer.md §6).
    std::unique_ptr<scene::OutputFrame> sceneFrame;
    render::FrameClock clock;

private:
    // Il ciclo dei frame (docs/renderer.md §4.3): il backend dice "frame"
    // (al vblank dopo una consegna, o subito se lo schermo era fermo), si
    // pianifica il disegno il più tardi possibile prima del vblank, e al
    // momento giusto si disegna.
    void onFrameEvent();
    void onFrame();
    void armLatch(int64_t when);
    // Il costo dei frame già consegnati, appena la GPU li ha finiti.
    void collectCosts();

    wlr_output_mode* m_modeBeforeOff = nullptr;
    int m_customBeforeOff[3] {}; // larghezza, altezza, mHz (schermi senza modalità)

    bool m_latching = true; // VELA_LATCH=0: si disegna subito, come prima di S3
    bool m_frameRequested = false;
    wl_event_source* m_idleFrame = nullptr; // "frame" subito, se lo schermo era fermo
    int m_latchFd = -1;
    wl_event_source* m_latchSource = nullptr;
    bool m_latchArmed = false;
    render::FrameClock::Plan m_plan {};
    bool m_planned = false;

    struct Delivery {
        uint32_t seq;
        int64_t start; // quando doveva cominciare il disegno
        int64_t wokeAt; // quando è cominciato davvero
        int64_t committedAt;
        uint64_t point; // lavoro della GPU; 0: nessuno (scanout, solo cursore)
        int timingSlot;
    };
    std::vector<Delivery> m_deliveries; // in attesa della misura del costo
    // Di cosa è fatto il costo (VELA_STATS): risveglio in ritardo, CPU fino
    // al commit, attesa prima che la GPU cominci, lavoro della GPU.
    struct Breakdown {
        double sum[4] {};
        double max[4] {};
        int count = 0;
        void add(int i, double ms) { sum[i] += ms; max[i] = std::max(max[i], ms); }
    } m_breakdown;
    // Quando il frame era pronto: commit fatto e GPU finita. false se non
    // si sa ancora.
    bool readyTime(const Delivery& delivery, int64_t& when) const;

    // Schermo headless: un vblank virtuale esatto al nanosecondo, per
    // provare qualunque frequenza (il timer del backend headless di wlroots
    // lavora al millisecondo e non ha vblank). Batte solo quando un frame
    // consegnato aspetta di comparire; un frame compare al primo vblank in
    // cui è pronto (anche la GPU deve aver finito), come su uno schermo vero.
    void startVirtualVblank();
    void armVirtualVblank();
    void onVirtualVblank();
    int m_vblankFd = -1;
    wl_event_source* m_vblankSource = nullptr;
    int64_t m_lastVblankNs = 0;
    bool m_vblankArmed = false;
    bool m_awaitingPresent = false; // un frame è stato consegnato e aspetta il vblank
    Delivery m_awaiting {};
};

// ----------------------------------------------------------------- Popup --

// Menu contestuali, tendine, tooltip. Si posizionano da soli rispetto al
// genitore; noi ci limitiamo a tenerli dentro lo schermo.
struct Popup {
    using BoxFn = std::function<wlr_box()>;

    Popup(wlr_xdg_popup* popup, scene::Tree* parent, BoxFn constraintBox);

    void unconstrain();

    wlr_xdg_popup* xdg;
    std::unique_ptr<scene::Tree> tree; // origine: quella della superficie del popup
    std::unique_ptr<scene::SurfaceNode> surfaceNode;
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
// Una finestra: di un'app Wayland (xdg-shell) o X11 (Xwayland). Tutto ciò
// che Vela fa con le finestre (animazioni, snap, taskbar, Alt+Tab) passa da
// qui; le poche cose che dipendono dal tipo sono nelle operazioni sotto
// "Verso l'app".
class Decoration;

struct Toplevel : SceneOwner {
    Toplevel(Server& server, wlr_xdg_toplevel* toplevel);
    Toplevel(Server& server, wlr_xwayland_surface* surface); // xwayland.cpp
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

    // ------------------------------------------------------ verso l'app --
    wlr_surface* surface() const; // null finché una finestra X11 non è associata
    // La parte visibile, rispetto all'origine della superficie (le app
    // Wayland possono disegnare un margine d'ombra attorno).
    wlr_box geometry() const;
    bool configurable() const; // può già ricevere dimensioni e stati
    const char* title() const;
    const char* appId() const;
    Toplevel* parent() const;
    void configureSize(int width, int height); // 0x0: la sceglie l'app
    void sendMaximized(bool on);
    void sendFullscreen(bool on);
    void sendTiled(uint32_t edges);
    void sendActivated(bool on);
    void sendClose();
    // X11: le app devono sapere dove sta la finestra sullo schermo (per
    // posizionare i propri menu). Si chiama a ogni frame.
    void syncX11Geometry();
    // Altezza della barra del titolo di Vela (logica), 0 se la finestra non
    // ce l'ha (finestre Wayland, X11 con barra propria, schermo intero).
    int titleBarHeight() const;
    // Crea o toglie la barra di Vela secondo ciò che la finestra chiede.
    void updateDecoration();

    Server& server;
    wlr_xdg_toplevel* xdg = nullptr;
    wlr_xwayland_surface* x11 = nullptr;
    bool activated = false; // la finestra attiva (tastiera)
    // Origine dell'albero: quella della superficie (non della geometria,
    // che può avere un margine per l'ombra).
    std::unique_ptr<scene::Tree> tree;
    std::unique_ptr<scene::SurfaceNode> surfaceNode;

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

    // Per catturarne l'immagine (anteprime di Alt+Tab): il nostro renderer
    // disegna solo lei.
    std::unique_ptr<scene::WindowCapture> capture;
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
    // Solo X11.
    Listener associate;
    Listener dissociate;
    Listener requestConfigure;
    Listener requestActivate;
    Listener setDecorations;

    // Richieste che arrivano dalla taskbar attraverso la maniglia.
    struct {
        Listener activate;
        Listener close;
        Listener maximize;
        Listener minimize;
        Listener fullscreen;
        Listener rectangle;
    } handleRequests;

    // Massimizzata, agganciata o a schermo intero: la riallinea al suo schermo.
    void keepInPlace();
    wlr_box restoreBox() const; // dove torna uscendo da massimizzata o schermo intero

private:
    void createHandle();
    void destroyHandle();
    void updateExtHandle();
    void updateHandleParent();
    void onMap();
    void onUnmap();
    void onCommit();
    void applyOpenFrame(double progress);
    void setOpacity(float opacity);

    bool m_animating = false;
    bool m_closeAnimated = false; // istantanea di chiusura già scattata
    Tween m_openTween;
    int m_openFrames = 0;
    double m_targetX = 0.0;
    double m_targetY = 0.0;

    // X11: dimensione chiesta all'app (0: la sua) e ultima geometria
    // comunicata.
    int m_x11Width = 0;
    int m_x11Height = 0;
    wlr_box m_x11Sent {};
    wlr_box m_x11Initial {}; // la geometria chiesta dall'app prima di comparire
    void connectX11Surface(); // xwayland.cpp: quando la superficie arriva

public:
    // La barra del titolo di Vela (decoration.hpp), se la finestra la usa.
    // Dopo `tree`: si distrugge prima di lui.
    std::unique_ptr<Decoration> decoration;
};

// ---------------------------------------------------------- LayerSurface --

// Un pezzo della shell (taskbar, menu Start, sfondo, notifiche) che usa il
// protocollo wlr-layer-shell.
struct LayerSurface : SceneOwner {
    static void create(Server& server, wlr_layer_surface_v1* surface);
    ~LayerSurface() override;

    Output* output() const;
    bool wantsKeyboard() const;

    // Posizione e dimensione secondo ancore e margini; riduce `usable` se
    // la superficie riserva spazio (la taskbar).
    void configure(const wlr_box& full, wlr_box& usable);

    Server& server;
    wlr_layer_surface_v1* wlr;
    std::unique_ptr<scene::Tree> tree;
    std::unique_ptr<scene::SurfaceNode> surfaceNode;
    zwlr_layer_shell_v1_layer layer; // lo strato in cui sta l'albero
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
Area snapArea(const Output* out, Snap side);

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
    // Il riquadro di una finestra (globale) rimesso dentro l'area utile
    // dello schermo: prima la dimensione, poi la posizione.
    static wlr_box fitInto(wlr_box frame, const Output& output);
    Output* outputNamed(const char* name) const;
    Output* outputUnderCursor() const;
    scene::Tree* layerTree(zwlr_layer_shell_v1_layer layer) const;

    // Interazione col puntatore
    void beginInteractive(Toplevel* toplevel, CursorMode mode, uint32_t edges, bool fromModifier = false);
    void updateSnapZone(); // durante il trascinamento: anteprima dello snap
    void endSnapZone(bool apply);

    // Animazioni: chiamate dal frame di ogni schermo, con l'istante in cui
    // quel frame verrà mostrato (docs/renderer.md §4.2).
    void addAnimation(Toplevel* toplevel);
    void tickAnimations(int64_t presentNs);
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
    // Comandi dalla shell (e da chi sta nella sessione) al compositor, su
    // $XDG_RUNTIME_DIR/vela-<WAYLAND_DISPLAY>.sock: "logout", "lock".
    // La sessione dell'utente (systemd, D-Bus): vedi session/vela-session-env.
    void setSessionEnvironment();
    void runSessionHook(const char* action);
    void listenForCommands();
    void stopListening();
    void handleCommand(const std::string& command);

    // --- stato wlroots ---
    wl_display* display = nullptr;
    wl_event_loop* loop = nullptr;
    wlr_backend* backend = nullptr;
    wlr_session* session = nullptr; // solo nella sessione vera (DRM): cambio di TTY
    // Il renderer di Vela (docs/renderer.md): device Vulkan nostro, il
    // renderer (anche wlr_renderer per wlroots) e l'allocatore GBM dei
    // buffer di schermi, cursori e catture.
    std::unique_ptr<render::VulkanDevice> vulkan;
    render::Renderer* velaRenderer = nullptr; // appartiene a wlroots: vedi shutdown()
    wlr_renderer* renderer = nullptr; // lo stesso, visto da wlroots
    wlr_allocator* allocator = nullptr;
    wlr_compositor* compositor = nullptr;
    wlr_linux_dmabuf_v1* dmabuf = nullptr;
    wlr_output_layout* outputLayout = nullptr;
    // wlr-output-management: disposizione, modalità, scala e rotazione
    // degli schermi da programmi esterni (wlr-randr, kanshi, wdisplays).
    wlr_output_manager_v1* outputManager = nullptr;
    void updateOutputConfiguration();
    void applyOutputConfiguration(wlr_output_configuration_v1* config, bool testOnly);
    std::unique_ptr<scene::Scene> sceneGraph;
    wlr_xdg_shell* xdgShell = nullptr;
    wlr_layer_shell_v1* layerShell = nullptr;
    wlr_foreign_toplevel_manager_v1* foreignToplevels = nullptr;
    wlr_ext_foreign_toplevel_list_v1* extToplevels = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* cursorManager = nullptr;
    wlr_seat* seat = nullptr;
    std::string socketName;
    struct CommandSocket {
        int fd = -1;
        wl_event_source* source = nullptr;
        std::string path;
        struct Client {
            int fd;
            wl_event_source* source;
            std::string buffer;
        };
        std::vector<std::unique_ptr<Client>> clients;
    } m_commands;
    bool nested = false; // dentro un'altra sessione (finestra Wayland o X11)

    // Giochi e app che vogliono il mouse tutto per sé (pointer.cpp):
    // movimenti relativi (girare la visuale) e puntatore bloccato o
    // confinato nella finestra.
    wlr_relative_pointer_manager_v1* relativePointers = nullptr;
    wlr_pointer_constraints_v1* pointerConstraints = nullptr;
    wlr_pointer_constraint_v1* activeConstraint = nullptr;
    void initPointerProtocols();
    void updatePointerConstraint(wlr_surface* focused);
    // Movimento relativo del mouse: false se il puntatore è bloccato.
    bool constrainMotion(double& dx, double& dy);

    // Un'app a fuoco può tenere per sé le scorciatoie (macchine virtuali,
    // desktop remoto, giochi).
    wlr_keyboard_shortcuts_inhibit_manager_v1* shortcutsInhibit = nullptr;
    bool shortcutsInhibited() const;

    // Blocco dello schermo e inattività (lock.cpp). Il blocco è quello
    // sicuro di ext-session-lock-v1: finché il programma di blocco non
    // sblocca, si vede solo lui; se va in crash, lo schermo resta nero e
    // bloccato, non si scopre il desktop.
    wlr_session_lock_manager_v1* lockManager = nullptr;
    wlr_idle_notifier_v1* idleNotifier = nullptr;
    wlr_idle_inhibit_manager_v1* idleInhibit = nullptr;
    bool locked = false;
    void initLock();
    void lockScreen(); // Win+L, inattività, prima di sospendere: avvia vela-lock
    void noteActivity(); // l'utente c'è: riaccende gli schermi e azzera l'attesa
    void outputRendered(Output* output); // a ogni frame consegnato
    void updateLockLayout(); // schermi cambiati mentre è bloccato

    // Xwayland (xwayland.cpp): parte al primo client X11.
    wlr_xwayland* xwayland = nullptr;
    Listener xwaylandReady; // staccati prima di distruggere Xwayland
    Listener xwaylandNewSurface;
    void initXwayland();
    void syncX11Windows(); // a ogni frame: le app X11 sanno dove sono

    // Strati della scena, dal basso verso l'alto
    struct {
        std::unique_ptr<scene::Tree> background;
        std::unique_ptr<scene::Tree> bottom;
        std::unique_ptr<scene::Tree> windows;
        std::unique_ptr<scene::Tree> top;
        std::unique_ptr<scene::Tree> fullscreen;
        std::unique_ptr<scene::Tree> x11Popups; // menu e tooltip delle app X11
        std::unique_ptr<scene::Tree> overlay;
        std::unique_ptr<scene::Tree> lock; // schermata di blocco, sopra tutto
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
    bool modifierGrab = false; // trascinamento con Super: il clic non è dell'app
    // La barra del titolo di Vela sotto il mouse, e l'ultimo clic sul
    // titolo (per il doppio clic).
    Toplevel* hoveredDecoration = nullptr;
    struct {
        Toplevel* toplevel = nullptr;
        uint32_t timeMsec = 0;
    } lastTitleClick;
    struct {
        Toplevel* toplevel = nullptr;
        double x = 0.0; // dove è stato premuto il titolo
        double y = 0.0;
    } pendingTitleDrag;
    void onDecorationPress(Toplevel* toplevel, uint32_t timeMsec);

    struct LockState {
        wlr_session_lock_v1* lock = nullptr;
        std::unique_ptr<scene::Tree> backdropTree; // in fondo allo strato del blocco
        std::vector<std::unique_ptr<scene::RectNode>> backdrop; // nero, uno per schermo
        std::vector<Output*> waitingFrames; // prima di dire all'app "bloccato"
        std::unique_ptr<Listener> newSurface;
        std::unique_ptr<Listener> unlock;
        std::unique_ptr<Listener> destroy;
        wl_event_source* respawn = nullptr; // rilancia vela-lock se sparisce
        std::vector<int64_t> respawns; // quando, per non insistere all'infinito
    } lockState;
    struct IdleState {
        wl_event_source* timer = nullptr;
        int screenOffMs = 0; // 0: mai
        bool lockOnIdle = true;
        bool locking = false; // bloccato per inattività, schermi da spegnere tra poco
        bool screensOff = false;
    } idle;

    // La tastiera a questa superficie (anche a un menu X11 che la chiede).
    void keyboardEnter(wlr_surface* surface);

private:
    Listener& on(wl_signal* signal, Listener::Callback callback);
    void onNewInput(wlr_input_device* device);
    void onCursorMotion(uint32_t timeMsec);
    void onCursorButton(wlr_pointer_button_event* event);

    // Il comando di avvio (la shell) viene rilanciato se si chiude male.
    void tickSnapshotAnimations(double nowMs);

    void supervise(const std::string& command);
    void onSupervisedExit();
    void stopSupervising();

    std::list<Listener> m_listeners; // listener globali, scollegati in shutdown()
    std::vector<Toplevel*> m_animating;
    // Il tempo delle animazioni non torna mai indietro, anche se schermi
    // diversi prevedono istanti diversi.
    double m_animationNowMs = 0.0;

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
        std::unique_ptr<scene::RectNode> rect;
        Area target {};
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
