// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_LOCK_H
#define VELA_LOCK_H

// Blocco dello schermo e inattività.
//
// Il blocco è ext-session-lock-v1: un programma (vela-lock) chiede di
// bloccare, e da quel momento il compositor mostra solo le sue superfici,
// una per schermo, su un fondo nero. Tutto il resto è spento: niente
// finestre, niente scorciatoie, niente tastiera alle app. Si sblocca solo
// quando il programma lo dice (password giusta); se va in crash, lo schermo
// resta nero e bloccato, e un nuovo programma di blocco può prenderne il
// posto (lo si rilancia, al massimo 5 volte al minuto).
//
// Inattività: dopo alcuni minuti senza input (predefinito 10; 0: mai) lo
// schermo si blocca e, pochi secondi dopo, si spegne. I minuti e il blocco
// stanno in vela.conf ("screen-off", "lock-on-idle"; VELA_SCREEN_OFF e
// VELA_LOCK_ON_IDLE hanno la precedenza). Un'app può impedirlo mentre
// mostra qualcosa (un video: idle-inhibit). Le app sanno quando l'utente è
// inattivo con ext-idle-notify. Annidati o headless non si spegne nulla.

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct vela_output;
struct vela_rect_node;
struct vela_server;
struct vela_tree;
struct wlr_idle_inhibit_manager_v1;
struct wlr_idle_notifier_v1;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_v1;

#define VELA_LOCK_RESPAWNS 5 // al massimo, in un minuto

struct vela_lock {
    struct vela_server *server;
    struct wlr_session_lock_manager_v1 *manager;
    struct wlr_session_lock_v1 *lock; // il programma di blocco attivo, o NULL
    struct vela_tree *backdrop_tree; // in fondo allo strato del blocco
    struct vela_rect_node **backdrop; // nero, uno per schermo
    int backdrop_count;
    int backdrop_capacity;
    // Gli schermi che devono ancora mostrare il nero prima di dire all'app
    // "bloccato".
    struct vela_output **waiting;
    int waiting_count;
    int waiting_capacity;
    struct wl_event_source *respawn; // rilancia vela-lock se sparisce
    int64_t respawns[VELA_LOCK_RESPAWNS]; // quando (ms), per non insistere all'infinito
    int respawn_count;

    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
    struct wl_event_source *idle_timer; // solo nella sessione vera
    int screen_off_ms; // 0: mai
    bool lock_on_idle;
    bool locking; // bloccato per inattività, schermi da spegnere tra poco
    bool screens_off;

    struct wl_listener new_lock;
    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
    struct wl_listener new_inhibitor;
};

struct vela_lock *vela_lock_create(struct vela_server *server);
void vela_lock_destroy(struct vela_lock *lock);

// Win+L, inattività, prima di sospendere: avvia vela-lock (VELA_LOCK ne
// sceglie un altro).
void vela_lock_screen(struct vela_lock *lock);
// Nasconde tutto tranne lo strato del blocco (nero finché vela-lock non
// c'è) e lo ricorda al supervisore.
void vela_lock_engage(struct vela_lock *lock);
// L'utente c'è: riaccende gli schermi e azzera l'attesa.
void vela_lock_note_activity(struct vela_lock *lock);
// Rilegge vela.conf (comando "reload-config").
void vela_lock_load_settings(struct vela_lock *lock);
// A ogni frame consegnato: il blocco aspetta il nero su ogni schermo.
void vela_lock_output_rendered(struct vela_lock *lock, struct vela_output *output);
// Gli schermi sono cambiati mentre è bloccato.
void vela_lock_update_layout(struct vela_lock *lock);

#endif
