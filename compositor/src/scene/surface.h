// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_SURFACE_H
#define VELA_SCENE_SURFACE_H

// Lo stato di Vela legato a ogni wlr_surface: su quali schermi si vede, e
// quanto. Da qui partono enter/leave, la scala preferita e lo schermo che
// scandisce i frame callback della superficie (docs/renderer.md §4.5, §3.6).
// Nasce con la superficie (vela_scene_watch) e muore con lei (wlr_addon).

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/addon.h>

struct vela_scene;
struct wlr_output;
struct wlr_surface;

struct vela_surface_on_output {
    struct wlr_output *output;
    int64_t overlap; // pixel della superficie che cadono sullo schermo
    int64_t visible; // di questi, quanti non sono coperti
};

struct vela_surface_state {
    struct wlr_surface *surface;
    struct vela_scene *scene;
    struct vela_surface_on_output *outputs;
    int output_count, output_capacity;
    // Lo schermo che mostra la parte maggiore: da lui la scala preferita.
    struct wlr_output *primary;
    // Lo schermo che ne mostra la parte visibile maggiore: da lui frame
    // callback e presentazione. NULL: la superficie è coperta o nascosta e
    // l'app può smettere di disegnare.
    struct wlr_output *pacing;
    // Lo schermo per cui l'app ha ricevuto il feedback dmabuf con la tranche
    // di scanout (è a schermo intero lì); NULL: feedback predefinito.
    struct wlr_output *scanout_feedback;

    struct wlr_addon addon;
    struct wl_listener commit;
};

// Crea lo stato di una superficie nuova (dalla scena).
void vela_surface_state_create(struct vela_scene *scene, struct wlr_surface *surface);
struct vela_surface_state *vela_surface_state_get(struct wlr_surface *surface);

// A ogni frame disegnato: la superficie occupa `overlap` pixel dello
// schermo, `visible` non coperti.
void vela_surface_report(struct wlr_surface *surface, struct wlr_output *output, int64_t overlap, int64_t visible);
// Non è più su quello schermo.
void vela_surface_forget(struct wlr_surface *surface, struct wlr_output *output);

#endif
