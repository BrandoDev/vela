// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_CAPTURE_H
#define VELA_SCENE_CAPTURE_H

// Catturare l'immagine di una finestra (anteprime di Alt+Tab, e in futuro la
// condivisione di una finestra): il nostro renderer la disegna da sola,
// senza ciò che le sta sopra o sotto, anche se è coperta o ridotta a icona.
// La possiede la finestra, che la distrugge con sé.

struct vela_node;
struct vela_renderer;
struct vela_scene;
struct vela_view;
struct vela_window_capture;
struct wlr_allocator;
struct wlr_ext_image_capture_source_v1;

// `source`: l'albero della finestra; il suo riquadro lo dice `view`.
struct vela_window_capture *vela_window_capture_create(struct vela_scene *scene, struct vela_node *source,
    struct vela_view *view, struct vela_renderer *renderer, struct wlr_allocator *allocator);
void vela_window_capture_destroy(struct vela_window_capture *capture);

// La sorgente, con dimensioni e formati aggiornati alla finestra.
struct wlr_ext_image_capture_source_v1 *vela_window_capture_source(struct vela_window_capture *capture);

#endif
