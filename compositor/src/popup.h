// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_POPUP_H
#define VELA_POPUP_H

// Context menus, drop-downs, tooltips (xdg-popup) of windows and shell pieces.
// They position themselves relative to their parent; Vela keeps them on their
// owner's output. They live as long as their wlr_xdg_popup.

struct vela_owner;
struct vela_tree;
struct wlr_xdg_popup;

// `parent`: the parent's tree (the window, the panel or another popup);
// `owner`: who owns the root surface.
void vela_popup_create(struct wlr_xdg_popup *popup, struct vela_tree *parent, struct vela_owner *owner);

#endif
