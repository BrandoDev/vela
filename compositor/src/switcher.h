// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SWITCHER_H
#define VELA_SWITCHER_H

// Alt+Tab: il compositor decide l'ordine (dalla più recente, solo il desktop
// in uso, come Windows) e la selezione; la shell disegna il pannello con le
// anteprime ("switcher-show N id1 id2...", "switcher-select N",
// "switcher-hide"). Si sceglie finché Alt resta premuto; rilasciandolo si
// passa alla finestra scelta, Esc annulla.

#include <stdbool.h>

struct vela_server;
struct vela_view;

struct vela_switcher {
    bool active;
    struct vela_view **views; // in ordine di uso recente
    int count;
    int capacity;
    int selected;
};

struct vela_switcher *vela_switcher_create(void);
void vela_switcher_destroy(struct vela_switcher *switcher);

// Un passo avanti (+1) o indietro; il primo apre il pannello.
void vela_switcher_step(struct vela_server *server, int direction);
// Chiude il pannello; activate: si passa alla finestra scelta.
void vela_switcher_finish(struct vela_server *server, bool activate);
bool vela_switcher_active(const struct vela_server *server);
// Un clic su un'anteprima (dalla shell).
void vela_switcher_pick(struct vela_server *server, int index);
// Una finestra che sparisce durante Alt+Tab: si chiude il selettore.
void vela_switcher_forget(struct vela_server *server, struct vela_view *view);

#endif
