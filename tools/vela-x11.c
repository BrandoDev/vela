// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// vela-x11: un'app X11 di prova (attraverso Xwayland), per le prove delle
// finestre X11 senza dipendere da programmi installati.
//
// Uso: vela-x11 [--menu X,Y] [LARGHEZZA ALTEZZA]   (predefinito 300x200)
//      vela-x11 --probe
//
// Apre una finestra arancione con classe "vela.x11" e titolo "vela-x11",
// che si chiude con WM_DELETE_WINDOW (Alt+F4). --menu apre anche un menu
// "override-redirect" verde di 120x80 nel punto (X, Y) dello schermo, come
// le tendine delle app X11.
//
// vela-x11 --probe si collega ed esce: fa partire Xwayland. Le prove lo
// usano prima di aprire la finestra, perché se Xwayland parte proprio per
// la connessione che mappa la finestra, la finestra non compare (un difetto
// noto, docs/c-core.md).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <xcb/xcb.h>

static xcb_atom_t atom(xcb_connection_t* connection, const char* name)
{
    xcb_intern_atom_reply_t* reply
        = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return result;
}

// Una finestra e il colore con cui riempirla. Lo sfondo da solo non basta, e
// nemmeno un disegno sull'Expose: Xwayland manda il primo buffer solo per un
// disegno che arriva dopo che ha preso in carico la finestra. Si ridisegna
// quindi dieci volte al secondo.
struct painted {
    xcb_window_t window;
    xcb_gcontext_t gc;
    int width;
    int height;
};

static void paint(xcb_connection_t* connection, const struct painted* p)
{
    const xcb_rectangle_t all = { 0, 0, (uint16_t)p->width, (uint16_t)p->height };
    xcb_poly_fill_rectangle(connection, p->window, p->gc, 1, &all);
}

static struct painted painted(xcb_connection_t* connection, xcb_window_t window, int width, int height,
    uint32_t color)
{
    struct painted p = { window, xcb_generate_id(connection), width, height };
    xcb_create_gc(connection, p.gc, window, XCB_GC_FOREGROUND, &color);
    return p;
}

static xcb_window_t create(xcb_connection_t* connection, xcb_screen_t* screen, int x, int y, int width, int height,
    uint32_t color, int overrideRedirect)
{
    xcb_window_t window = xcb_generate_id(connection);
    const uint32_t values[] = { color, (uint32_t)overrideRedirect, XCB_EVENT_MASK_EXPOSURE };
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, (int16_t)x, (int16_t)y,
        (uint16_t)width, (uint16_t)height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values);
    return window;
}

int main(int argc, char** argv)
{
    int width = 300;
    int height = 200;
    int menu = 0;
    int menuX = 0;
    int menuY = 0;
    int arg = 1;
    if (argc == 2 && !strcmp(argv[1], "--probe")) {
        xcb_connection_t* connection = xcb_connect(NULL, NULL);
        int failed = xcb_connection_has_error(connection);
        xcb_disconnect(connection);
        return failed ? 1 : 0;
    }
    if (arg + 1 < argc && !strcmp(argv[arg], "--menu") && sscanf(argv[arg + 1], "%d,%d", &menuX, &menuY) == 2) {
        menu = 1;
        arg += 2;
    }
    if (arg + 1 < argc) {
        width = atoi(argv[arg]);
        height = atoi(argv[arg + 1]);
    }
    xcb_connection_t* connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) {
        fprintf(stderr, "vela-x11: no X display (DISPLAY)\n");
        return 1;
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t window = create(connection, screen, 0, 0, width, height, 0xff8000, 0);
    const char title[] = "vela-x11";
    const char wmClass[] = "vela-x11\0vela.x11"; // istanza e classe
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        sizeof(title) - 1, title);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
        sizeof(wmClass), wmClass);
    xcb_atom_t protocols = atom(connection, "WM_PROTOCOLS");
    xcb_atom_t deleteWindow = atom(connection, "WM_DELETE_WINDOW");
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, protocols, XCB_ATOM_ATOM, 32, 1, &deleteWindow);
    struct painted windows[2] = { painted(connection, window, width, height, 0xff8000) };
    int count = 1;
    xcb_map_window(connection, window);
    if (menu) {
        xcb_window_t popup = create(connection, screen, menuX, menuY, 120, 80, 0x00c000, 1);
        windows[count++] = painted(connection, popup, 120, 80, 0x00c000);
        xcb_map_window(connection, popup);
    }
    xcb_flush(connection);

    struct pollfd fd = { xcb_get_file_descriptor(connection), POLLIN, 0 };
    int done = 0;
    while (!done && !xcb_connection_has_error(connection)) {
        for (int i = 0; i < count; ++i) {
            paint(connection, &windows[i]);
        }
        xcb_flush(connection);
        poll(&fd, 1, 100);
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_event(connection))) {
            if ((event->response_type & 0x7f) == XCB_CLIENT_MESSAGE) {
                const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)event;
                done = done || (message->type == protocols && message->data.data32[0] == deleteWindow);
            }
            free(event);
        }
    }
    xcb_disconnect(connection);
    return 0;
}
