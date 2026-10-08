// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_POPUP_H
#define VELA_POPUP_H

// Menu contestuali, tendine, tooltip (xdg-popup) di finestre e pezzi della
// shell. Si posizionano da soli rispetto al genitore; Vela li tiene dentro lo
// schermo di chi li possiede. Vivono quanto il loro wlr_xdg_popup.

struct vela_owner;
struct vela_tree;
struct wlr_xdg_popup;

// `parent`: l'albero del genitore (la finestra, il pannello o un altro
// popup); `owner`: chi possiede la superficie radice.
void vela_popup_create(struct wlr_xdg_popup *popup, struct vela_tree *parent, struct vela_owner *owner);

#endif
