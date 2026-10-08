// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_A11Y_H
#define VELA_A11Y_H

// Accessibilità e colore dello schermo, come Windows 11:
//
// - Luce notturna: i colori più caldi la sera, a mano (impostazioni
//   rapide) o pianificata, dalle-alle oppure dal tramonto all'alba (le ore
//   del sole si calcolano dalle coordinate del fuso orario, senza rete).
// - Filtri colore: scala di grigi e le correzioni per i daltonismi.
// - Lente di ingrandimento: Win+più ingrandisce lo schermo del cursore
//   attorno a lui, Win+meno riduce, Win+Esc chiude; la zona ingrandita
//   segue il cursore quando arriva ai bordi.
// - Tasti permanenti: Maiusc, Ctrl, Alt e Win premuti e lasciati valgono
//   per il tasto dopo; premuti due volte restano finché non si ripremono.
//
// Luce notturna e filtri li applica il renderer a tutto ciò che disegna
// (una matrice di colore, docs/renderer.md §7); la lente è la scena
// disegnata a una scala più grande. Le scelte stanno in vela.conf; la
// shell le accende dalle impostazioni rapide e riceve lo stato
// ("accessibility <json>").

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include "motion.h"

struct vela_output;
struct vela_server;
struct wl_event_source;
struct wlr_keyboard;

struct vela_a11y {
    struct vela_server *server;

    bool night_light; // accesa, a mano o dalla pianificazione
    int night_strength; // 0-100, come l'"Intensità" di Windows
    char schedule[16]; // "no", "sunset" (dal tramonto all'alba), "hours"
    int night_from; // minuti dalla mezzanotte
    int night_to;
    char schedule_key[64]; // la pianificazione letta l'ultima volta
    int scheduled; // cosa diceva la pianificazione l'ultima volta (-1: non si sa)
    // Il passaggio graduale tra spenta (0) e accesa (1).
    double night_level;
    double level_from;
    struct vela_tween level_tween;
    bool level_animating;

    bool color_filter;
    char color_filter_kind[32]; // grayscale, deuteranopia, protanopia, tritanopia
    bool color_filter_shortcut; // Win+Ctrl+C

    bool magnifier;
    int zoom_step; // percento per Win+più
    double zoom_target;
    double zoom; // quello mostrato (animato)
    double zoom_from;
    struct vela_tween zoom_tween;
    bool zoom_animating;
    struct vela_output *zoom_output; // lo schermo ingrandito (quello del cursore)
    double view_x; // l'angolo della zona ingrandita, nel layout
    double view_y;

    bool sticky_keys;
    uint32_t latched; // modificatori premuti e lasciati: valgono per il prossimo tasto
    uint32_t locked; // premuti due volte: restano finché non si ripremono
    uint32_t candidate; // il modificatore premuto ora, finché non arriva altro

    struct wl_event_source *timer; // la pianificazione, ogni minuto
};

struct vela_a11y *vela_a11y_create(struct vela_server *server);
void vela_a11y_destroy(struct vela_a11y *a11y);

// Rilegge vela.conf (all'avvio e con "reload-config").
void vela_a11y_load(struct vela_a11y *a11y);

// save: anche in vela.conf (no quando a decidere è la pianificazione).
void vela_a11y_set_night_light(struct vela_a11y *a11y, bool on, bool save);
void vela_a11y_set_color_filter(struct vela_a11y *a11y, bool on, bool save);
void vela_a11y_set_sticky_keys(struct vela_a11y *a11y, bool on, bool save);
void vela_a11y_set_magnifier(struct vela_a11y *a11y, bool on);
void vela_a11y_zoom(struct vela_a11y *a11y, int direction); // +1 Win+più, -1 Win+meno

// La zona ingrandita segue il cursore (dopo ogni suo movimento).
void vela_a11y_update_magnifier(struct vela_a11y *a11y);
// Le animazioni (luce notturna che sfuma, lente che si apre) all'istante
// `now_ms`: false quando non c'è più nulla da animare.
bool vela_a11y_tick(struct vela_a11y *a11y, double now_ms);
bool vela_a11y_animating(const struct vela_a11y *a11y);
void vela_a11y_output_destroyed(struct vela_a11y *a11y, struct vela_output *output);

// Tasti permanenti: ogni tasto passa di qui prima delle scorciatoie. true
// se Win è appena rimasto "premuto" per il tasto dopo (non apre Start).
bool vela_a11y_sticky_key(struct vela_a11y *a11y, struct wlr_keyboard *keyboard, const xkb_keysym_t *syms,
    int count, bool pressed);

// Lo stato per la shell: {"nightLight":true,...}. Restituisce false se non
// ci sta in `size`.
bool vela_a11y_json(const struct vela_a11y *a11y, char *out, size_t size);
void vela_a11y_announce(struct vela_a11y *a11y);

#endif
