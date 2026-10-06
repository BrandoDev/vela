// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Blocco dello schermo e inattività.
//
// Il blocco è ext-session-lock-v1: un programma (vela-lock) chiede di
// bloccare, e da quel momento il compositor mostra solo le sue superfici,
// una per schermo, su un fondo nero. Tutto il resto è spento: niente
// finestre, niente scorciatoie, niente tastiera alle app. Si sblocca solo
// quando il programma lo dice (password giusta); se va in crash, lo schermo
// resta nero e bloccato, e un nuovo programma di blocco può prenderne il
// posto.
//
// Inattività: dopo alcuni minuti senza input (predefinito 10; 0: mai) lo
// schermo si blocca e, pochi secondi dopo, si spegne. I minuti e il blocco
// stanno in ~/.config/vela/vela.conf, che scrive l'app Impostazioni e che si
// rilegge al comando "reload-config"; VELA_SCREEN_OFF e VELA_LOCK_ON_IDLE,
// se ci sono, hanno la precedenza. Un'app
// può impedirlo mentre mostra qualcosa (un video: idle-inhibit). Le app
// sanno quando l'utente è inattivo con ext-idle-notify (stato "assente").

#include "server.hpp"
#include "settings.hpp"
#include "supervisor.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "scene/surface.hpp"

namespace vela {

namespace {

constexpr int screenOffAfterLockMs = 5000; // bloccato per inattività: poi si spegne

// Una superficie del programma di blocco, grande quanto il suo schermo.
struct LockSurface {
    LockSurface(Server& server, wlr_session_lock_surface_v1* surface)
        : server(server)
        , wlr(surface)
        , tree(std::make_unique<scene::Tree>(server.layers.lock.get()))
        , surfaceNode(std::make_unique<scene::SurfaceNode>(tree.get(), surface->surface))
    {
        wlr->data = this;
        place();
        map.connect(&wlr->surface->events.map, [this](void*) {
            // La tastiera va allo schermo dove sta il mouse (o al primo).
            Output* under = this->server.outputUnderCursor();
            if (!this->server.seat->keyboard_state.focused_surface || (under && under->wlr == wlr->output)) {
                this->server.keyboardEnter(wlr->surface);
            }
            scene::Scene::changed();
        });
        destroy.connect(&wlr->events.destroy, [this](void*) {
            if (this->server.seat->keyboard_state.focused_surface == wlr->surface) {
                wlr_seat_keyboard_clear_focus(this->server.seat);
            }
            delete this;
        });
    }

    void place()
    {
        wlr_box box {};
        wlr_output_layout_get_box(server.outputLayout, wlr->output, &box);
        tree->setPosition(box.x, box.y);
        if (box.width > 0 && box.height > 0) {
            wlr_session_lock_surface_v1_configure(wlr, uint32_t(box.width), uint32_t(box.height));
        }
    }

    Server& server;
    wlr_session_lock_surface_v1* wlr;
    std::unique_ptr<scene::Tree> tree;
    std::unique_ptr<scene::SurfaceNode> surfaceNode;
    Listener map;
    Listener destroy;
};

// Gli strati che il blocco nasconde: tutti tranne il suo.
std::vector<scene::Tree*> unlockedLayers(Server& server)
{
    auto& l = server.layers;
    return { l.background.get(), l.bottom.get(), l.windows.get(), l.windowsOut.get(), l.top.get(), l.fullscreen.get(),
        l.x11Popups.get(), l.overlay.get() };
}

} // namespace

void Server::initLock()
{
    layers.lock->setEnabled(false);
    // Prima il fondo, poi (sopra) le superfici del programma di blocco.
    lockState.backdropTree = std::make_unique<scene::Tree>(layers.lock.get());
    lockState.respawn = wl_event_loop_add_timer(
        loop,
        [](void* data) {
            static_cast<Server*>(data)->lockScreen();
            return 0;
        },
        this);

    lockManager = wlr_session_lock_manager_v1_create(display);
    on(&lockManager->events.new_lock, [this](void* data) {
        auto* lock = static_cast<wlr_session_lock_v1*>(data);
        if (lockState.lock) {
            wlr_log(WLR_INFO, "Blocco: c'è già un programma di blocco, rifiuto il secondo");
            wlr_session_lock_v1_destroy(lock);
            return;
        }
        lockState.lock = lock;
        engageLock();
        // "Bloccato" si dice all'app quando ogni schermo ha mostrato il nero.
        lockState.waitingFrames.clear();
        for (Output* output : outputs) {
            if (output->wlr->enabled) {
                lockState.waitingFrames.push_back(output);
                output->scheduleFrame();
            }
        }
        if (lockState.waitingFrames.empty()) {
            wlr_session_lock_v1_send_locked(lock);
        }
        scene::Scene::changed();

        lockState.newSurface = std::make_unique<Listener>();
        lockState.newSurface->connect(&lock->events.new_surface, [this](void* data) {
            new LockSurface(*this, static_cast<wlr_session_lock_surface_v1*>(data));
        });
        lockState.unlock = std::make_unique<Listener>();
        lockState.unlock->connect(&lock->events.unlock, [this](void*) {
            // Sbloccato davvero: il desktop torna com'era.
            locked = false;
            if (const std::string flag = lockFlagPath(std::getenv("WAYLAND_DISPLAY")); !flag.empty()) {
                unlink(flag.c_str());
            }
            for (scene::Tree* layer : unlockedLayers(*this)) {
                layer->setEnabled(true);
            }
            layers.lock->setEnabled(false);
            lockState.backdrop.clear();
            wlr_seat_keyboard_clear_focus(seat);
            refocus();
            scene::Scene::changed();
            wlr_log(WLR_INFO, "Schermo sbloccato");
        });
        lockState.destroy = std::make_unique<Listener>();
        lockState.destroy->connect(&lock->events.destroy, [this](void*) {
            // Se non ha sbloccato (crash), resta tutto bloccato e nero: un
            // nuovo vela-lock può prendere il suo posto.
            if (locked) {
                wlr_log(WLR_ERROR, "Il programma di blocco è sparito senza sbloccare: resto bloccato");
                // Lo si rilancia tra un secondo (al massimo 5 volte al minuto):
                // senza, resterebbe solo lo schermo nero.
                const int64_t now = int64_t(render::nowNs() / 1'000'000);
                std::erase_if(lockState.respawns, [now](int64_t at) { return now - at > 60'000; });
                if (lockState.respawns.size() < 5) {
                    lockState.respawns.push_back(now);
                    wl_event_source_timer_update(lockState.respawn, 1000);
                }
            }
            lockState.lock = nullptr;
            lockState.waitingFrames.clear();
            lockState.newSurface.reset();
            lockState.unlock.reset();
            lockState.destroy->disconnect(); // siamo noi: lo sostituirà il prossimo blocco
        });
    });

    // --- inattività ---
    idleNotifier = wlr_idle_notifier_v1_create(display);
    idleInhibit = wlr_idle_inhibit_v1_create(display);
    on(&idleInhibit->events.new_inhibitor, [this](void* data) {
        auto* inhibitor = static_cast<wlr_idle_inhibitor_v1*>(data);
        // Alla sua fine l'inattività riparte da zero.
        auto* gone = new Listener;
        gone->connect(&inhibitor->events.destroy, [this, gone](void*) {
            noteActivity();
            delete gone;
        });
        noteActivity();
    });

    // Solo nella sessione vera: annidati o headless non si spegne nulla.
    if (!session) {
        return;
    }
    idle.timer = wl_event_loop_add_timer(
        loop,
        [](void* data) {
            auto* self = static_cast<Server*>(data);
            // Un'app sta mostrando qualcosa (un video): si riprova più tardi.
            bool inhibited = false;
            wlr_idle_inhibitor_v1* inhibitor;
            wl_list_for_each(inhibitor, &self->idleInhibit->inhibitors, link)
            {
                const scene::SurfaceState* state = scene::surfaceState(inhibitor->surface);
                inhibited = inhibited || (inhibitor->surface->mapped && state && state->pacing);
            }
            if (inhibited) {
                wl_event_source_timer_update(self->idle.timer, self->idle.screenOffMs);
                return 0;
            }
            if (self->idle.lockOnIdle && !self->locked && !self->idle.locking) {
                self->idle.locking = true;
                self->lockScreen();
                wl_event_source_timer_update(self->idle.timer, screenOffAfterLockMs);
                return 0;
            }
            self->idle.locking = false;
            self->idle.screensOff = true;
            for (Output* output : self->outputs) {
                output->setPowered(false);
            }
            return 0;
        },
        this);
    loadIdleSettings();
}

void Server::engageLock()
{
    if (locked) {
        return;
    }
    locked = true;
    // Per il supervisore: se il compositor va in crash adesso, il prossimo
    // riparte bloccato (supervisor.cpp).
    if (const std::string flag = lockFlagPath(std::getenv("WAYLAND_DISPLAY")); !flag.empty()) {
        if (const int fd = open(flag.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR); fd >= 0) {
            close(fd);
        }
    }
    for (scene::Tree* layer : unlockedLayers(*this)) {
        layer->setEnabled(false);
    }
    layers.lock->setEnabled(true);
    cursorMode = CursorMode::Passthrough;
    grabbed = nullptr;
    endSnapZone(false);
    if (switcherActive()) {
        switcherFinish(false);
    }
    wlr_seat_keyboard_clear_focus(seat);
    wlr_seat_pointer_clear_focus(seat);
    focusedLayerSurface = nullptr;
    wlr_cursor_set_xcursor(cursor, cursorManager, "default");
    updateLockLayout();
    scene::Scene::changed();
    wlr_log(WLR_INFO, "Schermo bloccato");
}

void Server::loadIdleSettings()
{
    if (!idle.timer) {
        return; // annidati o headless non si spegne nulla
    }
    const auto settings = readSettings();
    auto value = [&](const char* key, const char* env) -> std::string {
        if (const char* v = std::getenv(env); v && *v) {
            return v;
        }
        return setting(settings, key);
    };
    const std::string minutes = value("spegni-schermo", "VELA_SCREEN_OFF");
    idle.screenOffMs = std::max(0, minutes.empty() ? 10 : std::atoi(minutes.c_str())) * 60 * 1000;
    const std::string lockOnIdle = value("blocca", "VELA_LOCK_ON_IDLE");
    idle.lockOnIdle = lockOnIdle != "0" && lockOnIdle != "no";
    wlr_log(WLR_INFO, "Inattività: schermo spento dopo %d minuti%s", idle.screenOffMs / 60000,
        idle.lockOnIdle ? ", con blocco" : "");
    // L'attesa riparte da adesso, con i minuti nuovi.
    idle.locking = false;
    wl_event_source_timer_update(idle.timer, idle.screenOffMs);
}

void Server::lockScreen()
{
    if (locked && lockState.lock) {
        return; // già bloccato, e il programma di blocco c'è
    }
    // VELA_LOCK sceglie un altro programma (es. swaylock). Altrimenti
    // vela-lock accanto al compositor (installato) o nella cartella di build.
    std::string command;
    if (const char* custom = std::getenv("VELA_LOCK"); custom && *custom) {
        command = custom;
    } else {
        char self[PATH_MAX] {};
        if (const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1); n > 0) {
            std::string dir(self, size_t(n));
            dir.resize(dir.rfind('/'));
            for (const std::string& candidate : { dir + "/vela-lock", dir + "/../lock/vela-lock" }) {
                if (access(candidate.c_str(), X_OK) == 0) {
                    command = "'" + candidate + "'";
                    break;
                }
            }
        }
    }
    if (command.empty()) {
        wlr_log(WLR_ERROR, "Blocco: non trovo vela-lock (VELA_LOCK per sceglierne un altro)");
        return;
    }
    wlr_log(WLR_INFO, "Blocco lo schermo: %s", command.c_str());
    spawn(command);
}

void Server::noteActivity()
{
    if (idleNotifier) {
        wlr_idle_notifier_v1_notify_activity(idleNotifier, seat);
    }
    if (!idle.timer) {
        return;
    }
    if (idle.screensOff) {
        idle.screensOff = false;
        for (Output* output : outputs) {
            output->setPowered(true);
        }
    }
    idle.locking = false;
    if (idle.screenOffMs > 0) {
        wl_event_source_timer_update(idle.timer, idle.screenOffMs);
    }
}

void Server::outputRendered(Output* output)
{
    if (!lockState.lock || lockState.waitingFrames.empty()) {
        return;
    }
    std::erase(lockState.waitingFrames, output);
    if (lockState.waitingFrames.empty()) {
        wlr_session_lock_v1_send_locked(lockState.lock);
    }
}

void Server::updateLockLayout()
{
    if (!locked) {
        return;
    }
    // Un fondo nero per schermo, sotto le superfici del programma di blocco:
    // anche prima che disegni, o se non c'è più, non si vede nient'altro.
    lockState.backdrop.clear();
    for (Output* output : outputs) {
        const wlr_box box = output->box();
        if (wlr_box_empty(&box)) {
            continue;
        }
        auto rect = std::make_unique<scene::RectNode>(lockState.backdropTree.get(), box.width, box.height,
            wlr_render_color { 0.0f, 0.0f, 0.0f, 1.0f });
        rect->setPosition(box.x, box.y);
        lockState.backdrop.push_back(std::move(rect));
    }
    if (lockState.lock) {
        wlr_session_lock_surface_v1* surface;
        wl_list_for_each(surface, &lockState.lock->surfaces, link)
        {
            if (auto* ours = static_cast<LockSurface*>(surface->data)) {
                ours->place();
            }
        }
    }
}

} // namespace vela
