// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_DECORATION_H
#define VELA_DECORATION_H

// La barra del titolo che Vela disegna per le finestre che non ne hanno una
// propria (docs/renderer.md §9): misure di Windows 11 (alta 32, pulsanti
// 46×32), icona dell'app e titolo nel font di KDE, simboli disegnati alla
// dimensione fisica esatta, colori Mica con la tinta dello sfondo.
//
// La barra non conosce la finestra: chi la possiede le dice a ogni
// aggiornamento com'è la finestra adesso, e lei ridisegna solo ciò che è
// cambiato.

#include <stdbool.h>
#include <wlr/util/box.h>

struct vela_server;
struct vela_tree;

#define VELA_DECORATION_HEIGHT 32 // logici
#define VELA_DECORATION_BUTTON_WIDTH 46
#define VELA_DECORATION_ICON_SIZE 16
#define VELA_DECORATION_ICON_X 12

enum vela_decoration_part {
    VELA_DECORATION_NONE,
    VELA_DECORATION_ICON,
    VELA_DECORATION_TITLE,
    VELA_DECORATION_MINIMIZE,
    VELA_DECORATION_MAXIMIZE,
    VELA_DECORATION_CLOSE,
};

// La finestra com'è adesso.
struct vela_decoration_state {
    struct wlr_box geometry; // il riquadro con la barra, rispetto all'albero della finestra
    float scale; // dello schermo su cui sta
    const char *title;
    const char *app_id;
    bool active;
    bool maximized;
    bool fullscreen; // a schermo intero la barra sparisce
};

struct vela_decoration;

// La barra è un albero figlio di `parent` (quello della finestra).
struct vela_decoration *vela_decoration_create(struct vela_server *server, struct vela_tree *parent,
    const struct vela_decoration_state *state);
void vela_decoration_destroy(struct vela_decoration *decoration);

void vela_decoration_update(struct vela_decoration *decoration, const struct vela_decoration_state *state);

// Che cosa c'è in un punto globale della barra.
enum vela_decoration_part vela_decoration_part_at(const struct vela_decoration *decoration, double lx, double ly);
// Il pulsante sotto il mouse si illumina (Chiudi di rosso).
void vela_decoration_set_hover(struct vela_decoration *decoration, enum vela_decoration_part part);
// Il pulsante Ingrandisci in coordinate globali (lì sotto si aprono i
// layout di snap).
struct wlr_box vela_decoration_maximize_box(const struct vela_decoration *decoration);

#endif
