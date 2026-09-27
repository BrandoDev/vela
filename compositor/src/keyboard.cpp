#include "server.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace vela {

namespace {

// Il layout della tastiera come lo conosce già il sistema. In ordine: le
// variabili XKB_DEFAULT_* (le legge libxkbcommon da sé), le impostazioni di
// KDE (~/.config/kxkbrc, se KDE gestisce la tastiera), quelle di
// systemd-localed (localectl, /etc/X11/xorg.conf.d/00-keyboard.conf).
struct KeymapNames {
    std::string rules, model, layout, variant, options;
};

KeymapNames kdeKeymap()
{
    KeymapNames names;
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string path = config && *config ? std::string(config) + "/kxkbrc"
        : home                                ? std::string(home) + "/.config/kxkbrc"
                                              : std::string();
    std::ifstream file(path);
    std::string line;
    bool inLayout = false;
    std::map<std::string, std::string> values;
    while (std::getline(file, line)) {
        if (!line.empty() && line.front() == '[') {
            inLayout = line == "[Layout]";
            continue;
        }
        const size_t eq = line.find('=');
        if (inLayout && eq != std::string::npos) {
            values[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    if (values["Use"] != "true") {
        return names; // KDE non gestisce la tastiera: decide il sistema
    }
    names.layout = values["LayoutList"];
    names.variant = values["VariantList"];
    names.model = values["Model"];
    if (values["ResetOldOptions"] == "true") {
        names.options = values["Options"];
    }
    return names;
}

KeymapNames localedKeymap()
{
    KeymapNames names;
    std::ifstream file("/etc/X11/xorg.conf.d/00-keyboard.conf");
    std::string line;
    while (std::getline(file, line)) {
        // Option "XkbLayout" "us"
        std::istringstream words(line);
        std::string option, key, value;
        if (!(words >> option) || option != "Option") {
            continue;
        }
        std::getline(words >> std::ws, line);
        const size_t q1 = line.find('"');
        const size_t q2 = line.find('"', q1 + 1);
        const size_t q3 = line.find('"', q2 + 1);
        const size_t q4 = line.find('"', q3 + 1);
        if (q4 == std::string::npos) {
            continue;
        }
        key = line.substr(q1 + 1, q2 - q1 - 1);
        value = line.substr(q3 + 1, q4 - q3 - 1);
        if (key == "XkbLayout") {
            names.layout = value;
        } else if (key == "XkbVariant") {
            names.variant = value;
        } else if (key == "XkbModel") {
            names.model = value;
        } else if (key == "XkbOptions") {
            names.options = value;
        }
    }
    return names;
}

xkb_keymap* systemKeymap(xkb_context* context)
{
    KeymapNames names = kdeKeymap();
    const char* origin = "KDE";
    if (names.layout.empty()) {
        names = localedKeymap();
        origin = "localectl";
    }
    // Le variabili d'ambiente vincono su tutto, campo per campo.
    auto pick = [](const char* env, const std::string& fallback) -> const char* {
        const char* value = std::getenv(env);
        if (value && *value) {
            return value;
        }
        return fallback.empty() ? nullptr : fallback.c_str();
    };
    const xkb_rule_names rules {
        .rules = pick("XKB_DEFAULT_RULES", names.rules),
        .model = pick("XKB_DEFAULT_MODEL", names.model),
        .layout = pick("XKB_DEFAULT_LAYOUT", names.layout),
        .variant = pick("XKB_DEFAULT_VARIANT", names.variant),
        .options = pick("XKB_DEFAULT_OPTIONS", names.options),
    };
    static bool logged = false;
    if (!logged) {
        logged = true;
        wlr_log(WLR_INFO, "Tastiera: layout %s, variante %s%s%s (da %s)", rules.layout ? rules.layout : "us",
            rules.variant ? rules.variant : "-", rules.options ? ", opzioni " : "", rules.options ? rules.options : "",
            std::getenv("XKB_DEFAULT_LAYOUT") ? "XKB_DEFAULT_*" : names.layout.empty() ? "predefinito" : origin);
    }
    return xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

bool isSuper(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R;
}

bool isAlt(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R || sym == XKB_KEY_Meta_L || sym == XKB_KEY_Meta_R;
}

} // namespace

Keyboard::Keyboard(Server& s, wlr_keyboard* keyboard)
    : server(s)
    , wlr(keyboard)
{
    // Una tastiera virtuale (test automatici) porta il suo layout.
    const bool isVirtual = wlr_input_device_get_virtual_keyboard(&keyboard->base) != nullptr;

    // Il layout del sistema (vedi systemKeymap), o XKB_DEFAULT_*.
    if (!isVirtual) {
        xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap* keymap = systemKeymap(context);
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
    // Un'app a fuoco che tiene le scorciatoie (macchina virtuale, desktop
    // remoto) riceve anche il tasto Super da solo.
    const bool inhibited = server.shortcutsInhibited() || server.locked;
    server.noteActivity();
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < count; ++i) {
            if (isSuper(syms[i]) && !inhibited) {
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
            // Alt rilasciato durante Alt+Tab: si passa alla finestra scelta.
            if (isAlt(syms[i]) && server.switcherActive()) {
                server.switcherFinish(true);
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(server.seat, wlr);
        wlr_seat_keyboard_notify_key(server.seat, event->time_msec, event->keycode, event->state);
    }
}

} // namespace vela
