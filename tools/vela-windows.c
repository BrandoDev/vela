// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// vela-windows: elenca e comanda le finestre di Vela (wlr-foreign-toplevel).
//
// Uso: vela-windows [list]
//      vela-windows AZIONE APP_ID
//
// AZIONE: activate, minimize, restore, maximize, unmaximize, close.
// Agisce sulla prima finestra con quell'app_id (es. org.kde.konsole).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define MAX_WINDOWS 256

struct window {
    struct zwlr_foreign_toplevel_handle_v1* handle;
    char title[256];
    char appId[128];
    uint32_t state; // bit (1 << stato)
    int hasParent;
    int closed;
};

static struct window windows[MAX_WINDOWS];
static int windowCount;
static struct zwlr_foreign_toplevel_manager_v1* manager;
static struct wl_seat* seat;

static struct window* find(struct zwlr_foreign_toplevel_handle_v1* handle)
{
    for (int i = 0; i < windowCount; ++i) {
        if (windows[i].handle == handle) {
            return &windows[i];
        }
    }
    return NULL;
}

static void onTitle(void* data, struct zwlr_foreign_toplevel_handle_v1* h, const char* title)
{
    snprintf(find(h)->title, sizeof(windows[0].title), "%s", title);
}

static void onAppId(void* data, struct zwlr_foreign_toplevel_handle_v1* h, const char* appId)
{
    snprintf(find(h)->appId, sizeof(windows[0].appId), "%s", appId);
}

static void onOutputEnter(void* data, struct zwlr_foreign_toplevel_handle_v1* h, struct wl_output* o) { }
static void onOutputLeave(void* data, struct zwlr_foreign_toplevel_handle_v1* h, struct wl_output* o) { }

static void onState(void* data, struct zwlr_foreign_toplevel_handle_v1* h, struct wl_array* state)
{
    struct window* w = find(h);
    uint32_t* value;
    w->state = 0;
    wl_array_for_each(value, state)
    {
        w->state |= 1u << *value;
    }
}

static void onDone(void* data, struct zwlr_foreign_toplevel_handle_v1* h) { }

static void onClosed(void* data, struct zwlr_foreign_toplevel_handle_v1* h)
{
    find(h)->closed = 1;
}

static void onParent(void* data, struct zwlr_foreign_toplevel_handle_v1* h,
    struct zwlr_foreign_toplevel_handle_v1* parent)
{
    find(h)->hasParent = parent != NULL;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handleListener = {
    .title = onTitle,
    .app_id = onAppId,
    .output_enter = onOutputEnter,
    .output_leave = onOutputLeave,
    .state = onState,
    .done = onDone,
    .closed = onClosed,
    .parent = onParent,
};

static void onToplevel(void* data, struct zwlr_foreign_toplevel_manager_v1* m,
    struct zwlr_foreign_toplevel_handle_v1* handle)
{
    if (windowCount == MAX_WINDOWS) {
        return;
    }
    windows[windowCount++].handle = handle;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handleListener, NULL);
}

static void onFinished(void* data, struct zwlr_foreign_toplevel_manager_v1* m) { }

static const struct zwlr_foreign_toplevel_manager_v1_listener managerListener = {
    .toplevel = onToplevel,
    .finished = onFinished,
};

static void registryGlobal(void* data, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version)
{
    if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        manager = wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, 3);
        zwlr_foreign_toplevel_manager_v1_add_listener(manager, &managerListener, NULL);
    } else if (!strcmp(interface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    }
}

static void registryRemove(void* data, struct wl_registry* registry, uint32_t name) { }

static const struct wl_registry_listener registryListener = { registryGlobal, registryRemove };

static void printList(void)
{
    static const char* names[] = { " [maximized]", " [minimized]", " [active]", " [fullscreen]" };
    for (int i = 0; i < windowCount; ++i) {
        const struct window* w = &windows[i];
        if (w->closed) {
            continue;
        }
        printf("%-28s %-40.40s", w->appId, w->title);
        for (int s = 0; s < 4; ++s) {
            if (w->state & (1u << s)) {
                printf("%s", names[s]);
            }
        }
        printf("%s\n", w->hasParent ? " [dialogo]" : "");
    }
}

int main(int argc, char** argv)
{
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "vela-windows: no Wayland session (WAYLAND_DISPLAY)\n");
        return 1;
    }
    wl_registry_add_listener(wl_display_get_registry(display), &registryListener, NULL);
    wl_display_roundtrip(display);
    if (!manager) {
        fprintf(stderr, "vela-windows: the compositor doesn't offer wlr-foreign-toplevel\n");
        return 1;
    }
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    const char* action = argc > 1 ? argv[1] : "list";
    if (!strcmp(action, "list")) {
        printList();
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [list] | ACTION APP_ID\n", argv[0]);
        return 2;
    }

    struct window* target = NULL;
    for (int i = 0; i < windowCount && !target; ++i) {
        if (!windows[i].closed && !strcmp(windows[i].appId, argv[2])) {
            target = &windows[i];
        }
    }
    if (!target) {
        fprintf(stderr, "vela-windows: no window with app_id '%s'\n", argv[2]);
        return 1;
    }

    struct zwlr_foreign_toplevel_handle_v1* h = target->handle;
    if (!strcmp(action, "activate")) {
        zwlr_foreign_toplevel_handle_v1_activate(h, seat);
    } else if (!strcmp(action, "minimize")) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(h);
    } else if (!strcmp(action, "restore")) {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(h);
    } else if (!strcmp(action, "maximize")) {
        zwlr_foreign_toplevel_handle_v1_set_maximized(h);
    } else if (!strcmp(action, "unmaximize")) {
        zwlr_foreign_toplevel_handle_v1_unset_maximized(h);
    } else if (!strcmp(action, "close")) {
        zwlr_foreign_toplevel_handle_v1_close(h);
    } else {
        fprintf(stderr, "vela-windows: unknown action '%s'\n", action);
        return 2;
    }
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    return 0;
}
