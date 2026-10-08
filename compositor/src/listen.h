// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_LISTEN_H
#define VELA_LISTEN_H

// Connecting to wlroots signals. A listener that may be disconnected more
// than once (or before it was ever connected) starts with wl_list_init on
// its link.

#include <wayland-server-core.h>

static inline void vela_listen(struct wl_signal *signal, struct wl_listener *listener, wl_notify_func_t notify)
{
    listener->notify = notify;
    wl_signal_add(signal, listener);
}

static inline void vela_unlisten(struct wl_listener *listener)
{
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
}

#endif
