// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_NESTED_H
#define VELA_NESTED_H

// Vela in una finestra dentro un'altra sessione (es. KDE), per le prove.
// Il backend annidato di wlroots non sa due cose, e le facciamo noi parlando
// direttamente con il compositor ospite:
//
// - Nitidezza: l'ospite a scala frazionaria (KDE al 125%) ingrandirebbe la
//   nostra finestra e la sfocherebbe. Chiediamo la sua scala
//   (fractional-scale-v1) e disegniamo un buffer grande quanto i suoi pixel
//   fisici, dichiarando con viewporter la dimensione logica.
// - Scorciatoie: chiediamo all'ospite di non intercettare Super, Alt+Tab & co.
//   quando la finestra di Vela ha la tastiera (keyboard-shortcuts-inhibit).
//
// La possiede il suo vela_output.

struct vela_nested;
struct vela_output;

struct vela_nested *vela_nested_create(struct vela_output *output);
void vela_nested_destroy(struct vela_nested *nested);

// L'ospite chiede una nuova dimensione per la finestra (in unità sue).
void vela_nested_resize(struct vela_nested *nested, int width, int height);

// Pixel del nostro buffer per ogni unità della finestra ospite: il backend
// annidato riporta il puntatore in pixel del buffer.
double vela_nested_pointer_scale_x(const struct vela_nested *nested);
double vela_nested_pointer_scale_y(const struct vela_nested *nested);

#endif
