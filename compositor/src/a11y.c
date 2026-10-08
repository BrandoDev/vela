// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "a11y.h"

#include "color.h"
#include "config.h"
#include "input.h"
#include "output.h"
#include "scene/frame.h"
#include "server.h"
#include "shell.h"
#include "sun.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "scene/scene.h"

#define STICKY_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)

static const char *yes_no(bool on)
{
    return on ? "yes" : "no";
}

// ------------------------------------------------------------------ sole --

// Le coordinate del fuso orario del sistema, da zone1970.tab (o zone.tab):
// "+4154+01229" per Europe/Rome. Bastano per le ore del sole.
static bool timezone_coordinates(double *latitude, double *longitude)
{
    char zone[256];
    const char *tz = getenv("TZ");
    if (tz && *tz) {
        snprintf(zone, sizeof(zone), "%s", tz[0] == ':' ? tz + 1 : tz);
    } else {
        char target[512];
        ssize_t n = readlink("/etc/localtime", target, sizeof(target) - 1);
        if (n <= 0) {
            return false;
        }
        target[n] = '\0';
        const char *at = strstr(target, "zoneinfo/");
        if (!at) {
            return false;
        }
        snprintf(zone, sizeof(zone), "%s", at + 9);
    }
    const char *tables[] = { "/usr/share/zoneinfo/zone1970.tab", "/usr/share/zoneinfo/zone.tab" };
    for (size_t t = 0; t < sizeof(tables) / sizeof(tables[0]); ++t) {
        FILE *in = fopen(tables[t], "re");
        if (!in) {
            continue;
        }
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char codes[128], coordinates[64], name[256];
            if (line[0] == '#' || sscanf(line, "%127s %63s %255s", codes, coordinates, name) != 3
                || strcmp(name, zone) != 0) {
                continue;
            }
            fclose(in);
            return vela_sun_parse_coordinates(coordinates, latitude, longitude);
        }
        fclose(in);
    }
    return false;
}

// ---------------------------------------------------------------- colore --

// I filtri e la luce notturna alla scena: dove passano dal disegno, tutto
// va ridisegnato (dove passano dalla gamma del monitor basta il commit,
// che il frame fa comunque).
static void apply_color(struct vela_a11y *a11y)
{
    struct vela_scene *scene = a11y->server->scene;
    float m[9];
    memcpy(m, vela_identity, sizeof(m));
    if (a11y->color_filter) {
        vela_filter_matrix(a11y->color_filter_kind, m);
    }
    float gains[3] = { 1.0f, 1.0f, 1.0f };
    bool night = a11y->night_level > 0.0;
    if (night) {
        vela_night_gains(6500.0 + (vela_night_kelvin(a11y->night_strength) - 6500.0) * a11y->night_level, gains);
    }
    bool filter_changed = a11y->color_filter != scene->color_filtered
        || (a11y->color_filter && memcmp(m, scene->color_filter, sizeof(m)) != 0);
    bool night_changed = night != scene->night_active || memcmp(gains, scene->night_gains, sizeof(gains)) != 0;
    scene->color_filtered = a11y->color_filter;
    memcpy(scene->color_filter, m, sizeof(m));
    scene->night_active = night;
    memcpy(scene->night_gains, gains, sizeof(gains));
    if (night_changed) {
        ++scene->night_version;
    }
    if (filter_changed || night_changed) {
        struct vela_output *output;
        wl_list_for_each (output, &a11y->server->outputs, link) {
            vela_output_frame_damage_whole(output->frame);
        }
    }
}

static void check_night_schedule(struct vela_a11y *a11y)
{
    bool sunset = strcmp(a11y->schedule, "sunset") == 0;
    if (!sunset && strcmp(a11y->schedule, "hours") != 0) {
        a11y->scheduled = -1;
        return;
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    int minutes = local.tm_hour * 60 + local.tm_min;
    int from = a11y->night_from;
    int to = a11y->night_to;
    double latitude = 0.0;
    double longitude = 0.0;
    if (sunset && timezone_coordinates(&latitude, &longitude)) {
        int set = vela_sun_time(false, &local, latitude, longitude);
        int rise = vela_sun_time(true, &local, latitude, longitude);
        if (set >= 0 && rise >= 0) {
            from = set;
            to = rise;
        }
    }
    int wanted = vela_in_range(minutes, from, to) ? 1 : 0;
    if (wanted == a11y->scheduled) {
        return; // nessun passaggio: resta la scelta fatta a mano
    }
    a11y->scheduled = wanted;
    if ((bool)wanted != a11y->night_light) {
        wlr_log(WLR_INFO, "Night light: %s by the schedule (%02d:%02d-%02d:%02d)", wanted ? "on" : "off", from / 60,
            from % 60, to / 60, to % 60);
        vela_a11y_set_night_light(a11y, wanted, true);
    }
}

// Un controllo al minuto basta (e dopo una sospensione si rimette subito
// in pari).
static int handle_schedule_timer(void *data)
{
    struct vela_a11y *a11y = data;
    check_night_schedule(a11y);
    wl_event_source_timer_update(a11y->timer, 60000);
    return 0;
}

// ------------------------------------------------------------- creazione --

struct vela_a11y *vela_a11y_create(struct vela_server *server)
{
    struct vela_a11y *a11y = calloc(1, sizeof(*a11y));
    a11y->server = server;
    a11y->night_strength = 48;
    snprintf(a11y->schedule, sizeof(a11y->schedule), "no");
    a11y->night_from = 21 * 60;
    a11y->night_to = 7 * 60;
    a11y->scheduled = -1;
    a11y->level_tween = (struct vela_tween) { -1.0, 1.0, NULL };
    snprintf(a11y->color_filter_kind, sizeof(a11y->color_filter_kind), "grayscale");
    a11y->zoom_step = 100;
    a11y->zoom_target = 1.0;
    a11y->zoom = 1.0;
    a11y->zoom_from = 1.0;
    a11y->zoom_tween = (struct vela_tween) { -1.0, 1.0, NULL };
    a11y->timer = wl_event_loop_add_timer(server->loop, handle_schedule_timer, a11y);
    wl_event_source_timer_update(a11y->timer, 60000);
    return a11y;
}

void vela_a11y_destroy(struct vela_a11y *a11y)
{
    if (!a11y) {
        return;
    }
    wl_event_source_remove(a11y->timer);
    free(a11y);
}

void vela_a11y_load(struct vela_a11y *a11y)
{
    struct vela_config settings;
    vela_config_read(&settings);
    a11y->night_strength = vela_clamp(atoi(vela_config_get(&settings, "night-light-strength", "48")), 0, 100);
    snprintf(a11y->schedule, sizeof(a11y->schedule), "%s", vela_config_get(&settings, "night-light-schedule", "no"));
    a11y->night_from = vela_parse_clock(vela_config_get(&settings, "night-light-from", ""), 21 * 60);
    a11y->night_to = vela_parse_clock(vela_config_get(&settings, "night-light-to", ""), 7 * 60);
    snprintf(a11y->color_filter_kind, sizeof(a11y->color_filter_kind), "%s",
        vela_config_get(&settings, "color-filter", "grayscale"));
    a11y->color_filter_shortcut = vela_config_flag(&settings, "color-filters-shortcut", false);
    a11y->zoom_step = vela_clamp(atoi(vela_config_get(&settings, "magnifier-step", "100")), 25, 400);
    bool night = vela_config_flag(&settings, "night-light", false);
    bool filter = vela_config_flag(&settings, "color-filters", false);
    bool sticky = vela_config_flag(&settings, "sticky-keys", false);
    vela_config_finish(&settings);

    // Una pianificazione nuova (o cambiata) decide subito; altrimenti vale
    // ciò che c'è nel file (anche se a mano l'utente ha scelto diversamente
    // dalla pianificazione, fino al suo prossimo passaggio).
    char key[64];
    snprintf(key, sizeof(key), "%s %d %d", a11y->schedule, a11y->night_from, a11y->night_to);
    if (strcmp(key, a11y->schedule_key) != 0) {
        a11y->scheduled = -1;
    }
    snprintf(a11y->schedule_key, sizeof(a11y->schedule_key), "%s", key);
    a11y->night_light = night;
    a11y->color_filter = filter;
    a11y->sticky_keys = sticky;
    if (!sticky) {
        a11y->latched = 0;
        a11y->locked = 0;
    }
    check_night_schedule(a11y);
    // All'avvio niente passaggio graduale: com'era.
    if (a11y->night_level == 0.0 && a11y->night_light && !a11y->level_animating
        && a11y->server->animation_now_ms == 0.0) {
        a11y->night_level = 1.0;
    }
    if (a11y->night_light != (a11y->night_level > 0.5) && !a11y->level_animating) {
        a11y->level_from = a11y->night_level;
        vela_tween_start(&a11y->level_tween, 1000.0, &vela_decelerate);
        a11y->level_animating = true;
        vela_server_schedule_frames(a11y->server);
    }
    apply_color(a11y);
    vela_a11y_announce(a11y);
}

// --------------------------------------------------------------- scelte --

void vela_a11y_set_night_light(struct vela_a11y *a11y, bool on, bool save)
{
    if (save) {
        vela_config_write("night-light", yes_no(on));
    }
    if (on == a11y->night_light && !a11y->level_animating && a11y->night_level == (on ? 1.0 : 0.0)) {
        return;
    }
    a11y->night_light = on;
    // Come Windows: si scalda (o si raffredda) in un secondo.
    a11y->level_from = a11y->night_level;
    vela_tween_start(&a11y->level_tween, 1000.0, &vela_decelerate);
    a11y->level_animating = true;
    vela_server_schedule_frames(a11y->server);
    vela_a11y_announce(a11y);
}

void vela_a11y_set_color_filter(struct vela_a11y *a11y, bool on, bool save)
{
    if (save) {
        vela_config_write("color-filters", yes_no(on));
    }
    if (on == a11y->color_filter) {
        return;
    }
    a11y->color_filter = on;
    apply_color(a11y);
    vela_a11y_announce(a11y);
}

void vela_a11y_set_sticky_keys(struct vela_a11y *a11y, bool on, bool save)
{
    if (save) {
        vela_config_write("sticky-keys", yes_no(on));
    }
    if (on == a11y->sticky_keys) {
        return;
    }
    a11y->sticky_keys = on;
    if (!on && (a11y->latched || a11y->locked)) {
        a11y->latched = 0;
        a11y->locked = 0;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(a11y->server->seat);
        if (keyboard) {
            wlr_keyboard_notify_modifiers(keyboard, keyboard->modifiers.depressed, 0,
                keyboard->modifiers.locked & ~STICKY_MASK, keyboard->modifiers.group);
        }
    }
    a11y->candidate = 0;
    vela_a11y_announce(a11y);
}

// ----------------------------------------------------------------- lente --

void vela_a11y_set_magnifier(struct vela_a11y *a11y, bool on)
{
    if (on == a11y->magnifier) {
        return;
    }
    a11y->magnifier = on;
    // Come Windows: si parte dal doppio.
    a11y->zoom_target = on ? 1.0 + a11y->zoom_step / 100.0 : 1.0;
    a11y->zoom_from = a11y->zoom;
    vela_tween_start(&a11y->zoom_tween, VELA_MAGNIFIER_MS, &vela_decelerate);
    a11y->zoom_animating = true;
    struct wlr_cursor *cursor = a11y->server->cursor;
    if (on && !a11y->zoom_output) {
        a11y->zoom_output = vela_output_at(a11y->server, cursor->x, cursor->y);
        a11y->view_x = a11y->zoom_output ? vela_output_box(a11y->zoom_output).x : 0.0;
        a11y->view_y = a11y->zoom_output ? vela_output_box(a11y->zoom_output).y : 0.0;
    }
    wlr_log(WLR_INFO, "Magnifier %s", on ? "open" : "closed");
    vela_server_schedule_frames(a11y->server);
    vela_a11y_announce(a11y);
}

void vela_a11y_zoom(struct vela_a11y *a11y, int direction)
{
    if (!a11y->magnifier) {
        if (direction > 0) {
            vela_a11y_set_magnifier(a11y, true);
        }
        return;
    }
    double step = a11y->zoom_step / 100.0;
    double target = vela_clampd(a11y->zoom_target + direction * step, 1.0, 16.0);
    if (target == a11y->zoom_target) {
        return;
    }
    a11y->zoom_target = target;
    a11y->zoom_from = a11y->zoom;
    vela_tween_start(&a11y->zoom_tween, VELA_MAGNIFIER_MS, &vela_decelerate);
    a11y->zoom_animating = true;
    vela_server_schedule_frames(a11y->server);
}

// I cursori disegnati su uno schermo, in coordinate del suo buffer logico.
static void move_output_cursors(struct vela_output *output, double x, double y)
{
    struct wlr_output_cursor *c;
    wl_list_for_each (c, &output->wlr->cursors, link) {
        wlr_output_cursor_move(c, x, y);
    }
}

void vela_a11y_update_magnifier(struct vela_a11y *a11y)
{
    struct vela_server *server = a11y->server;
    struct wlr_cursor *cursor = server->cursor;
    struct vela_output *out = vela_output_at(server, cursor->x, cursor->y);
    bool zoomed = a11y->zoom > 1.0;
    struct vela_output *output;
    wl_list_for_each (output, &server->outputs, link) {
        if ((output != out || !zoomed) && output->frame->zoom > 1.0) {
            vela_output_frame_set_magnifier(output->frame, 1.0, 0.0, 0.0);
            // Il cursore torna dove lo mette wlr_cursor (punto logico dello schermo).
            struct wlr_box box = vela_output_box(output);
            move_output_cursors(output, cursor->x - box.x, cursor->y - box.y);
        }
    }
    if (!zoomed || !out) {
        a11y->zoom_output = zoomed ? NULL : a11y->zoom_output;
        return;
    }
    struct wlr_box box = vela_output_box(out);
    if (out != a11y->zoom_output) {
        // Un altro schermo: la zona ingrandita parte attorno al cursore.
        a11y->zoom_output = out;
        a11y->view_x = cursor->x - (cursor->x - box.x) / a11y->zoom;
        a11y->view_y = cursor->y - (cursor->y - box.y) / a11y->zoom;
    }
    double w = box.width / a11y->zoom;
    double h = box.height / a11y->zoom;
    // Il cursore spinge la zona quando arriva ai suoi bordi.
    a11y->view_x = vela_clampd(a11y->view_x, cursor->x - w, cursor->x);
    a11y->view_y = vela_clampd(a11y->view_y, cursor->y - h, cursor->y);
    a11y->view_x = vela_clampd(a11y->view_x, box.x, box.x + box.width - w);
    a11y->view_y = vela_clampd(a11y->view_y, box.y, box.y + box.height - h);
    vela_output_frame_set_magnifier(out->frame, a11y->zoom, a11y->view_x, a11y->view_y);
    // Il cursore dove si vede il punto che indica.
    move_output_cursors(out, (cursor->x - a11y->view_x) * a11y->zoom, (cursor->y - a11y->view_y) * a11y->zoom);
}

bool vela_a11y_animating(const struct vela_a11y *a11y)
{
    return a11y->level_animating || a11y->zoom_animating;
}

bool vela_a11y_tick(struct vela_a11y *a11y, double now_ms)
{
    bool running = false;
    if (a11y->level_animating) {
        double p = vela_tween_progress(&a11y->level_tween, now_ms);
        double target = a11y->night_light ? 1.0 : 0.0;
        a11y->night_level = a11y->level_from + (target - a11y->level_from) * p;
        if (vela_tween_finished(&a11y->level_tween, now_ms)) {
            a11y->night_level = target;
            a11y->level_animating = false;
        }
        apply_color(a11y);
        running = running || a11y->level_animating;
    }
    if (a11y->zoom_animating) {
        double p = vela_tween_progress(&a11y->zoom_tween, now_ms);
        double before = a11y->zoom;
        a11y->zoom = a11y->zoom_from + (a11y->zoom_target - a11y->zoom_from) * p;
        if (vela_tween_finished(&a11y->zoom_tween, now_ms)) {
            a11y->zoom = a11y->zoom_target;
            a11y->zoom_animating = false;
        }
        // Il punto sotto il cursore resta fermo sullo schermo.
        struct wlr_cursor *cursor = a11y->server->cursor;
        if (a11y->zoom_output && before > 0.0 && a11y->zoom > 0.0) {
            a11y->view_x = cursor->x - (cursor->x - a11y->view_x) * before / a11y->zoom;
            a11y->view_y = cursor->y - (cursor->y - a11y->view_y) * before / a11y->zoom;
        }
        if (!a11y->zoom_animating && a11y->zoom <= 1.0 && !a11y->magnifier) {
            a11y->zoom = 1.0;
        }
        vela_a11y_update_magnifier(a11y);
        running = running || a11y->zoom_animating;
    }
    return running;
}

void vela_a11y_output_destroyed(struct vela_a11y *a11y, struct vela_output *output)
{
    if (a11y->zoom_output == output) {
        a11y->zoom_output = NULL;
    }
}

// ------------------------------------------------------ tasti permanenti --

static uint32_t sticky_bit(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
        return WLR_MODIFIER_SHIFT;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
        return WLR_MODIFIER_CTRL;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
        return WLR_MODIFIER_ALT;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
        return WLR_MODIFIER_LOGO;
    default:
        return 0;
    }
}

static void apply_sticky(struct vela_a11y *a11y, struct wlr_keyboard *k)
{
    wlr_keyboard_notify_modifiers(k, k->modifiers.depressed, a11y->latched,
        (k->modifiers.locked & ~STICKY_MASK) | a11y->locked, k->modifiers.group);
}

bool vela_a11y_sticky_key(struct vela_a11y *a11y, struct wlr_keyboard *keyboard, const xkb_keysym_t *syms,
    int count, bool pressed)
{
    if (!a11y->sticky_keys) {
        return false;
    }
    uint32_t bit = 0;
    for (int i = 0; i < count; ++i) {
        bit |= sticky_bit(syms[i]);
    }
    if (!bit) {
        a11y->candidate = 0;
        // Il tasto dopo li ha usati: al suo rilascio si lasciano.
        if (!pressed && a11y->latched) {
            a11y->latched = 0;
            apply_sticky(a11y, keyboard);
        }
        return false;
    }
    if (pressed) {
        a11y->candidate = bit;
        return false;
    }
    if (a11y->candidate != bit) {
        return false; // usato con un altro tasto: niente da ricordare
    }
    a11y->candidate = 0;
    bool super_latched = false;
    if (bit == WLR_MODIFIER_LOGO) {
        // Win: la prima volta resta premuto per il tasto dopo; di nuovo,
        // apre Start (come il tasto da solo).
        if (a11y->latched & bit) {
            a11y->latched &= ~bit;
        } else {
            a11y->latched |= bit;
            super_latched = true;
        }
    } else if (a11y->locked & bit) {
        a11y->locked &= ~bit;
    } else if (a11y->latched & bit) {
        a11y->latched &= ~bit;
        a11y->locked |= bit;
    } else {
        a11y->latched |= bit;
    }
    apply_sticky(a11y, keyboard);
    return super_latched;
}

// ------------------------------------------------------------- la shell --

bool vela_a11y_json(const struct vela_a11y *a11y, char *out, size_t size)
{
    // Le ore del sole di oggi, per la pagina della Luce notturna ("Dal
    // tramonto all'alba").
    char sun[64] = "";
    double latitude = 0.0;
    double longitude = 0.0;
    if (timezone_coordinates(&latitude, &longitude)) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        int set = vela_sun_time(false, &local, latitude, longitude);
        int rise = vela_sun_time(true, &local, latitude, longitude);
        if (set >= 0 && rise >= 0) {
            snprintf(sun, sizeof(sun), ",\"sunset\":\"%02d:%02d\",\"sunrise\":\"%02d:%02d\"", set / 60, set % 60,
                rise / 60, rise % 60);
        }
    }
    const char *t = "true";
    const char *f = "false";
    return vela_format(out, size,
        "{\"nightLight\":%s,\"colorFilter\":%s,\"magnifier\":%s,\"stickyKeys\":%s,\"touchpad\":%s%s}",
        a11y->night_light ? t : f, a11y->color_filter ? t : f, a11y->magnifier ? t : f, a11y->sticky_keys ? t : f,
        vela_input_has_touchpad(a11y->server->input) ? t : f, sun);
}

void vela_a11y_announce(struct vela_a11y *a11y)
{
    char line[512] = "accessibility ";
    size_t used = strlen(line);
    if (vela_a11y_json(a11y, line + used, sizeof(line) - used)) {
        vela_shell_send(a11y->server, line);
    }
}
