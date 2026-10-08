// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_ICONS_H
#define VELA_ICONS_H

// Le icone delle app nella barra del titolo (docs/renderer.md §9.6): dal
// file .desktop dell'app (Icon=) al tema di icone di KDE (specifica
// freedesktop), disegnate alla dimensione fisica esatta. Gli SVG con
// librsvg, preferiti sempre; le PNG solo se non c'è altro, ridotte con un
// filtro di qualità. Senza librsvg (opzionale) niente icone.

#include "image.h"

struct vela_icons;

// Il tema di KDE e la sua catena di eredità; i file .desktop si leggono
// alla prima richiesta.
struct vela_icons *vela_icons_create(void);
void vela_icons_destroy(struct vela_icons *icons);

// L'icona dell'app (app_id Wayland o classe X11), size x size pixel. Mai
// NULL: un'immagine vuota (pixels NULL) se non si trova. Resta valida, e di
// proprietà di `icons`, finché `icons` esiste.
const struct vela_image *vela_icons_app(struct vela_icons *icons, const char *app_id, int size);

#endif
