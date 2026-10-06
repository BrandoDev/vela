// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cmath>

// Mouse, touchpad e gesti, come Windows 11:
//
// - mouse: velocità, "Migliora precisione puntatore" (accelerazione),
//   pulsante principale, righe per ogni scatto della rotellina;
// - touchpad: acceso o spento (anche solo con un mouse collegato),
//   velocità, tocco per cliccare (due dita: tasto destro), direzione dello
//   scorrimento, niente tocchi accidentali mentre si scrive;
// - gesti a tre e quattro dita: verso l'alto la Visualizzazione attività,
//   verso il basso il desktop, di lato cambia app (Alt+Tab, seguendo le
//   dita) o desktop virtuale. Gli altri gesti (pizzico, scorrimenti non
//   nostri) vanno alle app (pointer-gestures).
//
// Le scelte in vela.conf; VELA_NATURAL_SCROLL=0 vince sulla direzione.

namespace vela {

namespace {

// Di quanto spostarsi (unità del touchpad) perché un gesto valga.
constexpr double swipeThreshold = 90.0;
// "Cambia app": un passo ogni tanto spostamento.
constexpr double appStep = 140.0;

double speedSetting(const Settings& settings, const std::string& key)
{
    // Come il cursore di Windows: 1-20, 10 al centro; libinput va da -1 a 1.
    const int value = std::clamp(std::atoi(setting(settings, key, "10").c_str()), 1, 20);
    return (value - 10) / 10.0;
}

} // namespace

void Server::addPointer(wlr_input_device* device)
{
    auto pointer = std::make_unique<PointerDevice>();
    pointer->device = device;
#if WLR_HAS_LIBINPUT_BACKEND
    if (wlr_input_device_is_libinput(device)) {
        pointer->touchpad = libinput_device_config_tap_get_finger_count(wlr_libinput_get_device_handle(device)) > 0;
    }
#endif
    PointerDevice* raw = pointer.get();
    pointer->destroy.connect(&device->events.destroy, [this, raw](void*) {
        input.pointers.remove_if([raw](const std::unique_ptr<PointerDevice>& p) { return p.get() == raw; });
        loadInputSettings(); // un mouse in meno: il touchpad può riaccendersi
    });
    input.pointers.push_back(std::move(pointer));
    loadInputSettings();
}

bool Server::hasTouchpad() const
{
    return std::any_of(input.pointers.begin(), input.pointers.end(),
        [](const std::unique_ptr<PointerDevice>& p) { return p->touchpad; });
}

void Server::loadInputSettings()
{
    const Settings settings = readSettings();
    input.wheelFactor = std::clamp(std::atoi(setting(settings, "mouse-scroll-lines", "3").c_str()), 1, 20) / 3.0;
    input.threeFingers = setting(settings, "touchpad-three-fingers", "app");
    input.fourFingers = setting(settings, "touchpad-four-fingers", "desktop");
    for (auto& pointer : input.pointers) {
        configurePointer(*pointer);
    }
}

void Server::configurePointer(PointerDevice& pointer)
{
#if WLR_HAS_LIBINPUT_BACKEND
    if (!wlr_input_device_is_libinput(pointer.device)) {
        return; // es. backend annidato: il puntatore è del sistema ospite
    }
    const Settings settings = readSettings();
    libinput_device* handle = wlr_libinput_get_device_handle(pointer.device);

    // Il pulsante principale (anche per il touchpad, come Windows).
    if (libinput_device_config_left_handed_is_available(handle)) {
        libinput_device_config_left_handed_set(handle, setting(settings, "mouse-primary-button") == "right");
    }

    if (!pointer.touchpad) {
        if (libinput_device_config_accel_is_available(handle)) {
            libinput_device_config_accel_set_speed(handle, speedSetting(settings, "mouse-speed"));
            libinput_device_config_accel_set_profile(handle, settingFlag(settings, "mouse-precision", true)
                    ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                    : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
        }
        return;
    }

    // Touchpad: spento, o spento solo se c'è anche un mouse.
    const bool enabled = settingFlag(settings, "touchpad", true);
    const bool withMouse = settingFlag(settings, "touchpad-with-mouse", true);
    const bool mousePresent = std::any_of(input.pointers.begin(), input.pointers.end(),
        [](const std::unique_ptr<PointerDevice>& p) { return !p->touchpad && wlr_input_device_is_libinput(p->device); });
    libinput_device_config_send_events_set_mode(handle,
        !enabled || (!withMouse && mousePresent) ? LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
                                                 : LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
    if (libinput_device_config_accel_is_available(handle)) {
        libinput_device_config_accel_set_speed(handle, speedSetting(settings, "touchpad-speed"));
    }
    const bool tap = settingFlag(settings, "touchpad-tap", true);
    libinput_device_config_tap_set_enabled(handle, tap ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
    libinput_device_config_tap_set_drag_enabled(handle, tap ? LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
    // Due dita: tasto destro, tre: centrale (come Windows).
    libinput_device_config_tap_set_button_map(handle, LIBINPUT_CONFIG_TAP_MAP_LRM);
    if (libinput_device_config_dwt_is_available(handle)) {
        libinput_device_config_dwt_set_enabled(handle, LIBINPUT_CONFIG_DWT_ENABLED);
    }
    if (libinput_device_config_scroll_has_natural_scroll(handle)) {
        // "Movimento verso il basso: scorre verso l'alto", come Windows.
        bool natural = settingFlag(settings, "touchpad-natural-scroll", true);
        if (const char* env = std::getenv("VELA_NATURAL_SCROLL"); env && *env) {
            natural = std::strcmp(env, "0") != 0;
        }
        libinput_device_config_scroll_set_natural_scroll_enabled(handle, natural);
    }
    // Pulsanti del touchpad: con un tocco di due dita il destro (clickfinger),
    // dove il touchpad non ha zone disegnate.
    if (libinput_device_config_click_get_methods(handle) & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER) {
        libinput_device_config_click_set_method(handle, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
    }
    wlr_log(WLR_INFO, "Touchpad configured: %s%s", libinput_device_get_name(handle),
        libinput_device_config_send_events_get_mode(handle) == LIBINPUT_CONFIG_SEND_EVENTS_DISABLED ? " (off)" : "");
#else
    (void)pointer;
#endif
}

// ------------------------------------------------------------------ gesti --

void Server::initGestures()
{
    input.gestures = wlr_pointer_gestures_v1_create(display);

    on(&cursor->events.swipe_begin, [this](void* data) {
        auto* event = static_cast<wlr_pointer_swipe_begin_event*>(data);
        noteActivity();
        auto& swipe = input.swipe;
        swipe = {};
        swipe.active = true;
        swipe.action = event->fingers == 3 ? input.threeFingers : event->fingers == 4 ? input.fourFingers : "no";
        swipe.ours = swipe.action != "no" && !locked && !shortcutsInhibited();
        if (!swipe.ours) {
            wlr_pointer_gestures_v1_send_swipe_begin(input.gestures, seat, event->time_msec, event->fingers);
        }
    });
    on(&cursor->events.swipe_update, [this](void* data) {
        auto* event = static_cast<wlr_pointer_swipe_update_event*>(data);
        auto& swipe = input.swipe;
        if (!swipe.ours) {
            wlr_pointer_gestures_v1_send_swipe_update(input.gestures, seat, event->time_msec, event->dx, event->dy);
            return;
        }
        swipe.dx += event->dx;
        swipe.dy += event->dy;
        // "Cambia app": il pannello di Alt+Tab segue le dita.
        if (swipe.action == "app" && std::abs(swipe.dx) > std::abs(swipe.dy)) {
            const int steps = int(swipe.dx / appStep);
            while (swipe.steps != steps) {
                const int direction = steps > swipe.steps ? 1 : -1;
                switcherStep(direction);
                swipe.steps += direction;
            }
        }
    });
    on(&cursor->events.swipe_end, [this](void* data) {
        auto* event = static_cast<wlr_pointer_swipe_end_event*>(data);
        auto& swipe = input.swipe;
        const bool ours = swipe.ours;
        swipe.active = false;
        if (!ours) {
            wlr_pointer_gestures_v1_send_swipe_end(input.gestures, seat, event->time_msec, event->cancelled);
            return;
        }
        if (switcherActive()) {
            switcherFinish(!event->cancelled);
            return;
        }
        if (event->cancelled) {
            return;
        }
        const bool vertical = std::abs(swipe.dy) > std::abs(swipe.dx);
        if (vertical && swipe.dy < -swipeThreshold) {
            sendShellCommand("task-view"); // verso l'alto
        } else if (vertical && swipe.dy > swipeThreshold) {
            sendShellCommand("show-desktop"); // verso il basso
        } else if (!vertical && swipe.action == "desktop" && std::abs(swipe.dx) > swipeThreshold) {
            // Le dita verso sinistra portano il desktop di destra, come Windows.
            switchWorkspace(workspaces.current + (swipe.dx < 0 ? 1 : -1));
        }
    });
    // Pizzico (zoom nelle app) e tocco prolungato: sempre alle app.
    on(&cursor->events.pinch_begin, [this](void* data) {
        auto* event = static_cast<wlr_pointer_pinch_begin_event*>(data);
        noteActivity();
        wlr_pointer_gestures_v1_send_pinch_begin(input.gestures, seat, event->time_msec, event->fingers);
    });
    on(&cursor->events.pinch_update, [this](void* data) {
        auto* event = static_cast<wlr_pointer_pinch_update_event*>(data);
        wlr_pointer_gestures_v1_send_pinch_update(input.gestures, seat, event->time_msec, event->dx, event->dy,
            event->scale, event->rotation);
    });
    on(&cursor->events.pinch_end, [this](void* data) {
        auto* event = static_cast<wlr_pointer_pinch_end_event*>(data);
        wlr_pointer_gestures_v1_send_pinch_end(input.gestures, seat, event->time_msec, event->cancelled);
    });
    on(&cursor->events.hold_begin, [this](void* data) {
        auto* event = static_cast<wlr_pointer_hold_begin_event*>(data);
        wlr_pointer_gestures_v1_send_hold_begin(input.gestures, seat, event->time_msec, event->fingers);
    });
    on(&cursor->events.hold_end, [this](void* data) {
        auto* event = static_cast<wlr_pointer_hold_end_event*>(data);
        wlr_pointer_gestures_v1_send_hold_end(input.gestures, seat, event->time_msec, event->cancelled);
    });
}

// ------------------------------------------------------------- incollare --

void Server::pasteIntoFocused()
{
    // Un attimo dopo: il pannello della shell si chiude e la tastiera torna
    // all'app, poi Ctrl+V.
    if (!input.pasteTimer) {
        input.pasteTimer = wl_event_loop_add_timer(
            loop,
            [](void* data) {
                auto* self = static_cast<Server*>(data);
                wlr_keyboard* keyboard = wlr_seat_get_keyboard(self->seat);
                if (!keyboard && !self->keyboards.empty()) {
                    keyboard = self->keyboards.front()->wlr;
                    wlr_seat_set_keyboard(self->seat, keyboard);
                }
                Toplevel* target = self->focusedToplevel();
                if (!keyboard || !keyboard->keymap || !target || self->locked) {
                    return 0;
                }
                xkb_keymap* keymap = keyboard->keymap;
                // Il tasto che nel layout di ora scrive "v".
                xkb_keycode_t vKey = 0;
                for (xkb_keycode_t code = xkb_keymap_min_keycode(keymap); code <= xkb_keymap_max_keycode(keymap) && !vKey;
                     ++code) {
                    const xkb_keysym_t* syms = nullptr;
                    const int n = xkb_keymap_key_get_syms_by_level(keymap, code, 0, 0, &syms);
                    for (int i = 0; i < n; ++i) {
                        if (syms[i] == XKB_KEY_v) {
                            vKey = code;
                        }
                    }
                }
                if (!vKey) {
                    return 0;
                }
                // I terminali incollano con Ctrl+Maiusc+V.
                const std::string app = target->appId() ? target->appId() : "";
                const bool terminal = app.find("konsole") != std::string::npos || app.find("terminal") != std::string::npos
                    || app.find("kitty") != std::string::npos || app.find("alacritty") != std::string::npos
                    || app.find("foot") != std::string::npos || app.find("wezterm") != std::string::npos
                    || app.find("ghostty") != std::string::npos;
                uint32_t mods = 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
                if (terminal) {
                    mods |= 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
                }
                timespec now {};
                clock_gettime(CLOCK_MONOTONIC, &now);
                const uint32_t ms = uint32_t(now.tv_sec * 1000 + now.tv_nsec / 1000000);
                wlr_keyboard_modifiers pressed = keyboard->modifiers;
                pressed.depressed |= mods;
                wlr_seat_keyboard_notify_modifiers(self->seat, &pressed);
                wlr_seat_keyboard_notify_key(self->seat, ms, vKey - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
                wlr_seat_keyboard_notify_key(self->seat, ms, vKey - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
                wlr_seat_keyboard_notify_modifiers(self->seat, &keyboard->modifiers);
                return 0;
            },
            this);
    }
    wl_event_source_timer_update(input.pasteTimer, 120);
}

// ------------------------------------------------- finestre per la cattura --

std::string Server::windowRectsJson() const
{
    // [{"id":...,"title":...,"x":..,"y":..,"w":..,"h":..}], la più in alto per prima.
    auto quote = [](const char* text) {
        std::string out = "\"";
        for (const char* c = text ? text : ""; *c; ++c) {
            if (*c == '"' || *c == '\\') {
                out += '\\';
                out += *c;
            } else if (static_cast<unsigned char>(*c) < 0x20) {
                out += ' ';
            } else {
                out += *c;
            }
        }
        return out + "\"";
    };
    std::string json = "[";
    bool first = true;
    for (Toplevel* t : toplevels) { // ordine MRU: la prima è quella sopra
        if (!t->mapped || t->minimized || !t->onCurrentWorkspace() || !t->extHandle) {
            continue;
        }
        const wlr_box frame = t->frameBox();
        const int bar = t->titleBarHeight();
        json += std::string(first ? "" : ",") + "{\"id\":" + quote(t->extHandle->identifier) + ",\"title\":"
            + quote(t->title()) + ",\"x\":" + std::to_string(frame.x) + ",\"y\":" + std::to_string(frame.y - bar)
            + ",\"w\":" + std::to_string(frame.width) + ",\"h\":" + std::to_string(frame.height + bar) + "}";
        first = false;
    }
    return json + "]";
}

} // namespace vela
