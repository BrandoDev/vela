// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_WORKSPACE_H
#define VELA_WORKSPACE_H

// Desktop virtuali, come Windows 11.
//
// Ogni finestra sta su un desktop (o su tutti: "Mostra questa finestra su
// tutti i desktop"); si vedono solo quelle del desktop in uso, gli altri
// alberi sono spenti. La taskbar mostra solo le finestre del desktop in uso
// (la maniglia wlr-foreign-toplevel esiste solo per loro), mentre l'elenco
// ext-foreign-toplevel le ha tutte: la Visualizzazione attività della shell
// ne mostra le anteprime, e sa dove sta ciascuna dal messaggio "workspaces"
// (JSON) che il compositor le manda a ogni cambiamento.
//
// Il passaggio: le finestre che si lasciano vanno in layers.windows_out, che
// scivola via sfumando; quelle nuove (layers.windows) arrivano dall'altra
// parte. A fine corsa le uscenti tornano in layers.windows, spente, nel loro
// ordine.
//
// I desktop e i loro nomi si ricordano in ~/.config/vela/desktop.conf.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct vela_buffer;
struct vela_server;
struct vela_view;

struct vela_workspaces {
    struct vela_server *server;
    char **names; // uno per desktop; "": "Desktop N"
    int count;
    int capacity;
    int current;
    char **sticky_apps; // le loro finestre su tutti i desktop
    int sticky_count;
    int sticky_capacity;
    uint64_t next_map_serial;
    // Il passaggio in corso: le uscenti (dal basso verso l'alto) e da dove.
    struct vela_view **outgoing;
    int outgoing_count;
    int outgoing_capacity;
    double start_ms; // -1: al primo frame
    int direction; // +1: si va a destra (il nuovo desktop entra da destra); 0: fermi
};

// Legge desktop.conf (almeno un desktop c'è sempre).
struct vela_workspaces *vela_workspaces_create(struct vela_server *server);
void vela_workspaces_destroy(struct vela_workspaces *workspaces);

// Il nome da mostrare ("Desktop N" se non ne ha uno).
void vela_workspace_name(const struct vela_workspaces *workspaces, int index, char *out, size_t size);

// refocus_after: la tastiera va a una finestra del desktop nuovo (no
// quando la si sta già dando a una finestra di lì).
void vela_workspaces_switch(struct vela_server *server, int index, bool refocus_after);
int vela_workspaces_add(struct vela_server *server); // l'indice del nuovo desktop
void vela_workspaces_remove(struct vela_server *server, int index);
void vela_workspaces_rename(struct vela_server *server, int index, const char *name);
void vela_workspaces_move(struct vela_server *server, int from, int to);
void vela_workspaces_move_view(struct vela_server *server, struct vela_view *view, int index);
void vela_workspaces_set_sticky(struct vela_server *server, struct vela_view *view, bool on);
void vela_workspaces_set_app_sticky(struct vela_server *server, const char *app_id, bool on);

// Una finestra nuova: sul desktop in uso (o su tutti, se la sua app ci sta).
void vela_workspaces_view_mapped(struct vela_server *server, struct vela_view *view);
void vela_workspaces_forget(struct vela_server *server, struct vela_view *view);
bool vela_view_on_current_workspace(const struct vela_view *view);
// Le maniglie della taskbar: solo le finestre del desktop in uso, in
// ordine di apertura.
void vela_workspaces_sync_taskbar(struct vela_server *server);

// Il passaggio animato all'istante `now_ms`: false quando ha finito.
bool vela_workspaces_tick(struct vela_server *server, double now_ms);
// Un passaggio ancora in corso finisce subito.
void vela_workspaces_finish_switch(struct vela_server *server);

// Lo stato per la shell: {"current":0,"names":[...],"windows":{...},...}.
void vela_workspaces_json(struct vela_server *server, struct vela_buffer *out);
void vela_workspaces_announce(struct vela_server *server); // alla shell, a ogni cambiamento

#endif
