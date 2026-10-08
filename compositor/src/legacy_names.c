// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "legacy_names.h"

#include <stddef.h>
#include <string.h>

static const struct {
    const char *from;
    const char *to;
} keys[] = {
    { "spegni-schermo", "screen-off" },
    { "blocca", "lock-on-idle" },
    { "tastiera-layout", "keyboard-layout" },
    { "tastiera-variante", "keyboard-variant" },
    { "tastiera-opzioni", "keyboard-options" },
    { "tastiera-ritardo", "keyboard-repeat-delay" },
    { "tastiera-velocita", "keyboard-repeat-rate" },
    { "luce-notturna", "night-light" },
    { "luce-notturna-intensita", "night-light-strength" },
    { "luce-notturna-pianifica", "night-light-schedule" },
    { "luce-notturna-dalle", "night-light-from" },
    { "luce-notturna-alle", "night-light-to" },
    { "filtri-colore", "color-filters" },
    { "filtro-colore", "color-filter" },
    { "filtri-colore-scorciatoia", "color-filters-shortcut" },
    { "lente-incremento", "magnifier-step" },
    { "tasti-permanenti", "sticky-keys" },
    { "frequenza-variabile", "variable-refresh" },
    { "mouse-velocita", "mouse-speed" },
    { "mouse-precisione", "mouse-precision" },
    { "mouse-pulsante-principale", "mouse-primary-button" },
    { "mouse-righe", "mouse-scroll-lines" },
    { "touchpad-con-mouse", "touchpad-with-mouse" },
    { "touchpad-velocita", "touchpad-speed" },
    { "touchpad-tocco", "touchpad-tap" },
    { "touchpad-scorrimento-naturale", "touchpad-natural-scroll" },
    { "touchpad-tre-dita", "touchpad-three-fingers" },
    { "touchpad-quattro-dita", "touchpad-four-fingers" },
    { "lingua", "language" },
};

static const struct {
    const char *key; // nome nuovo; NULL: qualunque chiave
    const char *from;
    const char *to;
} values[] = {
    { NULL, "sì", "yes" },
    { NULL, "si", "yes" },
    { "night-light-schedule", "tramonto", "sunset" },
    { "night-light-schedule", "ore", "hours" },
    { "variable-refresh", "giochi", "games" },
    { "variable-refresh", "sempre", "always" },
    { "color-filter", "grigi", "grayscale" },
    { "mouse-primary-button", "sinistro", "left" },
    { "mouse-primary-button", "destro", "right" },
};

const char *vela_legacy_key(const char *key)
{
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (strcmp(key, keys[i].from) == 0) {
            return keys[i].to;
        }
    }
    return NULL;
}

const char *vela_legacy_value(const char *key, const char *value)
{
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if ((!values[i].key || strcmp(key, values[i].key) == 0) && strcmp(value, values[i].from) == 0) {
            return values[i].to;
        }
    }
    return NULL;
}
