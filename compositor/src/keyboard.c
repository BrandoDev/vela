// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboard.h"

#include "switcher.h"
#include "lock.h"
#include "interact.h"
#include "bindings.h"
#include "a11y.h"
#include "config.h"
#include "input.h"
#include "server.h"
#include "shell.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

// I nomi XKB di un layout; vuoti: non scelti.
struct keymap_names {
    char rules[64];
    char model[64];
    char layout[256];
    char variant[256];
    char options[512];
};

static void copy(char *out, size_t size, const char *value)
{
    snprintf(out, size, "%s", value);
}

// Da kxkbrc, la sezione [Layout] (solo se KDE gestisce la tastiera).
static void kde_keymap(struct keymap_names *names)
{
    char path[4096];
    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (config && *config) {
        snprintf(path, sizeof(path), "%s/kxkbrc", config);
    } else if (home) {
        snprintf(path, sizeof(path), "%s/.config/kxkbrc", home);
    } else {
        return;
    }
    FILE *file = fopen(path, "re");
    if (!file) {
        return;
    }
    struct keymap_names found = { 0 };
    bool use = false;
    bool reset_options = false;
    char options[512] = "";
    bool in_layout = false;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[') {
            in_layout = strcmp(line, "[Layout]") == 0;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!in_layout || !eq) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;
        if (strcmp(key, "Use") == 0) {
            use = strcmp(value, "true") == 0;
        } else if (strcmp(key, "LayoutList") == 0) {
            copy(found.layout, sizeof(found.layout), value);
        } else if (strcmp(key, "VariantList") == 0) {
            copy(found.variant, sizeof(found.variant), value);
        } else if (strcmp(key, "Model") == 0) {
            copy(found.model, sizeof(found.model), value);
        } else if (strcmp(key, "ResetOldOptions") == 0) {
            reset_options = strcmp(value, "true") == 0;
        } else if (strcmp(key, "Options") == 0) {
            copy(options, sizeof(options), value);
        }
    }
    fclose(file);
    if (!use) {
        return; // KDE non gestisce la tastiera: decide il sistema
    }
    if (reset_options) {
        copy(found.options, sizeof(found.options), options);
    }
    *names = found;
}

// Da systemd-localed: Option "XkbLayout" "us".
static void localed_keymap(struct keymap_names *names)
{
    FILE *file = fopen("/etc/X11/xorg.conf.d/00-keyboard.conf", "re");
    if (!file) {
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        // La prima parola è "Option", poi due testi tra virgolette.
        char *word = line + strspn(line, " \t");
        if (strncmp(word, "Option", 6) != 0 || !strchr(" \t", word[6]) || word[6] == '\0') {
            continue;
        }
        char *q1 = strchr(word + 6, '"');
        char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        char *q3 = q2 ? strchr(q2 + 1, '"') : NULL;
        char *q4 = q3 ? strchr(q3 + 1, '"') : NULL;
        if (!q4) {
            continue;
        }
        *q2 = '\0';
        *q4 = '\0';
        const char *key = q1 + 1;
        const char *value = q3 + 1;
        if (strcmp(key, "XkbLayout") == 0) {
            copy(names->layout, sizeof(names->layout), value);
        } else if (strcmp(key, "XkbVariant") == 0) {
            copy(names->variant, sizeof(names->variant), value);
        } else if (strcmp(key, "XkbModel") == 0) {
            copy(names->model, sizeof(names->model), value);
        } else if (strcmp(key, "XkbOptions") == 0) {
            copy(names->options, sizeof(names->options), value);
        }
    }
    fclose(file);
}

static void vela_keymap(struct keymap_names *names, const struct vela_config *settings)
{
    copy(names->layout, sizeof(names->layout), vela_config_get(settings, "keyboard-layout", ""));
    copy(names->variant, sizeof(names->variant), vela_config_get(settings, "keyboard-variant", ""));
    copy(names->options, sizeof(names->options), vela_config_get(settings, "keyboard-options", ""));
    // Più layout: Win+Spazio passa al successivo, come su Windows.
    if (strchr(names->layout, ',') && !strstr(names->options, "grp:")) {
        size_t used = strlen(names->options);
        snprintf(names->options + used, sizeof(names->options) - used, "%sgrp:win_space_toggle",
            used ? "," : "");
    }
}

// La variabile d'ambiente, se c'è; altrimenti il nome scelto (NULL se vuoto).
static const char *pick(const char *env, const char *fallback)
{
    const char *value = getenv(env);
    if (value && *value) {
        return value;
    }
    return *fallback ? fallback : NULL;
}

static struct xkb_keymap *system_keymap(struct vela_input *input, struct xkb_context *context,
    const struct vela_config *settings)
{
    struct keymap_names names = { 0 };
    vela_keymap(&names, settings);
    const char *origin = "Vela settings";
    if (!names.layout[0]) {
        memset(&names, 0, sizeof(names));
        kde_keymap(&names);
        origin = "KDE";
    }
    if (!names.layout[0]) {
        memset(&names, 0, sizeof(names));
        localed_keymap(&names);
        origin = "localectl";
    }
    const struct xkb_rule_names rules = {
        .rules = pick("XKB_DEFAULT_RULES", names.rules),
        .model = pick("XKB_DEFAULT_MODEL", names.model),
        .layout = pick("XKB_DEFAULT_LAYOUT", names.layout),
        .variant = pick("XKB_DEFAULT_VARIANT", names.variant),
        .options = pick("XKB_DEFAULT_OPTIONS", names.options),
    };
    // Nel log solo quando cambia (una riga per tastiera sarebbe rumore).
    char *logged = input->keymap_logged;
    char description[1024];
    snprintf(description, sizeof(description), "%s/%s/%s", rules.layout ? rules.layout : "",
        rules.variant ? rules.variant : "", rules.options ? rules.options : "");
    if (strcmp(description, logged) != 0) {
        copy(logged, sizeof(input->keymap_logged), description);
        wlr_log(WLR_INFO, "Keyboard: layout %s, variant %s%s%s (from %s)", rules.layout ? rules.layout : "us",
            rules.variant ? rules.variant : "-", rules.options ? ", options " : "", rules.options ? rules.options : "",
            getenv("XKB_DEFAULT_LAYOUT") ? "XKB_DEFAULT_*" : !names.layout[0] ? "default" : origin);
    }
    return xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

void vela_keyboard_apply_settings(struct vela_keyboard *keyboard, const struct vela_config *settings)
{
    // Una tastiera virtuale (prove automatiche) porta il suo layout.
    if (wlr_input_device_get_virtual_keyboard(&keyboard->wlr->base)) {
        return;
    }
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = system_keymap(keyboard->input, context, settings);
    if (!keymap) {
        wlr_log(WLR_ERROR, "Invalid XKB layout, using the default one");
        const struct xkb_rule_names fallback = { .layout = "us" };
        keymap = xkb_keymap_new_from_names(context, &fallback, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    wlr_keyboard_set_keymap(keyboard->wlr, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    // Ripetizione tasti: predefiniti ritardo 400 ms e 30 caratteri/s (simili
    // a quelli di Windows).
    int delay = atoi(vela_config_get(settings, "keyboard-repeat-delay", "400"));
    int rate = atoi(vela_config_get(settings, "keyboard-repeat-rate", "30"));
    wlr_keyboard_set_repeat_info(keyboard->wlr, vela_clamp(rate, 1, 100), vela_clamp(delay, 100, 2000));
}

// ------------------------------------------------------------------ tasti --

static bool is_super(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R;
}

static bool is_alt(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R || sym == XKB_KEY_Meta_L || sym == XKB_KEY_Meta_R;
}

static void handle_modifiers(struct wl_listener *listener, void *data)
{
    struct vela_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    struct vela_server *server = keyboard->input->server;
    struct vela_a11y *a11y = server->a11y;
    struct wlr_keyboard *wlr = keyboard->wlr;
    // Tasti permanenti: chi manda i modificatori da sé (le tastiere
    // virtuali) non sa di quelli rimasti premuti: si rimettono.
    const struct wlr_keyboard_modifiers *m = &wlr->modifiers;
    uint32_t latched = m->latched | a11y->latched;
    uint32_t locked = m->locked | a11y->locked;
    if (a11y->sticky_keys && !keyboard->restoring && (latched != m->latched || locked != m->locked)) {
        keyboard->restoring = true;
        wlr_keyboard_notify_modifiers(wlr, m->depressed, latched, locked, m->group); // richiama qui
        keyboard->restoring = false;
        return;
    }
    wlr_seat_set_keyboard(server->seat, wlr);
    wlr_seat_keyboard_notify_modifiers(server->seat, &wlr->modifiers);
}

static void handle_key(struct wl_listener *listener, void *data)
{
    struct vela_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct vela_input *input = keyboard->input;
    struct vela_server *server = input->server;
    struct wlr_keyboard_key_event *event = data;
    uint32_t keycode = event->keycode + 8; // libinput -> xkb
    const xkb_keysym_t *syms = NULL;
    int count = xkb_state_key_get_syms(keyboard->wlr->xkb_state, keycode, &syms);
    // I modificatori qui sono quelli PRIMA di questo tasto.
    uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr);
    bool pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;

    // "Sposta"/"Ridimensiona" da tastiera: i tasti sono tutti del compositor.
    if (vela_interact_keyboard(server, syms, count, mods, pressed)) {
        return;
    }

    bool handled = false;
    // Un'app a fuoco che tiene le scorciatoie (macchina virtuale, desktop
    // remoto) riceve anche il tasto Super da solo.
    bool inhibited = vela_input_shortcuts_inhibited(input) || server->locked;
    vela_lock_note_activity(server->lock);
    // Tasti permanenti: un modificatore premuto e lasciato vale per il tasto
    // dopo (i modificatori di questo tasto, `mods`, li comprendono già); al
    // rilascio di quel tasto si lasciano.
    if (vela_a11y_sticky_key(server->a11y, keyboard->wlr, syms, count, pressed)) {
        input->super_tap = false;
    }
    for (int i = 0; i < count; ++i) {
        if (pressed) {
            if (is_super(syms[i]) && !inhibited) {
                input->super_tap = (mods & ~WLR_MODIFIER_LOGO) == 0;
                continue;
            }
            input->super_tap = false;
            handled = handled || vela_bindings_handle(server, mods, syms[i]);
            continue;
        }
        if (is_super(syms[i]) && input->super_tap) {
            input->super_tap = false;
            vela_shell_send(server, "toggle-start");
        }
        // Alt rilasciato durante Alt+Tab: si passa alla finestra scelta.
        if (is_alt(syms[i]) && vela_switcher_active(server)) {
            vela_switcher_finish(server, true);
        }
    }
    if (!handled) {
        wlr_seat_set_keyboard(server->seat, keyboard->wlr);
        wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
    }
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct vela_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    vela_keyboard_destroy(keyboard);
}

struct vela_keyboard *vela_keyboard_create(struct vela_input *input, struct wlr_keyboard *wlr)
{
    struct vela_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->input = input;
    keyboard->wlr = wlr;
    struct vela_config settings;
    vela_config_read(&settings);
    vela_keyboard_apply_settings(keyboard, &settings);
    vela_config_finish(&settings);

    keyboard->modifiers.notify = handle_modifiers;
    wl_signal_add(&wlr->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = handle_key;
    wl_signal_add(&wlr->events.key, &keyboard->key);
    keyboard->destroy.notify = handle_destroy;
    wl_signal_add(&wlr->base.events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(input->server->seat, wlr);
    wl_list_insert(input->keyboards.prev, &keyboard->link);
    return keyboard;
}

void vela_keyboard_destroy(struct vela_keyboard *keyboard)
{
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}
