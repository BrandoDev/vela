// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_BINDINGS_H
#define VELA_BINDINGS_H

// Le scorciatoie di Vela (come Windows: Win+L, Alt+Tab, Win+frecce, Win+D,
// Win+Ctrl+←/→...; dentro un'altra sessione, dove l'ospite si tiene Super,
// le varianti con Alt), il menu della finestra e le sue azioni.

#include <stdbool.h>
#include <stdint.h>

struct vela_server;
struct vela_view;

// Il tasto `sym` con i modificatori di prima: true se era una scorciatoia
// (e non va all'app).
bool vela_bindings_handle(struct vela_server *server, uint32_t modifiers, uint32_t sym);

// Il menu della finestra (docs/renderer.md §14.8): lo disegna la shell, nel
// punto (lx, ly) del layout. keyboard: aperto da tastiera (Alt+Spazio).
void vela_window_menu_show(struct vela_server *server, struct vela_view *view, double lx, double ly, bool keyboard);
// Un'azione del menu della finestra, della Visualizzazione attività, dei
// layout di snap o della taskbar: restore, move, resize, minimize, maximize,
// close, activate, snap-left, snap-right, "snap x0 y0 x1 y1 [quiet
// [finestra]]", activate-group, "move-to N", move-to-new, sticky, unsticky,
// app-sticky, app-unsticky.
void vela_window_action(struct vela_server *server, struct vela_view *view, const char *action);

#endif
