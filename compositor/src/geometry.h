// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_GEOMETRY_H
#define VELA_GEOMETRY_H

// La geometria della nitidezza (docs/renderer.md §3): la scala predefinita
// dai DPI, VELA_SCALE, e come si sceglie la dimensione logica di una finestra
// perché il suo buffer copra esattamente i pixel dell'area. Funzioni pure,
// provate in compositor/tests/geometry_test.cpp.

#include <stdbool.h>
#include <stdint.h>

// La scala predefinita, come fa Windows (§3.8): dai DPI dello schermo, a
// passi del 25%, tra 100% e 300%. I pannelli dei portatili si guardano più da
// vicino: riferimento 105,6 DPI invece di 96. Misure assurde (proiettori, TV,
// adattatori che inventano l'EDID: diagonale fuori da 8"–100", o proporzioni
// diverse da quelle dei pixel) danno 100%, e *dpi 0.
float vela_scale_for_dpi(double phys_width_mm, double phys_height_mm, int width, int height, bool internal,
    double *dpi);

// Un pannello interno (portatile, tablet) dal nome del connettore.
bool vela_is_internal_panel(const char *name);

// La scala chiesta con VELA_SCALE per lo schermo `name`: "1.25" per tutti,
// oppure "DP-1=1.5,HDMI-A-1=1". 0 se non c'è.
float vela_requested_scale(const char *value, const char *name);

// I pixel del buffer che un client disegna per `logical` unità a quella
// scala (fractional-scale-v1, in 120esimi, arrotondamento a metà).
int64_t vela_buffer_pixels(int64_t logical, double scale);

struct vela_axis {
    double offset; // posizione logica rispetto all'origine dello schermo
    int size; // dimensione logica
};

// Un asse di una finestra che deve occupare i pixel [start, start + size)
// di uno schermo largo `screen` pixel (§3.5): la dimensione logica il cui
// buffer è esattamente `size` pixel. Se non esiste (a 150% 2560 pixel
// sarebbero 1706,67 unità), il pixel in più sborda fuori dallo schermo dal
// lato di un bordo libero (senza un altro schermo accanto: open_before e
// open_after); altrimenti si resta un pixel dentro.
struct vela_axis vela_place_axis(int start, int size, int screen, double scale, bool open_before, bool open_after);

// Un bordo dell'area libera dai pannelli, in pixel dello schermo: quelli che
// toccano i bordi dello schermo restano esattamente sui suoi pixel.
int vela_physical_edge(int logical, int full_start, int full_end, int pixels, double scale);

// Un rettangolo in pixel dello schermo.
struct vela_pixel_box {
    int x, y, width, height;
};

// Un rettangolo logico in pixel (§3.2): si arrotondano i bordi, non
// posizione e dimensione, così due rettangoli adiacenti restano adiacenti.
struct vela_pixel_box vela_edges_to_pixels(double x, double y, double width, double height, double origin_x,
    double origin_y, double scale);

// L'app ha disegnato alla scala dello schermo (§3.4): il buffer, a coordinate
// intere, è grande quanto la sua area fisica a meno dell'arrotondamento. Allora
// la superficie occupa esattamente i pixel del buffer.
bool vela_buffer_matches_area(double src_x, double src_y, double buffer_width, double buffer_height, double width,
    double height, double scale);

// Copia 1:1, senza ricampionamento (§3.3): ogni pixel del buffer cade
// esattamente su un pixel dello schermo. `swapped`: ruotato di 90°.
bool vela_one_to_one(double src_x, double src_y, double src_width, double src_height, int box_width,
    int box_height, bool swapped);

#endif
