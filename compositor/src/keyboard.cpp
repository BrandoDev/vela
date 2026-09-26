#include "server.hpp"

namespace vela {

namespace {

bool isSuper(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R;
}

} // namespace

Keyboard::Keyboard(Server& s, wlr_keyboard* keyboard)
    : server(s)
    , wlr(keyboard)
{
    // Una tastiera virtuale (test automatici) porta il suo layout.
    const bool isVirtual = wlr_input_device_get_virtual_keyboard(&keyboard->base) != nullptr;

    // Il layout arriva dalle variabili XKB_DEFAULT_LAYOUT/VARIANT/OPTIONS
    // (es. XKB_DEFAULT_LAYOUT=it). Più avanti lo leggeremo dalle impostazioni.
    if (!isVirtual) {
        xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) {
            wlr_log(WLR_ERROR, "Layout XKB non valido, uso quello di base");
            xkb_rule_names fallback {};
            fallback.layout = "us";
            keymap = xkb_keymap_new_from_names(context, &fallback, XKB_KEYMAP_COMPILE_NO_FLAGS);
        }
        wlr_keyboard_set_keymap(wlr, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);

        // Ripetizione tasti: ritardo 400 ms, 30 caratteri/s (valori simili a
        // quelli predefiniti di Windows).
        wlr_keyboard_set_repeat_info(wlr, 30, 400);
    }

    modifiers.connect(&wlr->events.modifiers, [this](void*) {
        wlr_seat_set_keyboard(server.seat, wlr);
        wlr_seat_keyboard_notify_modifiers(server.seat, &wlr->modifiers);
    });
    key.connect(&wlr->events.key, [this](void* data) {
        onKey(static_cast<wlr_keyboard_key_event*>(data));
    });
    destroy.connect(&wlr->base.events.destroy, [this](void*) { delete this; });

    wlr_seat_set_keyboard(server.seat, wlr);
    server.keyboards.push_back(this);
}

Keyboard::~Keyboard()
{
    server.keyboards.remove(this);
}

void Keyboard::onKey(wlr_keyboard_key_event* event)
{
    const uint32_t keycode = event->keycode + 8; // libinput -> xkb
    const xkb_keysym_t* syms = nullptr;
    const int count = xkb_state_key_get_syms(wlr->xkb_state, keycode, &syms);
    // I modificatori qui sono quelli PRIMA di questo tasto.
    const uint32_t mods = wlr_keyboard_get_modifiers(wlr);

    bool handled = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < count; ++i) {
            if (isSuper(syms[i])) {
                server.superTap = (mods & ~WLR_MODIFIER_LOGO) == 0;
                continue;
            }
            server.superTap = false;
            handled = handled || server.handleBinding(mods, syms[i]);
        }
    } else {
        for (int i = 0; i < count; ++i) {
            if (isSuper(syms[i]) && server.superTap) {
                server.superTap = false;
                server.sendShellCommand("toggle-start");
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(server.seat, wlr);
        wlr_seat_keyboard_notify_key(server.seat, event->time_msec, event->keycode, event->state);
    }
}

} // namespace vela
