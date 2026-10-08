// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_FOCUS_H
#define VELA_FOCUS_H

// La tastiera: a quale finestra o pezzo della shell va. Come Windows, la
// finestra a fuoco sale in cima (alla scena e alla lista per Alt+Tab), si
// "accende" per l'app e per la taskbar, e cede la tastiera solo a un
// pannello che la chiede (menu Start, impostazioni rapide). I dialoghi di
// sistema che aspettano una risposta restano sopra le finestre normali.
//
// Lo stato è in vela_server: focused_layer e previous_layer.

#include <stdbool.h>

struct vela_layer_surface;
struct vela_server;
struct vela_view;

void vela_focus_view(struct vela_server *server, struct vela_view *view);
// Una finestra che compare o chiede da sola il primo piano (non per un
// clic): se un dialogo di sistema aspetta, resta sotto di lui e non gli
// toglie la tastiera.
void vela_focus_new_view(struct vela_server *server, struct vela_view *view);
void vela_focus_layer(struct vela_server *server, struct vela_layer_surface *layer);
// La tastiera torna a chi deve averla: il pannello di prima, o la finestra
// più recente del desktop in uso (dopo uno sblocco, una chiusura...).
void vela_focus_refocus(struct vela_server *server);

// La finestra sparisce (è già fuori dalla lista): chi la ricordava
// (trascinamento, Alt+Tab, clic, layout di snap) se ne dimentica; se aveva
// la tastiera, la tastiera passa oltre.
void vela_focus_forget_view(struct vela_server *server, struct vela_view *view, bool was_focused);
void vela_focus_layer_unmapped(struct vela_server *server, struct vela_layer_surface *layer);
void vela_focus_forget_layer(struct vela_server *server, struct vela_layer_surface *layer);

#endif
