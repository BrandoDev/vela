// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_KEYBOARD_H
#define VELA_KEYBOARD_H

// Una tastiera: il suo layout, la ripetizione dei tasti e il giro di ogni
// tasto (tasti permanenti, Super da solo per Start, scorciatoie, poi l'app
// a fuoco).
//
// Il layout, in ordine: le variabili XKB_DEFAULT_* (campo per campo), la
// scelta fatta nelle Impostazioni di Vela (vela.conf), quelle di KDE
// (~/.config/kxkbrc, se KDE gestisce la tastiera), quelle di
// systemd-localed (localectl, /etc/X11/xorg.conf.d/00-keyboard.conf). Una
// tastiera virtuale (prove automatiche) porta il suo.

#include <stdbool.h>
#include <wayland-server-core.h>

struct vela_config;
struct vela_input;
struct wlr_keyboard;

struct vela_keyboard {
    struct wl_list link; // vela_input.keyboards
    struct vela_input *input;
    struct wlr_keyboard *wlr;
    bool restoring; // tasti permanenti: si stanno rimettendo i modificatori

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

// La tastiera diventa quella del seat.
struct vela_keyboard *vela_keyboard_create(struct vela_input *input, struct wlr_keyboard *wlr);
void vela_keyboard_destroy(struct vela_keyboard *keyboard);

// Layout e ripetizione dei tasti da vela.conf.
void vela_keyboard_apply_settings(struct vela_keyboard *keyboard, const struct vela_config *settings);

#endif
